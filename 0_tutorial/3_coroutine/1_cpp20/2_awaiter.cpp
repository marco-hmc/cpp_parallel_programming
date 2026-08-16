#include <coroutine>
#include <iostream>

// ============================================================
// 本文件所有 demo 共用的小工具：一个最小 Task<void>
namespace {
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(
                *this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit Task(handle_type h) : coro(h) {}
};
}  // namespace

// ============================================================
namespace awaiter_three_methods {
/*
    1. co_await 背后发生了什么？
        当协程执行 `co_await expr;` 时，编译器会调用三个方法：
        - await_ready()：快速路径 —— 如果返回 true，直接跳到 await_resume()，不挂起
        - await_suspend(handle)：挂起协程，把句柄交给外部。外部在合适时机 resume 它
        - await_resume()：协程恢复时调用，返回值就是 co_await 表达式的结果

    2. 这个例子演示什么？
        一个 awaiter 在三个方法里分别打印日志，你可以看到调用顺序。
*/

struct LoggingAwaiter {
    bool await_ready() const noexcept {
        std::cout << "  await_ready() → false（准备挂起）\n";
        return false;  // 总是挂起
    }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        std::cout << "  await_suspend() → 保存句柄，然后从 main 里 resume\n";
        // 演示用：直接在这里 resume。实际场景会存起来等事件触发
        h.resume();
    }
    void await_resume() noexcept {
        std::cout << "  await_resume() → 协程恢复了\n";
    }
};

Task demo_coro() {
    std::cout << "  协程体开始\n";
    co_await LoggingAwaiter{};
    std::cout << "  协程体结束\n";
}

void task() {
    std::cout << "main: 创建协程\n";
    auto t = demo_coro();
    std::cout << "main: 协程已完成（awaiter 在 await_suspend 里 resume 了）\n";
}
}  // namespace awaiter_three_methods

// ============================================================
namespace await_ready_fast_path {
/*
    1. await_ready 返回 true 会怎样？
        跳过 await_suspend，直接调用 await_resume，协程不挂起。
        这是一种"同步短路"优化：数据已经在缓存里，不需要异步等待。

    2. 使用场景
        - 缓存命中，直接返回结果
        - 锁立即可用，无需排队
        - 数据已经就绪，不需要等 I/O
*/

struct ReadyAwaiter {
    bool await_ready() const noexcept {
        std::cout << "  await_ready() → true（数据就绪，不挂起！）\n";
        return true;
    }
    void await_suspend(std::coroutine_handle<>) noexcept {
        std::cout << "  await_suspend() —— 这行不会被执行\n";
    }
    int await_resume() noexcept {
        std::cout << "  await_resume() → 直接拿到结果\n";
        return 42;
    }
};

Task demo_coro() {
    std::cout << "  协程体开始\n";
    int result = co_await ReadyAwaiter{};
    std::cout << "  拿到结果：" << result << "（注意：没有挂起过）\n";
}

void task() {
    auto t = demo_coro();
}
}  // namespace await_ready_fast_path

// ============================================================
namespace await_suspend_returns {
/*
    1. await_suspend 的三种返回值
        - void：协程挂起，等待外部 resume()。最常见。
        - bool：true 表示确认挂起；false 表示"算了不挂了"，协程继续执行
        - coroutine_handle<>：控制权转移 —— 不返回给调用者，而是直接 resume 另一个协程

    2. 什么时候用 bool false？
        相当于 await_ready 的二次检查。比如尝试获取锁，发现刚好释放了，
        那就没必要挂起，直接继续。
*/

std::coroutine_handle<> saved_handle;
bool external_resumed = false;

// --- 返回 void：挂起等外部 resume ---
struct VoidSuspendAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        std::cout << "  await_suspend 返回 void：挂起，等待外部 resume\n";
        saved_handle = h;  // 存起来
    }
    void await_resume() noexcept {
        std::cout << "  从外部 resume 后恢复执行\n";
    }
};

// --- 返回 bool false：不挂起 ---
struct BoolNoSuspendAwaiter {
    bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<>) noexcept {
        std::cout << "  await_suspend 返回 false：请求挂起但被驳回，继续执行\n";
        return false;
    }
    void await_resume() noexcept {
        std::cout << "  await_resume（没挂起，直接到这里）\n";
    }
};

// --- 返回 coroutine_handle：控制权转移 ---
Task other_work() {
    std::cout << "    [other_work] 开始\n";
    co_return;
}

struct TransferAwaiter {
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        std::cout << "  await_suspend 返回另一个协程的 handle：控制权转移\n";
        // 注意：当前协程 h 被挂起，直接 resume other_work
        auto other = other_work();
        auto handle = other.coro;
        // 注意：other 对象的析构要小心管理。这里简化演示。
        return handle;  // 运行时直接 resume other_work
    }
    void await_resume() noexcept {
        std::cout << "  await_resume（other_work 完成后回到这里）\n";
    }
};

Task demo_void() {
    std::cout << "  [void 版本] 开始\n";
    co_await VoidSuspendAwaiter{};
    std::cout << "  [void 版本] 结束\n";
}

Task demo_bool() {
    std::cout << "  [bool 版本] 开始\n";
    co_await BoolNoSuspendAwaiter{};
    std::cout << "  [bool 版本] 结束\n";
}

void task() {
    std::cout << "1. void 返回 —— 挂起等外部 resume：\n";
    {
        auto t = demo_void();
        // 协程挂起了，我们手动 resume
        if (saved_handle) {
            std::cout << "main: 外部 resume 协程\n";
            saved_handle.resume();
        }
    }

    std::cout << "\n2. bool 返回 false —— 不挂起：\n";
    { auto t = demo_bool(); }
}
}  // namespace await_suspend_returns

// ============================================================
namespace builtin_awaiters {
/*
    1. 标准库自带的 awaiter
        - std::suspend_always：总是挂起（await_ready 返回 false）
        - std::suspend_never：从不挂起（await_ready 返回 true）
        它们主要用于 initial_suspend 和 final_suspend，但也可以在协程体内直接用。

    2. 什么时候在体内用？
        - co_await std::suspend_always{}：主动让出控制权（协作式调度中的 yield 点）
        - co_await std::suspend_never{}：逻辑上的"断点标记"，不实际挂起（几乎不用）
*/

Task demo_yield() {
    std::cout << "  第 1 步\n";
    co_await std::suspend_always{};  // 主动让出
    std::cout << "  第 2 步（resume 之后）\n";
    co_await std::suspend_always{};
    std::cout << "  第 3 步（再次 resume 之后）\n";
}

void task() {
    std::cout << "main: 创建协程";
    auto t = demo_yield();
    // 协程会在第 1 步之后挂起
    std::cout << "（协程已挂起，main 做其他事情...）\n";
    std::cout << "main: 第一次 resume\n";
    t.coro.resume();
    std::cout << "main: 第二次 resume\n";
    t.coro.resume();
}
}  // namespace builtin_awaiters

// ============================================================
int main() {
    std::cout << "===== 1. awaiter 三步流程 =====\n";
    awaiter_three_methods::task();

    std::cout << "\n===== 2. await_ready 快速路径 =====\n";
    await_ready_fast_path::task();

    std::cout << "\n===== 3. await_suspend 三种返回类型 =====\n";
    await_suspend_returns::task();

    std::cout << "\n===== 4. suspend_always / suspend_never =====\n";
    builtin_awaiters::task();

    return 0;
}
