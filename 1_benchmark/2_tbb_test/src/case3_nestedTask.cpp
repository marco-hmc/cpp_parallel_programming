#include <benchmark/benchmark.h>
#include <oneapi/tbb.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "bench_common.h"
#include "threadPool.h"

// Case 3 — 嵌套并行：flat（拍平） vs nested（嵌套） vs isolate（隔离）。
//
// 研究问题：嵌套 parallel_for 的影响——嵌套本身有无额外开销？跨层级工作窃取
// 在什么条件下值钱？this_task_arena::isolate 的隔离税有多大？
//
// 配对对比：全策略复用同一确定性负载（Case3Spec，mt19937(42) 固定种子，
// 计时窗外算好参考值），唯一变量是任务结构（拍平/嵌套/隔离/深度/粒度/偏斜）。
// 归约全部走槽位归约（元素顺序）→ 与串行逐位一致，校验为精确比较。
//
// 旧版设计缺陷（已修正）：
// - 旧版每任务 std::random_device 无种子，串行/并行各测不同负载，3.4x 不可信；
// - 旧版 6 策略只测了 2 个（queuing_mutex 超时拖垮套件），死锁分析仅源码推理；
// - 旧版 threadpool_nested_unsafe 已裁剪：1_threadPool_test case2 已用看门狗
//   确定性覆盖线程池嵌套死锁/并行度侵蚀，且其结论不能泛化到 TBB（见本 case 结果）。
//
// 教程声明核对（docs/1_threads/4_tbb.md §4.1）：嵌套 parallel_for + mutex + 窃取
// → 死锁。见门控演示 case3_tbb_steal_deadlock_demo / _fixed。

namespace {

    constexpr int MEAN_INNER = 8000;  // 外层任务的平均内层元素数

    using namespace StdThreadPool;
    // 双池锚点（6+6）：静态切分线程池下安全的嵌套方案，与 TBB 嵌套配对。
    ThreadPool gOuterPool(6);
    ThreadPool gInnerPool(6);

}  // namespace

// 串行基线 ×2：均匀/偏斜。
void case3_serial_nested(benchmark::State& state) {
    const bool skewed = state.range(0) != 0;
    const auto& spec = case3Spec(32, MEAN_INNER, skewed, 256);

    for (auto _ : state) {
        double total = 0.0;
        for (int i = 0; i < spec.outer; ++i) {
            for (int j = 0; j < spec.inner_sizes[i]; ++j) {
                total += case3ElemOp(i, j);
            }
        }
        VERIFY_RESULT(total, spec.totalReference(), 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// 拍平：blocked_range2d 一次并行（教程 §2.2 推荐的二维做法）。
// 偏斜负载下矩形域存在「幻影」元素，用 j < inner_sizes[i] 守卫跳过。
// 槽位按 (行块, 列块) 排序合并 = 元素顺序 → 与串行逐位一致。
void case3_flat_2d(benchmark::State& state) {
    const int g = static_cast<int>(state.range(0));
    const bool skewed = state.range(1) != 0;
    const auto& spec = case3Spec(32, MEAN_INNER, skewed, g);
    const int max_inner = spec.maxInner();
    const int row_slots = (32 + g - 1) / g;
    const int col_slots = (max_inner + g - 1) / g;

    for (auto _ : state) {
        std::vector<std::atomic<double>> sums(row_slots * col_slots);

        tbb::parallel_for(
            tbb::blocked_range2d<int>(0, 32, g, 0, max_inner, g),
            [&](const tbb::blocked_range2d<int>& r) {
                const auto& rows = r.rows();
                const auto& cols = r.cols();
                // 二维规范 chunk（grain 对齐）拆分 + 原子累加：
                // 2D 分裂同样不保证对齐（等分优先），槽位必须 fetch_add
                for (int rb = rows.begin(); rb < rows.end();) {
                    const int re = std::min(rb + (g - rb % g), rows.end());
                    for (int cb = cols.begin(); cb < cols.end();) {
                        const int ce = std::min(cb + (g - cb % g), cols.end());
                        double local = 0.0;
                        for (int i = rb; i < re; ++i) {
                            for (int j = cb; j < ce; ++j) {
                                if (j < spec.inner_sizes[i]) {
                                    local += case3ElemOp(i, j);
                                }
                            }
                        }
                        sums[(rb / g) * col_slots + cb / g].fetch_add(
                            local, std::memory_order_relaxed);
                        cb = ce;
                    }
                    rb = re;
                }
            },
            tbb::static_partitioner{});

        const double total = slotMerge(sums);
        VERIFY_RESULT(total, spec.totalReference(), 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// 嵌套：外层 parallel_for（每任务一 range）→ 内层 parallel_for（grain g）。
// 内层任务可被任何空闲 worker 窃取（跨层级窃取）——外层负载不均时由此回收尾端空闲。
void case3_nested(benchmark::State& state) {
    const int outer = static_cast<int>(state.range(0));
    const int g = static_cast<int>(state.range(1));
    const bool skewed = state.range(2) != 0;
    const auto& spec = case3Spec(outer, MEAN_INNER, skewed, g);

    for (auto _ : state) {
        std::vector<std::atomic<double>> sums(spec.total_chunks);

        tbb::parallel_for(
            tbb::blocked_range<int>(0, outer, 1),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i < r.end(); ++i) {
                    const int n = spec.inner_sizes[i];
                    const int base = spec.chunk_offsets[i];
                    tbb::parallel_for(
                        tbb::blocked_range<int>(0, n, g),
                        [&](const tbb::blocked_range<int>& cr) {
                            slotAccumulate(sums, cr.begin(), cr.end(), g, base,
                                           [&](double& acc, int b, int e) {
                                               for (int j = b; j < e; ++j) {
                                                   acc += case3ElemOp(i, j);
                                               }
                                           });
                        },
                        tbb::static_partitioner{});
                }
            },
            tbb::static_partitioner{});

        const double total = slotMerge(sums);
        VERIFY_RESULT(total, spec.totalReference(), 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// 嵌套深度 3：外层 → 中层（每任务 2 段）→ 内层（grain g）。
// 研究深度增加后的收益递减：任务树更深，可窃取粒度更细，调度开销更大。
void case3_nested_depth3(benchmark::State& state) {
    const int outer = static_cast<int>(state.range(0));
    const int g = static_cast<int>(state.range(1));
    const bool skewed = state.range(2) != 0;
    const auto& spec = case3Spec(outer, MEAN_INNER, skewed, g);

    for (auto _ : state) {
        std::vector<std::atomic<double>> sums(spec.total_chunks);

        tbb::parallel_for(
            tbb::blocked_range<int>(0, outer, 1),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i < r.end(); ++i) {
                    const int n = spec.inner_sizes[i];
                    const int base = spec.chunk_offsets[i];
                    tbb::parallel_for(
                        tbb::blocked_range<int>(0, n, n / 2),
                        [&](const tbb::blocked_range<int>& mr) {
                            tbb::parallel_for(
                                tbb::blocked_range<int>(mr.begin(), mr.end(), g),
                                [&](const tbb::blocked_range<int>& cr) {
                                    slotAccumulate(sums, cr.begin(), cr.end(), g,
                                                   base,
                                                   [&](double& acc, int b, int e) {
                                                       for (int j = b; j < e; ++j) {
                                                           acc += case3ElemOp(i, j);
                                                       }
                                                   });
                                },
                                tbb::static_partitioner{});
                        },
                        tbb::static_partitioner{});
                }
            },
            tbb::static_partitioner{});

        const double total = slotMerge(sums);
        VERIFY_RESULT(total, spec.totalReference(), 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// 隔离：与嵌套同结构，但内层包 this_task_arena::isolate——禁止内层任务被
// 其他 worker 窃取。与 case3_nested 同参数配对，差值 = 跨层级窃取的收益。
// 预期：outer=8（<12 worker）时付出利用率税，outer=32 时与嵌套持平。
void case3_isolate(benchmark::State& state) {
    const int outer = static_cast<int>(state.range(0));
    const int g = static_cast<int>(state.range(1));
    const bool skewed = state.range(2) != 0;
    const auto& spec = case3Spec(outer, MEAN_INNER, skewed, g);

    for (auto _ : state) {
        std::vector<std::atomic<double>> sums(spec.total_chunks);

        tbb::parallel_for(
            tbb::blocked_range<int>(0, outer, 1),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i < r.end(); ++i) {
                    const int n = spec.inner_sizes[i];
                    const int base = spec.chunk_offsets[i];
                    tbb::this_task_arena::isolate([&] {
                        tbb::parallel_for(
                            tbb::blocked_range<int>(0, n, g),
                            [&](const tbb::blocked_range<int>& cr) {
                                slotAccumulate(sums, cr.begin(), cr.end(), g,
                                               base,
                                               [&](double& acc, int b, int e) {
                                                   for (int j = b; j < e; ++j) {
                                                       acc += case3ElemOp(i, j);
                                                   }
                                               });
                            },
                            tbb::static_partitioner{});
                    });
                }
            },
            tbb::static_partitioner{});

        const double total = slotMerge(sums);
        VERIFY_RESULT(total, spec.totalReference(), 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// 跨框架锚点：双线程池（6 外层 + 6 内层），静态切分线程池下安全的嵌套方案。
// 与嵌套同负载配对：两个静态池之间没有跨池平衡，外层负载不均时尾端空闲无法回收。
void case3_threadpool_two_level(benchmark::State& state) {
    const bool skewed = state.range(0) != 0;
    const int g = 256;
    const auto& spec = case3Spec(32, MEAN_INNER, skewed, g);

    for (auto _ : state) {
        std::vector<std::future<double>> outer_futures;
        outer_futures.reserve(spec.outer);

        for (int i = 0; i < spec.outer; ++i) {
            outer_futures.push_back(gOuterPool.submitTask([i, &spec]() {
                const int n = spec.inner_sizes[i];
                std::vector<std::future<double>> inner_futures;
                inner_futures.reserve((n + 255) / 256);
                for (int begin = 0; begin < n; begin += 256) {
                    const int end = std::min(begin + 256, n);
                    inner_futures.push_back(gInnerPool.submitTask([i, begin, end]() {
                        double local = 0.0;
                        for (int j = begin; j < end; ++j) {
                            local += case3ElemOp(i, j);
                        }
                        return local;
                    }));
                }
                // 提交顺序归约 = 元素顺序 → 与串行逐位一致
                double inner_sum = 0.0;
                for (auto& f : inner_futures) {
                    inner_sum += f.get();
                }
                return inner_sum;
            }));
        }

        double total = 0.0;
        for (auto& f : outer_futures) {
            total += f.get();
        }
        VERIFY_RESULT(total, spec.totalReference(), 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// ---- 门控：教程 §4.1 死锁声明实测 ----
// 外层 body = {lock(std::mutex); 内层 parallel_for; unlock;}。
// 死锁机理：worker A 持锁执行外层任务；worker B 窃取 A 的内层 chunk 完成后，
// 再窃取另一个外层任务 → 阻塞在同一把锁上；A 等待被 B 窃走的内层 chunk → 死锁。
// 默认不注册（死锁后看门狗 abort 会杀死进程）；单独运行：
//   RUN_TBB_STEAL_DEMO=1 DEADLOCK_TIMEOUT_SECONDS=30 ./tbb_benchmark \
//       --benchmark_filter='case3_tbb_steal_deadlock'
void case3_tbb_steal_deadlock_demo(benchmark::State& state) {
    const int outer = static_cast<int>(state.range(0));
    const int inner = static_cast<int>(state.range(1));
    const char* timeout_env = std::getenv("DEADLOCK_TIMEOUT_SECONDS");
    const int timeout_seconds = timeout_env ? std::atoi(timeout_env) : 30;

    std::mutex mtx;

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
                // stderr 而非 stdout：abort() 不冲刷 stdio 缓冲区
                std::fprintf(stderr,
                             "DEADLOCK CONFIRMED: nested parallel_for + mutex "
                             "deadlocked (outer=%d, inner=%d) for over %ds\n",
                             outer, inner, timeout_seconds);
                std::abort();
            }
        });

        tbb::parallel_for(0, outer, [&](int i) {
            std::lock_guard<std::mutex> lock(mtx);
            tbb::parallel_for(
                tbb::blocked_range<int>(0, inner, 256),
                [&](const tbb::blocked_range<int>& r) {
                    double acc = 0.0;
                    for (int j = r.begin(); j < r.end(); ++j) {
                        acc += case3ElemOp(i, j);
                    }
                    benchmark::DoNotOptimize(acc);
                });
        });

        done.store(true);
        watchdog.join();
        std::printf("UNEXPECTED: steal deadlock demo completed - analysis wrong?\n");
    }
}

// 修复版：内层包 this_task_arena::isolate，禁止内层任务被窃取 →
// 持锁 worker 独自完成内层工作并释放锁，其他 worker 阻塞在锁上而非偷走内层任务。
// 与 _demo 同为门控注册，先跑本行验证修复有效，再跑 _demo 验证死锁存在。
void case3_tbb_steal_deadlock_fixed(benchmark::State& state) {
    const int outer = static_cast<int>(state.range(0));
    const int inner = static_cast<int>(state.range(1));

    std::mutex mtx;

    for (auto _ : state) {
        tbb::parallel_for(0, outer, [&](int i) {
            std::lock_guard<std::mutex> lock(mtx);
            tbb::this_task_arena::isolate([&] {
                tbb::parallel_for(
                    tbb::blocked_range<int>(0, inner, 256),
                    [&](const tbb::blocked_range<int>& r) {
                        double acc = 0.0;
                        for (int j = r.begin(); j < r.end(); ++j) {
                            acc += case3ElemOp(i, j);
                        }
                        benchmark::DoNotOptimize(acc);
                    });
            });
        });
    }
}

BENCHMARK(case3_serial_nested)
    ->Args({0})
    ->Args({1})
    ->Unit(benchmark::kMillisecond);

// 头部参数：outer=32, inner≈8000, g=256, uniform, depth=2
BENCHMARK(case3_flat_2d)
    ->Args({256, 0})
    ->Args({4096, 0})
    ->Args({256, 1})
    ->Args({4096, 1})
    ->Unit(benchmark::kMillisecond);

// 嵌套轴：头部配对 / outer=8（<12 worker）/ g_inner∈{1,4096} / 偏斜
BENCHMARK(case3_nested)
    ->Args({32, 256, 0})
    ->Args({8, 256, 0})
    ->Args({32, 1, 0})
    ->Args({32, 4096, 0})
    ->Args({32, 256, 1})
    ->Unit(benchmark::kMillisecond);

// 深度 3：与嵌套头部同参数
BENCHMARK(case3_nested_depth3)
    ->Args({32, 256, 0})
    ->Unit(benchmark::kMillisecond);

// 隔离：与嵌套同参数配对（outer=8 轴 + 头部）
BENCHMARK(case3_isolate)
    ->Args({32, 256, 0})
    ->Args({8, 256, 0})
    ->Unit(benchmark::kMillisecond);

// 双池锚点：均匀/偏斜
BENCHMARK(case3_threadpool_two_level)
    ->Args({0})
    ->Args({1})
    ->Unit(benchmark::kMillisecond);

// 环境变量门控注册（全局作用域只能写声明，语句必须包在 lambda 初始化器里）
static bool g_register_steal_demo = []() {
    if (std::getenv("RUN_TBB_STEAL_DEMO")) {
        BENCHMARK(case3_tbb_steal_deadlock_fixed)
            ->Args({256, 4096})
            ->Unit(benchmark::kMillisecond);
        BENCHMARK(case3_tbb_steal_deadlock_demo)
            ->Args({256, 4096})
            ->Unit(benchmark::kMillisecond);
    }
    return true;
}();
