#include <spdlog/spdlog.h>

#include <vector>

#include "task.h"
#include "threadPool.h"
#include "utils.h"

constexpr int NUM_TASKS = 12;
constexpr int NUM_ITERATIONS = 1'000'000;

namespace {
    void _testWithoutLock() {
        StdThreadPool::ThreadPool stdPool(std::thread::hardware_concurrency());
        std::vector<std::future<void>> parentFutures;
        parentFutures.reserve(NUM_TASKS);
        for (int i = 0; i < NUM_TASKS; ++i) {
            parentFutures.emplace_back(
                stdPool.submitTask(countNumberWithoutLock, NUM_ITERATIONS));
        }
        for (auto& future : parentFutures) {
            future.get();
        }
    }

    void _testLock() {
        StdThreadPool::ThreadPool stdPool(std::thread::hardware_concurrency());
        std::vector<std::future<void>> parentFutures;
        parentFutures.reserve(NUM_TASKS);
        for (int i = 0; i < NUM_TASKS; ++i) {
            parentFutures.emplace_back(
                stdPool.submitTask(countNumberWithLock, NUM_ITERATIONS));
        }
        for (auto& future : parentFutures) {
            future.get();
        }
    }

    void testWithoutLock() {
        spdlog::info("measuring without lock performance...");
        myUtils::measure_time(_testWithoutLock);
    }
    void testLock() {
        spdlog::info("measuring with lock performance...");
        myUtils::measure_time(_testLock);
    }
}  // namespace

void lockCost_test() {
    spdlog::info("Running benchmarks for testing lock cost...");
    for (int i = 0; i < 3; i++) {
        spdlog::info("round {}", i);
        testWithoutLock();
        testLock();
        spdlog::info("--------------------------------------------");
    }
    spdlog::flush_on(spdlog::level::info);

    spdlog::info("done\n\n\n");
}