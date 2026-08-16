// case1_algorithmComparison.cpp
// Question: For pure serial N-Queens solving, which algorithm is fastest?
//
// Compares three fundamentally different approaches at the same problem size:
//   1. Classic backtracking with bool arrays
//   2. Dancing Links (Knuth Algorithm X / exact cover)
//   3. Bit-optimized backtracking (uint32_t masks + hardware CTZ)
//
// Variables: N ∈ {12, 14, 15}
//   N=12: ~14K solutions, fast enough for many iterations
//   N=14: ~365K solutions, moderate runtime
//   N=15: ~2.3M solutions, stress test
//
// Expected ranking: bit-opt >> DL > classic backtracking
// Bit-opt should be 10-30× faster than classic due to:
//   - register-resident state (no memory loads for column/diag checks)
//   - __builtin_ctz = single CPU instruction for "find next candidate"
//   - ALU-parallel attack-set computation via bitwise OR+NOT

#include <benchmark/benchmark.h>

#include <iostream>

#include "nqueens.h"

using namespace nqueens;

// ---- Serial baselines at fixed N ----

static void case1_backtracking(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        int64_t count = solve_backtracking_serial(N);
        benchmark::DoNotOptimize(count);
    }
}

static void case1_dancing_links(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        int64_t count = solve_dancing_links(N);
        benchmark::DoNotOptimize(count);
    }
}

static void case1_bitopt(benchmark::State& state) {
    int N = static_cast<int>(state.range(0));
    for (auto _ : state) {
        int64_t count = solve_bitopt_serial(N);
        benchmark::DoNotOptimize(count);
    }
}

// ---- Registration ----
// N=12: fast, many iterations
BENCHMARK(case1_backtracking)->Arg(12)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_dancing_links)->Arg(12)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_bitopt)->Arg(12)->Unit(benchmark::kMillisecond);

// N=14: moderate
BENCHMARK(case1_backtracking)->Arg(14)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_dancing_links)->Arg(14)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_bitopt)->Arg(14)->Unit(benchmark::kMillisecond);

// N=15: stress test
BENCHMARK(case1_backtracking)->Arg(15)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_dancing_links)->Arg(15)->Unit(benchmark::kMillisecond);
BENCHMARK(case1_bitopt)->Arg(15)->Unit(benchmark::kMillisecond);

namespace {
int _case1_info = []() -> int {
    std::cout << "[case1] Algorithm comparison: backtracking vs Dancing Links "
                 "vs bit-optimized (serial)"
              << std::endl;
    return 0;
}();
}  // namespace
