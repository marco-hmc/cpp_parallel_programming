#include <benchmark/benchmark.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

#include "utils.h"

/*
Experiment 4: thread count vs performance — where is the sweet spot,
and how much do excess threads hurt?

Fixed total workload: 1000 tasks × 6M increments (serial ≈ 2.5 s).
The only variable is the thread count T, swept from 1 to 1000.

Key design points:
- Threads are pre-created OUTSIDE the timed window (manual timing via
  UseManualTime + SetIterationTime), so thread creation cost does not
  pollute the sweep. What is measured is purely "how fast T threads
  running concurrently finish the fixed workload".
- Tasks are claimed via an atomic index (work-stealing style), so load
  balancing is independent of T and of tasks % T.
- The sweep must cover: below physical cores, at physical cores (2),
  at logical cores (4), and oversubscribed (5+ .. 1000).

Contrast benchmark: case4_per_task_thread creates one thread per task
(1000 create+join pairs) — creation/teardown IS inside the timed window,
to answer "what does the per-task-thread pattern cost on top of the
concurrency effects?".
*/

namespace {

NO_OPTIMIZE void countNumber(int counter) {
    int value = 0;
    char padding[128];
    benchmark::DoNotOptimize(padding);
    benchmark::DoNotOptimize(value);
    for (int i = 0; i < counter; ++i) {
        ++value;
    }
    assert(value == counter);
}

NO_OPTIMIZE void taskCntNumbers() { countNumber(6'000'000); }

constexpr int total_tasks = 1000;

}  // namespace

// Thread-count sweep: pre-created threads, creation/ready/join outside
// the timed window. Timed region = go released -> all tasks done.
NO_OPTIMIZE void case4_thread_count(benchmark::State& state) {
    const int thread_count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        std::atomic<int> ready{0};
        std::atomic<int> done{0};
        std::atomic<int> next_task{0};
        std::atomic<bool> go{false};

        auto worker = [&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (;;) {
                const int t = next_task.fetch_add(1, std::memory_order_relaxed);
                if (t >= total_tasks) break;
                taskCntNumbers();
            }
            done.fetch_add(1, std::memory_order_release);
        };

        // Creation + ready sync: outside the timed window.
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (int i = 0; i < thread_count; ++i) {
            threads.emplace_back(worker);
        }
        while (ready.load(std::memory_order_acquire) != thread_count) {
            std::this_thread::yield();
        }

        // Timed region: release all workers, wait until every task is done.
        const auto t0 = std::chrono::steady_clock::now();
        go.store(true, std::memory_order_release);
        while (done.load(std::memory_order_acquire) != thread_count) {
            std::this_thread::yield();
        }
        state.SetIterationTime(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count());

        // Teardown: outside the timed window.
        for (auto& t : threads) {
            t.join();
        }
    }
}

// Contrast: one thread per task, create+join inside the timed window —
// the per-task-thread pattern's full cost (creation + memory + switching).
void case4_per_task_thread(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(total_tasks);
        for (int i = 0; i < total_tasks; ++i) {
            threads.emplace_back(taskCntNumbers);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
}

// Sweep points: below physical cores (1,2,3,4), at physical cores (6),
// between physical and logical (8), at logical cores (12),
// oversubscribed (16, 24, 48, 100, 1000).
BENCHMARK(case4_thread_count)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(6)
    ->Arg(8)
    ->Arg(12)
    ->Arg(16)
    ->Arg(24)
    ->Arg(48)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond)
    ->UseManualTime();

BENCHMARK(case4_per_task_thread)->Unit(benchmark::kMillisecond);
