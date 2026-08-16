#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// =================================================================================
// 示例 1: initial_suspend - 控制协程的启动行为
// =================================================================================
namespace InitialSuspendExample {

    // 为 Eager 示例定义一个独立的 Task 类型
    template <typename T>
    struct EagerTask {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type coro;

        explicit EagerTask(handle_type h) : coro(h) {}
        ~EagerTask() {
            if (coro) coro.destroy();
        }
        EagerTask(const EagerTask&) = delete;
        EagerTask& operator=(const EagerTask&) = delete;
        EagerTask(EagerTask&& other) noexcept
            : coro(std::exchange(other.coro, nullptr)) {}
        EagerTask& operator=(EagerTask&& other) noexcept {
            if (this != &other) {
                if (coro) coro.destroy();
                coro = std::exchange(other.coro, nullptr);
            }
            return *this;
        }
    };

    template <typename T>
    struct EagerTask<T>::promise_type {
        T result;
        EagerTask<T> get_return_object() {
            return EagerTask<T>{handle_type::from_promise(*this)};
        }

        // ---> 关键点 <---
        // 返回 suspend_never，协程被调用后会立即执行
        std::suspend_never initial_suspend() {
            std::cout << "  [Promise] initial_suspend: suspend_never, "
                         "coroutine will start eagerly.\n";
            return {};
        }

        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T val) { result = val; }
        void unhandled_exception() { std::terminate(); }
    };

    EagerTask<int> eager_coroutine() {
        std::cout << "[Coroutine Body] Eager coroutine starts execution "
                     "immediately.\n";
        co_return 42;
    }

    // 为 Lazy 示例定义一个独立的 Task 类型
    template <typename T>
    struct LazyTask {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type coro;

        explicit LazyTask(handle_type h) : coro(h) {}
        ~LazyTask() {
            if (coro) coro.destroy();
        }
        LazyTask(const LazyTask&) = delete;
        LazyTask& operator=(const LazyTask&) = delete;
        LazyTask(LazyTask&& other) noexcept
            : coro(std::exchange(other.coro, nullptr)) {}
        LazyTask& operator=(LazyTask&& other) noexcept {
            if (this != &other) {
                if (coro) coro.destroy();
                coro = std::exchange(other.coro, nullptr);
            }
            return *this;
        }

        T get() {
            if (coro && !coro.done()) {
                coro.resume();
            }
            return coro.promise().result;
        }
    };

    template <typename T>
    struct LazyTask<T>::promise_type {
        T result;
        LazyTask<T> get_return_object() {
            return LazyTask<T>{handle_type::from_promise(*this)};
        }

        // ---> 关键点 <---
        // 返回 suspend_always，协程被调用后会立即挂起
        std::suspend_always initial_suspend() {
            std::cout << "  [Promise] initial_suspend: suspend_always, "
                         "coroutine will start lazily.\n";
            return {};
        }

        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T val) { result = val; }
        void unhandled_exception() { std::terminate(); }
    };

    LazyTask<int> lazy_coroutine() {
        std::cout << "[Coroutine Body] Lazy coroutine is now executing because "
                     "it was resumed.\n";
        co_return 100;
    }

    void run() {
        std::cout << "\n--- InitialSuspendExample ---\n";
        std::cout << "1. Eager Start Example:\n";
        {
            std::cout << "Calling eager_coroutine...\n";
            auto task = eager_coroutine();  // 协程体在这里已经执行
            std::cout << "Coroutine object created.\n";
        }

        std::cout << "\n2. Lazy Start Example:\n";
        {
            std::cout << "Calling lazy_coroutine...\n";
            auto task = lazy_coroutine();  // 协程体在这里不会执行
            std::cout << "Coroutine object created. Coroutine is suspended.\n";
            std::cout << "Manually resuming to get the result...\n";
            int result = task.get();  // get 内部调用 resume() 才会执行协程体
            std::cout << "Result: " << result << "\n";
        }
    }
}  // namespace InitialSuspendExample

// =================================================================================
// 示例 2: return_value 和 return_void - 处理协程返回值
// =================================================================================
namespace ReturnValueExample {

    template <typename T>
    struct ReturnTask {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type coro;

        explicit ReturnTask(handle_type h) : coro(h) {}
        ~ReturnTask() {
            if (coro) coro.destroy();
        }
        ReturnTask(const ReturnTask&) = delete;
        ReturnTask& operator=(const ReturnTask&) = delete;
        ReturnTask(ReturnTask&& other) noexcept
            : coro(std::exchange(other.coro, nullptr)) {}
        ReturnTask& operator=(ReturnTask&& other) noexcept {
            if (this != &other) {
                if (coro) coro.destroy();
                coro = std::exchange(other.coro, nullptr);
            }
            return *this;
        }
    };

    // 2.1 return_value: 返回一个具体的值
    template <>
    struct ReturnTask<std::string>::promise_type {
        std::string result;
        ReturnTask<std::string> get_return_object() {
            return ReturnTask<std::string>{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(std::string val) {
            std::cout
                << "  [Promise] return_value: Storing the returned string.\n";
            result = std::move(val);
        }
        void unhandled_exception() { std::terminate(); }
    };

    ReturnTask<std::string> coroutine_with_return_value() {
        co_return "Hello, from coroutine!";
    }

    // 2.2 return_void: 协程没有返回值
    template <>
    struct ReturnTask<void>::promise_type {
        ReturnTask<void> get_return_object() {
            return ReturnTask<void>{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() {
            std::cout << "  [Promise] return_void: Coroutine finished without "
                         "returning a value.\n";
        }
        void unhandled_exception() { std::terminate(); }
    };

    ReturnTask<void> coroutine_with_return_void() {
        std::cout
            << "[Coroutine Body] This coroutine does not return anything.\n";
        co_return;
    }

    void run() {
        std::cout << "\n--- ReturnValueExample ---\n";
        std::cout << "1. Coroutine with return_value:\n";
        {
            auto task = coroutine_with_return_value();
            task.coro.resume();  // 启动协程
            std::cout << "Result: " << task.coro.promise().result << "\n";
        }

        std::cout << "\n2. Coroutine with return_void:\n";
        {
            auto task = coroutine_with_return_void();
            task.coro.resume();  // 启动协程
            std::cout << "Coroutine with void return has completed.\n";
        }
    }
}  // namespace ReturnValueExample

// =================================================================================
// 示例 3: unhandled_exception - 捕获协程内部的异常
// =================================================================================
namespace ExceptionExample {

    template <typename T>
    struct ExceptionTask {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type coro;

        explicit ExceptionTask(handle_type h) : coro(h) {}
        ~ExceptionTask() {
            if (coro) coro.destroy();
        }
        // ... 省略移动构造/赋值和删除拷贝构造/赋值 ...
    };

    template <typename T>
    struct ExceptionTask<T>::promise_type {
        std::variant<T, std::exception_ptr> result;
        ExceptionTask<T> get_return_object() {
            return ExceptionTask<T>{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T val) { result = val; }

        void unhandled_exception() {
            std::cout << "  [Promise] unhandled_exception: Caught an "
                         "exception, storing it.\n";
            result = std::current_exception();  // 保存异常
        }
    };

    ExceptionTask<int> coroutine_that_throws() {
        std::cout << "[Coroutine Body] About to throw an exception...\n";
        throw std::runtime_error("Something went wrong!");
        co_return 0;  // 这行不会被执行
    }

    void run() {
        std::cout << "\n--- ExceptionExample ---\n";
        auto task = coroutine_that_throws();
        task.coro.resume();  // 启动协程，它会立即抛出异常并被 promise 捕获

        auto& promise = task.coro.promise();
        if (std::holds_alternative<std::exception_ptr>(promise.result)) {
            try {
                std::rethrow_exception(
                    std::get<std::exception_ptr>(promise.result));
            } catch (const std::exception& e) {
                std::cout << "Successfully caught exception from coroutine: "
                          << e.what() << "\n";
            }
        }
    }
}  // namespace ExceptionExample

// =================================================================================
// 示例 4: final_suspend - 控制协程结束时的行为
// =================================================================================
namespace FinalSuspendExample {

    // 4.1 final_suspend 返回 suspend_never: 协程自动销毁
    struct AutoDestroyTask {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type coro;

        explicit AutoDestroyTask(handle_type h) : coro(h) {}
        ~AutoDestroyTask() {
            if (coro) {
                std::cout << "  [AutoDestroyTask dtor] Task object destroyed. "
                             "Coroutine frame should be gone already.\n";
            }
        }
    };

    struct AutoDestroyTask::promise_type {
        AutoDestroyTask get_return_object() {
            return AutoDestroyTask{handle_type::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }  // 立即执行

        // ---> 关键点 <---
        // 返回 suspend_never，协程执行完毕后，其状态帧将自动销毁。
        // 不需要调用者手动管理。
        std::suspend_never final_suspend() noexcept {
            std::cout << "  [Promise] final_suspend: suspend_never, coroutine "
                         "frame will be auto-destroyed.\n";
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    AutoDestroyTask auto_destroy_coroutine() {
        std::cout << "[Coroutine Body] This coroutine will self-destruct upon "
                     "completion.\n";
        co_return;
    }

    // 4.2 final_suspend 返回 suspend_always: 协程需要手动销毁
    struct ManualDestroyTask {
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        handle_type coro;

        explicit ManualDestroyTask(handle_type h) : coro(h) {}
        ~ManualDestroyTask() {
            if (coro) {
                std::cout
                    << "  [ManualDestroyTask dtor] Task object destroyed. "
                       "Destroying coroutine frame now.\n";
                coro.destroy();
            }
        }
        // 省略移动构造/赋值和删除拷贝构造/赋值
    };

    struct ManualDestroyTask::promise_type {
        ManualDestroyTask get_return_object() {
            return ManualDestroyTask{handle_type::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }

        // ---> 关键点 <---
        // 返回 suspend_always，协程执行完毕后会保持挂起状态。
        // 其状态帧必须由调用者（如此处的 ManualDestroyTask 对象）手动销毁。
        std::suspend_always final_suspend() noexcept {
            std::cout << "  [Promise] final_suspend: suspend_always, coroutine "
                         "frame must be manually destroyed.\n";
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    ManualDestroyTask manual_destroy_coroutine() {
        std::cout << "[Coroutine Body] This coroutine must be manually "
                     "destroyed after completion.\n";
        co_return;
    }

    void run() {
        std::cout << "\n--- FinalSuspendExample ---\n";
        std::cout
            << "1. Calling a coroutine that auto-destroys (suspend_never)...\n";
        {
            auto task = auto_destroy_coroutine();
            // task 的析构函数被调用，但协程帧已经自毁
        }
        std::cout << "AutoDestroyTask object went out of scope.\n";

        std::cout << "\n2. Calling a coroutine that needs manual destruction "
                     "(suspend_always)...\n";
        {
            auto task = manual_destroy_coroutine();
            // task 的析构函数被调用，它会负责调用 coro.destroy()
        }
        std::cout << "ManualDestroyTask object went out of scope.\n";
    }

}  // namespace FinalSuspendExample

int main() {
    InitialSuspendExample::run();
    ReturnValueExample::run();
    ExceptionExample::run();
    FinalSuspendExample::run();
    return 0;
}