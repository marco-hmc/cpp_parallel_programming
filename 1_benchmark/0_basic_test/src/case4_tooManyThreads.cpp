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

    NO_OPTIMIZE void taskCntNumbers() { countNumber(120'000'000); }

    const int total_tasks = 1000;   // 总任务数
    const int many_threads = 1000;  // 大量线程数

}  // namespace

// 场景一：不并行，全部串行
void case4_serial_execution(benchmark::State& state) {
    for (auto _ : state) {
        for (int i = 0; i < total_tasks; ++i) {
            taskCntNumbers();
        }
    }
}

// 场景二：开核心数量那么多的线程去跑
void case4_optimal_threads(benchmark::State& state) {
    const int num_cores = std::thread::hardware_concurrency();
    const int tasks_per_thread = total_tasks / num_cores;
    const int remaining_tasks = total_tasks % num_cores;

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_cores);

        // 为每个核心分配任务
        for (int i = 0; i < num_cores; ++i) {
            int task_count = tasks_per_thread;
            if (i < remaining_tasks) {
                task_count++;  // 处理剩余任务
            }

            threads.emplace_back([task_count]() {
                for (int j = 0; j < task_count; ++j) {
                    taskCntNumbers();
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }
}

// 场景三：开1000个线程去跑
void case4_too_many_threads(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(many_threads);

        // 每个线程执行一个任务
        for (int i = 0; i < total_tasks; ++i) {
            threads.emplace_back(taskCntNumbers);
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }
}

BENCHMARK(case4_serial_execution)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_optimal_threads)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_too_many_threads)->Unit(benchmark::kMillisecond);