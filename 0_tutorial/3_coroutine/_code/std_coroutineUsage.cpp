#include <chrono>
#include <concepts>
#include <coroutine>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/*
====================================================================================================
                                C++20 协程 (Coroutine) 教学文档
====================================================================================================

1. 什么是协程？
   - 协程是可以暂停和恢复执行的函数
   - 比线程更轻量级，比回调更容易理解
   - 提供了一种编写异步代码的新方式

2. 协程的核心概念：
   - co_await: 暂停执行并等待结果
   - co_yield: 产生值并暂停
   - co_return: 返回值并结束协程

3. 协程的优势：
   - 避免回调地狱
   - 代码更易读和维护
   - 更好的错误处理
   - 减少内存开销

4. 主要应用场景：
   - 异步I/O操作
   - 生成器/迭代器
   - 状态机
   - 网络编程
   - 任务调度

====================================================================================================
*/

using namespace std::chrono_literals;

// ================================================================================================
// 1. 基础协程概念 - Generator（生成器）
// ================================================================================================

namespace BasicCoroutines {

    struct Generator {
        struct promise_type {
            int current_value;

            Generator get_return_object() {
                return Generator{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void unhandled_exception() { std::terminate(); }

            std::suspend_always yield_value(int value) {
                current_value = value;
                return {};
            }

            void return_void() {}
        };

        std::coroutine_handle<promise_type> h_;

        explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}

        ~Generator() {
            if (h_) h_.destroy();
        }

        Generator(Generator&& other) noexcept
            : h_(std::exchange(other.h_, {})) {}
        Generator& operator=(Generator&& other) noexcept {
            if (this != &other) {
                if (h_) h_.destroy();
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }

        Generator& operator=(const Generator&) = delete;

        struct iterator {
            std::coroutine_handle<promise_type> h_;

            iterator(std::coroutine_handle<promise_type> h) : h_(h) {}

            iterator& operator++() {
                h_.resume();
                return *this;
            }

            int operator*() const { return h_.promise().current_value; }

            bool operator==(const iterator& other) const {
                if (h_.address() == other.h_.address()) return true;
                if (!h_) return other.h_ ? other.h_.done() : true;
                if (!other.h_) return h_.done();
                return h_.done() && other.h_.done();
            }

            bool operator!=(const iterator& other) const {
                return !(*this == other);
            }
        };

        iterator begin() {
            h_.resume();
            return iterator{h_};
        }

        iterator end() { return iterator{{}}; }
    };

    // 斐波那契生成器
    Generator fibonacci(int count) {
        int a = 0, b = 1;
        for (int i = 0; i < count; ++i) {
            co_yield a;
            auto next = a + b;
            a = b;
            b = next;
        }
    }

    // 范围生成器
    Generator range(int start, int end, int step = 1) {
        for (int i = start; i < end; i += step) {
            co_yield i;
        }
    }

    void demonstrateBasicGenerator() {
        std::cout << "\n=== 基础生成器演示 ===" << std::endl;

        std::cout << "斐波那契数列 (前10个): ";
        for (auto value : fibonacci(10)) {
            std::cout << value << " ";
        }
        std::cout << std::endl;

        std::cout << "范围生成器 (1-10, 步长2): ";
        for (auto value : range(1, 10, 2)) {
            std::cout << value << " ";
        }
        std::cout << std::endl;
    }

}  // namespace BasicCoroutines

// ================================================================================================
// 2. 异步协程 - Task（任务）
// ================================================================================================

namespace AsyncCoroutines {

    // 异步任务协程
    template <typename T>
    struct Task {
        struct promise_type {
            std::optional<T> result;
            std::exception_ptr exception;
            // 原子保存等待者的地址，避免竞态（不能直接原子保存 coroutine_handle）
            std::atomic<void*> awaiting{nullptr};

            Task get_return_object() {
                return Task{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            // 不在创建时立即执行协程，避免与 awaiter 的竞态
            std::suspend_always initial_suspend() noexcept { return {}; }

            // 在协程结束时，如果有等待者则安全地 resume 它
            struct final_awaiter {
                bool await_ready() noexcept { return false; }
                void await_suspend(
                    std::coroutine_handle<promise_type> h) noexcept {
                    void* addr =
                        h.promise().awaiting.load(std::memory_order_acquire);
                    if (addr) {
                        std::coroutine_handle<>::from_address(addr).resume();
                    }
                }
                void await_resume() noexcept {}
            };

            final_awaiter final_suspend() noexcept { return {}; }

            void unhandled_exception() { exception = std::current_exception(); }

            void return_value(T value) { result = std::move(value); }
        };

        std::coroutine_handle<promise_type> h_;

        explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}

        ~Task() {
            if (h_) h_.destroy();
        }

        // 移动语义
        Task(Task&& other) noexcept : h_(std::exchange(other.h_, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (h_) h_.destroy();
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }

        // 禁止拷贝
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        // 获取结果（同步调用者主动启动协程直到完成）
        T get() {
            if (!h_.done()) {
                // 同步启动协程，循环直至完成（示例代码的简单策略）
                h_.resume();
                while (!h_.done()) {
                    // 如果协程在外部事件中等待，这里可能需要更复杂的等待机制
                    std::this_thread::yield();
                }
            }

            if (h_.promise().exception) {
                std::rethrow_exception(h_.promise().exception);
            }

            return std::move(h_.promise().result.value());
        }

        // await 接口
        bool await_ready() { return h_.done(); }

        // 保存等待者的地址（原子），然后启动被等待的协程（如果尚未完成）
        void await_suspend(std::coroutine_handle<> awaiter) {
            h_.promise().awaiting.store(awaiter.address(),
                                        std::memory_order_release);
            if (!h_.done()) {
                h_.resume();
            }
        }

        T await_resume() { return get(); }
    };

    // Task<void> 特化
    template <>
    struct Task<void> {
        struct promise_type {
            std::exception_ptr exception;
            std::atomic<void*> awaiting{nullptr};

            Task get_return_object() {
                return Task{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept { return {}; }

            struct final_awaiter {
                bool await_ready() noexcept { return false; }
                void await_suspend(
                    std::coroutine_handle<promise_type> h) noexcept {
                    void* addr =
                        h.promise().awaiting.load(std::memory_order_acquire);
                    if (addr) {
                        std::coroutine_handle<>::from_address(addr).resume();
                    }
                }
                void await_resume() noexcept {}
            };

            final_awaiter final_suspend() noexcept { return {}; }

            void unhandled_exception() { exception = std::current_exception(); }

            void return_void() {}
        };

        std::coroutine_handle<promise_type> h_;

        explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}

        ~Task() {
            if (h_) h_.destroy();
        }

        // 移动语义
        Task(Task&& other) noexcept : h_(std::exchange(other.h_, {})) {}
        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (h_) h_.destroy();
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }

        // 禁止拷贝
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        // 等待完成并检查异常
        void get() {
            if (!h_.done()) {
                h_.resume();
                while (!h_.done()) {
                    std::this_thread::yield();
                }
            }
            if (h_.promise().exception) {
                std::rethrow_exception(h_.promise().exception);
            }
        }

        bool await_ready() { return h_.done(); }
        void await_suspend(std::coroutine_handle<> awaiter) {
            h_.promise().awaiting.store(awaiter.address(),
                                        std::memory_order_release);
            if (!h_.done()) {
                h_.resume();
            }
        }
        void await_resume() { get(); }
    };

    // 简单的异步操作模拟
    Task<int> async_compute(int value) {
        // 模拟异步工作
        std::this_thread::sleep_for(100ms);
        int result = value * value;
        std::cout << "[AsyncCompute] 计算完成: " << result << std::endl;

        co_return result;
    }

    // 链式异步操作
    Task<std::string> async_process_chain(int input) {
        std::cout << "[ProcessChain] 开始处理: " << input << std::endl;
        auto squared = co_await async_compute(input);
        std::this_thread::sleep_for(50ms);
        auto result = "结果: " + std::to_string(squared);

        std::cout << "[ProcessChain] 处理完成: " << result << std::endl;
        co_return result;
    }

    void demonstrateAsyncTask() {
        std::cout << "\n=== 异步任务演示 ===" << std::endl;
        auto task1 = async_compute(5);
        auto task2 = async_process_chain(7);
        std::cout << "任务1结果: " << task1.get() << std::endl;
        std::cout << "任务2结果: " << task2.get() << std::endl;
    }
}  // namespace AsyncCoroutines

// ================================================================================================
// 3. 高级协程模式 - 可等待对象
// ================================================================================================

namespace AdvancedCoroutines {

    // 定时器可等待对象
    struct Timer {
        std::chrono::milliseconds duration;

        explicit Timer(std::chrono::milliseconds ms) : duration(ms) {}

        bool await_ready() { return duration.count() == 0; }

        void await_suspend(std::coroutine_handle<> h) {
            std::thread([h, duration = this->duration]() {
                std::this_thread::sleep_for(duration);
                h.resume();
            }).detach();
        }

        void await_resume() {}
    };

    // HTTP请求模拟
    struct HttpRequest {
        std::string url;

        explicit HttpRequest(std::string url) : url(std::move(url)) {}

        bool await_ready() { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            std::thread([h, url = this->url]() {
                // 模拟网络延迟
                std::this_thread::sleep_for(200ms);
                h.resume();
            }).detach();
        }

        std::string await_resume() { return "HTTP响应来自: " + url; }
    };

    // 文件读取可等待对象
    struct FileReader {
        std::string filename;

        explicit FileReader(std::string filename)
            : filename(std::move(filename)) {}

        bool await_ready() { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            std::thread([h, filename = this->filename]() {
                // 模拟文件读取延迟
                std::this_thread::sleep_for(150ms);
                h.resume();
            }).detach();
        }

        std::string await_resume() {
            // 模拟文件内容
            return "文件内容来自: " + filename;
        }
    };

    // 使用高级可等待对象的协程
    AsyncCoroutines::Task<std::string> complex_async_operation() {
        std::cout << "[ComplexOp] 开始复杂异步操作" << std::endl;

        // 等待定时器
        std::cout << "[ComplexOp] 等待定时器..." << std::endl;
        co_await Timer(100ms);

        // 发起HTTP请求
        std::cout << "[ComplexOp] 发起HTTP请求..." << std::endl;
        auto response = co_await HttpRequest("https://api.example.com/data");
        std::cout << "[ComplexOp] " << response << std::endl;

        // 读取文件
        std::cout << "[ComplexOp] 读取文件..." << std::endl;
        auto content = co_await FileReader("config.txt");
        std::cout << "[ComplexOp] " << content << std::endl;

        co_return "复杂操作完成";
    }

    void demonstrateAdvancedAwaitables() {
        std::cout << "\n=== 高级可等待对象演示 ===" << std::endl;

        auto task = complex_async_operation();
        std::cout << "最终结果: " << task.get() << std::endl;
    }

}  // namespace AdvancedCoroutines

// ================================================================================================
// 4. 协程调度器
// ================================================================================================

namespace CoroutineScheduler {

    // 简单的协程调度器
    class SimpleScheduler {
      private:
        std::queue<std::coroutine_handle<>> ready_queue;
        std::mutex queue_mutex;
        std::condition_variable cv;
        bool running = true;

      public:
        void schedule(std::coroutine_handle<> h) {
            std::lock_guard lock(queue_mutex);
            ready_queue.push(h);
            cv.notify_one();
        }

        void run() {
            while (running) {
                std::unique_lock lock(queue_mutex);
                cv.wait(lock,
                        [this] { return !ready_queue.empty() || !running; });

                if (!running) break;

                auto h = ready_queue.front();
                ready_queue.pop();
                lock.unlock();

                if (!h.done()) {
                    h.resume();
                }
            }
        }

        void stop() {
            std::lock_guard lock(queue_mutex);
            running = false;
            cv.notify_all();
        }
    };

    // 调度器感知的可等待对象
    struct ScheduledTimer {
        std::chrono::milliseconds duration;
        SimpleScheduler* scheduler;

        ScheduledTimer(std::chrono::milliseconds ms, SimpleScheduler* sched)
            : duration(ms), scheduler(sched) {}

        bool await_ready() { return duration.count() == 0; }

        void await_suspend(std::coroutine_handle<> h) {
            std::thread([h, duration = this->duration,
                         scheduler = this->scheduler]() {
                std::this_thread::sleep_for(duration);
                scheduler->schedule(h);
            }).detach();
        }

        void await_resume() {}
    };

    // 使用调度器的协程
    AsyncCoroutines::Task<int> scheduled_task(int id,
                                              SimpleScheduler& scheduler) {
        std::cout << "[Task" << id << "] 开始执行" << std::endl;

        for (int i = 0; i < 3; ++i) {
            std::cout << "[Task" << id << "] 迭代 " << i << std::endl;
            co_await ScheduledTimer(100ms, &scheduler);
        }

        std::cout << "[Task" << id << "] 完成" << std::endl;
        co_return id;
    }

    void demonstrateScheduler() {
        std::cout << "\n=== 协程调度器演示 ===" << std::endl;

        SimpleScheduler scheduler;

        // 启动调度器线程
        std::thread scheduler_thread([&scheduler] { scheduler.run(); });

        // 创建多个协程任务
        auto task1 = scheduled_task(1, scheduler);
        auto task2 = scheduled_task(2, scheduler);
        auto task3 = scheduled_task(3, scheduler);

        // 等待一段时间让协程执行
        std::this_thread::sleep_for(1s);

        // 停止调度器
        scheduler.stop();
        scheduler_thread.join();

        std::cout << "所有任务完成" << std::endl;
    }

}  // namespace CoroutineScheduler

// ================================================================================================
// 5. 协程状态机
// ================================================================================================

namespace CoroutineStateMachine {

    // 状态枚举
    enum class State { Init, Loading, Processing, Completed, Error };

    // 状态机协程
    class StateMachine {
      public:
        struct promise_type {
            State current_state = State::Init;
            std::string message;

            StateMachine get_return_object() {
                return StateMachine{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void unhandled_exception() { std::terminate(); }

            std::suspend_always yield_value(State state) {
                current_state = state;
                return {};
            }

            void return_void() {}
        };

      private:
        std::coroutine_handle<promise_type> h_;

      public:
        explicit StateMachine(std::coroutine_handle<promise_type> h) : h_(h) {}

        ~StateMachine() {
            if (h_) h_.destroy();
        }

        // 移动语义
        StateMachine(StateMachine&& other) noexcept
            : h_(std::exchange(other.h_, {})) {}
        StateMachine& operator=(StateMachine&& other) noexcept {
            if (this != &other) {
                if (h_) h_.destroy();
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }

        // 禁止拷贝
        StateMachine(const StateMachine&) = delete;
        StateMachine& operator=(const StateMachine&) = delete;

        void step() {
            if (!h_.done()) {
                h_.resume();
            }
        }

        State getCurrentState() const {
            return h_.done() ? State::Completed : h_.promise().current_state;
        }

        bool isDone() const { return h_.done(); }

        void setMessage(const std::string& msg) {
            if (!h_.done()) {
                h_.promise().message = msg;
            }
        }

        std::string getMessage() const {
            return h_.done() ? "" : h_.promise().message;
        }
    };

    // 状态转换协程
    StateMachine data_processing_state_machine() {
        std::cout << "[StateMachine] 初始化" << std::endl;
        co_yield State::Init;

        std::cout << "[StateMachine] 开始加载数据" << std::endl;
        co_yield State::Loading;

        // 模拟加载时间
        std::this_thread::sleep_for(200ms);

        std::cout << "[StateMachine] 开始处理数据" << std::endl;
        co_yield State::Processing;

        // 模拟处理时间
        std::this_thread::sleep_for(300ms);

        // 随机决定成功或失败
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10);

        if (dis(gen) <= 8) {  // 80% 成功率
            std::cout << "[StateMachine] 处理完成" << std::endl;
            co_yield State::Completed;
        } else {
            std::cout << "[StateMachine] 处理失败" << std::endl;
            co_yield State::Error;
        }
    }

    const char* stateToString(State state) {
        switch (state) {
            case State::Init:
                return "初始化";
            case State::Loading:
                return "加载中";
            case State::Processing:
                return "处理中";
            case State::Completed:
                return "已完成";
            case State::Error:
                return "错误";
            default:
                return "未知";
        }
    }

    void demonstrateStateMachine() {
        std::cout << "\n=== 协程状态机演示 ===" << std::endl;

        auto state_machine = data_processing_state_machine();

        while (!state_machine.isDone()) {
            state_machine.step();
            auto current_state = state_machine.getCurrentState();
            std::cout << "当前状态: " << stateToString(current_state)
                      << std::endl;

            // 在某些状态下添加额外处理
            if (current_state == State::Loading) {
                state_machine.setMessage("正在从数据库加载...");
                std::cout << "状态消息: " << state_machine.getMessage()
                          << std::endl;
            } else if (current_state == State::Processing) {
                state_machine.setMessage("正在应用业务规则...");
                std::cout << "状态消息: " << state_machine.getMessage()
                          << std::endl;
            }

            std::this_thread::sleep_for(100ms);
        }

        std::cout << "状态机执行完成" << std::endl;
    }

}  // namespace CoroutineStateMachine

// ================================================================================================
// 6. 错误处理和异常
// ================================================================================================

namespace CoroutineErrorHandling {

    // 带错误处理的任务
    template <typename T>
    struct Result {
        std::optional<T> value;
        std::optional<std::string> error;

        static Result success(T val) {
            return Result{std::move(val), std::nullopt};
        }

        static Result failure(std::string err) {
            return Result{std::nullopt, std::move(err)};
        }

        bool isSuccess() const { return value.has_value(); }
        bool isError() const { return error.has_value(); }

        T& getValue() { return value.value(); }
        const T& getValue() const { return value.value(); }

        const std::string& getError() const { return error.value(); }
    };

    template <typename T>
    struct SafeTask {
        struct promise_type {
            Result<T> result;

            SafeTask get_return_object() {
                return SafeTask{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_never initial_suspend() { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void unhandled_exception() {
                try {
                    std::rethrow_exception(std::current_exception());
                } catch (const std::exception& e) {
                    result = Result<T>::failure(e.what());
                } catch (...) {
                    result = Result<T>::failure("未知异常");
                }
            }

            void return_value(Result<T> res) { result = std::move(res); }
        };

        std::coroutine_handle<promise_type> h_;

        explicit SafeTask(std::coroutine_handle<promise_type> h) : h_(h) {}

        ~SafeTask() {
            if (h_) h_.destroy();
        }

        // 移动语义
        SafeTask(SafeTask&& other) noexcept : h_(std::exchange(other.h_, {})) {}
        SafeTask& operator=(SafeTask&& other) noexcept {
            if (this != &other) {
                if (h_) h_.destroy();
                h_ = std::exchange(other.h_, {});
            }
            return *this;
        }

        // 禁止拷贝
        SafeTask(const SafeTask&) = delete;
        SafeTask& operator=(const SafeTask&) = delete;

        Result<T> get() {
            if (!h_.done()) {
                h_.resume();
            }
            return std::move(h_.promise().result);
        }
    };

    // 可能失败的异步操作
    SafeTask<int> risky_operation(int input) {
        std::cout << "[RiskyOp] 执行危险操作: " << input << std::endl;

        // 模拟异步工作
        std::this_thread::sleep_for(100ms);

        // 随机失败
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10);

        if (dis(gen) <= 7) {  // 70% 成功率
            auto result = input * 2;
            std::cout << "[RiskyOp] 操作成功: " << result << std::endl;
            co_return Result<int>::success(result);
        } else {
            std::cout << "[RiskyOp] 操作失败" << std::endl;
            co_return Result<int>::failure("数值处理失败");
        }
    }

    // 链式错误处理
    SafeTask<std::string> chained_operations(int input) {
        std::cout << "[ChainedOp] 开始链式操作: " << input << std::endl;

        // 第一步操作
        auto task1 = risky_operation(input);
        auto result1 = task1.get();

        if (result1.isError()) {
            co_return Result<std::string>::failure("第一步失败: " +
                                                   result1.getError());
        }

        // 第二步操作
        auto task2 = risky_operation(result1.getValue());
        auto result2 = task2.get();

        if (result2.isError()) {
            co_return Result<std::string>::failure("第二步失败: " +
                                                   result2.getError());
        }

        auto final_result =
            "链式操作结果: " + std::to_string(result2.getValue());
        std::cout << "[ChainedOp] " << final_result << std::endl;

        co_return Result<std::string>::success(final_result);
    }

    void demonstrateErrorHandling() {
        std::cout << "\n=== 协程错误处理演示 ===" << std::endl;

        for (int i = 1; i <= 5; ++i) {
            std::cout << "\n--- 尝试 " << i << " ---" << std::endl;

            auto task = chained_operations(i);
            auto result = task.get();

            if (result.isSuccess()) {
                std::cout << "成功: " << result.getValue() << std::endl;
            } else {
                std::cout << "失败: " << result.getError() << std::endl;
            }
        }
    }

}  // namespace CoroutineErrorHandling

// ================================================================================================
// 7. 实际应用示例 - 协程版HTTP客户端
// ================================================================================================

namespace PracticalExample {

    // 模拟HTTP响应
    struct HttpResponse {
        int status_code;
        std::string body;
        std::chrono::milliseconds response_time;
    };

    // 异步HTTP客户端
    class AsyncHttpClient {
      public:
        // HTTP GET请求的可等待对象
        struct HttpGetRequest {
            std::string url;

            explicit HttpGetRequest(std::string url) : url(std::move(url)) {}

            bool await_ready() { return false; }

            void await_suspend(std::coroutine_handle<> h) {
                std::thread([h, url = this->url]() {
                    // 模拟网络延迟
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> delay_dis(100, 500);
                    std::uniform_int_distribution<> status_dis(1, 10);

                    auto delay = std::chrono::milliseconds(delay_dis(gen));
                    std::this_thread::sleep_for(delay);

                    h.resume();
                }).detach();
            }

            HttpResponse await_resume() {
                // 模拟随机响应
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> status_dis(1, 10);
                std::uniform_int_distribution<> delay_dis(100, 500);

                int status = (status_dis(gen) <= 8) ? 200 : 500;
                std::string body =
                    (status == 200) ? "成功响应来自: " + url : "服务器错误";

                return HttpResponse{status, body,
                                    std::chrono::milliseconds(delay_dis(gen))};
            }
        };

        static HttpGetRequest get(const std::string& url) {
            return HttpGetRequest{url};
        }
    };

    // 批量HTTP请求协程
    AsyncCoroutines::Task<std::vector<HttpResponse>> fetch_multiple_urls(
        const std::vector<std::string>& urls) {
        std::vector<HttpResponse> responses;
        responses.reserve(urls.size());

        std::cout << "[BatchFetch] 开始批量获取 " << urls.size() << " 个URL"
                  << std::endl;

        for (const auto& url : urls) {
            std::cout << "[BatchFetch] 请求: " << url << std::endl;

            try {
                auto response = co_await AsyncHttpClient::get(url);
                std::cout << "[BatchFetch] 响应: " << response.status_code
                          << " (" << response.response_time.count() << "ms)"
                          << std::endl;

                responses.push_back(std::move(response));

                // 添加请求间延迟，避免过于频繁
                co_await AdvancedCoroutines::Timer(50ms);

            } catch (const std::exception& e) {
                std::cout << "[BatchFetch] 请求失败: " << e.what() << std::endl;
                responses.push_back(HttpResponse{0, "请求失败", 0ms});
            }
        }

        std::cout << "[BatchFetch] 批量请求完成" << std::endl;
        co_return responses;
    }

    // 网站监控协程
    AsyncCoroutines::Task<void> monitor_website(const std::string& url,
                                                int check_count) {
        std::cout << "[Monitor] 开始监控网站: " << url << std::endl;

        int success_count = 0;
        int total_response_time = 0;

        for (int i = 0; i < check_count; ++i) {
            std::cout << "[Monitor] 检查 " << (i + 1) << "/" << check_count
                      << std::endl;

            auto response = co_await AsyncHttpClient::get(url);

            if (response.status_code == 200) {
                success_count++;
                total_response_time += response.response_time.count();
                std::cout << "[Monitor] ✓ 网站正常 ("
                          << response.response_time.count() << "ms)"
                          << std::endl;
            } else {
                std::cout << "[Monitor] ✗ 网站异常 (状态码: "
                          << response.status_code << ")" << std::endl;
            }

            // 等待间隔
            co_await AdvancedCoroutines::Timer(200ms);
        }

        double success_rate = (double)success_count / check_count * 100;
        double avg_response_time =
            success_count > 0 ? (double)total_response_time / success_count : 0;

        std::cout << "[Monitor] 监控完成:" << std::endl;
        std::cout << "  成功率: " << success_rate << "%" << std::endl;
        std::cout << "  平均响应时间: " << avg_response_time << "ms"
                  << std::endl;
    }

    void demonstratePracticalExample() {
        std::cout << "\n=== 实际应用示例 - HTTP客户端 ===" << std::endl;

        // 示例1：批量获取URL
        std::vector<std::string> urls = {
            "https://api.github.com/users", "https://httpbin.org/json",
            "https://api.example.com/data",
            "https://jsonplaceholder.typicode.com/posts/1"};

        std::cout << "\n--- 批量HTTP请求 ---" << std::endl;
        auto batch_task = fetch_multiple_urls(urls);
        auto responses = batch_task.get();

        std::cout << "\n批量请求结果统计:" << std::endl;
        int success_count = 0;
        for (const auto& response : responses) {
            if (response.status_code == 200) {
                success_count++;
            }
        }
        std::cout << "成功: " << success_count << "/" << responses.size()
                  << std::endl;

        // 示例2：网站监控
        std::cout << "\n--- 网站监控 ---" << std::endl;
        auto monitor_task = monitor_website("https://example.com", 5);
        monitor_task.get();
    }

}  // namespace PracticalExample

// ================================================================================================
// 8. 性能对比和最佳实践
// ================================================================================================

namespace BestPractices {

    // 传统回调方式
    void traditional_callback_approach() {
        std::cout << "\n--- 传统回调方式 ---" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        std::function<void(int)> callback3 = [start_time](int result) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);
            std::cout << "回调链完成: " << result
                      << " (耗时: " << duration.count() << "ms)" << std::endl;
        };

        std::function<void(int)> callback2 = [callback3](int result) {
            std::cout << "回调2: " << result << std::endl;
            std::thread([callback3, result]() {
                std::this_thread::sleep_for(100ms);
                callback3(result * 3);
            }).detach();
        };

        std::function<void(int)> callback1 = [callback2](int result) {
            std::cout << "回调1: " << result << std::endl;
            std::thread([callback2, result]() {
                std::this_thread::sleep_for(100ms);
                callback2(result * 2);
            }).detach();
        };

        std::thread([callback1]() {
            std::this_thread::sleep_for(100ms);
            callback1(10);
        }).detach();

        std::this_thread::sleep_for(500ms);  // 等待完成
    }

    // 协程方式
    AsyncCoroutines::Task<int> coroutine_approach() {
        std::cout << "\n--- 协程方式 ---" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();

        int result = 10;

        co_await AdvancedCoroutines::Timer(100ms);
        result *= 2;
        std::cout << "步骤1: " << result << std::endl;

        co_await AdvancedCoroutines::Timer(100ms);
        result *= 3;
        std::cout << "步骤2: " << result << std::endl;

        co_await AdvancedCoroutines::Timer(100ms);

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        std::cout << "协程链完成: " << result << " (耗时: " << duration.count()
                  << "ms)" << std::endl;

        co_return result;
    }

    // 内存使用对比
    void memory_usage_comparison() {
        std::cout << "\n--- 内存使用对比 ---" << std::endl;

        const int num_tasks = 1000;

        // 协程方式
        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<BasicCoroutines::Generator> generators;
        generators.reserve(num_tasks);

        for (int i = 0; i < num_tasks; ++i) {
            generators.emplace_back(BasicCoroutines::range(0, 100));
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto creation_time =
            std::chrono::duration_cast<std::chrono::microseconds>(end_time -
                                                                  start_time);

        std::cout << "创建 " << num_tasks
                  << " 个协程生成器耗时: " << creation_time.count() << " 微秒"
                  << std::endl;

        // 模拟使用
        int total = 0;
        start_time = std::chrono::high_resolution_clock::now();

        for (auto& gen : generators) {
            for (auto value : gen) {
                total += value;
                if (total > 1000000) break;  // 避免计算过久
            }
        }

        end_time = std::chrono::high_resolution_clock::now();
        auto execution_time =
            std::chrono::duration_cast<std::chrono::microseconds>(end_time -
                                                                  start_time);

        std::cout << "执行协程生成器耗时: " << execution_time.count() << " 微秒"
                  << std::endl;
        std::cout << "协程的优势: 内存占用小，创建开销低" << std::endl;
    }

    void demonstrate_best_practices() {
        std::cout << "\n=== 最佳实践和性能对比 ===" << std::endl;

        // 比较传统方式和协程方式
        traditional_callback_approach();

        auto coroutine_task = coroutine_approach();
        coroutine_task.get();

        // 内存使用对比
        memory_usage_comparison();

        std::cout << "\n协程最佳实践:" << std::endl;
        std::cout << "1. 避免在协程中使用阻塞操作" << std::endl;
        std::cout << "2. 合理使用 co_await 来暂停和恢复" << std::endl;
        std::cout << "3. 注意协程的生命周期管理" << std::endl;
        std::cout << "4. 使用 RAII 来管理资源" << std::endl;
        std::cout << "5. 考虑异常安全性" << std::endl;
        std::cout << "6. 避免悬空引用" << std::endl;
    }

}  // namespace BestPractices

// ================================================================================================
// 主函数 - 运行所有演示
// ================================================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "     C++20 协程 (Coroutine) 教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        // 基础协程概念
        BasicCoroutines::demonstrateBasicGenerator();

        // // 异步协程
        // AsyncCoroutines::demonstrateAsyncTask();

        // // 高级可等待对象
        // AdvancedCoroutines::demonstrateAdvancedAwaitables();

        // // 协程调度器
        // CoroutineScheduler::demonstrateScheduler();

        // // 协程状态机
        // CoroutineStateMachine::demonstrateStateMachine();

        // // 错误处理
        // CoroutineErrorHandling::demonstrateErrorHandling();

        // // 实际应用示例
        // PracticalExample::demonstratePracticalExample();

        // // 最佳实践
        // BestPractices::demonstrate_best_practices();

    } catch (const std::exception& e) {
        std::cout << "程序异常: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n重要总结：" << std::endl;
    std::cout << "1. 协程提供了编写异步代码的新方式" << std::endl;
    std::cout << "2. co_await, co_yield, co_return 是三个关键字" << std::endl;
    std::cout << "3. 协程比传统回调更易读和维护" << std::endl;
    std::cout << "4. 适用于I/O密集型和生成器场景" << std::endl;
    std::cout << "5. 需要注意生命周期和异常处理" << std::endl;
    std::cout << "6. C++20标准库支持，未来趋势" << std::endl;

    return 0;
}
