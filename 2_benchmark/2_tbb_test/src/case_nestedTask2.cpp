#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <tbb/task_group.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "task.h"
#include "tbbThreadPool.h"
#include "threadPool.h"
#include "utils.h"

constexpr int NUM_PARENT_TASKS = 300;
constexpr int NUM_CHILD_TASKS = 20;

namespace {
    void _testTBBThreadPool() {
        ParallelLib::ThreadPool tbbPool(std::thread::hardware_concurrency());
        std::atomic<int> tbb_i = 0;
        std::vector<std::future<void>> parentFutures;
        parentFutures.reserve(NUM_PARENT_TASKS);
        for (int i = 0; i < NUM_PARENT_TASKS; ++i) {
            parentFutures.emplace_back(tbbPool.submitTask([&tbb_i]() {
                ParallelLib::ThreadPool tbbPool(
                    std::thread::hardware_concurrency());
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
            parentFutures.emplace_back(tbbPool.submitTask([&tbb_i]() {
                ParallelLib2::ThreadPool tbbPool(
                    std::thread::hardware_concurrency());
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
            parentFutures.emplace_back(stdPool.submitTask([&std_i]() {
                StdThreadPool::ThreadPool stdPool(
                    std::thread::hardware_concurrency());
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

    void testTBBThreadPool() {
        spdlog::info("measuring TBB ThreadPool performance...");
        myUtils::measure_time(_testTBBThreadPool);
    }

    void testStdThreadPool() {
        spdlog::info("Measuring STD ThreadPool performance:");
        myUtils::measure_time(_testStdThreadPool);
    }

    void testTBBThreadPool2() {
        spdlog::info("Measuring TBB ThreadPool2 performance:");
        myUtils::measure_time(_testTBBThreadPool2);
    }
}  // namespace

void multiPool_nested_test() {
    spdlog::info(
        "Running benchmarks for testing nested task (multiple threadPool "
        "case)...");
    for (int i = 0; i < 2; i++) {
        std::cout << "i =" << i << std::endl;
        spdlog::info("round {}", i);
        testTBBThreadPool();
        std::cout << " done for tbb threadPool" << std::endl;
        // testTBBThreadPool2();
        // std::cout << " done for tbb threadPool2" << std::endl;
        testStdThreadPool();
        std::cout << " done for std threadPool" << std::endl;
        spdlog::info("--------------------------------------------");
        spdlog::flush_on(spdlog::level::info);
    }

    spdlog::info("done\n\n");
}