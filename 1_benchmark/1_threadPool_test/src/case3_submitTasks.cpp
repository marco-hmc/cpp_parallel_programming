#include <benchmark/benchmark.h>

#include <condition_variable>
#include <future>
#include <mutex>
#include <numeric>
#include <queue>
#include <thread>
#include <vector>

#include "threadPool.h"
#include "utils.h"

namespace {

    NO_OPTIMIZE std::vector<int> heavyComputation(int size) {
        std::vector<int> result(size);
        for (int i = 0; i < size; ++i) {
            result[i] =
                i * i + static_cast<int>(std::sin(i)) * 1000;  // 模拟复杂计算
        }
        return result;
    }

    NO_OPTIMIZE int serialComputation(const std::vector<int>& data) {
        int sum = 0;
        for (int value : data) {
            sum += value % 7 +
                   static_cast<int>(std::sqrt(value));  // 模拟复杂的串行计算
        }
        return sum;
    }

    using namespace StdThreadPool;
    ThreadPool gPool(std::thread::hardware_concurrency());

}  // namespace

// 方式1：阻塞等待所有任务完成，然后批量串行处理
void case3_block_then_serial(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<std::vector<int>>> futures;
        futures.reserve(tasks_nums);

        // 并行阶段：提交所有任务
        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask(
                [vector_size, i]() { return heavyComputation(vector_size); }));
        }

        // 阻塞等待所有任务完成
        std::vector<std::vector<int>> results;
        results.reserve(tasks_nums);
        for (auto& future : futures) {
            results.push_back(future.get());
        }

        // 串行阶段：批量处理所有结果
        int total_sum = 0;
        for (const auto& result : results) {
            total_sum += serialComputation(result);
        }

        benchmark::DoNotOptimize(total_sum);
    }
}

// 方式2：流水线处理 - 一个任务完成就立即串行处理
void case3_pipeline_processing(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<std::vector<int>>> futures;
        futures.reserve(tasks_nums);

        // 并行阶段：提交所有任务
        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask(
                [vector_size, i]() { return heavyComputation(vector_size); }));
        }

        // 流水线处理：按任务完成顺序立即处理
        int total_sum = 0;
        for (auto& future : futures) {
            auto result = future.get();  // 按提交顺序等待
            total_sum += serialComputation(result);
        }

        benchmark::DoNotOptimize(total_sum);
    }
}

// 方式3：使用回调函数的模拟实现（真正的异步串行处理）
void case3_callback_style(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);

    for (auto _ : state) {
        std::mutex result_mutex;
        std::condition_variable result_cv;
        std::queue<std::vector<int>> ready_results;
        std::atomic<int> completed_tasks{0};
        std::atomic<int> total_sum{0};

        // 串行处理线程
        std::thread serial_processor([&]() {
            while (true) {
                std::unique_lock<std::mutex> lock(result_mutex);
                result_cv.wait(lock, [&]() {
                    return !ready_results.empty() ||
                           completed_tasks.load() == tasks_nums;
                });

                while (!ready_results.empty()) {
                    auto result = std::move(ready_results.front());
                    ready_results.pop();
                    lock.unlock();

                    // 立即进行串行计算
                    int sum = serialComputation(result);
                    total_sum.fetch_add(sum);

                    lock.lock();
                }

                if (completed_tasks.load() == tasks_nums &&
                    ready_results.empty()) {
                    break;
                }
            }
        });

        // 并行阶段：提交所有任务
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_nums);

        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask([&, vector_size, i]() {
                // 执行并行计算
                auto result = heavyComputation(vector_size);

                // 将结果放入队列，触发串行处理
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    ready_results.push(std::move(result));
                }
                result_cv.notify_one();
                completed_tasks.fetch_add(1);
            }));
        }

        // 等待所有任务完成
        for (auto& future : futures) {
            future.get();
        }

        serial_processor.join();
        benchmark::DoNotOptimize(total_sum.load());
    }
}

// 方式4：使用 std::async 的懒惰求值
void case3_async_lazy(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<std::vector<int>>> parallel_futures;
        parallel_futures.reserve(tasks_nums);

        // 并行阶段：使用 std::async
        for (int i = 0; i < tasks_nums; ++i) {
            parallel_futures.push_back(std::async(
                std::launch::async,
                [vector_size, i]() { return heavyComputation(vector_size); }));
        }

        // 串行阶段：按需获取结果并立即处理
        int total_sum = 0;
        for (auto& future : parallel_futures) {
            auto result = future.get();
            total_sum += serialComputation(result);
        }

        benchmark::DoNotOptimize(total_sum);
    }
}

// 方式5：使用两个线程池（一个用于并行计算，一个用于串行处理）
void case3_dual_threadpool(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);

    static ThreadPool serial_pool(1);  // 单线程池用于串行处理

    for (auto _ : state) {
        std::atomic<int> total_sum{0};
        std::atomic<int> completed_tasks{0};

        // 并行阶段：提交所有并行任务
        for (int i = 0; i < tasks_nums; ++i) {
            gPool.submitTask([&, vector_size, i]() {
                // 执行并行计算
                auto result = heavyComputation(vector_size);

                // 将串行处理任务提交到串行线程池
                serial_pool.submitTask([result = std::move(result), &total_sum,
                                        &completed_tasks]() {
                    int sum = serialComputation(result);
                    total_sum.fetch_add(sum);
                    completed_tasks.fetch_add(1);
                });
            });
        }

        // 等待所有串行任务完成
        while (completed_tasks.load() < tasks_nums) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        benchmark::DoNotOptimize(total_sum.load());
    }
}

// 注册基准测试
BENCHMARK(case3_block_then_serial)
    ->Args({50, 10'000})
    ->Args({50, 50'000})
    ->Args({200, 10'000})
    ->Args({200, 50'000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case3_pipeline_processing)
    ->Args({50, 10'000})
    ->Args({50, 50'000})
    ->Args({200, 10'000})
    ->Args({200, 50'000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case3_callback_style)
    ->Args({50, 10'000})
    ->Args({50, 50'000})
    ->Args({200, 10'000})
    ->Args({200, 50'000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case3_async_lazy)
    ->Args({50, 10'000})
    ->Args({50, 50'000})
    ->Args({200, 10'000})
    ->Args({200, 50'000})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case3_dual_threadpool)
    ->Args({50, 10'000})
    ->Args({50, 50'000})
    ->Args({200, 10'000})
    ->Args({200, 50'000})
    ->Unit(benchmark::kMillisecond);