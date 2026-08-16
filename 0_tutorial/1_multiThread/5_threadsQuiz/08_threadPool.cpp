#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/*
  线程池（Thread Pool）是最常用的并发设计模式之一。
  预先创建固定数量的工作线程，通过任务队列分派任务，避免了频繁创建/销毁线程的开销。
*/

// ============================================================
namespace threadpool_basic {
    /*
    1. 线程池核心组件：
        - 工作线程池：一组预先创建的线程
        - 任务队列：存放待执行的任务（通常用 std::packaged_task 或 std::function）
        - 同步机制：mutex + condition_variable 协调生产者和消费者

    2. 工作流程：
        1. 创建 N 个工作线程，每个线程在循环中等待任务
        2. 主线程（或其他线程）向队列提交任务
        3. 工作线程取出任务并执行
        4. 析构时通知所有线程退出，join 所有线程
    */

    class SimpleThreadPool {
      public:
        explicit SimpleThreadPool(size_t num_threads) : stop_(false) {
            workers_.reserve(num_threads);
            for (size_t i = 0; i < num_threads; ++i) {
                workers_.emplace_back([this] { worker_loop(); });
            }
        }

        ~SimpleThreadPool() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }
            cv_.notify_all();
            for (auto& w : workers_) {
                w.join();
            }
        }

        // 提交任务并返回 future 以便获取结果
        template <typename F, typename... Args>
        auto submit(F&& f, Args&&... args)
            -> std::future<decltype(f(args...))> {
            using return_type = decltype(f(args...));

            // 用 packaged_task 包装任务
            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stop_) {
                    throw std::runtime_error("向已停止的线程池提交任务");
                }
                tasks_.emplace([task]() { (*task)(); });
            }
            cv_.notify_one();
            return result;
        }

        size_t pending_tasks() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return tasks_.size();
        }

      private:
        void worker_loop() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                    if (stop_ && tasks_.empty()) {
                        return;  // 退出工作线程
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();  // 执行任务（不加锁）
            }
        }

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        bool stop_;
    };

    int compute_square(int x) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return x * x;
    }

    void task() {
        SimpleThreadPool pool(4);  // 4 个工作线程
        std::cout << "创建了 4 个工作线程的线程池\n";

        // 提交多个任务
        std::vector<std::future<int>> results;
        results.reserve(10);
        for (int i = 1; i <= 10; ++i) {
            results.push_back(pool.submit(compute_square, i));
            std::cout << "提交任务: compute_square(" << i << ")\n";
        }

        // 获取结果
        std::cout << "\n获取结果:\n";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "  " << (i + 1) << "² = " << results[i].get() << "\n";
        }
    }
}  // namespace threadpool_basic

// ============================================================
namespace threadpool_lambda {
    /*
    线程池也支持提交 lambda 表达式和各种可调用对象。
    下面演示使用 lambda 和不同的返回类型。
    */

    using SimpleThreadPool = threadpool_basic::SimpleThreadPool;

    void task() {
        SimpleThreadPool pool(3);

        // 不同返回类型的任务
        auto f1 = pool.submit([] { return 42; });
        auto f2 = pool.submit([](int a, int b) { return a + b; }, 10, 20);
        auto f3 = pool.submit(
            [](const std::string& s) { return "Hello, " + s + "!"; },
            std::string("线程池"));

        std::cout << "f1: " << f1.get() << "\n";
        std::cout << "f2: " << f2.get() << "\n";
        std::cout << "f3: " << f3.get() << "\n";
    }
}  // namespace threadpool_lambda

// ============================================================
namespace threadpool_work_stealing {
    /*
    1. Work Stealing 简介：
        高级线程池（如 TBB 和 Java ForkJoinPool）使用 work stealing 策略：
        每个工作线程有自己的任务队列，当自己的队列空时，从其他线程的队列"偷"任务。

    2. 优势：
        - 更好的负载均衡
        - 减少对单一全局队列的竞争
        - 支持任务嵌套（子任务可以放回本地队列）

    3. 以下是一个简单的概念演示（不是完整实现）。
    */

    void task() {
        std::cout << "Work Stealing 的概念:\n\n";
        std::cout << "  线程 1 的任务队列: [A, B, C]\n";
        std::cout << "  线程 2 的任务队列: []        ← 空了!\n";
        std::cout << "  线程 2 从线程 1 的队列尾部偷走任务 C\n";
        std::cout << "  线程 1: [A, B]    线程 2: [C]\n\n";
        std::cout << "  关键设计:\n";
        std::cout << "  - 线程从自己队列头部取任务 (LIFO)\n";
        std::cout << "  - 窃取者从目标队列尾部取任务 (FIFO)\n";
        std::cout << "  - 这种不对称设计减少了竞争概率\n";
        std::cout << "  - TBB 和 Java ForkJoinPool 都采用了这种策略\n";
    }
}  // namespace threadpool_work_stealing

// ============================================================
int main() {
    std::cout << "===== 1. 简单线程池实现 =====\n";
    threadpool_basic::task();

    std::cout << "\n===== 2. 线程池 + lambda =====\n";
    threadpool_lambda::task();

    std::cout << "\n===== 3. Work Stealing 概念 =====\n";
    threadpool_work_stealing::task();

    return 0;
}
