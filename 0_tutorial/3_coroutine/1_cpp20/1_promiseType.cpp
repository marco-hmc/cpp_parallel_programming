#include <coroutine>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <variant>

// ============================================================
namespace coro_lifecycle {
/*
    1. 协程是怎么创建和销毁的？
        C++20 协程的核心是三个东西：
        - coroutine_handle：协程句柄，控制协程的恢复和销毁
        - promise_type：定义协程的行为（怎么开始、怎么结束、怎么返回值）
        - coroutine frame（协程帧）：编译器在堆上分配的"状态机"，存放局部变量和 promise
        一个协程被调用时：
        1) 编译器分配协程帧（默认在堆上，可被优化省略）
        2) 在帧内构造 promise_type
        3) 调用 promise.get_return_object() 获取返回给调用者的对象
        4) 执行 initial_suspend —— 决定是否立即开始，还是挂起等调用者手动 resume
        5) 执行协程体
        6) 执行 final_suspend —— 决定销毁帧还是让调用者手动 destroy()
        7) 析构 promise 和局部变量
        8) 释放协程帧

    2. 这个例子演示什么？
        一个最小协程的完整生命周期。每一步都有日志输出，方便观察执行顺序。
*/

struct LifecycleTask {
    struct promise_type {
        std::string log;  // 记录生命周期日志

        LifecycleTask get_return_object() {
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            std::cout << "[1] get_return_object() —— 返回外壳对象\n";
            return LifecycleTask{h};
        }

        std::suspend_never initial_suspend() {
            std::cout << "[2] initial_suspend() —— suspend_never：不挂起，直接执行协程体\n";
            return {};
        }

        std::suspend_never final_suspend() noexcept {
            std::cout << "[4] final_suspend() —— suspend_never：协程结束后自动销毁帧\n";
            return {};
        }

        void return_void() {
            std::cout << "[3] return_void() —— 协程体执行完毕，co_return;\n";
        }

        void unhandled_exception() { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;

    explicit LifecycleTask(handle_type h) : coro(h) {}
    ~LifecycleTask() {
        if (coro) coro.destroy();
    }
};

LifecycleTask simple_coroutine() {
    std::cout << "      协程体开始执行...\n";
    co_return;
}

void task() {
    std::cout << "调用协程前...\n";
    {
        auto task = simple_coroutine();  // 协程在这里已经执行完了
    }
    std::cout << "协程对象已析构（帧已自动销毁）\n";
}
}  // namespace coro_lifecycle

// ============================================================
namespace eager_vs_lazy {
/*
    1. initial_suspend 的两种选择
        - suspend_never（立即执行 / eager）：协程创建后立刻执行，不等调用者。适合"即开即用"的场景。
        - suspend_always（延迟执行 / lazy）：协程创建后挂起，调用者需要显式 resume() 才执行。适合生成器、
          Task<T> 等需要调用者控制执行时机的场景。

    2. 延迟执行有什么好处？
        - 调用者可以先存储协程，在合适的时机再启动
        - 生成器场景下，调用者每次 resume 拿一个值，自然需要 lazy 启动
*/

struct EagerTask {
    struct promise_type {
        EagerTask get_return_object() {
            return EagerTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }  // ← eager
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit EagerTask(handle_type h) : coro(h) {}
};

struct LazyTask {
    struct promise_type {
        LazyTask get_return_object() {
            return LazyTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }  // ← lazy
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit LazyTask(handle_type h) : coro(h) {}
    ~LazyTask() {
        if (coro) coro.destroy();
    }
    void resume() {
        if (coro && !coro.done()) coro.resume();
    }
};

EagerTask eager_coro() {
    std::cout << "  eager: 协程体立即执行了（调用者什么都没做）\n";
    co_return;
}

LazyTask lazy_coro() {
    std::cout << "  lazy: 协程体在 resume() 之后才执行\n";
    co_return;
}

void task() {
    std::cout << "创建 eager 协程：\n";
    auto e = eager_coro();  // 协程体在这里已经执行完了

    std::cout << "创建 lazy 协程：\n";
    auto l = lazy_coro();  // 协程体还没有执行
    std::cout << "调用 resume()：\n";
    l.resume();  // 协程体现在才执行
    std::cout << "再次 resume() 结束协程：\n";
    l.resume();
}
}  // namespace eager_vs_lazy

// ============================================================
namespace return_void_vs_value {
/*
    1. return_value 和 return_void
        协程只能声明其中一个，不能同时声明：
        - co_return value;  → 需要 promise.return_value(value)
        - co_return;        → 需要 promise.return_void()
        如果声明了 return_value 却写 co_return;（或反过来），编译报错。

    2. 结果存在哪里？
        通常存在 promise 的成员变量里（比如 value_），调用者通过协程外壳对象拿到。
*/

template <typename T>
struct ReturnTask {
    struct promise_type {
        T value_;
        ReturnTask get_return_object() {
            return ReturnTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T v) { value_ = v; }
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit ReturnTask(handle_type h) : coro(h) {}
    T get() { return coro.promise().value_; }
};

ReturnTask<int> compute_value() {
    std::cout << "  协程体计算中...\n";
    co_return 42;
}

void task() {
    auto t = compute_value();
    std::cout << "  拿到返回值：" << t.get() << "\n";
}
}  // namespace return_void_vs_value

// ============================================================
namespace exception_handling {
/*
    1. 协程内部抛异常怎么办？
        协程内部如果抛出未捕获的异常，编译器会调用 promise.unhandled_exception()。
        通常的做法是在 unhandled_exception() 里用 std::current_exception() 捕获异常，
        存到 promise 成员里。调用者后续可以通过 get() 重新抛出。

    2. 为什么不在 unhandled_exception() 里直接抛？
        因为 unhandled_exception() 是在协程栈展开过程中被调用的，
        直接抛异常会导致 std::terminate()。必须先把异常存起来。
*/

template <typename T>
struct SafeTask {
    struct promise_type {
        std::variant<T, std::exception_ptr> result_;

        SafeTask get_return_object() {
            return SafeTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_value(T v) { result_ = v; }
        void unhandled_exception() { result_ = std::current_exception(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit SafeTask(handle_type h) : coro(h) {}

    T get() {
        auto& result = coro.promise().result_;
        if (std::holds_alternative<std::exception_ptr>(result)) {
            std::rethrow_exception(std::get<std::exception_ptr>(result));
        }
        return std::get<T>(result);
    }
};

SafeTask<int> might_throw(bool do_throw) {
    if (do_throw) {
        throw std::runtime_error("协程内部抛出的异常");
    }
    co_return 100;
}

void task() {
    {
        auto t = might_throw(false);
        std::cout << "  正常结果：" << t.get() << "\n";
    }
    {
        auto t = might_throw(true);
        try {
            t.get();
        } catch (const std::exception& e) {
            std::cout << "  捕获到异常：" << e.what() << "\n";
        }
    }
}
}  // namespace exception_handling

// ============================================================
namespace final_suspend_management {
/*
    1. 谁负责销毁协程帧？
        - final_suspend 返回 suspend_never：协程结束 → 自动销毁帧 → 调用者不能碰 handle 了
        - final_suspend 返回 suspend_always：协程在最后挂起 → 需要调用者显式 destroy()

    2. 什么时候需要手动销毁？
        当调用者需要在协程结束后还要读取 promise 里的结果（比如 Task<T>::get() 拿返回值），
        就必须让 final_suspend 挂起。否则帧自动销毁后，promise 里的值也没了。
*/

struct AutoDestroyTask {
    struct promise_type {
        AutoDestroyTask get_return_object() {
            return AutoDestroyTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept {
            std::cout << "  final_suspend: suspend_never → 帧即将自动销毁\n";
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit AutoDestroyTask(handle_type h) : coro(h) {}
};

struct ManualDestroyTask {
    struct promise_type {
        ManualDestroyTask get_return_object() {
            return ManualDestroyTask{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept {
            std::cout << "  final_suspend: suspend_always → 帧挂起，等调用者 destroy\n";
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit ManualDestroyTask(handle_type h) : coro(h) {}
    ~ManualDestroyTask() {
        if (coro) {
            std::cout << "  调用者 destroy 协程帧\n";
            coro.destroy();
        }
    }
};

AutoDestroyTask auto_destroy_coro() { co_return; }
ManualDestroyTask manual_destroy_coro() { co_return; }

void task() {
    std::cout << "自动销毁：\n";
    { auto t = auto_destroy_coro(); }
    std::cout << "手动销毁：\n";
    { auto t = manual_destroy_coro(); }
}
}  // namespace final_suspend_management

// ============================================================
int main() {
    std::cout << "===== 1. 协程生命周期 =====\n";
    coro_lifecycle::task();

    std::cout << "\n===== 2. 立即执行 vs 延迟执行 =====\n";
    eager_vs_lazy::task();

    std::cout << "\n===== 3. return_value vs return_void =====\n";
    return_void_vs_value::task();

    std::cout << "\n===== 4. 协程异常处理 =====\n";
    exception_handling::task();

    std::cout << "\n===== 5. 自动销毁 vs 手动销毁帧 =====\n";
    final_suspend_management::task();

    return 0;
}
