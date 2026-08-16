// case3_optimizationBreakdown.cpp
// Question: What is the individual contribution of each optimization?
//
// Starts from the classic backtracking baseline and layers on improvements:
//   1. Classic backtracking (bool arrays, serial)      — baseline
//   2. + Bit-optimized (bitmasks, hardware CTZ)         — algorithm improvement
//   3. + Multi-threaded (top-level split + symmetry)    — parallelism
//   4. = Bit-optimized + Multi-threaded                 — combined
//
// This isolates: algorithm gain (1→2), parallelism gain on classic (1→3),
// and parallelism gain on optimized (2→4).
//
// Fixed: N=15 (large enough to benefit from parallelism, not too slow)
//
// Also tests the classic backtracking MT variant to show that parallelism
// alone can help even without the bit-opt improvement.

#include <benchmark/benchmark.h>

#include <iostream>
#include <thread>

#include "nqueens.h"

using namespace nqueens;

static void case3_classic_serial(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        int64_t count = solve_backtracking_serial(N);
        benchmark::DoNotOptimize(count);
    }
}

static void case3_classic_mt(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        int64_t count = solve_backtracking_mt(N, hw);
        benchmark::DoNotOptimize(count);
    }
}

static void case3_bitopt_serial(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        int64_t count = solve_bitopt_serial(N);
        benchmark::DoNotOptimize(count);
    }
}

static void case3_bitopt_mt(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        int64_t count = solve_bitopt_mt(N, hw);
        benchmark::DoNotOptimize(count);
    }
}

// ---- Registration ----
// N=14: moderate size, all variants complete in reasonable time
BENCHMARK(case3_classic_serial)->Arg(14)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_classic_mt)->Arg(14)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_bitopt_serial)->Arg(14)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_bitopt_mt)->Arg(14)->Unit(benchmark::kMillisecond);

// N=15: larger, better shows parallelism benefit
BENCHMARK(case3_classic_serial)->Arg(15)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_classic_mt)->Arg(15)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_bitopt_serial)->Arg(15)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_bitopt_mt)->Arg(15)->Unit(benchmark::kMillisecond);

namespace {
int _case3_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "[case3] Optimization breakdown: "
              << "classic / classic-MT / bitopt / bitopt-MT, "
              << "N ∈ {14,15}, threads=" << hw << std::endl;
    return 0;
}();
}  // namespace
