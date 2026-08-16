#include <benchmark/benchmark.h>

#include <atomic>
#include <queue>
#include <thread>
#include <vector>

#include "threadPool.h"
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

// Use the library thread pool instead of an inline copy
StdThreadPool::ThreadPool gPool(std::thread::hardware_concurrency());

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

// Thread pool — threads are reused, only task queue overhead
void case3_thread_pool_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<int> counter(0);
        const int totalTasks = static_cast<int>(std::thread::hardware_concurrency());

        for (int i = 0; i < totalTasks; ++i) {
            gPool.submitTask([&counter] {
                taskCntNumbers();
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        while (counter.load(std::memory_order_relaxed) < totalTasks) {
            std::this_thread::yield();
        }
    }
}

BENCHMARK(case3_task_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_single_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_multi_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_thread_pool_cost)->Unit(benchmark::kMillisecond);
