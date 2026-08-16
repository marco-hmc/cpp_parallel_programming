// case4_symmetry.cpp
// Question: How much does mirror symmetry actually help?
//
// N-Queens has horizontal mirror symmetry: for every solution, its
// left-right mirror is also a solution. The MT solver exploits this
// by only computing the left half (plus middle column if N is odd).
//
// This case isolates the symmetry contribution:
//   - MT with symmetry:  solves (N+1)/2 first-row columns, doubles results
//   - MT without symmetry: solves all N first-row columns
//
// Variable: N ∈ {12, 14, 15, 16}
//   Hypothesis: symmetry ≈ 2× speedup (slightly less due to odd-N middle col)
//
// Also tests thread count scaling at fixed N=15:
//   threads ∈ {1, 2, 4, hw_concurrency}
//   Shows whether sub-problem count (=N/2 ≈ 8 for N=15) limits scalability.

#include <benchmark/benchmark.h>

#include <iostream>
#include <thread>

#include "nqueens.h"

using namespace nqueens;

// ---- Symmetry on/off ----
static void case4_mt_with_symmetry(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        int64_t count = solve_bitopt_mt(N, hw);
        benchmark::DoNotOptimize(count);
    }
}

static void case4_mt_without_symmetry(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    for (auto _ : state) {
        int64_t count = solve_bitopt_mt_no_symmetry(N, hw);
        benchmark::DoNotOptimize(count);
    }
}

// ---- Thread-count sweep at fixed N=15 ----
// Vary thread count from 1 (serial-equivalent) to hw_concurrency.
// Shows scalability limits: with N=15 there are only 8 sub-problems
// (after symmetry), so >8 threads cannot all be utilized.

static void case4_thread_sweep(benchmark::State& state) {
    int num_threads = static_cast<int>(state.range(0));
    constexpr int kN = 15;
    for (auto _ : state) {
        int64_t count = solve_bitopt_mt(kN, num_threads);
        benchmark::DoNotOptimize(count);
    }
}

// ---- Registration ----
BENCHMARK(case4_mt_with_symmetry)->Arg(12)->Arg(14)->Arg(15)->Arg(16)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case4_mt_without_symmetry)->Arg(12)->Arg(14)->Arg(15)->Arg(16)
    ->Unit(benchmark::kMillisecond);

// Thread-count sweep at N=15
static int hw = static_cast<int>(std::thread::hardware_concurrency());
BENCHMARK(case4_thread_sweep)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(6)->Arg(8)->Arg(hw)
    ->Unit(benchmark::kMillisecond);

namespace {
int _case4_info = []() -> int {
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "[case4] Symmetry benefit: with/without mirror symmetry, "
              << "N ∈ {12,14,15,16}, threads=" << hw << std::endl;
    std::cout << "[case4] Thread sweep at N=15: "
              << "threads ∈ {1,2,4,6,8," << hw << "}" << std::endl;
    return 0;
}();
}  // namespace
