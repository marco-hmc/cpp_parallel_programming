#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <tbb/task_group.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "tbbThreadPool.h"
#include "threadPool.h"
#include "utils.h"

constexpr int NUM_TASKS = 50;
constexpr int NUM_ITERATIONS = 500000;

namespace {
    std::mutex mtx;
    int shared_resource = 0;

    void increment_with_lock() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            std::lock_guard<std::mutex> lock(mtx);
            shared_resource++;
        }
    }

    void _testTBBThreadPoolWithLock() {
        spdlog::info("Testing TBB ThreadPool with lock...");
        ParallelLib::ThreadPool tbbPool(std::thread::hardware_concurrency());
        shared_resource = 0;

        std::vector<std::future<void>> futures;
        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.emplace_back(tbbPool.submitTask(increment_with_lock));
        }

        for (auto& future : futures) {
            future.get();
        }

        spdlog::info("Final shared_resource value (TBB): {}", shared_resource);
    }

    void _testStdThreadPoolWithLock() {
        spdlog::info("Testing Std ThreadPool with lock...");
        StdThreadPool::ThreadPool stdPool(std::thread::hardware_concurrency());
        shared_resource = 0;

        std::vector<std::future<void>> futures;
        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.emplace_back(stdPool.submitTask(increment_with_lock));
        }

        for (auto& future : futures) {
            future.get();
        }

        spdlog::info("Final shared_resource value (Std): {}", shared_resource);
    }

    void _testTBBThreadPool2WithLock() {
        spdlog::info("Testing TBB ThreadPool2 with lock...");
        ParallelLib2::ThreadPool tbbPool(std::thread::hardware_concurrency());
        shared_resource = 0;

        std::vector<std::future<void>> futures;
        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.emplace_back(tbbPool.submitTask(increment_with_lock));
        }

        for (auto& future : futures) {
            future.get();
        }

        spdlog::info("Final shared_resource value (TBB2): {}", shared_resource);
    }

    void testTBBThreadPool2WithLock() {
        spdlog::info("Measuring TBB ThreadPool2 performance with lock...");
        myUtils::measure_time(_testTBBThreadPool2WithLock);
    }

    void testTBBThreadPoolWithLock() {
        spdlog::info("Measuring TBB ThreadPool performance with lock...");
        myUtils::measure_time(_testTBBThreadPoolWithLock);
    }

    void testStdThreadPoolWithLock() {
        spdlog::info("Measuring Std ThreadPool performance with lock...");
        myUtils::measure_time(_testStdThreadPoolWithLock);
    }
}  // namespace

void mutex_test() {
    spdlog::info(
        "Running mutex benchmarks for testing mutex behaviour in tbb...");
    for (int i = 0; i < 3; i++) {
        spdlog::info("round {}", i);
        testTBBThreadPoolWithLock();
        testTBBThreadPool2WithLock();
        testStdThreadPoolWithLock();
        spdlog::info("--------------------------------------------");
    }
    spdlog::flush_on(spdlog::level::info);
    spdlog::info("done\n\n");
}