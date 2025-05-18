#include <benchmark/benchmark.h>

#include <future>
#include <thread>
#include <vector>

#include "threadPool.h"
#include "utils.h"

namespace {

    NO_OPTIMIZE void countNumber(int counter) {
        int value = 0;
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void taskNear20ms() { countNumber(48'000'000); }

    using namespace StdThreadPool;
    ThreadPool gPool(std::thread::hardware_concurrency());

}  // namespace

void case1_no_pool(benchmark::State& state) {
    const int tasks_nums = state.range(0);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(tasks_nums);

        for (int i = 0; i < tasks_nums; ++i) {
            threads.emplace_back(taskNear20ms);
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

void case1_pool(benchmark::State& state) {
    const int tasks_nums = state.range(0);

    for (auto _ : state) {
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_nums);

        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask(taskNear20ms));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

BENCHMARK(case1_no_pool)->Arg(50)->Arg(200)->Unit(benchmark::kMillisecond);

BENCHMARK(case1_pool)->Arg(50)->Arg(200)->Unit(benchmark::kMillisecond);
