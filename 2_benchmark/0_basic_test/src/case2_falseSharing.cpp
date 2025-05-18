#include <benchmark/benchmark.h>

#include <thread>
#include <vector>

#include "utils.h"

namespace {

    NO_OPTIMIZE void countNumber(int counter) {
        int value = 0;
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void countNumberWithRef(int& value, int counter = 240'000'000) {
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void taskNear50ms() { countNumber(240'000'000); }

}  // namespace

void case2_task_cost(benchmark::State& state) {
    for (auto _ : state) {
        taskNear50ms();
    }
}

void case2_single_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            taskNear50ms();
        }
    }
}

void case2_multi_thread_falseSharing_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<int> nums(std::thread::hardware_concurrency());

        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([&nums, i]() { countNumberWithRef(nums[i]); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

void case2_multi_thread_no_falseSharing_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([]() { taskNear50ms(); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

// BENCHMARK(case2_task_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_single_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_multi_thread_falseSharing_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_multi_thread_no_falseSharing_cost)
    ->Unit(benchmark::kMillisecond);