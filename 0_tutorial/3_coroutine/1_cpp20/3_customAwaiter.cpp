#include <chrono>
#include <coroutine>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

// ============================================================
// 共用最小 Task<void>
namespace {
struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
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
namespace operator_co_await {
/*
    1. co_await 一个"不认识"的类型怎么办？
        编译器按以下顺序尝试找到 awaiter：
        1) 如果 promise 有 await_transform()，用它转换
        2) 如果类型自己有 operator co_await()，用它
        3) 如果类型实现了 await_ready/await_suspend/await_resume 三个方法，直接用
        4) 全局 operator co_await() 重载

    2. 这个例子演示什么？
        给一个普通类型（比如我们自己写的 Timer 类）添加 operator co_await，
        让它可以被 co_await 使用。
*/

struct MyTimer {
    int ms_;
    explicit MyTimer(int ms) : ms_(ms) {}
};

// 给 MyTimer 配一个 awaiter
struct TimerAwaiter {
    int ms_;
    explicit TimerAwaiter(int ms) : ms_(ms) {}

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        std::cout << "  等待 " << ms_ << "ms...（新线程模拟）\n";
        std::thread([h, ms = ms_]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            h.resume();
        }).detach();
    }
    void await_resume() noexcept {
        std::cout << "  等待完成\n";
    }
};

// 关键：给 MyTimer 实现 operator co_await
TimerAwaiter operator co_await(MyTimer t) {
    return TimerAwaiter{t.ms_};
}

Task demo_coro() {
    std::cout << "  开始\n";
    co_await MyTimer{300};  // MyTimer 本身不是 awaiter，但有 operator co_await
    std::cout << "  结束\n";
}

void task() {
    auto t = demo_coro();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 等后台线程完成
}
}  // namespace operator_co_await

// ============================================================
namespace timer_awaiter {
/*
    1. 计时器 awaiter —— "挂起不阻塞线程"
        普通 sleep 会阻塞当前线程，其他事情都干不了。
        协程 + 计时器 awaiter 的做法是：挂起协程 → 启动一个后台线程计时 →
        计时结束 resume 协程 → 这期间调用者线程可以干别的事。

    2. 和上面的 operator_co_await 版本的区别？
        这个版本把 awaiter 直接作为 co_await 的目标（实现了三个 await 方法），
        不需要 operator co_await。
*/

struct SleepAwaiter {
    int ms_;
    explicit SleepAwaiter(int ms) : ms_(ms) {}

    bool await_ready() const noexcept {
        // 如果时间很短（比如 <= 0），直接不挂起
        return ms_ <= 0;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        std::cout << "  [SleepAwaiter] 挂起 " << ms_ << "ms\n";
        std::thread([h, ms = ms_]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            h.resume();
        }).detach();
    }

    void await_resume() noexcept {
        std::cout << "  [SleepAwaiter] 恢复\n";
    }
};

Task delay_print(const std::string& msg, int delay_ms) {
    co_await SleepAwaiter{delay_ms};
    std::cout << "  " << msg << "\n";
}

void task() {
    std::cout << "main: 启动 3 个并发等待...（注意总等待时间 = max，不是 sum）\n";
    auto t1 = delay_print("第一条消息（300ms 后）", 300);
    auto t2 = delay_print("第二条消息（100ms 后）", 100);
    auto t3 = delay_print("第三条消息（200ms 后）", 200);
    // 三个协程的计时器各自跑在独立线程上，互不阻塞
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "main: 所有消息应该都已打印\n";
}
}  // namespace timer_awaiter

// ============================================================
namespace callback_awaiter {
/*
    1. 把回调式的异步 API 包成协程
        很多 C/C++ 库的异步接口是回调式的：发起操作时传一个回调函数，
        操作完成后回调被调用。协程可以把这个模式包装成 awaiter：
        - await_suspend 里注册回调
        - 回调里 resume 协程
        这样异步代码写起来就像同步代码。

    2. 线程安全注意事项
        回调可能在另一个线程被调用，resume 协程时要注意：
        - coroutine_handle 本身是线程安全的（可以跨线程 resume）
        - 但协程体内如果有共享数据，需要自己保护
*/

// 模拟一个"异步文件读取"的 C API：发起后过一段时间回调
struct AsyncReader {
    static void read_async(const std::string& path,
                           std::function<void(const std::string&)> callback) {
        // 用新线程模拟：过 200ms 后回调
        std::thread([path, callback = std::move(callback)]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            callback("文件内容: " + path);
        }).detach();
    }
};

struct ReadAwaiter {
    std::string path_;
    std::string result_;
    std::mutex mtx_;
    bool done_ = false;

    explicit ReadAwaiter(std::string path) : path_(std::move(path)) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        AsyncReader::read_async(path_, [this, h](const std::string& content) {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                result_ = content;
                done_ = true;
            }
            h.resume();  // 在回调线程里 resume 协程
        });
    }

    std::string await_resume() {
        std::lock_guard<std::mutex> lk(mtx_);
        return result_;
    }
};

Task read_file_demo() {
    std::cout << "  发起异步读取...\n";
    std::string content = co_await ReadAwaiter{"data.txt"};
    std::cout << "  读取完成：" << content << "\n";
}

void task() {
    auto t = read_file_demo();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
}  // namespace callback_awaiter

// ============================================================
namespace symmetric_transfer {
/*
    1. 对称转移 —— 协程 A 直接切到协程 B
        如果 await_suspend 返回另一个协程的 handle（而不是 void 或 bool），
        运行时不会返回调用者，而是直接 resume 那个协程。
        这可以避免一层层的调用栈返回，实现"无栈协程间的对称切换"。

    2. 和普通 resume 的区别？
        - 普通：A 挂起 → 返回调用者 → 调用者 resume B
        - 对称：A 挂起 → 直接 resume B → ... → B 结束后回到 A 的 await_resume

        关键：B 结束后回到谁那里？如果 B 的 final_suspend 返回 B 的 handle，
        A 就能收到。这是实现协程链式调用的基础。
*/

Task coro_b() {
    std::cout << "    [B] 开始执行\n";
    co_return;
}

Task coro_a() {
    std::cout << "  [A] 开始，即将切到 B\n";
    // 注意：这里直接调用 coro_b() 建立了一个子协程
    auto b = coro_b();
    // 对称转移：A 挂起，直接 resume B（不回到 main）
    co_await std::suspend_always{};  // 简化演示：手动切
    std::cout << "  [A] 恢复（B 已完成）\n";
}

void task() {
    std::cout << "对称转移演示（简洁版）：\n";
    auto a = coro_a();
    // A 在创建后进入 initial_suspend（因为我们的 Task 用的 suspend_never，
    // 所以 A 已经运行了一部分）。手动 resume 让 A 继续。
    std::cout << "main: resume A\n";
    a.coro.resume();
    std::cout << "main: A 已完成，对比：如果 main 不 resume，协程只会走到第一个挂起点\n";
}
}  // namespace symmetric_transfer

// ============================================================
int main() {
    std::cout << "===== 1. operator co_await —— 让普通类型可被 await =====\n";
    operator_co_await::task();

    std::cout << "\n===== 2. 计时器 awaiter —— 挂起不阻塞线程 =====\n";
    timer_awaiter::task();

    std::cout << "\n===== 3. 回调 → 协程适配 =====\n";
    callback_awaiter::task();

    std::cout << "\n===== 4. 对称转移 =====\n";
    symmetric_transfer::task();

    return 0;
}
