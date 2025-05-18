#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <tbb/task_group.h>

#include <atomic>
#include <thread>
#include <vector>

#include "task.h"
#include "tbbThreadPool.h"
#include "threadPool.h"
#include "utils.h"

constexpr int NUM_PARENT_TASKS = 10;
constexpr int NUM_CHILD_TASKS = 20;

namespace {
    void _testTBBThreadPool() {
        ParallelLib::ThreadPool tbbPool(std::thread::hardware_concurrency());
        std::atomic<int> tbb_i = 0;
        std::vector<std::future<void>> parentFutures;
        parentFutures.reserve(NUM_PARENT_TASKS);
        for (int i = 0; i < NUM_PARENT_TASKS; ++i) {
            parentFutures.emplace_back(tbbPool.submitTask([&tbbPool, &tbb_i]() {
                std::vector<std::future<int>> childFutures;
                childFutures.reserve(NUM_CHILD_TASKS);
                for (int j = 0; j < NUM_CHILD_TASKS; ++j) {
                    childFutures.emplace_back(
                        tbbPool.submitTask([]() { return busyWait200ms(); }));
                }

                for (auto& future : childFutures) {
                    tbb_i += future.get();
                }
            }));
        }

        for (auto& future : parentFutures) {
            future.get();
        }
    }

    void _testTBBThreadPool2() {
        ParallelLib2::ThreadPool tbbPool(std::thread::hardware_concurrency());
        std::atomic<int> tbb_i = 0;
        std::vector<std::future<void>> parentFutures;
        parentFutures.reserve(NUM_PARENT_TASKS);
        for (int i = 0; i < NUM_PARENT_TASKS; ++i) {
            parentFutures.emplace_back(tbbPool.submitTask([&tbbPool, &tbb_i]() {
                std::vector<std::future<int>> childFutures;
                childFutures.reserve(NUM_CHILD_TASKS);
                for (int j = 0; j < NUM_CHILD_TASKS; ++j) {
                    childFutures.emplace_back(
                        tbbPool.submitTask([]() { return busyWait200ms(); }));
                }

                for (auto& future : childFutures) {
                    tbb_i += future.get();
                }
            }));
        }

        for (auto& future : parentFutures) {
            future.get();
        }
    }

    void _testStdThreadPool() {
        StdThreadPool::ThreadPool stdPool(std::thread::hardware_concurrency());
        std::atomic<int> std_i = 0;

        std::vector<std::future<void>> parentFutures;
        parentFutures.reserve(NUM_PARENT_TASKS);
        for (int i = 0; i < NUM_PARENT_TASKS; ++i) {
            parentFutures.emplace_back(stdPool.submitTask([&stdPool, &std_i]() {
                std::vector<std::future<int>> childFutures;
                childFutures.reserve(NUM_CHILD_TASKS);
                for (int j = 0; j < NUM_CHILD_TASKS; ++j) {
                    childFutures.emplace_back(
                        stdPool.submitTask([]() { return busyWait200ms(); }));
                }

                for (auto& future : childFutures) {
                    std_i += future.get();
                }
            }));
        }

        for (auto& future : parentFutures) {
            future.get();
        }
    }

    void testTBBThreadPool2() {
        spdlog::info("Measuring TBB ThreadPool2 performance:");
        myUtils::measure_time(_testTBBThreadPool2);
    }

    void testTBBThreadPool() {
        spdlog::info("measuring TBB ThreadPool performance...");
        myUtils::measure_time(_testTBBThreadPool);
    }

    void testStdThreadPool() {
        spdlog::info("Measuring STD ThreadPool performance:");
        myUtils::measure_time(_testStdThreadPool);
    }
}  // namespace

void nested_test() {
    spdlog::info(
        "Running benchmarks for testing nested task (one threadPool case)...");
    for (int i = 0; i < 3; i++) {
        spdlog::info("round {}", i);
        testTBBThreadPool();
        // testTBBThreadPool2();
        testStdThreadPool();
        spdlog::info("--------------------------------------------");
        spdlog::flush_on(spdlog::level::info);
    }

    spdlog::info("done\n\n");
}