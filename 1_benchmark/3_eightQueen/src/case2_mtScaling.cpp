// case2_mtScaling.cpp
// Question: How does multi-threaded speedup scale with problem size?
//
// The N-Queens search tree grows O(N!) — at larger N, the computation
// dominates thread management overhead, yielding better speedup.
//
// Variables: N ∈ {12, 14, 15, 16}, mode ∈ {serial, mt}
//   Fixed: bit-optimized algorithm (best serial baseline)
//   MT uses: top-level first-row split + mirror symmetry
//
// Hypothesis:
//   - Small N (12): modest speedup, thread overhead visible
//   - Large N (16): near-linear speedup, computation dominates
//   - This demonstrates that "embarrassingly parallel" still has
//     a minimum problem size for efficiency.

#include <benchmark/benchmark.h>

#include <iostream>
#include <thread>

#include "nqueens.h"

using namespace nqueens;

// ---- Serial ----
static void case2_serial(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        int64_t count = solve_bitopt_serial(N);
        benchmark::DoNotOptimize(count);
    }
}

// ---- Multi-threaded ----
static void case2_mt(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        int64_t count = solve_bitopt_mt(N, hw);
        benchmark::DoNotOptimize(count);
    }
}

// ---- Registration ----
BENCHMARK(case2_serial)->Arg(12)->Arg(14)->Arg(15)->Arg(16)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case2_mt)->Arg(12)->Arg(14)->Arg(15)->Arg(16)
    ->Unit(benchmark::kMillisecond);

namespace {
int _case2_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "[case2] MT scaling: bit-opt serial vs MT, "
              << "N ∈ {12,14,15,16}, threads=" << hw << std::endl;
    return 0;
}();
}  // namespace
