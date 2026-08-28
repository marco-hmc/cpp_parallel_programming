#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    NO_OPTIMIZE void taskNear20ms() { countNumber(7'500'000); }

    using namespace StdThreadPool;
    ThreadPool gPool(std::thread::hardware_concurrency());

}  // namespace

// 同一池嵌套等待（安全区）：父任务在 worker 上阻塞等待子任务。
// 父任务数 < worker 数时不死锁，但每个父任务独占一个 worker，
// 池的有效并行度线性下降——并行度侵蚀。
void case2_one_thread_pool(benchmark::State& state) {
    const int parent_task = state.range(0);
    const int child_task = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<void>> futures;
        for (int i = 0; i < parent_task; ++i) {
            futures.emplace_back(gPool.submitTask([child_task]() {
                std::vector<std::future<void>> childFutures;
                childFutures.reserve(child_task);
                for (int j = 0; j < child_task; ++j) {
                    childFutures.emplace_back(
                        gPool.submitTask([]() { return taskNear20ms(); }));
                }
                for (auto& future : childFutures) {
                    future.get();
                }
            }));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

// 每父任务独立子池：消除死锁风险，但线程数随父任务数爆炸（过度订阅）。
void case2_multi_thread_pool(benchmark::State& state) {
    const int parent_task = state.range(0);
    const int child_task = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<void>> futures;
        for (int i = 0; i < parent_task; ++i) {
            futures.emplace_back(gPool.submitTask([child_task]() {
                std::vector<std::future<void>> childFutures;
                childFutures.reserve(child_task);

                ThreadPool pool(std::thread::hardware_concurrency());
                for (int j = 0; j < child_task; ++j) {
                    childFutures.emplace_back(
                        pool.submitTask([]() { return taskNear20ms(); }));
                }
                for (auto& future : childFutures) {
                    future.get();
                }
            }));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

// 死锁演示：父任务数(12) == worker 数(12) 时，所有 worker 被父任务占满
// 且都在等待子任务 → 子任务永远得不到 worker → 死锁（数学必然）。
// 看门狗线程超时后打印 DEADLOCK CONFIRMED 并 abort——完成反而说明分析有误。
// 默认不注册（abort 会杀死整个进程）；单独运行：
//   RUN_DEADLOCK_DEMO=1 DEADLOCK_TIMEOUT_SECONDS=30 \
//       ./pool_benchmark --benchmark_filter=case2_deadlock_demo
void case2_deadlock_demo(benchmark::State& state) {
    const int parent_task = state.range(0);
    const int child_task = state.range(1);

    const char* timeout_env = std::getenv("DEADLOCK_TIMEOUT_SECONDS");
    const int timeout_seconds = timeout_env ? std::atoi(timeout_env) : 30;

    for (auto _ : state) {
        std::atomic<bool> done{false};

        std::thread watchdog([&]() {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(timeout_seconds);
            while (!done.load() &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!done.load()) {
                // stderr 而非 stdout：abort() 不冲刷 stdio 缓冲区，
                // 管道重定向下 stdout 的 DEADLOCK CONFIRMED 会丢失
                std::fprintf(stderr,
                             "DEADLOCK CONFIRMED: %d parent tasks blocked "
                             "all %d workers for over %ds; %d child tasks "
                             "per parent never ran\n",
                             parent_task,
                             static_cast<int>(
                                 std::thread::hardware_concurrency()),
                             timeout_seconds, child_task);
                std::abort();
            }
        });

        std::vector<std::future<void>> futures;
        for (int i = 0; i < parent_task; ++i) {
            futures.emplace_back(gPool.submitTask([child_task]() {
                std::vector<std::future<void>> childFutures;
                childFutures.reserve(child_task);
                for (int j = 0; j < child_task; ++j) {
                    childFutures.emplace_back(
                        gPool.submitTask([]() { return taskNear20ms(); }));
                }
                for (auto& future : childFutures) {
                    future.get();
                }
            }));
        }
        for (auto& future : futures) {
            future.get();  // 死锁时永远到不了这里
        }

        done.store(true);
        watchdog.join();
        std::printf("UNEXPECTED: deadlock demo completed - analysis wrong?\n");
    }
}

// one 与 multi 跑同一参数集，保证横向可比：
//   {4, 30}、{8, 30} 均 < 12 worker，共享池安全（不死锁）
BENCHMARK(case2_one_thread_pool)
    ->Args({4, 30})
    ->Args({8, 30})
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case2_multi_thread_pool)
    ->Args({4, 30})
    ->Args({8, 30})
    ->Unit(benchmark::kMillisecond);

// 死锁组环境变量门控注册：全局作用域只能写声明（BENCHMARK 宏展开为声明），
// 语句必须包在 lambda 初始化器里
static bool g_register_deadlock_demo = []() {
    if (std::getenv("RUN_DEADLOCK_DEMO")) {
        BENCHMARK(case2_deadlock_demo)
            ->Args({12, 30})
            ->Unit(benchmark::kMillisecond);
    }
    return true;
}();
