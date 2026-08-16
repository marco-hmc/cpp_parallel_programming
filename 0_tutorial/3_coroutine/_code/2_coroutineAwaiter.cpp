#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// =================================================================================
// 示例 5: awaiter 三方法对比
// =================================================================================
namespace AwaiterExample {
    template <typename T>
    struct ReturnTask;

    // Insert: minimal coroutine ReturnTask<void> so the file is self-contained and compiles.
    template <>
    struct ReturnTask<void> {
        struct promise_type {
            ReturnTask<void> get_return_object() noexcept {
                return ReturnTask<void>{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            // start suspended; caller will resume
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() { std::terminate(); }
        };

        std::coroutine_handle<> coro;

        explicit ReturnTask(std::coroutine_handle<> h) noexcept : coro(h) {}
        ReturnTask(ReturnTask&& other) noexcept : coro(other.coro) {
            other.coro = nullptr;
        }
        ReturnTask& operator=(ReturnTask&& other) noexcept {
            if (this != &other) {
                if (coro) coro.destroy();
                coro = other.coro;
                other.coro = nullptr;
            }
            return *this;
        }
        ~ReturnTask() {
            if (coro) coro.destroy();
        }

        // non-copyable
        ReturnTask(const ReturnTask&) = delete;
        ReturnTask& operator=(const ReturnTask&) = delete;
    };

    // 说明：
    // 1) await_ready() -> 如果返回 true，则不挂起，直接调用 await_resume()。
    // 2) await_suspend(handle) -> 仅在 await_ready() 返回 false 时被调用。返回值：
    //      - void：协程挂起，由外部决定何时 resume。
    //      - bool：返回 true 保持挂起，返回 false 表示不挂起（立即继续）。
    //      - std::coroutine_handle<>：将控制权转给返回的协程句柄。
    // 3) await_resume() -> await 完成后返回结果或继续逻辑。
    //
    // 下面三个 Awaiter 演示常见组合。

    // 例 A：await_ready 返回 true —— 不会调用 await_suspend，立即执行 await_resume
    struct ReadyAwaiter {
        bool await_ready() noexcept {
            std::cout
                << "  [ReadyAwaiter] await_ready() -> true (no suspend)\n";
            return true;
        }
        void await_suspend(std::coroutine_handle<>) noexcept {
            std::cout
                << "  [ReadyAwaiter] await_suspend() (shouldn't be called)\n";
        }
        void await_resume() noexcept {
            std::cout << "  [ReadyAwaiter] await_resume()\n";
        }
    };

    // 例 B：await_ready 返回 false，await_suspend 返回 bool（此处返回 false -> 不挂起）
    struct BoolDecideAwaiter {
        bool await_ready() noexcept {
            std::cout << "  [BoolDecideAwaiter] await_ready() -> false\n";
            return false;
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            std::cout << "  [BoolDecideAwaiter] await_suspend() -> return "
                         "false (do not suspend)\n";
            (void)h;
            return false;  // 不挂起，立即继续
        }
        void await_resume() noexcept {
            std::cout << "  [BoolDecideAwaiter] await_resume()\n";
        }
    };

    // 例 C：await_ready 返回 false，await_suspend 返回 true（挂起），由外部 resume
    static std::coroutine_handle<>
        saved_handle_for_manual;  // 保存句柄以便外部 resume
    struct ManualSuspendAwaiter {
        bool await_ready() noexcept {
            std::cout << "  [ManualSuspendAwaiter] await_ready() -> false\n";
            return false;
        }
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            std::cout << "  [ManualSuspendAwaiter] await_suspend() -> return "
                         "true (suspend)\n";
            saved_handle_for_manual = h;
            return true;  // 保持挂起
        }
        void await_resume() noexcept {
            std::cout << "  [ManualSuspendAwaiter] await_resume()\n";
        }
    };

    ReturnTask<void> ready_coroutine() {
        std::cout << "[ready_coroutine] before co_await ReadyAwaiter\n";
        co_await ReadyAwaiter{};
        std::cout << "[ready_coroutine] after co_await ReadyAwaiter\n";
        co_return;
    }

    ReturnTask<void> bool_decide_coroutine() {
        std::cout
            << "[bool_decide_coroutine] before co_await BoolDecideAwaiter\n";
        co_await BoolDecideAwaiter{};
        std::cout
            << "[bool_decide_coroutine] after co_await BoolDecideAwaiter\n";
        co_return;
    }

    ReturnTask<void> manual_suspend_coroutine() {
        std::cout << "[manual_suspend_coroutine] before co_await "
                     "ManualSuspendAwaiter\n";
        co_await ManualSuspendAwaiter{};
        std::cout << "[manual_suspend_coroutine] after co_await "
                     "ManualSuspendAwaiter (resumed)\n";
        co_return;
    }

    void run() {
        std::cout << "\n--- AwaiterExample ---\n";

        std::cout << "\n1. ReadyAwaiter (await_ready == true):\n";
        {
            auto task = ready_coroutine();
            task.coro.resume();  // 启动协程
        }

        std::cout
            << "\n2. BoolDecideAwaiter (await_suspend 返回 false -> 不挂起):\n";
        {
            auto task = bool_decide_coroutine();
            task.coro.resume();  // 启动协程
        }

        std::cout << "\n3. ManualSuspendAwaiter (挂起，需要外部 resume):\n";
        {
            auto task = manual_suspend_coroutine();
            task.coro.resume();  // 启动协程，执行到 await_suspend 并挂起
            std::cout << "  [run] coroutine suspended, externally resume via "
                         "saved_handle_for_manual...\n";
            if (saved_handle_for_manual) {
                saved_handle_for_manual.resume();
                saved_handle_for_manual = nullptr;
            }
        }
    }

}  // namespace AwaiterExample

int main() {
    AwaiterExample::run();
    return 0;
}