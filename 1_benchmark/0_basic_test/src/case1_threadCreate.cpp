#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "utils.h"

/*
Experiment 1: Measure pure thread creation overhead.

Only the cost of bringing a thread into existence is timed:
from the first std::thread construction until all N threads
have begun executing their entry function (startup latency).
Each thread signals arrival via an atomic counter the moment
it starts running, so the timing window closes as soon as the
last thread is scheduled.

Join/teardown is deliberately excluded: it runs after
SetIterationTime() and never enters the measurement (Google
Benchmark manual timing via UseManualTime()).
*/

namespace {

// The thread's only job: signal that it has started executing.
// No sleep, no syscall, no payload.
NO_OPTIMIZE void empty_task(std::atomic<int>& started) {
    started.fetch_add(1, std::memory_order_release);
}

}  // namespace

// Measures: pthread_create + kernel thread init + first scheduling
// to the entry point. Teardown (join) happens outside the timed
// window.
NO_OPTIMIZE void case1_create_only(benchmark::State& state) {
    const auto thread_count = state.range(0);
    for (auto _ : state) {
        std::atomic<int> started{0};
        std::vector<std::thread> threads;
        threads.reserve(thread_count);

        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < thread_count; ++i) {
            threads.emplace_back(empty_task, std::ref(started));
        }
        // Busy-wait until every thread has been scheduled and has
        // executed its entry point. Yield so the waiting main thread
        // does not starve the cores the new threads need.
        while (started.load(std::memory_order_acquire) != thread_count) {
            std::this_thread::yield();
        }
        state.SetIterationTime(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count());

        // Teardown — not part of the measurement.
        for (auto& t : threads) {
            t.join();
        }
    }
}

// Small counts (10, 50, 100, 500, 1000) to show linearity
// and find the per-thread creation constant.
BENCHMARK(case1_create_only)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond)
    ->UseManualTime();
