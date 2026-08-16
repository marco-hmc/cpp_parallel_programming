#include <benchmark/benchmark.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "utils.h"

namespace {

// Shared mutex — all threads contend on the SAME lock (unlike the previous
// version where each function call created a local, uncontended mutex).
std::mutex g_shared_mutex;

NO_OPTIMIZE void countNumberWithoutLock(int counter) {
    int value = 0;
    benchmark::DoNotOptimize(value);
    for (int i = 0; i < counter; ++i) {
        ++value;
    }
    assert(value == counter);
}

// Single-threaded lock overhead: one thread acquires/releases g_shared_mutex
// in a tight loop. No contention, but the lock/unlock cost is real.
NO_OPTIMIZE void countNumberWithLockSerial(int counter) {
    int value = 0;
    benchmark::DoNotOptimize(value);
    for (int i = 0; i < counter; ++i) {
        std::lock_guard<std::mutex> lock(g_shared_mutex);
        ++value;
    }
    assert(value == counter);
}

// Multi-threaded contended lock: N threads each do counter/N increments
// under the SAME global mutex. This exposes true contention cost:
// cache-line bouncing, kernel arbitration, and queueing delays.
void contendedLockWorkload(benchmark::State& state, int total_counter,
                           int num_threads) {
    for (auto _ : state) {
        int per_thread = total_counter / num_threads;
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([per_thread]() {
                for (int i = 0; i < per_thread; ++i) {
                    std::lock_guard<std::mutex> lock(g_shared_mutex);
                    benchmark::DoNotOptimize(i);
                }
            });
        }
        for (auto& t : threads) t.join();
    }
}

// Atomic increment — no mutex, just hardware atomic RMW
void contendedAtomicWorkload(benchmark::State& state, int total_counter,
                             int num_threads) {
    for (auto _ : state) {
        std::atomic<int> counter{0};
        int per_thread = total_counter / num_threads;
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&counter, per_thread]() {
                for (int i = 0; i < per_thread; ++i) {
                    counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : threads) t.join();
        benchmark::DoNotOptimize(counter.load());
    }
}

}  // namespace

// ----- Serial baselines -----

// No lock at all — pure ALU work. The absolute baseline.
void case5_without_lock_cost(benchmark::State& state) {
    for (auto _ : state) {
        countNumberWithoutLock(120'000'000);
    }
}

// Single thread, global mutex — uncontended lock/unlock overhead.
// Each iteration: lock → increment → unlock.  120M times.
// Expected: 50–200× slower than without_lock.
void case5_with_lock_serial(benchmark::State& state) {
    for (auto _ : state) {
        countNumberWithLockSerial(120'000'000);
    }
}

// ----- Contended lock — the REAL lock cost -----
//
// Fixed total work (1.2M iterations), varying thread count.
// More threads = more contention on the SAME global mutex = worse performance.
// This is Amdahl's Law visualized: the serialized critical section
// dominates as thread count grows.

constexpr int kContendedTotal = 1'200'000;  // per-thread work × threads = const

void case5_lock_contended_1thread(benchmark::State& state) {
    contendedLockWorkload(state, kContendedTotal, 1);
}
void case5_lock_contended_2threads(benchmark::State& state) {
    contendedLockWorkload(state, kContendedTotal, 2);
}
void case5_lock_contended_4threads(benchmark::State& state) {
    contendedLockWorkload(state, kContendedTotal, 4);
}
void case5_lock_contended_hwthreads(benchmark::State& state) {
    contendedLockWorkload(state, kContendedTotal,
                          std::thread::hardware_concurrency());
}

// ----- Atomic — the fast alternative -----

void case5_atomic_contended_1thread(benchmark::State& state) {
    contendedAtomicWorkload(state, kContendedTotal, 1);
}
void case5_atomic_contended_2threads(benchmark::State& state) {
    contendedAtomicWorkload(state, kContendedTotal, 2);
}
void case5_atomic_contended_4threads(benchmark::State& state) {
    contendedAtomicWorkload(state, kContendedTotal, 4);
}
void case5_atomic_contended_hwthreads(benchmark::State& state) {
    contendedAtomicWorkload(state, kContendedTotal,
                            std::thread::hardware_concurrency());
}

// ===== Registration =====

// Serial baselines
BENCHMARK(case5_without_lock_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_with_lock_serial)->Unit(benchmark::kMillisecond);

// Contended lock: same total work, different thread counts
BENCHMARK(case5_lock_contended_1thread)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_lock_contended_2threads)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_lock_contended_4threads)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_lock_contended_hwthreads)->Unit(benchmark::kMillisecond);

// Atomic reference: how much better is lock-free?
BENCHMARK(case5_atomic_contended_1thread)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_atomic_contended_2threads)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_atomic_contended_4threads)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_atomic_contended_hwthreads)->Unit(benchmark::kMillisecond);
