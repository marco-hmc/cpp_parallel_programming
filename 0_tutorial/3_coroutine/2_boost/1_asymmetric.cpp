/*
    Boost.Coroutine2 非对称协程参考
    =================================
    本文件仅供阅读学习，不参与构建（本机未安装 Boost）。
    若安装了 Boost，可手动编译：
      g++ -std=c++17 1_asymmetric.cpp -lboost_coroutine -pthread

    核心概念：
    - pull_type：消费端（caller 拉取数据）
    - push_type：生产端（协程推送数据）
    - sink(value)：协程内部用来传值给 pull 端的方法，调用后协程挂起
    - pull.get() / pull()：caller 拉取下一个值，协程恢复执行
    - pull 的 operator bool()：检查协程是否还有更多数据

    类比 C++20：
    - pull_type  ≈ Generator<T> 的 iterator（但由库管理）
    - push_type  ≈ promise_type::yield_value（但传给了协程的用户 lambda）
    - sink(x)    ≈ co_yield x

    使用前提：
    - #include <boost/coroutine2/all.hpp>
    - 链接 boost_coroutine（需要 boost_context）
*/

// #include <boost/coroutine2/all.hpp>
// #include <iostream>
// #include <string>
// #include <vector>
//
// using namespace boost::coroutines2;

namespace demo_basic_generator {
/*
    1. 最简单的生成器
        coroutine<int>::pull_type source([](auto& sink) {
            sink(1);  // 产出 1，挂起
            sink(2);  // 产出 2，挂起
            sink(3);  // 产出 3，挂起 → 协程结束
        });
        // 三种遍历方式：
        // while(source) { int v = source.get(); }
        // for (int v : source) { ... }        （推荐）
        // int v = source(); source(); ...
*/

void demo() {
    // coroutine<int>::pull_type source([](coroutine<int>::push_type& sink) {
    //     for (int i = 1; i <= 3; ++i) {
    //         sink(i);
    //     }
    // });
    //
    // std::cout << "生成器产出：\n";
    // for (int v : source) {
    //     std::cout << "  " << v << "\n";
    // }
}
}  // namespace demo_basic_generator

namespace demo_fibonacci {
/*
    2. 无限序列生成器
        和 C++20 Generator 一样，只要不 break 就会一直产出。
        pull 析构时协程自动清理。
*/

void demo() {
    // coroutine<long long>::pull_type fib([](auto& sink) {
    //     long long a = 0, b = 1;
    //     while (true) {
    //         sink(a);
    //         auto next = a + b;
    //         a = b;
    //         b = next;
    //     }
    // });
    //
    // int count = 0;
    // std::cout << "斐波那契前 10 项：\n";
    // for (auto v : fib) {
    //     std::cout << v << " ";
    //     if (++count >= 10) break;
    // }
    // std::cout << "\n";
}
}  // namespace demo_fibonacci

namespace demo_push_driven {
/*
    3. 推模式 —— caller 向协程喂数据
        有些场景是 caller 有数据，协程负责处理。
        这时用 push_type（caller 持有 push，协程体接收 pull）。
        caller 通过 push(data) 把数据推进去，协程收到后处理。

        类比 C++20：相当于 co_yield 的反向——caller 给协程传值。
        在 C++20 中，这通常通过 co_await 的 awaiter 传参实现。
*/

void demo() {
    // coroutine<std::string>::push_type processor([](auto& pull) {
    //     // 协程体：接收 caller 推来的数据
    //     while (pull) {
    //         std::string word = pull.get();  // 从 caller 拉取
    //         std::cout << "  处理中：" << word << "\n";
    //     }
    // });
    //
    // // caller：推数据给协程
    // std::cout << "推三个单词给协程处理：\n";
    // processor("Hello");
    // processor("Boost");
    // processor("Coroutine");
    // // processor 析构 → pull 的 operator bool() 变 false → 协程退出循环
}
}  // namespace demo_push_driven

namespace demo_exception {
/*
    4. 异常跨边界传播
        Boost.Coroutine2 支持异常在 caller 和协程之间传播：
        - 协程内抛异常 → caller 的 get()/operator() 处重新抛出
        - caller 端抛异常 → 协程内 sink() 处重新抛出（如果协程正在等待）
*/

void demo() {
    // try {
    //     coroutine<int>::pull_type source([](auto& sink) {
    //         for (int i = 1; i <= 5; ++i) {
    //             if (i == 3) throw std::runtime_error("协程内部故障");
    //             sink(i);
    //         }
    //     });
    //
    //     for (int v : source) {
    //         std::cout << "  " << v << "\n";
    //     }
    // } catch (const std::exception& e) {
    //     std::cout << "捕获异常：" << e.what() << "\n";
    // }
}
}  // namespace demo_exception

namespace demo_stack_allocator {
/*
    5. 栈分配器（性能优化）
        默认情况下每个协程分配独立的栈（默认约 64KB）。
        可以通过 attributes 自定义栈大小或使用池化分配器减少 malloc 开销。
        适用于创建大量短生命周期协程的场景。
*/

void demo() {
    // boost::coroutines2::attributes attrs;
    // attrs.size = 4096;  // 自定义栈大小（最小值因平台而异）
    //
    // coroutine<int>::pull_type source(attrs, [](auto& sink) {
    //     sink(42);
    // });
    //
    // std::cout << "使用自定义栈的协程产出：" << source.get() << "\n";
}
}  // namespace demo_stack_allocator

/*
    注意事项：
    1. pull_type 和 push_type 必须在对方存在时才能操作。
       如果 push_type 已析构而 pull_type 还尝试拉取 → 未定义行为。
    2. 不要在不同线程中同时操作 pull 和 push。
       Boost.Coroutine2 是单线程的，跨线程使用需要外部同步。
    3. 新版 Boost（1.55+）的 asymmetric_coroutine 在 coroutines2 命名空间中，
       旧的 coroutines 命名空间已弃用。
    4. 如果协程体 lambda 按引用捕获外部变量，确保协程存活期间这些变量有效。
*/

// int main() {
//     std::cout << "===== Boost.Coroutine2 非对称协程 =====\n\n";
//     demo_basic_generator::demo();
//     demo_fibonacci::demo();
//     demo_push_driven::demo();
//     demo_exception::demo();
//     demo_stack_allocator::demo();
//     return 0;
// }
