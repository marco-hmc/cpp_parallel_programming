// case5_uneven_tasks.cpp
// Question: For CPU tasks with high cost variance (load imbalance), which
//           partition strategy to use? When does nested parallel_for help?
//
// Task distribution (2000 tasks, ~4M total elements):
//   20 heavy  tasks (100K elements each) = 2.0M (50%) — clustered at front
//   200 medium tasks (  8K elements each) = 1.6M (40%)
//   1780 light tasks (~225 elements each) = 0.4M (10%)
//
// Key insight: heavy tasks concentrated at indices 0..19 create worst-case
// load imbalance for static partitioners. The experiment tests whether
// work-stealing partitioners (auto, affinity) and nested decomposition
// can mitigate this.
//
// Variables:
//   Partition strategy: auto, static, affinity, chunked(grain)
//   Decomposition:      flat, nested(outer_parts), outer-only(outer_parts)
// Fixed: task distribution, total elements ≈ kCpuElements

#include <benchmark/benchmark.h>

#include <atomic>
#include <iostream>
#include <vector>

#include "strategies.h"

using namespace ppl_strategy;

// ======================================================================
// 1. Serial baseline — the reference for all speedup calculations
// ======================================================================
static void case5_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = 0.0;
        for (int i = 0; i < kUnevenNumTasks; ++i) {
            result += uneven_cpu_work(i);
        }
        benchmark::DoNotOptimize(result);
    }
}

// ======================================================================
// 2. Flat parallel_for — auto_partitioner (work-stealing)
//    Hypothesis: best performer. The scheduler detects per-iteration
//    cost variance and steals work from busy threads.
// ======================================================================
static void case5_ppl_auto(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, kUnevenNumTasks, [&](int i) {
            double local = uneven_cpu_work(i);
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// ======================================================================
// 3. Flat parallel_for — static_partitioner (equal range splits)
//    Hypothesis: worst performer. Equal-sized ranges get tasks of very
//    different total cost. No work-stealing → thread with the heavy
//    range is the bottleneck.
// ======================================================================
static void case5_ppl_static(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(
            0, kUnevenNumTasks,
            [&](int i) {
                double local = uneven_cpu_work(i);
                sum.fetch_add(local, std::memory_order_relaxed);
            },
            Concurrency::static_partitioner());
        benchmark::DoNotOptimize(sum.load());
    }
}

// ======================================================================
// 4. Flat parallel_for — affinity_partitioner
//    Like auto but remembers range→thread mapping across iterations.
//    Useful when the same task distribution repeats (benchmark loop).
// ======================================================================
static void case5_ppl_affinity(benchmark::State& state) {
    static Concurrency::affinity_partitioner ap;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(
            0, kUnevenNumTasks,
            [&](int i) {
                double local = uneven_cpu_work(i);
                sum.fetch_add(local, std::memory_order_relaxed);
            },
            ap);
        benchmark::DoNotOptimize(sum.load());
    }
}

// ======================================================================
// 5. Chunked — manual grain control, single parallel_for over chunks
//    Tasks within a chunk run serially (amortises schedule overhead).
//    Variable: grain = tasks per chunk.
//      grain=1   → 2000 chunks (fine: high overhead, good balance)
//      grain=10  → 200 chunks
//      grain=50  → 40 chunks
//      grain=200 → 10 chunks (coarse: low overhead, risk of imbalance)
// ======================================================================
static void case5_ppl_chunked(benchmark::State& state) {
    int grain = static_cast<int>(state.range(0));
    int num_chunks = (kUnevenNumTasks + grain - 1) / grain;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, num_chunks, [&](int c) {
            int start = c * grain;
            int end = std::min(start + grain, kUnevenNumTasks);
            double local = 0.0;
            for (int i = start; i < end; ++i) {
                local += uneven_cpu_work(i);
            }
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// ======================================================================
// 6. Nested parallel_for — outer + inner, both levels parallel
//    Outer: parallel_for over outer_parts groups.
//    Inner: parallel_for over each task index within the group.
//    PPL scheduler can steal at both levels — rich work-stealing tree.
//    Variable: outer_parts (10 / 50 / 200).
// ======================================================================
static void case5_ppl_nested(benchmark::State& state) {
    int outer_parts = static_cast<int>(state.range(0));
    int outer_size = (kUnevenNumTasks + outer_parts - 1) / outer_parts;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, outer_parts, [&](int o) {
            int start = o * outer_size;
            int end = std::min(start + outer_size, kUnevenNumTasks);
            if (start >= end) return;
            std::atomic<double> inner_sum{0.0};
            Concurrency::parallel_for(start, end, [&](int i) {
                double local = uneven_cpu_work(i);
                inner_sum.fetch_add(local, std::memory_order_relaxed);
            });
            sum.fetch_add(inner_sum.load(), std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// ======================================================================
// 7. Outer-only — outer parallel, inner SERIAL (control for nested)
//    Same decomposition as nested, but inner work is a plain for-loop.
//    Shows how much overhead the inner parallel_for introduces.
//    When inner work per chunk is small, outer-only may beat nested.
// ======================================================================
static void case5_ppl_outer_only(benchmark::State& state) {
    int outer_parts = static_cast<int>(state.range(0));
    int outer_size = (kUnevenNumTasks + outer_parts - 1) / outer_parts;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, outer_parts, [&](int o) {
            int start = o * outer_size;
            int end = std::min(start + outer_size, kUnevenNumTasks);
            double local = 0.0;
            for (int i = start; i < end; ++i) {
                local += uneven_cpu_work(i);
            }
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// ======================================================================
// 8. Thread pool — manual static partition, no work-stealing
//    Pre-split into N equal chunks, one future per chunk.
//    Reference point: shows the gap between manual static partition
//    and PPL's built-in work-stealing.
// ======================================================================
static void case5_pool(benchmark::State& state) {
    int parts = static_cast<int>(state.range(0));
    static StdThreadPool::ThreadPool pool(
        std::thread::hardware_concurrency());
    int chunk = (kUnevenNumTasks + parts - 1) / parts;
    for (auto _ : state) {
        std::vector<std::future<double>> futures;
        futures.reserve(static_cast<std::size_t>(parts));
        for (int p = 0; p < parts; ++p) {
            int start = p * chunk;
            int end = std::min(start + chunk, kUnevenNumTasks);
            if (start >= end) continue;
            futures.push_back(pool.submitTask([start, end] {
                double local = 0.0;
                for (int i = start; i < end; ++i) {
                    local += uneven_cpu_work(i);
                }
                return local;
            }));
        }
        double sum = 0.0;
        for (auto& f : futures) sum += f.get();
        benchmark::DoNotOptimize(sum);
    }
}

// ======================================================================
// SHUFFLED VARIANTS
// Same 2000 tasks, but randomly permuted (seed=42).  The heavy tasks are
// no longer contiguous — they scatter across indices.  This represents a
// realistic workload where task cost is unpredictable.  Every clustered
// strategy gets a shuffled sibling for head-to-head comparison.
//
// Key question: does the clustered→shuffled change flip the strategy
// ranking?  If so, the clustered result is an artifact of data layout,
// not a general property of the partitioner.
//
// Shuffled implementations are NOT refactored into strategies.h because
// each one differs only in which data array it reads (kUnevenTaskSizes vs
// kUnevenTaskSizesShuffled); duplicating the 8 runner functions would
// bloat the header for a one-line difference.
// ======================================================================

// --- Shuffled: auto_partitioner (default) ---
// With random task layout, auto should distribute heavy tasks evenly
// across threads via work-stealing.  Compare vs static to measure the
// residual benefit of stealing when the layout is already fair.
static void case5_ppl_auto_shuffled(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, kUnevenNumTasks, [&](int i) {
            double local = uneven_cpu_work_shuffled(i);
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// --- Shuffled: static_partitioner (equal range splits, no stealing) ---
// Hypothesis: since heavy tasks are evenly distributed, even equal
// range splits should give each thread ~1-2 heavy tasks.  Static should
// perform close to auto — disproving the clustered result as pathological.
static void case5_ppl_static_shuffled(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(
            0, kUnevenNumTasks,
            [&](int i) {
                double local = uneven_cpu_work_shuffled(i);
                sum.fetch_add(local, std::memory_order_relaxed);
            },
            Concurrency::static_partitioner());
        benchmark::DoNotOptimize(sum.load());
    }
}

// --- Shuffled: nested parallel_for ---
// With random layout, the two-level work-stealing that helped in
// clustered should be unnecessary — flat auto already distributes
// heavy tasks evenly.  Nested adds scheduling overhead without
// a compensating balance improvement.
static void case5_ppl_nested_shuffled(benchmark::State& state) {
    int outer_parts = static_cast<int>(state.range(0));
    int outer_size = (kUnevenNumTasks + outer_parts - 1) / outer_parts;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, outer_parts, [&](int o) {
            int start = o * outer_size;
            int end = std::min(start + outer_size, kUnevenNumTasks);
            if (start >= end) return;
            std::atomic<double> inner_sum{0.0};
            Concurrency::parallel_for(start, end, [&](int i) {
                double local = uneven_cpu_work_shuffled(i);
                inner_sum.fetch_add(local, std::memory_order_relaxed);
            });
            sum.fetch_add(inner_sum.load(), std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// --- Shuffled: affinity_partitioner ---
// Same memory-as-work-stealing but remembers range→thread across
// iterations.  With random layout the remembered mapping is noise.
static void case5_ppl_affinity_shuffled(benchmark::State& state) {
    static Concurrency::affinity_partitioner ap;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(
            0, kUnevenNumTasks,
            [&](int i) {
                double local = uneven_cpu_work_shuffled(i);
                sum.fetch_add(local, std::memory_order_relaxed);
            },
            ap);
        benchmark::DoNotOptimize(sum.load());
    }
}

// --- Shuffled: chunked ---
// Manual grain control with random task order.  Every grain should
// perform similarly since heavy tasks are spread evenly.
static void case5_ppl_chunked_shuffled(benchmark::State& state) {
    int grain = static_cast<int>(state.range(0));
    int num_chunks = (kUnevenNumTasks + grain - 1) / grain;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, num_chunks, [&](int c) {
            int start = c * grain;
            int end = std::min(start + grain, kUnevenNumTasks);
            double local = 0.0;
            for (int i = start; i < end; ++i) {
                local += uneven_cpu_work_shuffled(i);
            }
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// --- Shuffled: outer-only (outer parallel, inner serial) ---
// Outer parallel_for, inner for-loop.  With random layout, each
// outer chunk2019s serial inner work is roughly equal → good balance.
static void case5_ppl_outer_only_shuffled(benchmark::State& state) {
    int outer_parts = static_cast<int>(state.range(0));
    int outer_size = (kUnevenNumTasks + outer_parts - 1) / outer_parts;
    for (auto _ : state) {
        std::atomic<double> sum{0.0};
        Concurrency::parallel_for(0, outer_parts, [&](int o) {
            int start = o * outer_size;
            int end = std::min(start + outer_size, kUnevenNumTasks);
            double local = 0.0;
            for (int i = start; i < end; ++i) {
                local += uneven_cpu_work_shuffled(i);
            }
            sum.fetch_add(local, std::memory_order_relaxed);
        });
        benchmark::DoNotOptimize(sum.load());
    }
}

// --- Shuffled: thread pool ---
// Manual static partition, no work-stealing.  Random distribution
// means equal chunks get roughly equal work → should rival PPL.
static void case5_pool_shuffled(benchmark::State& state) {
    int parts = static_cast<int>(state.range(0));
    static StdThreadPool::ThreadPool pool(
        std::thread::hardware_concurrency());
    int chunk = (kUnevenNumTasks + parts - 1) / parts;
    for (auto _ : state) {
        std::vector<std::future<double>> futures;
        futures.reserve(static_cast<std::size_t>(parts));
        for (int p = 0; p < parts; ++p) {
            int start = p * chunk;
            int end = std::min(start + chunk, kUnevenNumTasks);
            if (start >= end) continue;
            futures.push_back(pool.submitTask([start, end] {
                double local = 0.0;
                for (int i = start; i < end; ++i) {
                    local += uneven_cpu_work_shuffled(i);
                }
                return local;
            }));
        }
        double sum = 0.0;
        for (auto& f : futures) sum += f.get();
        benchmark::DoNotOptimize(sum);
    }
}

// ======================================================================
// Registration
// ======================================================================

// ======================================================================
// REGISTRATION
// ======================================================================
// Each BENCHMARK() / BENCHMARK()->Arg() line adds one row to the output
// table.  The framework runs each row enough iterations to get a stable
// measurement, then reports wall-clock time and CPU time.
//
// Reading the table:
//   auto  50ms  → 50 ms per run on average
//   nested/200  23ms → nested with --benchmark_filter=case5_ppl_nested --benchmark_filter=200
//   chunked/1    38ms → chunked with grain=1 (state.range(0) == 1)
//
// Running a subset:
//   ./exe --benchmark_filter="case5_ppl_auto"         # just auto
//   ./exe --benchmark_filter="case5.*nested"          # all nested variants
//   ./exe --benchmark_filter="case5" --benchmark_min_time=0.5s  # longer runs

// --- [A] 基线：单线程串行 ---
// 所有 speedup 计算的参照物。时间 ≈ 底层 workload 总计算量。
static int hw = static_cast<int>(std::thread::hardware_concurrency());
BENCHMARK(case5_serial)->Unit(benchmark::kMillisecond);

// =========================================================================
// [B] CLUSTERED — 20个重任务连续排在 indices 0..19
// 问题：当重任务扎堆时，哪种 partitioner 能扛住负载不均？
// 这是人为制造的最坏场景 — 用来放大 partitioner 差异。
// =========================================================================

// [B1] 内置 partitioner: auto／static／affinity
//   问：work-stealing 对扎堆重任务有效吗？affinity 有额外收益吗？
BENCHMARK(case5_ppl_auto)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_ppl_static)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_ppl_affinity)->Unit(benchmark::kMillisecond);

// [B2] 手动分块 (chunked):  grain = tasks/chunk, 内层串行
//   问：粒度从极细(1)调到极粗(200)时，负载不均如何恶化？
//   预期：grain↑ → 重任务被锁在少数 chunk 里 → 瓶颈加深
BENCHMARK(case5_ppl_chunked)
    ->Arg(1)->Arg(10)->Arg(50)->Arg(200)
    ->Unit(benchmark::kMillisecond);

// [B3] 嵌套 parallel_for:  outer=分组数, inner=组内并行
//   问：两级 work-stealing 能否把扎堆的重任务"拆碎"分出去？
//   预期：outer 越多 → 重任务被关在越少的 outer group 里
//         → 空闲线程更容易偷到 inner 迭代 → 均衡更好
BENCHMARK(case5_ppl_nested)
    ->Arg(10)->Arg(50)->Arg(200)
    ->Unit(benchmark::kMillisecond);

// [B4] 外并行内串行 (outer-only):  比 nested 少一层 inner parallel_for
//   问：嵌套的优势到底来自"两级并行"还是仅仅来自"外层分组"？
//   这是 nested 的对照组 — 差值 = inner parallel_for 的净收益
BENCHMARK(case5_ppl_outer_only)
    ->Arg(10)->Arg(50)->Arg(200)
    ->Unit(benchmark::kMillisecond);

// [B5] 线程池手动分区:  parts=硬件线程数, parts=4× 过订阅
//   问：手动静态分块(无 work-stealing)跟 PPL 内置 partitioner 差多少？
BENCHMARK(case5_pool)->Arg(hw)->Arg(hw * 4)->Unit(benchmark::kMillisecond);

// =========================================================================
// [C] SHUFFLED — 相同的 2000 个 task, 种子=42 随机打乱
// 问题：把重任务均匀散开后，[B] 的结论哪些还是对的，哪些是假象？
// 这是真实场景的近似 — 大多数情况下你不知道哪个 task 重。
// =========================================================================

// [C1] 内置 partitioner (shuffled)
//   问：shuffle 之后 static 还输 auto 吗？affinity 还有用吗？
BENCHMARK(case5_ppl_auto_shuffled)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_ppl_static_shuffled)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_ppl_affinity_shuffled)->Unit(benchmark::kMillisecond);

// [C2] 手动分块 (shuffled)
//   问：grain 大小在随机分布下还重要吗？
//   预期：不重要 — 因为每个 chunk 的 work 期望值相同
BENCHMARK(case5_ppl_chunked_shuffled)
    ->Arg(1)->Arg(10)->Arg(50)->Arg(200)
    ->Unit(benchmark::kMillisecond);

// [C3] 嵌套 parallel_for (shuffled)
//   问：随机分布下两级 work-stealing 还有额外收益吗？
//   预期：没有 — 平坦 auto 已经足够均匀
BENCHMARK(case5_ppl_nested_shuffled)
    ->Arg(10)->Arg(50)->Arg(200)
    ->Unit(benchmark::kMillisecond);

// [C4] 外并行内串行 (shuffled)
//   问：随机分布时 outer-only 的负载均衡改善了吗？
//   预期：大幅改善 — 每个 outer chunk 的期望工作量相同
BENCHMARK(case5_ppl_outer_only_shuffled)
    ->Arg(10)->Arg(50)->Arg(200)
    ->Unit(benchmark::kMillisecond);

// [C5] 线程池 (shuffled)
//   问：随机分布时手动静态分块能否追上 PPL 内置 partitioner？
BENCHMARK(case5_pool_shuffled)->Arg(hw)->Arg(hw * 4)->Unit(benchmark::kMillisecond);

// ======================================================================
// Startup info
// ======================================================================
namespace {
int _case5_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    // Use only constexpr values here — kUnevenTaskSizes may not be
    // initialized yet (static init order across translation units).
    constexpr int kHeavyCount = 20;
    constexpr int kHeavySize = 100'000;
    constexpr int kMediumCount = 200;
    constexpr int kMediumSize = 8'000;
    constexpr int kLightCount =
        kUnevenNumTasks - kHeavyCount - kMediumCount;  // 1780
    constexpr long long kLightTotal =
        kUnevenTotalElements - kHeavyCount * kHeavySize -
        kMediumCount * kMediumSize;  // 400,000
    constexpr int kLightSize = kLightTotal / kLightCount;  // 224
    constexpr long long kActualTotal =
        kHeavyCount * kHeavySize + kMediumCount * kMediumSize +
        kLightCount * kLightSize;
    std::cout << "[case5] hardware_concurrency = " << hw << std::endl;
    std::cout << "[case5] num_tasks = " << kUnevenNumTasks
              << ", total_elements = " << kActualTotal
              << " (≈" << kUnevenTotalElements << ")" << std::endl;
    std::cout << "[case5] task distribution: heavy(100K)=" << kHeavyCount
              << ", medium(8K)=" << kMediumCount
              << ", light(" << kLightSize << ")=" << kLightCount << std::endl;
    std::cout << "[case5] heavy tasks clustered at indices 0.."
              << (kHeavyCount - 1)
              << " (worst-case for static partitioner)" << std::endl;
    return 0;
}();
}  // namespace
