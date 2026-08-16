// Boost.Coroutine2 API 速查与示例 (C++17)
// 目标：补充 coroutine 中常用函数、类名、成员函数的重要用法，聚焦 API 解释与最小示例

#include <boost/coroutine2/all.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// =============================================================================================
// 1) 核心命名与类型
//    命名空间：boost::coroutines2
//    - coroutine<T>             非对称协程（asymmetric）。核心嵌套类型：
//        * coroutine<T>::push_type  —— 协程体侧（callee）使用的“推送端”，在协程体中调用以 yield 值给调用方
//        * coroutine<T>::pull_type  —— 调用方侧（caller）使用的“拉取端”，用于 resume 协程并取回值
//    - symmetric_coroutine<T>   对称协程（symmetric）。核心嵌套类型：
//        * symmetric_coroutine<T>::call_type  —— 可调用的协程句柄（启动/恢复某个协程）
//        * symmetric_coroutine<T>::yield_type —— 在协程体中用于切换到“下一个”协程或返回
//
// 2) 基本语义
//    - 非对称：pull 调用 operator() 恢复协程；协程体中通过 push(value) 将控制权与数据交回 pull
//    - 对称：在协程体中可 yield(next) 将控制权直接移交给另一个 call 协程，或 yield() 切回调用者
//
// 3) 移动语义
//    push_type / pull_type / call_type 均不可拷贝，仅可移动（持有上下文所有权）。
//
// 4) 异常传播
//    协程体内未捕获的异常会在对端 resume/get 时重新抛出（跨边界传播）。
//
// 5) 栈与属性
//    - 缺省使用 protected_fixedsize_stack（平台相关）。
//    - 可通过 allocator_arg + 自定义栈分配器（fixedsize_stack / protected_fixedsize_stack / segmented_stack）配置栈。
//    - attributes 可配置是否保存 FPU 上下文（preserve_fpu）。
// =============================================================================================

namespace api_asymmetric {
    using coro_t = boost::coroutines2::coroutine<int>;

    // 协程体函数签名：void(push_type&)
    // 在协程体中：
    //   - sink(value)  -> yield 一个值给调用方（pull 端）并暂停
    //   - sink()       -> yield（无值，T=void 时使用）
    void generator_body(coro_t::push_type& sink, int n) {
        for (int i = 1; i <= n; ++i) {
            sink(i);  // yield i
        }
        // 退出协程体即完成（pull 端 operator bool() 将变为 false）
    }

    void demo_basic() {
        std::cout << "[asymmetric] 基本用法\n";

        // 构造1：直接传入可调用对象（lambda/函数），形参为 push_type&
        // pull_type::operator() 用于恢复协程；get() 读取上次 yield 的值；operator bool() 判断是否仍可继续。
        coro_t::pull_type source(
            [&](coro_t::push_type& sink) { generator_body(sink, 5); });

        while (source) {           // 相当于“协程仍存活”
            int v = source.get();  // 读取当前值（T& get() / const T& get()）
            std::cout << v << " ";
            source();  // 恢复协程，继续到下一次 yield/完成
        }
        std::cout << "\n";
    }
}  // namespace api_asymmetric

namespace api_asymmetric_void {
    using coro_v = boost::coroutines2::coroutine<void>;

    void worker(coro_v::push_type& sink, int rounds) {
        for (int i = 0; i < rounds; ++i) {
            // ...
            sink();  // T=void 时使用 sink() 作为“yield 点”
        }
    }

    void demo_void() {
        std::cout << "[asymmetric<void>] 用法\n";
        coro_v::pull_type source(
            [&](coro_v::push_type& sink) { worker(sink, 3); });
        while (source) {
            // 无返回值，单纯将控制权在调用方与协程体之间来回切换
            source();
            std::cout << "tick ";
        }
        std::cout << "\n";
    }
}  // namespace api_asymmetric_void

namespace api_push_driven {
    using coro_t = boost::coroutines2::coroutine<int>;

    // 构造 push_type：传入的可调用对象形参为 pull_type&（在协程体中从调用方“拉取”数据）
    void consumer_body(coro_t::pull_type& source) {
        std::cout << "[push-driven] consumer start\n";
        while (source) {
            int v = source.get();
            std::cout << "  recv: " << v << "\n";
            source();  // 请求下一个值
        }
        std::cout << "[push-driven] consumer end\n";
    }

    void demo_push_driver() {
        std::cout << "[asymmetric] push 驱动用法\n";
        coro_t::push_type sink(consumer_body);  // 在调用方手动推送数据给协程体
        for (int i = 1; i <= 3; ++i) {
            sink(i);  // push_type::operator()(const T&) / operator()(T&&)
        }
        // 当 push_type 析构或失效时，协程体完成；也可通过布尔转换检查：if (!sink) { ... }
    }
}  // namespace api_push_driven

namespace api_exceptions {
    using coro_t = boost::coroutines2::coroutine<int>;

    void body_throw(coro_t::push_type& sink) {
        sink(1);
        throw std::runtime_error("boom");  // 未捕获异常将跨边界在对端重新抛出
    }

    void demo_exception() {
        std::cout << "[asymmetric] 异常传播\n";
        try {
            coro_t::pull_type src(body_throw);
            while (src) {
                std::cout << "  got: " << src.get() << "\n";
                src();
            }
        } catch (const std::exception& e) {
            std::cout << "  caught: " << e.what() << "\n";
        }
    }
}  // namespace api_exceptions

// Note: symmetric_coroutine API may not be available in all Boost versions;
// omitted here for portability. See Boost docs for symmetric coroutine usage.

namespace api_move_semantics {
    using coro_t = boost::coroutines2::coroutine<int>;

    void body(coro_t::push_type& sink) {
        sink(10);
        sink(20);
    }

    void demo_move() {
        std::cout << "[ownership] 移动语义/所有权转移\n";
        coro_t::pull_type src(body);
        std::cout << "  src.valid? " << static_cast<bool>(src) << "\n";

        // 移动构造：src 失效，dst 接管上下文
        coro_t::pull_type dst(std::move(src));
        std::cout << "  after move: src.valid? " << static_cast<bool>(src)
                  << ", dst.valid? " << static_cast<bool>(dst) << "\n";

        while (dst) {
            std::cout << "  got: " << dst.get() << "\n";
            dst();
        }

        // swap 示例：部分平台/版本可能不支持 default-construct pull_type
        std::cout << "  swap example omitted for portability\n";
    }
}  // namespace api_move_semantics

namespace api_stacks {
    using coro_t = boost::coroutines2::coroutine<int>;

    void gen(coro_t::push_type& sink, int n) {
        for (int i = 0; i < n; ++i) sink(i);
    }

    void demo_stacks() {
        std::cout << "[stacks] 自定义栈分配器示例 (omitted custom allocs)\n";
        coro_t::pull_type p([&](coro_t::push_type& sink) { gen(sink, 3); });
        while (p) {
            std::cout << "  protected: " << p.get() << "\n";
            p();
        }
        coro_t::pull_type s([&](coro_t::push_type& sink) { gen(sink, 2); });
        while (s) {
            std::cout << "  segmented: " << s.get() << "\n";
            s();
        }
    }
}  // namespace api_stacks

// =============================================================================================
// 常用 API 速览（非完整原型，强调语义）：
//
// 非对称 coroutine<T>：
//  - pull_type(Fn)                                 // 构造：Fn 形参为 push_type&
//  - pull_type(allocator_arg_t, StackAlloc, Fn[, attributes])
//  - explicit operator bool() const                // 是否仍可继续（未完成/未异常终止）
//  - void operator()()                             // 恢复执行到下一个 yield/结束
//  - T& get(); const T& get() const                // 读取最近一次 yield 的值
//  - void swap(pull_type&) noexcept                // 交换句柄（移动所有权）
//  - 移动构造/赋值，禁止拷贝
//
//  - push_type                                   // 仅在协程体内或由调用方持有用于 push
//    * void operator()(const T& v) / void operator()(T&& v) // push/ yield 值并切换
//    * explicit operator bool() const                       // 对端是否还存活
//    * void swap(push_type&) noexcept
//    * push_type 也仅可移动
//
// 对称 symmetric_coroutine<T>：
//  - call_type(Fn)                                  // 构造：Fn 形参为 yield_type&
//  - explicit operator bool() const                 // 是否可继续
//  - void operator()()                              // 恢复执行
//  - void swap(call_type&) noexcept
//  - 移动构造/赋值，禁止拷贝
//  - yield_type                                     // 仅在协程体内
//    * void operator()()                            // 切回调用者
//    * void operator()(call_type& next)             // 直接切到 next 协程
//
// 栈/属性：
//  - fixedsize_stack / protected_fixedsize_stack / segmented_stack
//  - attributes attrs(true /*preserve_fpu*/)
//  - 通过 allocator_arg 传入自定义栈分配器与属性
//
// 线程与安全：
//  - 单个协程上下文的 resume/yield 需在同一执行序中使用；类型本身不是线程安全容器。
//  - 不要跨线程同时操作同一协程对象。
// =============================================================================================

// 9) 常见陷阱与注意事项（要点速记）
//  - get() 的前置条件：仅在 pull/call 端“已从对端 yield 一个值”的窗口内调用；在首次 operator() 之前直接 get() 是未定义行为。
//  - 生命周期：yield 传递的是值（或引用类型 T 时的引用），确保传出对象在对端使用期间有效；避免返回悬空引用。
//  - 移动语义：句柄（pull/push/call）为独占所有权，移动后源句柄失效；可使用 swap 管理所有权。
//  - 异常：协程体未捕获的异常在对端 resume/get 处重新抛出，按需在对端 try/catch；协程完成后 operator bool()==false。
//  - 线程：不要跨线程并发触碰同一协程句柄；若在线程间交接，务必通过移动转移所有权且保持串行访问。
//  - 对称切换：yield(next) 将栈/上下文切至 next；注意避免形成复杂环导致可读性差。
//  - 栈选择：
//      * fixedsize/protected_fixedsize：开销小，容量固定；受保护版本溢出更易被发现。
//      * segmented_stack：更适合深递归，但有额外管理成本；是否可用取决于平台/编译器支持。
//  - 链接：需链接 boost_context 与 pthread（Linux：-lboost_context -pthread）。
//  - 与 Asio：协程切换仅是用户态上下文切换，不等同于 I/O 多路复用；搭配 Asio 需将异步回调包装到协程的 yield/resume 中。

int main() {
    api_asymmetric::demo_basic();
    api_asymmetric_void::demo_void();
    api_push_driven::demo_push_driver();
    api_exceptions::demo_exception();
    api_move_semantics::demo_move();
    api_stacks::demo_stacks();

    std::cout << "Done\n";
    return 0;
}
