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

    // 标定目标 ~20ms（实测值见 BENCHMARK_REPORT.md，机器相关）
    NO_OPTIMIZE void taskNear20ms() { countNumber(5'000'000); }

    using namespace StdThreadPool;
    ThreadPool gPool(std::thread::hardware_concurrency());

}  // namespace

// 裸线程：N 个任务 = N 次线程创建 + N 次 join，且 N 个线程同时争抢 12 核。
// 测量的两个变量搅在一起：线程创建开销 + 过度订阅。
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

// 控制组：tasks_nums 个 worker 的池，池构造在计时窗外。
// 消除了线程创建开销，保留过度订阅 → no_pool − oversized = 纯创建开销。
void case1_pool_oversized(benchmark::State& state) {
    const int tasks_nums = state.range(0);

    ThreadPool pool(tasks_nums);  // 构造不计时，只测执行阶段

    for (auto _ : state) {
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_nums);

        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(pool.submitTask(taskNear20ms));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

// 固定 12 worker 的池：既无创建开销也无过度订阅 → oversized − pool = 纯过度订阅税。
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
BENCHMARK(case1_pool_oversized)->Arg(50)->Arg(200)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_pool)->Arg(50)->Arg(200)->Unit(benchmark::kMillisecond);
