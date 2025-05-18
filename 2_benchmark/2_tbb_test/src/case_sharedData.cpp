#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "task.h"
#include "tbbThreadPool.h"
#include "threadPool.h"
#include "utils.h"

constexpr int RANGE_START = 1;
constexpr int RANGE_END = 1'000'000;
constexpr int NUM_TASKS = 100;

namespace {

    void _testTBBThreadPool() {
        ParallelLib::ThreadPool tbbPool(std::thread::hardware_concurrency());
        std::vector<int> primes;
        std::mutex primes_mutex;

        int range_per_task = (RANGE_END - RANGE_START + 1) / NUM_TASKS;
        std::vector<std::future<void>> futures;

        for (int i = 0; i < NUM_TASKS; ++i) {
            int start = RANGE_START + i * range_per_task;
            int end =
                (i == NUM_TASKS - 1) ? RANGE_END : start + range_per_task - 1;

            futures.emplace_back(tbbPool.submitTask(findPrimesInRange, start,
                                                    end, std::ref(primes),
                                                    std::ref(primes_mutex)));
        }

        for (auto& future : futures) {
            future.get();
        }

        spdlog::info("TBB ThreadPool found {} primes in range [{}, {}]",
                     primes.size(), RANGE_START, RANGE_END);
    }

    void _testTBBThreadPool2() {
        ParallelLib2::ThreadPool tbbPool(std::thread::hardware_concurrency());
        std::vector<int> primes;
        std::mutex primes_mutex;

        int range_per_task = (RANGE_END - RANGE_START + 1) / NUM_TASKS;
        std::vector<std::future<void>> futures;

        for (int i = 0; i < NUM_TASKS; ++i) {
            int start = RANGE_START + i * range_per_task;
            int end =
                (i == NUM_TASKS - 1) ? RANGE_END : start + range_per_task - 1;

            futures.emplace_back(tbbPool.submitTask(findPrimesInRange, start,
                                                    end, std::ref(primes),
                                                    std::ref(primes_mutex)));
        }

        for (auto& future : futures) {
            future.get();
        }

        spdlog::info("TBB ThreadPool found {} primes in range [{}, {}]",
                     primes.size(), RANGE_START, RANGE_END);
    }

    void _testStdThreadPool() {
        StdThreadPool::ThreadPool stdPool(std::thread::hardware_concurrency());
        std::vector<int> primes;
        std::mutex primes_mutex;

        int range_per_task = (RANGE_END - RANGE_START + 1) / NUM_TASKS;
        std::vector<std::future<void>> futures;

        for (int i = 0; i < NUM_TASKS; ++i) {
            int start = RANGE_START + i * range_per_task;
            int end =
                (i == NUM_TASKS - 1) ? RANGE_END : start + range_per_task - 1;

            futures.emplace_back(stdPool.submitTask(findPrimesInRange, start,
                                                    end, std::ref(primes),
                                                    std::ref(primes_mutex)));
        }

        for (auto& future : futures) {
            future.get();
        }

        spdlog::info("STD ThreadPool found {} primes in range [{}, {}]",
                     primes.size(), RANGE_START, RANGE_END);
    }

    void _testNoParallel() {
        std::vector<int> primes;
        std::mutex primes_mutex;

        int range_per_task = (RANGE_END - RANGE_START + 1) / NUM_TASKS;
        for (int i = 0; i < NUM_TASKS; ++i) {
            int start = RANGE_START + i * range_per_task;
            int end =
                (i == NUM_TASKS - 1) ? RANGE_END : start + range_per_task - 1;
            findPrimesInRange(start, end, std::ref(primes),
                              std::ref(primes_mutex));
        }

        spdlog::info("No parallel found {} primes in range [{}, {}]",
                     primes.size(), RANGE_START, RANGE_END);
    }

    void testTBBThreadPool() {
        spdlog::info("measuring TBB ThreadPool performance...");
        myUtils::measure_time(_testTBBThreadPool);
    }

    void testTBBThreadPool2() {
        spdlog::info("measuring TBB ThreadPool performance...");
        myUtils::measure_time(_testTBBThreadPool2);
    }

    void testStdThreadPool() {
        spdlog::info("Measuring STD ThreadPool performance:");
        myUtils::measure_time(_testStdThreadPool);
    }

    void testNoParallel() {
        spdlog::info("Measuring no parallel skill performance:");
        myUtils::measure_time(_testNoParallel);
    }
}  // namespace

void sharedData_test() {
    spdlog::info("Running benchmarks for finding primes...");
    for (int i = 0; i < 3; i++) {
        spdlog::info("Round {}", i);
        testTBBThreadPool();
        testStdThreadPool();
        testNoParallel();

        spdlog::info("--------------------------------------------");
    }
    spdlog::flush_on(spdlog::level::info);

    spdlog::info("done\n\n");
}
