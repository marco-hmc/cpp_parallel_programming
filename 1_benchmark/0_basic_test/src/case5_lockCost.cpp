#include <benchmark/benchmark.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "utils.h"

/*
Experiment 5: what does a lock cost, and what does lock-free cost?

Three structurally identical counters do the SAME total work
(kTotalWork increments, split evenly across N threads). The only
variable is HOW the shared counter is updated:

  baseline: each thread increments its own local int      — no sharing,
                                                            no sync,
                                                            absolute floor
  mutex   : all threads increment one shared int guarded
            by std::mutex                                 — the lock's cost
  atomic  : all threads increment one shared
            std::atomic<int> (fetch_add, relaxed)         — the lock-free cost

The two numbers this case answers:
  lock cost      = case5_mutex   − case5_baseline
  lock-free cost = case5_atomic  − case5_baseline
either as wall time or normalised to ns per increment.

Thread count sweeps {1, 2, 4, hw_threads}:
  - 1 thread  → pure mechanism cost, zero contention
  - 2/4/hw    → cost under growing contention

Timing follows the case1/case4 pattern (UseManualTime): threads are
created, synced via `ready`, and joined OUTSIDE the timed window, and
the shared state is built per iteration before `go` is released. Only
the pure counting region is measured, so neither thread creation nor
shared-state setup pollutes the numbers.

Note: `mutex` here locks once per increment — deliberately fine-grained.
That is exactly the anti-pattern whose cost this case quantifies.
*/

namespace {

constexpr int kTotalWork = 1'200'000;  // fixed total increments, all groups

// ----- The three counter implementations (identical loop shape) -----

// Absolute floor: per-thread local counter, no sharing, no sync.
NO_OPTIMIZE void countLocal(int per_thread) {
    int value = 0;
    for (int i = 0; i < per_thread; ++i) {
        ++value;
    }
    benchmark::DoNotOptimize(value);
}

// The lock: every increment takes the shared mutex.
NO_OPTIMIZE void countWithMutex(int per_thread, std::mutex& mtx,
                                int& counter) {
    for (int i = 0; i < per_thread; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++counter;
    }
}

// Lock-free: every increment is an atomic RMW on the shared counter.
NO_OPTIMIZE void countWithAtomic(int per_thread, std::atomic<int>& counter) {
    for (int i = 0; i < per_thread; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

// ----- Per-iteration shared state -----

struct LocalState {};  // baseline: nothing is shared

struct MutexState {
    std::mutex mtx;
    int counter = 0;
};

struct AtomicState {
    std::atomic<int> counter{0};
};

// ----- Shared benchmark driver (case4 manual-timing pattern) -----

// range(0): thread count; the sentinel 0 means hardware_concurrency().
inline int resolveThreadCount(benchmark::State& state) {
    int threads = static_cast<int>(state.range(0));
    if (threads == 0) {
        threads = static_cast<int>(std::thread::hardware_concurrency());
    }
    return threads;
}

// Each iteration: build the shared state (outside the timed window),
// create N workers that wait on `go`, release them, and time until all
// are done. Creation/ready sync and join stay outside the measurement.
template <typename MakeShared, typename Work, typename Verify>
NO_OPTIMIZE void parallelTimedBenchmark(benchmark::State& state,
                                        MakeShared make_shared, Work work,
                                        Verify verify) {
    const int num_threads = resolveThreadCount(state);
    for (auto _ : state) {
        auto shared = make_shared();  // shared state, outside the timed window

        std::atomic<int> ready{0};
        std::atomic<int> done{0};
        std::atomic<bool> go{false};

        auto worker = [&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            work(*shared);
            done.fetch_add(1, std::memory_order_release);
        };

        // Creation + ready sync: outside the timed window.
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back(worker);
        }
        while (ready.load(std::memory_order_acquire) != num_threads) {
            std::this_thread::yield();
        }

        // Timed region: release all workers, wait until every one is done.
        const auto t0 = std::chrono::steady_clock::now();
        go.store(true, std::memory_order_release);
        while (done.load(std::memory_order_acquire) != num_threads) {
            std::this_thread::yield();
        }
        state.SetIterationTime(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count());

        // Teardown: outside the timed window.
        for (auto& t : threads) {
            t.join();
        }
        verify(*shared);
    }
}

}  // namespace

// ----- The three groups -----

void case5_baseline(benchmark::State& state) {
    const int per_thread = kTotalWork / resolveThreadCount(state);
    parallelTimedBenchmark(
        state, [] { return std::make_unique<LocalState>(); },
        [per_thread](LocalState&) { countLocal(per_thread); },
        [](LocalState&) {});
}

void case5_mutex(benchmark::State& state) {
    const int per_thread = kTotalWork / resolveThreadCount(state);
    parallelTimedBenchmark(
        state, [] { return std::make_unique<MutexState>(); },
        [per_thread](MutexState& s) {
            countWithMutex(per_thread, s.mtx, s.counter);
        },
        [](MutexState& s) { assert(s.counter == kTotalWork); });
}

void case5_atomic(benchmark::State& state) {
    const int per_thread = kTotalWork / resolveThreadCount(state);
    parallelTimedBenchmark(
        state, [] { return std::make_unique<AtomicState>(); },
        [per_thread](AtomicState& s) {
            countWithAtomic(per_thread, s.counter);
        },
        [](AtomicState& s) {
            assert(s.counter.load(std::memory_order_relaxed) == kTotalWork);
        });
}

// ===== Registration =====

// Thread sweep {1, 2, 4, hw_threads}. 0 is a sentinel resolved to
// hardware_concurrency() inside the benchmark body, so the arg list never
// collides with 1/2/4 even when hw happens to equal one of them (the
// benchmark name would then be case5_*/0, distinct from case5_*/4).
// NOTE: the BENCHMARK macro expands to a declaration, so it cannot be
// passed to a helper function — the sweep is a chained macro instead.
#define CASE5_THREAD_SWEEP() \
    ->Arg(1)->Arg(2)->Arg(4)->Arg(0)

BENCHMARK(case5_baseline)->Unit(benchmark::kMillisecond)->UseManualTime() CASE5_THREAD_SWEEP();
BENCHMARK(case5_mutex)->Unit(benchmark::kMillisecond)->UseManualTime() CASE5_THREAD_SWEEP();
BENCHMARK(case5_atomic)->Unit(benchmark::kMillisecond)->UseManualTime() CASE5_THREAD_SWEEP();
