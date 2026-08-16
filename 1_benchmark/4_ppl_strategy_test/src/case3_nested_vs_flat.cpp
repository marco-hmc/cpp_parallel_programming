// case3_nested_vs_flat.cpp
// Question: When work is naturally 2D (outer × inner subtasks), should you
//           nest two parallel_for calls, flatten to one, or partition manually?
//
// Variable: decomposition shape (all same total = 4M elements)
//   - Deep:    5000 outer × 800 inner  (many small outer tasks)
//   - Medium:  1000 outer × 4000 inner (balanced)
//   - Shallow: 100  outer × 40000 inner (few large outer tasks)
// Fixed: CPU-bound workload
// Strategies: serial, nested_pf, flat_chunked, flat_auto, outer_only,
//             task_group_nested, pool_flat

#include <benchmark/benchmark.h>

#include <iostream>

#include "strategies.h"

using namespace ppl_strategy;

// ====== Serial baseline — shared across all shapes ======
static void case3_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_serial(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Nested parallel_for (varying decomposition shape) ======
static void case3_ppl_nested(benchmark::State& state) {
    int outer = static_cast<int>(state.range(0));
    int inner = static_cast<int>(state.range(1));
    for (auto _ : state) {
        double result = run_ppl_nested(cpu_work, kCpuElements, outer, inner);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Flattened: single parallel_for, chunked at optimal (medium) grain ======
static void case3_flat_chunked(benchmark::State& state) {
    for (auto _ : state) {
        double result =
            run_ppl_chunked(cpu_work, kCpuElements, kGrainMedium);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Flattened: single parallel_for, auto partitioner ======
static void case3_flat_auto(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_auto(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Outer-only: parallel on outer, serial on inner ======
static void case3_outer_only(benchmark::State& state) {
    int outer = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_ppl_outer_only(cpu_work, kCpuElements, outer);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Nested task_group ======
static void case3_task_group_nested(benchmark::State& state) {
    int outer = static_cast<int>(state.range(0));
    int inner = static_cast<int>(state.range(1));
    for (auto _ : state) {
        double result =
            run_ppl_task_group_nested(cpu_work, kCpuElements, outer, inner);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Thread pool: manual flat partition ======
static void case3_pool_flat(benchmark::State& state) {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        double result = run_pool(cpu_work, kCpuElements, hw * 4);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Registration ======
BENCHMARK(case3_serial)->Unit(benchmark::kMillisecond);

// Flat strategies (no shape parameter — total is always the same)
BENCHMARK(case3_flat_chunked)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_flat_auto)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_pool_flat)->Unit(benchmark::kMillisecond);

// Shape-dependent strategies
// Deep:    5000 outer × 800 inner
BENCHMARK(case3_ppl_nested)
    ->Args({kNestDeepOuter, kNestDeepInner})
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case3_outer_only)
    ->Arg(kNestDeepOuter)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case3_task_group_nested)
    ->Args({kNestDeepOuter, kNestDeepInner})
    ->Unit(benchmark::kMillisecond);

// Medium:  1000 outer × 4000 inner
BENCHMARK(case3_ppl_nested)
    ->Args({kNestMidOuter, kNestMidInner})
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case3_outer_only)
    ->Arg(kNestMidOuter)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case3_task_group_nested)
    ->Args({kNestMidOuter, kNestMidInner})
    ->Unit(benchmark::kMillisecond);

// Shallow: 100 outer × 40000 inner
BENCHMARK(case3_ppl_nested)
    ->Args({kNestShallowOuter, kNestShallowInner})
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case3_outer_only)
    ->Arg(kNestShallowOuter)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case3_task_group_nested)
    ->Args({kNestShallowOuter, kNestShallowInner})
    ->Unit(benchmark::kMillisecond);

static int hw = static_cast<int>(std::thread::hardware_concurrency());

namespace {
int _case3_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "[case3] hardware_concurrency = " << hw
              << ", total elements = " << kCpuElements << std::endl;
    return 0;
}();
}  // namespace
