#include <benchmark/benchmark.h>

#include <thread>
#include <vector>

#include "threadPool.h"
#include "utils.h"

namespace {

    NO_OPTIMIZE void countNumber(int counter) {
        int value = 0;
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void taskNear100ms() { countNumber(240'000'000); }

    using namespace StdThreadPool;
    ThreadPool gPool(std::thread::hardware_concurrency());

}  // namespace

void case2_one_thread_pool(benchmark::State& state) {
    const int parent_task = state.range(0);
    const int child_task = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<void>> futures;
        for (int i = 0; i < parent_task; ++i) {
            futures.emplace_back(gPool.submitTask([child_task]() {
                std::vector<std::future<void>> childFutures;
                childFutures.reserve(child_task);
                for (int j = 0; j < child_task; ++j) {
                    childFutures.emplace_back(
                        gPool.submitTask([]() { return taskNear100ms(); }));
                }
                for (auto& future : childFutures) {
                    future.get();
                }
            }));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

void case2_multi_thread_pool(benchmark::State& state) {
    const int parent_task = state.range(0);
    const int child_task = state.range(1);

    for (auto _ : state) {
        std::vector<std::future<void>> futures;
        for (int i = 0; i < parent_task; ++i) {
            futures.emplace_back(gPool.submitTask([child_task]() {
                std::vector<std::future<void>> childFutures;
                childFutures.reserve(child_task);

                ThreadPool pool(std::thread::hardware_concurrency());
                for (int j = 0; j < child_task; ++j) {
                    childFutures.emplace_back(
                        pool.submitTask([]() { return taskNear100ms(); }));
                }
                for (auto& future : childFutures) {
                    future.get();
                }
            }));
        }
        for (auto& future : futures) {
            future.get();
        }
    }
}

// if pass args {15, 30} in, it will cause thread starvation and deadlock.
// BENCHMARK(case2_one_thread_pool)->Args({10, 30})->Unit(benchmark::kMillisecond);

// BENCHMARK(case2_multi_thread_pool)
//     ->Args({10, 30})
//     ->Args({15, 30})
//     ->Unit(benchmark::kMillisecond);
