#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace StdThreadPool {

    class ThreadPool {
      public:
        inline explicit ThreadPool(size_t threads) : stop(false) {
            for (size_t i = 0; i < threads; ++i)
                workers.emplace_back([this] {
                    for (;;) {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(
                                this->queue_mutex);
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

        template <class F, class... Args>
        auto submitTask(F&& f, Args&&... args)
            -> std::future<typename std::invoke_result<F, Args...>::type> {
            using return_type = typename std::invoke_result<F, Args...>::type;

            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> res = task->get_future();
            {
                std::unique_lock<std::mutex> lock(queue_mutex);

                if (stop)
                    throw std::runtime_error("enqueue on stopped ThreadPool");

                tasks.emplace([task]() { (*task)(); });
            }
            condition.notify_one();
            return res;
        }

        template <class Iterator, class F, class... Args>
        auto submitTasks(Iterator begin, Iterator end, F&& f, Args&&... args)
            -> std::vector<std::future<typename std::invoke_result<
                F, typename Iterator::value_type, Args...>::type>> {
            using return_type = typename std::invoke_result<
                F, typename Iterator::value_type, Args...>::type;

            std::vector<std::future<return_type>> futures;
            for (auto it = begin; it != end; ++it) {
                auto task = std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(std::forward<F>(f), *it,
                              std::forward<Args>(args)...));

                futures.emplace_back(task->get_future());
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);

                    if (stop)
                        throw std::runtime_error(
                            "enqueue on stopped ThreadPool");

                    tasks.emplace([task]() { (*task)(); });
                }
                condition.notify_one();
            }
            return futures;
        }

        inline ~ThreadPool() {
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                stop = true;
            }
            condition.notify_all();
            for (std::thread& worker : workers) worker.join();
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

      private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex queue_mutex;
        std::condition_variable condition;
        bool stop;
    };

}  // namespace StdThreadPool