#include <benchmark/benchmark.h>

#include <cassert>
#include <thread>
#include <vector>

#include "utils.h"

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

NO_OPTIMIZE void taskCntNumbers() { countNumber(240'000'000); }

}  // namespace

// Baseline: run the task once (unit of work)
void case3_task_cost(benchmark::State& state) {
    for (auto _ : state) {
        taskCntNumbers();
    }
}

// Serial: same total work as multi-threaded, but on one thread
// Total work = hw_concurrency × one_task = same as parallel versions
void case3_single_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        for (int i = 0; i < static_cast<int>(std::thread::hardware_concurrency()); ++i) {
            taskCntNumbers();
        }
    }
}

// One std::thread per core — create+join overhead included
void case3_multi_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([]() { taskCntNumbers(); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

BENCHMARK(case3_task_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_single_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_multi_thread_cost)->Unit(benchmark::kMillisecond);
