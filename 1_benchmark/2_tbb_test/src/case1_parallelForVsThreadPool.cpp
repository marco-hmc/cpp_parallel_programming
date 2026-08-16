#include <benchmark/benchmark.h>
#include <oneapi/tbb.h>

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include "threadPool.h"  // 使用提供的线程池

// 计算密集型任务
double compute_task(int start, int end) {
    double result = 0.0;
    for (int i = start; i < end; ++i) {
        result += std::sin(i) * std::cos(i) + std::sqrt(i + 1);
    }
    return result;
}

// 测试参数
constexpr int TOTAL_WORK = 1000000;
constexpr int CHUNK_SIZE = 1000;

// 串行版本 - 作为基准
void case1_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = compute_task(0, TOTAL_WORK);
        benchmark::DoNotOptimize(result);
    }
}

// TBB parallel_for 版本
void case1_tbb_parallel_for(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> total_result{0.0};

        tbb::parallel_for(
            tbb::blocked_range<int>(0, TOTAL_WORK, CHUNK_SIZE),
            [&](const tbb::blocked_range<int>& range) {
                double local_result = compute_task(range.begin(), range.end());
                total_result.fetch_add(local_result, std::memory_order_relaxed);
            });

        benchmark::DoNotOptimize(total_result.load());
    }
}

// ThreadPool 版本 - 使用提供的线程池
void case1_threadpool(benchmark::State& state) {
    const int num_threads = std::thread::hardware_concurrency();
    StdThreadPool::ThreadPool pool(num_threads);

    for (auto _ : state) {
        std::vector<std::future<double>> futures;

        // 将工作分块提交到线程池
        for (int i = 0; i < TOTAL_WORK; i += CHUNK_SIZE) {
            int end = std::min(i + CHUNK_SIZE, TOTAL_WORK);
            futures.push_back(
                pool.submitTask([i, end]() { return compute_task(i, end); }));
        }

        // 收集结果
        double total_result = 0.0;
        for (auto& future : futures) {
            total_result += future.get();
        }

        benchmark::DoNotOptimize(total_result);
    }
}

// TBB parallel_for 自动分块版本
void case1_tbb_auto_partitioner(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> total_result{0.0};

        tbb::parallel_for(
            tbb::blocked_range<int>(0, TOTAL_WORK),
            [&](const tbb::blocked_range<int>& range) {
                double local_result = compute_task(range.begin(), range.end());
                total_result.fetch_add(local_result, std::memory_order_relaxed);
            },
            tbb::auto_partitioner());

        benchmark::DoNotOptimize(total_result.load());
    }
}

// TBB parallel_for 简单分块版本
void case1_tbb_simple_partitioner(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> total_result{0.0};

        tbb::parallel_for(
            tbb::blocked_range<int>(0, TOTAL_WORK),
            [&](const tbb::blocked_range<int>& range) {
                double local_result = compute_task(range.begin(), range.end());
                total_result.fetch_add(local_result, std::memory_order_relaxed);
            },
            tbb::simple_partitioner());

        benchmark::DoNotOptimize(total_result.load());
    }
}

// 原生 std::thread 版本作为对比
void case1_native_threads(benchmark::State& state) {
    const int num_threads = std::thread::hardware_concurrency();

    for (auto _ : state) {
        std::vector<std::thread> threads;
        std::vector<double> results(num_threads);

        const int work_per_thread = TOTAL_WORK / num_threads;

        for (int i = 0; i < num_threads; ++i) {
            int start = i * work_per_thread;
            int end =
                (i == num_threads - 1) ? TOTAL_WORK : (i + 1) * work_per_thread;

            threads.emplace_back([&results, i, start, end]() {
                results[i] = compute_task(start, end);
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        double total_result = 0.0;
        for (double result : results) {
            total_result += result;
        }

        benchmark::DoNotOptimize(total_result);
    }
}

// 注册基准测试
BENCHMARK(case1_serial)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_tbb_parallel_for)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_threadpool)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_tbb_auto_partitioner)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_tbb_simple_partitioner)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_native_threads)->Unit(benchmark::kMillisecond);