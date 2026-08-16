#include <coroutine>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

// ============================================================
// 完整的 Task<T> —— 类似 std::future 的协程版本
// 特点：lazy 启动（initial_suspend = suspend_always）、手动 get() 获取结果
//       异常通过 variant 存储，get() 时重新抛出
template <typename T>
struct Task {
    struct promise_type {
        std::variant<T, std::exception_ptr> result_;

        Task get_return_object() {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }  // lazy
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T v) { result_ = std::move(v); }
        void unhandled_exception() { result_ = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;

    explicit Task(handle_type h) : coro(h) {}
    Task(const Task&) = delete;
    Task(Task&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro) coro.destroy();
            coro = other.coro;
            other.coro = nullptr;
        }
        return *this;
    }
    ~Task() {
        if (coro) coro.destroy();
    }

    T get() {
        // 执行协程直到完成
        while (!coro.done()) {
            coro.resume();
        }
        auto& result = coro.promise().result_;
        if (std::holds_alternative<std::exception_ptr>(result)) {
            std::rethrow_exception(std::get<std::exception_ptr>(result));
        }
        return std::get<T>(result);
    }

    bool done() const { return coro.done(); }
};

// ============================================================
namespace task_return_value {
/*
    1. Task<T> 是什么？
        类似 std::async 返回的 std::future<T>，Task<T> 代表一个"将来会返回 T"的异步任务。
        区别是 Task<T> 用协程实现，是 lazy 的——不调用 get() 就不会执行。

    2. 和 Generator<T> 的区别
        - Generator：co_yield 产出多个值，按需拉取
        - Task<T>：co_return 返回一个值，一次性计算
*/

Task<int> compute_async() {
    std::cout << "  计算中...\n";
    int result = 1 + 2 + 3;
    co_return result * 10;
}

void task() {
    auto t = compute_async();
    std::cout << "  还没有开始计算（lazy）\n";
    int result = t.get();
    std::cout << "  拿到结果：" << result << "\n";
}
}  // namespace task_return_value

// ============================================================
namespace task_exception {
/*
    1. 异常的传播路径
        协程体内 throw → promise.unhandled_exception() 捕获 → 存入 variant →
        调用者 get() → rethrow_exception → 调用者 catch
*/

Task<int> divide(int a, int b) {
    if (b == 0) {
        throw std::runtime_error("除数为零");
    }
    co_return a / b;
}

void task() {
    auto ok = divide(10, 2);
    std::cout << "  10 / 2 = " << ok.get() << "\n";

    auto err = divide(10, 0);
    try {
        err.get();
    } catch (const std::exception& e) {
        std::cout << "  10 / 0 → 异常：" << e.what() << "\n";
    }
}
}  // namespace task_exception

// ============================================================
namespace task_chain {
/*
    1. 协程嵌套 —— 一个协程 co_await 另一个协程
        协程 A 里调用协程 B 并 co_await B 的结果。这是把异步逻辑组合成
        更大的异步逻辑的关键方式。

    2. "awaitable Task" 模式
        需要给 Task 实现 await_ready/await_suspend/await_resume 三个方法，
        让 Task 本身成为一个 awaiter。这样 co_await taskInstance 就可以嵌套。

    3. 所有权注意
        co_await 一个 Task 时，Task 对象要以值传递的形式被 capture 进 awaiter，
        确保协程 B 的生命周期被正确管理。
*/

// ---- 给 Task 添加 awaiter 接口 ----
// 用一个新的包装类型来避免修改上面的 Task
template <typename T>
struct AwaitableTask {
    Task<T> task_;

    explicit AwaitableTask(Task<T>&& t) : task_(std::move(t)) {}

    bool await_ready() noexcept { return task_.done(); }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        // 不断 resume 子任务直到完成
        while (!task_.done()) {
            task_.coro.resume();
        }
        h.resume();  // 子任务完成后恢复父协程
    }

    T await_resume() { return task_.get(); }
};

// 辅助函数：让 Task 可以直接被 co_await
template <typename T>
AwaitableTask<T> make_awaitable(Task<T> t) {
    return AwaitableTask<T>(std::move(t));
}

Task<int> fetch_price(const std::string& symbol) {
    // 模拟：不同股票有不同的"计算耗时"
    if (symbol == "AAPL") co_return 150;
    if (symbol == "GOOG") co_return 2800;
    co_return 0;
}

Task<int> compute_total() {
    std::cout << "  开始获取价格...\n";

    // 用 make_awaitable 包装子 Task，使其可被 co_await
    auto price1 = co_await make_awaitable(fetch_price("AAPL"));
    std::cout << "  AAPL = " << price1 << "\n";

    auto price2 = co_await make_awaitable(fetch_price("GOOG"));
    std::cout << "  GOOG = " << price2 << "\n";

    co_return price1 + price2;
}

void task() {
    auto t = compute_total();
    int total = t.get();
    std::cout << "  总价 = " << total << "\n";
}
}  // namespace task_chain

// ============================================================
// Task<void> 特化 —— 必须在文件作用域，不能在 namespace 内
template <>
struct Task<void> {
    struct promise_type {
        std::exception_ptr exception_;

        Task get_return_object() {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { exception_ = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;

    explicit Task(handle_type h) : coro(h) {}
    Task(const Task&) = delete;
    Task(Task&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro) coro.destroy();
            coro = other.coro;
            other.coro = nullptr;
        }
        return *this;
    }
    ~Task() {
        if (coro) coro.destroy();
    }

    void get() {
        while (!coro.done()) coro.resume();
        auto& e = coro.promise().exception_;
        if (e) std::rethrow_exception(e);
    }
};

// ============================================================
namespace task_void {
/*
    1. Task<void> —— 没有返回值的异步任务
        promise_type 用 return_void 代替 return_value(T)。
        用于"执行一个耗时操作，结束后通知调用者"的场景。
*/

Task<void> log_and_wait(const std::string& msg) {
    std::cout << "  [log] " << msg << "\n";
    co_return;  // co_return; 对应 return_void
}

void task() {
    auto t = log_and_wait("任务完成通知");
    t.get();
    std::cout << "  调用者收到完成通知\n";
}
}  // namespace task_void

// ============================================================
int main() {
    std::cout << "===== 1. Task<T> 基本用法 =====\n";
    task_return_value::task();

    std::cout << "\n===== 2. Task<T> 异常传播 =====\n";
    task_exception::task();

    std::cout << "\n===== 3. 协程嵌套调用链 =====\n";
    task_chain::task();

    std::cout << "\n===== 4. Task<void> =====\n";
    task_void::task();

    return 0;
}
