#include <benchmark/benchmark.h>
#include <oneapi/tbb.h>

#include <future>
#include <thread>
#include <vector>

#include "bench_common.h"
#include "threadPool.h"

// Case 1 — 分区器 × 粒度 × 负载形态。
//
// 研究问题：分组策略的最佳实践——auto/static/simple/affinity 四种分区器
// 在不同 grain size 与负载形态（均匀/阶梯偏斜）下表现如何？工作窃取何时值钱？
//
// 配对对比：同一负载（elemCost(i, amp) 纯函数），同一槽位归约，
// 唯一变量是分区器 + 粒度 + 偏斜三轴。
// 控制组/基线：case1_serial（串行基线）；case1_threadpool_static_chunk
// （静态分片线程池，与 static_partitioner 同调度形状——跨框架锚点）。
//
// 旧版设计缺陷（已修正）：
// - 旧「显式 chunk vs auto_partitioner」两行实际都是 auto_partitioner，
//   只差 grain size 提示（blocked_range 构造参数），对比无效——v2 每格显式指定分区器；
// - 旧 simple_partitioner 归因「无 work stealing / 负载不均衡」错误：
//   grainsize=1 的 1M 单元素任务 + 共享 cache line 原子争用才是慢因。
//   v2 用槽位归约消灭共享原子，并对 simple 做全粒度扫描，粒度轴单独归因。

namespace {

    using namespace StdThreadPool;
    // 与 TBB 默认 arena（12 worker）同线程数的静态分片线程池，构造在计时窗外。
    ThreadPool gPool(std::thread::hardware_concurrency());

    // 万能引用：auto/static/simple 以右值绑定 const& 重载，
    // affinity_partitioner 以左值绑定非 const 引用重载（它要跨迭代更新划分树）。
    template <typename Partitioner>
    void runTbbPartitioner(benchmark::State& state, Partitioner&& partitioner) {
        const int grain = static_cast<int>(state.range(0));
        const int amp = static_cast<int>(state.range(1));
        const double ref = case1Reference(amp);
        const int slots = TOTAL_WORK / grain;

        for (auto _ : state) {
            std::vector<std::atomic<double>> sums(slots);

            tbb::parallel_for(
                tbb::blocked_range<int>(0, TOTAL_WORK, grain),
                [&](const tbb::blocked_range<int>& r) {
                    slotAccumulate(sums, r.begin(), r.end(), grain, 0,
                                   [&](double& acc, int b, int e) {
                                       for (int i = b; i < e; ++i) {
                                           acc += elemCost(i, amp);
                                       }
                                   });
                },
                std::forward<Partitioner>(partitioner));

            const double total = slotMerge(sums);
            VERIFY_RESULT(total, ref, 1e-9);
            benchmark::DoNotOptimize(total);
        }
    }

}  // namespace

// 串行基线：量化各偏斜档位的负载本身成本。
void case1_serial(benchmark::State& state) {
    const int amp = static_cast<int>(state.range(0));
    const double ref = case1Reference(amp);

    for (auto _ : state) {
        double total = 0.0;
        for (int i = 0; i < TOTAL_WORK; ++i) {
            total += elemCost(i, amp);
        }
        VERIFY_RESULT(total, ref, 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// auto_partitioner：默认分区器，自适应决定分裂深度。
void case1_tbb_auto_partitioner(benchmark::State& state) {
    runTbbPartitioner(state, tbb::auto_partitioner{});
}

// static_partitioner：静态均分，分裂次数只由 grain 决定，不做窃取驱动的再平衡。
void case1_tbb_static_partitioner(benchmark::State& state) {
    runTbbPartitioner(state, tbb::static_partitioner{});
}

// simple_partitioner：按 grain 切到最细，无工作窃取。
// 旧版结论「禁用」建立在其唯一测过的 grainsize=1 上——本矩阵给出它在合理粒度下的真实表现。
void case1_tbb_simple_partitioner(benchmark::State& state) {
    runTbbPartitioner(state, tbb::simple_partitioner{});
}

// affinity_partitioner：缓存上次分裂树，重复调用同形负载时复用划分。
// 注意：划分树只对同一 Range 形状（含 grain）有效——跨 Args 行共用一个
// partitioner 会把 grain=1 的树复用到 grain=256，产生非法分裂点。
// Google Benchmark 每个 Args 组合单独调用一次本函数，因此函数内构造
// （非 static）恰好做到「跨迭代复用、跨参数隔离」。实测验证了这一点：
// static 版本在 /256/0 行丢部分和（VERIFY FAILED，误差 0.8%）。
void case1_tbb_affinity_partitioner(benchmark::State& state) {
    tbb::affinity_partitioner ap;
    runTbbPartitioner(state, ap);
}

// 跨框架锚点：静态分片线程池（chunk = grain，与 static_partitioner 同调度形状）。
// 与 static_partitioner 的配对差值 = TBB 调度基础设施本身的成本。
void case1_threadpool_static_chunk(benchmark::State& state) {
    const int grain = static_cast<int>(state.range(0));
    const int amp = static_cast<int>(state.range(1));
    const double ref = case1Reference(amp);
    const int chunks = TOTAL_WORK / grain;

    for (auto _ : state) {
        std::vector<std::future<double>> futures;
        futures.reserve(chunks);
        for (int c = 0; c < chunks; ++c) {
            const int begin = c * grain;
            const int end = begin + grain;
            futures.push_back(gPool.submitTask([begin, end, amp]() {
                double local = 0.0;
                for (int i = begin; i < end; ++i) {
                    local += elemCost(i, amp);
                }
                return local;
            }));
        }

        // 提交顺序归约 = 元素顺序 → 与串行逐位一致
        double total = 0.0;
        for (auto& f : futures) {
            total += f.get();
        }
        VERIFY_RESULT(total, ref, 1e-9);
        benchmark::DoNotOptimize(total);
    }
}

// 全矩阵 8 组 Args{grain, amp}：{1, 256, 4096, 65536} × 偏斜 {0, 8}。
// 串行只按 amp 分 2 行。
BENCHMARK(case1_serial)
    ->Args({0})
    ->Args({8})
    ->Unit(benchmark::kMillisecond);

#define CASE1_MATRIX(FN)                                             \
    BENCHMARK(FN)                                                    \
        ->Args({1, 0})                                               \
        ->Args({256, 0})                                             \
        ->Args({4096, 0})                                            \
        ->Args({65536, 0})                                           \
        ->Args({1, 8})                                               \
        ->Args({256, 8})                                             \
        ->Args({4096, 8})                                            \
        ->Args({65536, 8})                                           \
        ->Unit(benchmark::kMillisecond)

CASE1_MATRIX(case1_tbb_auto_partitioner);
CASE1_MATRIX(case1_tbb_static_partitioner);
CASE1_MATRIX(case1_tbb_simple_partitioner);
CASE1_MATRIX(case1_tbb_affinity_partitioner);
CASE1_MATRIX(case1_threadpool_static_chunk);
