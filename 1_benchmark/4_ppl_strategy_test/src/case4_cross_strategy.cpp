// case4_cross_strategy.cpp
// Question: Across (granularity × workload_type × strategy), what's the
//           best combination?
//
// Grid: 3 grains × 2 workloads × 4 strategies = 24 benchmarks
//   Grains:    fine(64), medium(1024), coarse(16384)
//   Workloads: cpu_work, memory_work
//   Strategies: serial, ppl_auto, ppl_chunked, ppl_static, run_pool

#include <benchmark/benchmark.h>

#include <iostream>

#include "strategies.h"

using namespace ppl_strategy;

// ============================================================
// CPU-bound block
// ============================================================

static void case4_cpu_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_serial(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

static void case4_cpu_auto(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_auto(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

static void case4_cpu_chunked(benchmark::State& state) {
    int grain = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_ppl_chunked(cpu_work, kCpuElements, grain);
        benchmark::DoNotOptimize(result);
    }
}

static void case4_cpu_static(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_static(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

static void case4_cpu_pool(benchmark::State& state) {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        double result = run_pool(cpu_work, kCpuElements, hw * 4);
        benchmark::DoNotOptimize(result);
    }
}

// ============================================================
// Memory-bound block
// ============================================================

static void case4_mem_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_serial(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

static void case4_mem_auto(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_auto(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

static void case4_mem_chunked(benchmark::State& state) {
    int grain = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_ppl_chunked(memory_work, kMemElements, grain);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

static void case4_mem_static(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_static(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

static void case4_mem_pool(benchmark::State& state) {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        double result = run_pool(memory_work, kMemElements, hw * 4);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

// ============================================================
// Registration
// ============================================================

// CPU: serial + auto + static + pool (grain-independent)
BENCHMARK(case4_cpu_serial)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_cpu_auto)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_cpu_static)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_cpu_pool)->Unit(benchmark::kMillisecond);

// CPU: chunked at 3 grains
BENCHMARK(case4_cpu_chunked)
    ->Arg(kGrainFine)       // 64:   fine
    ->Arg(kGrainMedium)     // 1024: medium
    ->Arg(kGrainCoarse)     // 16K:  coarse
    ->Unit(benchmark::kMillisecond);

// Memory: serial + auto + static + pool
BENCHMARK(case4_mem_serial)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_mem_auto)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_mem_static)->Unit(benchmark::kMillisecond);
BENCHMARK(case4_mem_pool)->Unit(benchmark::kMillisecond);

// Memory: chunked at 3 grains
BENCHMARK(case4_mem_chunked)
    ->Arg(kGrainFine)
    ->Arg(kGrainMedium)
    ->Arg(kGrainCoarse)
    ->Unit(benchmark::kMillisecond);

static int hw = static_cast<int>(std::thread::hardware_concurrency());

namespace {
int _case4_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "[case4] hardware_concurrency = " << hw << std::endl;
    std::cout << "[case4] cpu elements = " << kCpuElements
              << ", mem elements = " << kMemElements << std::endl;
    return 0;
}();
}  // namespace
