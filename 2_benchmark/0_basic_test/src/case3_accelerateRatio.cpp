#include <benchmark/benchmark.h>

#include <queue>
#include <thread>

#include "utils.h"

namespace {

    class ThreadPool {
      public:
        ThreadPool(size_t numThreads) : stop(false) {
            for (size_t i = 0; i < numThreads; ++i) {
                workers.emplace_back([this] {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this->queueMutex);
                            this->condition.wait(lock, [this] {
                                return this->stop || !this->tasks.empty();
                            });
                            if (this->stop && this->tasks.empty()) return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }
                        task();
                    }
                });
            }
        }

        ~ThreadPool() {
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                stop = true;
            }
            condition.notify_all();
            for (std::thread& worker : workers) {
                worker.join();
            }
        }

        template <class F>
        void enqueue(F&& f) {
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                if (stop)
                    throw std::runtime_error("enqueue on stopped ThreadPool");
                tasks.emplace(std::forward<F>(f));
            }
            condition.notify_one();
        }

      private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex queueMutex;
        std::condition_variable condition;
        std::atomic<bool> stop;
    };

    NO_OPTIMIZE void countNumber(int counter) {
        int value = 0;
        char padding[128];
        benchmark::DoNotOptimize(padding);
        benchmark::DoNotOptimize(value);
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void taskNear50ms() { countNumber(240'000'000); }

    ThreadPool pool(std::thread::hardware_concurrency());

}  // namespace

void case3_task_cost(benchmark::State& state) {
    for (auto _ : state) {
        taskNear50ms();
    }
}

void case3_single_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            taskNear50ms();
        }
        taskNear50ms();
    }
}

void case3_multi_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([]() { taskNear50ms(); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

void case3_thread_pool_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<int> counter(0);
        const int totalTasks = std::thread::hardware_concurrency();

        // 提交任务到线程池
        for (int i = 0; i < totalTasks; ++i) {
            pool.enqueue([&counter] {
                taskNear50ms();
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        while (counter.load(std::memory_order_relaxed) < totalTasks) {
            std::this_thread::yield();
        }
    }
}

BENCHMARK(case3_task_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_single_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_multi_thread_cost)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_thread_pool_cost)->Unit(benchmark::kMillisecond);