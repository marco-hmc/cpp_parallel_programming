// case2_granularity_memory.cpp
// Question: When bottleneck is MEMORY bandwidth (not CPU), does task
//           granularity affect performance differently?
//
// Variable: grain (chunk size) ∈ {8, 64, 1024, 16384, 65536}
// Fixed:    memory-bound workload (random-access 128MB array), 8M elements
// Strategies: same set as case1 + affinity_partitioner

#include <benchmark/benchmark.h>

#include <iostream>

#include "strategies.h"

using namespace ppl_strategy;

// ====== Serial baseline ======
static void case2_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_serial(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

// ====== Chunked — the granularity variable ======
static void case2_ppl_chunked(benchmark::State& state) {
    int grain = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_ppl_chunked(memory_work, kMemElements, grain);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

// ====== Partitioner-based strategies ======
static void case2_ppl_auto(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_auto(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

static void case2_ppl_static(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_static(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

static void case2_ppl_affinity(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_affinity(memory_work, kMemElements);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

// ====== Thread pool ======
static void case2_pool(benchmark::State& state) {
    int parts = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_pool(memory_work, kMemElements, parts);
        benchmark::DoNotOptimize(result);
    }
    benchmark::ClobberMemory();
}

// ====== Registration ======
BENCHMARK(case2_serial)->Unit(benchmark::kMillisecond);

BENCHMARK(case2_ppl_chunked)
    ->Arg(kGrainUltraFine)
    ->Arg(kGrainFine)
    ->Arg(kGrainMedium)
    ->Arg(kGrainCoarse)
    ->Arg(kGrainExtraCoarse)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(case2_ppl_auto)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_ppl_static)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_ppl_affinity)->Unit(benchmark::kMillisecond);

static int hw = static_cast<int>(std::thread::hardware_concurrency());
BENCHMARK(case2_pool)->Arg(hw)->Arg(hw * 4)->Unit(benchmark::kMillisecond);

namespace {
int _case2_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "[case2] hardware_concurrency = " << hw
              << ", total elements = " << kMemElements
              << ", memory array = "
              << (kMemArrayElements * sizeof(double)) / (1024 * 1024)
              << " MB" << std::endl;
    return 0;
}();
}  // namespace
