// case1_granularity_cpu.cpp
// Question: For pure CPU work, what chunk size is optimal per PPL strategy?
//
// Variable: grain (chunk size) ∈ {8, 64, 1024, 16384, 65536}
// Fixed:    CPU-bound workload, same total work (4M elements)
// Strategies: serial baseline, chunked(5 grains), auto, static, simple

#include <benchmark/benchmark.h>

#include <iostream>

#include "strategies.h"

using namespace ppl_strategy;

// ====== Serial baseline ======
static void case1_serial(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_serial(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Chunked (explicit grain control) — the granularity variable ======
static void case1_ppl_chunked(benchmark::State& state) {
    int grain = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_ppl_chunked(cpu_work, kCpuElements, grain);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Partitioner-based strategies (grain implied by partitioner) ======
static void case1_ppl_auto(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_auto(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

static void case1_ppl_static(benchmark::State& state) {
    for (auto _ : state) {
        double result = run_ppl_static(cpu_work, kCpuElements);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Additional reference: thread pool ======
static void case1_pool(benchmark::State& state) {
    int parts = static_cast<int>(state.range(0));
    for (auto _ : state) {
        double result = run_pool(cpu_work, kCpuElements, parts);
        benchmark::DoNotOptimize(result);
    }
}

// ====== Registration ======
BENCHMARK(case1_serial)->Unit(benchmark::kMillisecond);

// Chunked at 5 granularities: ultra_fine → extra_coarse
BENCHMARK(case1_ppl_chunked)
    ->Arg(kGrainUltraFine)    // 8:   500K tasks — scheduling overhead ceiling
    ->Arg(kGrainFine)         // 64:  62.5K tasks
    ->Arg(kGrainMedium)       // 1K:  ~3.9K tasks — expected sweet spot
    ->Arg(kGrainCoarse)       // 16K: 244 tasks
    ->Arg(kGrainExtraCoarse)  // 64K: 61 tasks — load imbalance floor
    ->Unit(benchmark::kMillisecond);

// PPL built-in partitioners
BENCHMARK(case1_ppl_auto)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_ppl_static)->Unit(benchmark::kMillisecond);

// Thread pool: hw_concurrency partitions (perfect static) and 4x (oversubscribed)
static int hw = static_cast<int>(std::thread::hardware_concurrency());
BENCHMARK(case1_pool)->Arg(hw)->Arg(hw * 4)->Unit(benchmark::kMillisecond);
namespace {
    int _case1_info = []() -> int {
        int hw = static_cast<int>(std::thread::hardware_concurrency());
        std::cout << "[case1] hardware_concurrency = " << hw
                  << ", total elements = " << kCpuElements << std::endl;
        return 0;
    }();
}  // namespace
