#include "tbbThreadPool.h"

#include <tbb/blocked_range.h>
#include <tbb/concurrent_queue.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

#include <functional>
#include <thread>
#include <vector>

namespace ParallelLib {
    namespace detail {

        class ThreadPoolImpl {
          public:
            explicit ThreadPoolImpl(size_t thread_count)
                : arena(thread_count), stop(false) {
                for (size_t i = 0; i < thread_count; ++i) {
                    workers.emplace_back([this]() { workerLoop(); });
                }
            }

            void submit(std::function<void()> task) {
                tasks.push(std::move(task));
                arena.enqueue([this]() { processTasks(); });
            }

            ~ThreadPoolImpl() {
                stop = true;
                for (auto& worker : workers) {
                    if (worker.joinable()) {
                        worker.join();
                    }
                }
            }

          private:
            tbb::task_arena arena;
            tbb::concurrent_queue<std::function<void()>> tasks;
            std::vector<std::thread> workers;
            std::atomic<bool> stop;

            void workerLoop() {
                while (!stop) {
                    processTasks();
                }
            }

            void processTasks() {
                std::function<void()> task;
                while (tasks.try_pop(task)) {
                    task();
                }
            }
        };

    }  // namespace detail

    ThreadPool::ThreadPool(size_t threadCount)
        : pImpl(std::make_unique<detail::ThreadPoolImpl>(threadCount)) {}

    ThreadPool::~ThreadPool() = default;

    void ThreadPool::submitImpl(std::function<void()> task) {
        pImpl->submit(std::move(task));
    }

}  // namespace ParallelLib

namespace ParallelLib2 {
    namespace detail {

        class ThreadPoolImpl {
          public:
            explicit ThreadPoolImpl(size_t thread_count)
                : gc(tbb::global_control::max_allowed_parallelism,
                     thread_count) {}

            void submit(std::function<void()> task) {
                group.run(std::move(task));
            }

            ~ThreadPoolImpl() { group.wait(); }

          private:
            tbb::task_group group;
            tbb::global_control gc;
        };

    }  // namespace detail

    ThreadPool::ThreadPool(size_t threadCount)
        : pImpl(std::make_unique<detail::ThreadPoolImpl>(threadCount)) {}

    ThreadPool::~ThreadPool() = default;

    void ThreadPool::submitImpl(std::function<void()> task) {
        pImpl->submit(std::move(task));
    }

}  // namespace ParallelLib2

namespace ParallelLib3 {
    namespace detail {

        class ThreadPoolImpl {
          public:
            explicit ThreadPoolImpl(size_t thread_count)
                : gc(tbb::global_control::max_allowed_parallelism,
                     thread_count) {}

            void submit(std::function<void()> task) {
                group.run(std::move(task));
            }

            ~ThreadPoolImpl() { group.wait(); }

          private:
            tbb::task_group group;
            tbb::global_control gc;
        };

    }  // namespace detail

    ThreadPool::ThreadPool(size_t threadCount)
        : pImpl(std::make_unique<detail::ThreadPoolImpl>(threadCount)) {}

    ThreadPool::~ThreadPool() = default;

    void ThreadPool::submitImpl(std::function<void()> task) {
        pImpl->submit(std::move(task));
    }

}  // namespace ParallelLib3