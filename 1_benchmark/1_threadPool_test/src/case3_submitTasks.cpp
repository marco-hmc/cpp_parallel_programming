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

    // 串行阶段工作量由 serial_factor 旋钮控制：
    // factor ×k = 串行工作量 ×k，而并行阶段不变 → 串行/并行比值可调。
    // NO_OPTIMIZE 防止循环不变量外提（重复调用同数据时编译器可能合并）。
    NO_OPTIMIZE int serialComputationK(const std::vector<int>& data,
                                       int factor) {
        int sum = 0;
        for (int rep = 0; rep < factor; ++rep) {
            for (int value : data) {
                sum += value % 7 +
                       static_cast<int>(std::sqrt(value));  // 模拟串行计算
            }
        }
        return sum;
    }

    using namespace StdThreadPool;
    ThreadPool gPool(std::thread::hardware_concurrency());

}  // namespace

// ==================== 配对 1：block ↔ pipeline（唯一差异：交错） ====================
// 两者同为 gPool 调度、主线程执行 Reduce、按提交顺序等待。

// block：等待所有 Map 任务完成后，批量串行处理
void case3_block_then_serial(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);
    const int serial_factor = state.range(2);

    for (auto _ : state) {
        std::vector<std::future<std::vector<int>>> futures;
        futures.reserve(tasks_nums);

        // 并行阶段：提交所有任务
        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask(
                [vector_size]() { return heavyComputation(vector_size); }));
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
            total_sum += serialComputationK(result, serial_factor);
        }

        benchmark::DoNotOptimize(total_sum);
    }
}

// pipeline：不等全部 Map 完成，按提交顺序逐个等待并立即 Reduce，
// 使主线程的 Reduce 与剩余 worker 的 Map 交错执行。
void case3_pipeline_processing(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);
    const int serial_factor = state.range(2);

    for (auto _ : state) {
        std::vector<std::future<std::vector<int>>> futures;
        futures.reserve(tasks_nums);

        // 并行阶段：提交所有任务
        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask(
                [vector_size]() { return heavyComputation(vector_size); }));
        }

        // 流水线处理：按提交顺序逐个等待并立即 Reduce
        int total_sum = 0;
        for (auto& future : futures) {
            auto result = future.get();  // 提交顺序，不是完成顺序
            total_sum += serialComputationK(result, serial_factor);
        }

        benchmark::DoNotOptimize(total_sum);
    }
}

// ==================== 配对 2：pipeline ↔ std_async（唯一差异：调度器） ====================
// 结构与 pipeline 完全相同（提交全部 → 提交顺序 get → 主线程 Reduce），
// 唯一差异是任务调度器：自研池 vs std::async。
// 注意：std::async(launch::async) 并非"零开销"——
//   Windows/MSVC 下走 PPL 线程池（且线程数受限，长任务会饿死后续任务）；
//   Linux/libstdc++ 下才是每任务一个新线程。结论平台相关。
void case3_std_async(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);
    const int serial_factor = state.range(2);

    for (auto _ : state) {
        std::vector<std::future<std::vector<int>>> parallel_futures;
        parallel_futures.reserve(tasks_nums);

        // 并行阶段：使用 std::async（eager，提交即执行）
        for (int i = 0; i < tasks_nums; ++i) {
            parallel_futures.push_back(std::async(
                std::launch::async,
                [vector_size]() { return heavyComputation(vector_size); }));
        }

        // 串行阶段：按提交顺序获取结果并立即处理
        int total_sum = 0;
        for (auto& future : parallel_futures) {
            auto result = future.get();
            total_sum += serialComputationK(result, serial_factor);
        }

        benchmark::DoNotOptimize(total_sum);
    }
}

// ==================== 配对 3：callback ↔ dual（唯一差异：Reduce 执行者） ====================
// 两者同为完成顺序感知（结果完成即进入队列），
// 唯一差异是 Reduce 执行者：裸线程+条件变量 vs 单线程池。

// callback：专用串行线程 + 条件变量队列，按完成顺序立即 Reduce。
// 状态变更（push + completed 递增）必须先于 notify_one——否则最后一次
// 递增不伴随唤醒，串行线程可能带着"已完成"的谓词永久睡眠（lost-wakeup）。
void case3_callback_style(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);
    const int serial_factor = state.range(2);

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
                    total_sum.fetch_add(
                        serialComputationK(result, serial_factor));

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
            futures.push_back(gPool.submitTask([&, vector_size]() {
                // 执行并行计算
                auto result = heavyComputation(vector_size);

                // 结果入队 + 计数递增必须在同一临界区内完成，
                // notify 在其后发出
                {
                    std::lock_guard<std::mutex> lock(result_mutex);
                    ready_results.push(std::move(result));
                    completed_tasks.fetch_add(1);
                }
                result_cv.notify_one();
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

// dual：并行池（Map）+ 单线程池（Reduce），同为完成顺序感知。
// 等待方式用 futures（而非自旋轮询），与代码库风格一致。
void case3_dual_threadpool(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);
    const int serial_factor = state.range(2);

    static ThreadPool serial_pool(1);  // 单线程池用于串行处理

    for (auto _ : state) {
        std::atomic<int> total_sum{0};

        // serial 任务的 future 由多个 worker 并发追加，需互斥保护
        std::mutex future_mutex;
        std::vector<std::future<void>> serial_futures;
        serial_futures.reserve(tasks_nums);

        // 并行阶段：提交所有并行任务
        std::vector<std::future<void>> heavy_futures;
        heavy_futures.reserve(tasks_nums);
        for (int i = 0; i < tasks_nums; ++i) {
            heavy_futures.push_back(gPool.submitTask([&, vector_size]() {
                // 执行并行计算
                auto result = heavyComputation(vector_size);

                // 将串行处理任务提交到串行线程池
                auto serial_future = serial_pool.submitTask(
                    [result = std::move(result), &total_sum, serial_factor]() {
                        total_sum.fetch_add(
                            serialComputationK(result, serial_factor));
                    });
                {
                    std::lock_guard<std::mutex> lock(future_mutex);
                    serial_futures.push_back(std::move(serial_future));
                }
            }));
        }

        // 先等 Map 全部完成（此时所有 serial 任务才全部入队），再等 Reduce
        for (auto& future : heavy_futures) {
            future.get();
        }
        for (auto& future : serial_futures) {
            future.get();
        }

        benchmark::DoNotOptimize(total_sum.load());
    }
}

// ==================== 基线：parallel_reduce（无串行阶段） ====================
// 量化"Reduce 必须串行"这个前提的成本：Map 任务内部顺手 Reduce，
// 全部在并行池上完成，串行阶段完全消失。
void case3_parallel_reduce(benchmark::State& state) {
    const int tasks_nums = state.range(0);
    const int vector_size = state.range(1);
    const int serial_factor = state.range(2);

    for (auto _ : state) {
        std::atomic<int> total_sum{0};
        std::vector<std::future<void>> futures;
        futures.reserve(tasks_nums);

        for (int i = 0; i < tasks_nums; ++i) {
            futures.push_back(gPool.submitTask([&, vector_size, serial_factor]() {
                auto result = heavyComputation(vector_size);
                total_sum.fetch_add(serialComputationK(result, serial_factor));
            }));
        }

        for (auto& future : futures) {
            future.get();
        }

        benchmark::DoNotOptimize(total_sum.load());
    }
}

// 注册基准测试：{任务数, 向量大小, 串行占比旋钮}
//   - vector_size 同时缩放 Map 与 Reduce 两个阶段（每元素成本同量级），
//     不改变串行/并行比值——比值只能由 serial_factor 控制
//   - serial_factor ∈ {1, 8}：串行工作量 ×k，直接检验
//     "callback 何时占优"的假设
#define CASE3_ARGS()                                                          \
    ->Args({50, 10'000, 1})                                                   \
        ->Args({50, 10'000, 8})                                               \
        ->Args({50, 50'000, 1})                                               \
        ->Args({50, 50'000, 8})                                               \
        ->Args({200, 10'000, 1})                                              \
        ->Args({200, 10'000, 8})                                              \
        ->Args({200, 50'000, 1})                                              \
        ->Args({200, 50'000, 8})

BENCHMARK(case3_block_then_serial) CASE3_ARGS()->Unit(benchmark::kMillisecond);
BENCHMARK(case3_pipeline_processing) CASE3_ARGS()->Unit(benchmark::kMillisecond);
BENCHMARK(case3_std_async) CASE3_ARGS()->Unit(benchmark::kMillisecond);
BENCHMARK(case3_callback_style) CASE3_ARGS()->Unit(benchmark::kMillisecond);
BENCHMARK(case3_dual_threadpool) CASE3_ARGS()->Unit(benchmark::kMillisecond);
BENCHMARK(case3_parallel_reduce) CASE3_ARGS()->Unit(benchmark::kMillisecond);
