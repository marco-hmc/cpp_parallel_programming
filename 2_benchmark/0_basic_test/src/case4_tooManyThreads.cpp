#include <benchmark/benchmark.h>

#include <queue>
#include <thread>

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

    NO_OPTIMIZE void taskNear50ms() { countNumber(120'000'000); }

    const int task_numbers = 1200;

}  // namespace

void case4_task_cost(benchmark::State& state) {
    for (auto _ : state) {
        taskNear50ms();
    }
}

void case4_single_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        for (int i = 0; i < task_numbers; ++i) {
            taskNear50ms();
        }
        taskNear50ms();
    }
}

void case4_limited_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([]() {
                for (int i = 0;
                     i < task_numbers / std::thread::hardware_concurrency();
                     ++i) {
                    taskNear50ms();
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

void case4_enough_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(task_numbers);

        for (int i = 0; i < task_numbers; ++i) {
            threads.emplace_back(taskNear50ms);
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

BENCHMARK(case4_task_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_single_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_limited_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_enough_thread_cost)->Unit(benchmark::kMillisecond);