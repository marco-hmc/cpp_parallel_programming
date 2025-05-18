#pragma once

#include <functional>
#include <future>
#include <memory>

namespace ParallelLib {
    namespace detail {
        class ThreadPoolImpl;
    }  // namespace detail

    class ThreadPool {
      public:
        explicit ThreadPool(size_t threadCount);
        ~ThreadPool();
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        template <typename Func, typename... Args>
        auto submitTask(Func&& func, Args&&... args)
            -> std::future<typename std::result_of<Func(Args...)>::type> {
            using ReturnType = typename std::result_of<Func(Args...)>::type;

            auto task =
                std::make_shared<std::packaged_task<ReturnType()>>(std::bind(
                    std::forward<Func>(func), std::forward<Args>(args)...));

            std::future<ReturnType> result = task->get_future();
            submitImpl([task]() { (*task)(); });
            return result;
        }

      private:
        void submitImpl(std::function<void()> task);

        std::unique_ptr<detail::ThreadPoolImpl> pImpl;
    };

}  // namespace ParallelLib

namespace ParallelLib2 {
    namespace detail {
        class ThreadPoolImpl;
    }  // namespace detail

    class ThreadPool {
      public:
        explicit ThreadPool(size_t threadCount);
        ~ThreadPool();
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        template <typename Func, typename... Args>
        auto submitTask(Func&& func, Args&&... args)
            -> std::future<typename std::result_of<Func(Args...)>::type> {
            using ReturnType = typename std::result_of<Func(Args...)>::type;

            auto task =
                std::make_shared<std::packaged_task<ReturnType()>>(std::bind(
                    std::forward<Func>(func), std::forward<Args>(args)...));

            std::future<ReturnType> result = task->get_future();
            submitImpl([task]() { (*task)(); });
            return result;
        }

      private:
        void submitImpl(std::function<void()> task);

        std::unique_ptr<detail::ThreadPoolImpl> pImpl;
    };

}  // namespace ParallelLib2


namespace ParallelLib3 {
    namespace detail {
        class ThreadPoolImpl;
    }  // namespace detail

    class ThreadPool {
      public:
        explicit ThreadPool(size_t threadCount);
        ~ThreadPool();
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        template <typename Func, typename... Args>
        auto submitTask(Func&& func, Args&&... args)
            -> std::future<typename std::result_of<Func(Args...)>::type> {
            using ReturnType = typename std::result_of<Func(Args...)>::type;

            auto task =
                std::make_shared<std::packaged_task<ReturnType()>>(std::bind(
                    std::forward<Func>(func), std::forward<Args>(args)...));

            std::future<ReturnType> result = task->get_future();
            submitImpl([task]() { (*task)(); });
            return result;
        }

      private:
        void submitImpl(std::function<void()> task);

        std::unique_ptr<detail::ThreadPoolImpl> pImpl;
    };

}  // namespace ParallelLib2