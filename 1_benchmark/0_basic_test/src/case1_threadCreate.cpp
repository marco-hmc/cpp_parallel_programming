#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "utils.h"

/*
Experiment 1: Measure the overhead of creating and destroying threads.

Pure create+join overhead: each thread executes an empty body
and the main thread waits for all of them via join(). No sleep,
no syscall inside the thread body — this isolates thread creation
and teardown cost from everything else.

Also measures the alternative: create+detach (fire-and-forget),
which avoids join cost but leaks threads past the measurement window
(not recommended for production, shown here for comparison).
*/

namespace {

NO_OPTIMIZE void empty_task() {
    // Truly empty — the thread does nothing.
    // benchmark::DoNotOptimize prevents the compiler from
    // optimizing away the function call entirely.
    benchmark::DoNotOptimize(0);
}

}  // namespace

// Variant A: create + join — the full lifecycle cost
// Measures: pthread_create + kernel thread init + context switch
//           to run empty body + pthread_join + kernel thread teardown.
NO_OPTIMIZE void case1_create_and_join(benchmark::State& state) {
    const int thread_count = state.range(0);
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (int i = 0; i < thread_count; ++i) {
            threads.emplace_back(empty_task);
        }
        for (auto& t : threads) {
            t.join();
        }
    }
}

// Variant B: create + detach — fire-and-forget (no join cost)
// Measures: pthread_create + kernel thread init only.
// Warning: detach() means threads may outlive the benchmark iteration.
// The OS cleans them up asynchronously, so the measured time is an
// UNDERestimate of the true cost (teardown is paid later, outside
// the measurement window).
NO_OPTIMIZE void case1_create_and_detach(benchmark::State& state) {
    const int thread_count = state.range(0);
    for (auto _ : state) {
        for (int i = 0; i < thread_count; ++i) {
            std::thread t(empty_task);
            t.detach();
        }
    }
}

// Variant C: measure per-thread cost by varying thread count
// Small counts (10, 50, 100, 500, 1000) to show linearity
// and find the per-thread overhead constant.
BENCHMARK(case1_create_and_join)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case1_create_and_detach)
    ->Arg(10)
    ->Arg(50)
    ->Arg(100)
    ->Arg(500)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond);
