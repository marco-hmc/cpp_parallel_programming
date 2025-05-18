#include <benchmark/benchmark.h>

#include "utils.h"

namespace {

    NO_OPTIMIZE void countNumberWithLock(int counter) {
        int value = 0;
        benchmark::DoNotOptimize(value);
        std::mutex mtx;
        for (int i = 0; i < counter; ++i) {
            std::lock_guard<std::mutex> lock(mtx);
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void countNumberWithoutLock(int counter) {
        int value = 0;
        benchmark::DoNotOptimize(value);
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

}  // namespace

void case5_with_lock_cost(benchmark::State& state) {
    for (auto _ : state) {
        countNumberWithLock(120'000'000);
    }
}

void case5_without_lock_cost(benchmark::State& state) {
    for (auto _ : state) {
        countNumberWithoutLock(120'000'000);
    }
}

BENCHMARK(case5_with_lock_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case5_without_lock_cost)->Unit(benchmark::kMillisecond);