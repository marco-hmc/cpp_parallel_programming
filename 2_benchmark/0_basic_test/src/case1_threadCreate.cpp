#include <benchmark/benchmark.h>

#include <thread>

#include "utils.h"

NO_OPTIMIZE void case1_create_threads(benchmark::State& state) {
    const int thread_count = state.range(0);
    for (auto _ : state) {
        for (int i = 0; i < thread_count; ++i) {
            std::thread t([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            });
            t.detach();
        }
    }
}

BENCHMARK(case1_create_threads)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond);
