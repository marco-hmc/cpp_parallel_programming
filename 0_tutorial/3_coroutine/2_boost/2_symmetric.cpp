/*
    Boost.Coroutine2 对称协程参考
    =================================
    本文件仅供阅读学习，不参与构建（本机未安装 Boost）。

    对称协程 vs 非对称协程：
    - 非对称：pull/push 是调用-被调用关系，数据单向流动
    - 对称：两个协程地位平等，可以互相 yield 到对方

    symmetric_coroutine<T>::call_type：
    - yield_type：对方协程的类型
    - call_type：当前协程的类型
    - yield(x)：产出值 x 并切换到对方协程
    - call.get()：从对方获取值

    类比：两个对称协程就像两个人在打乒乓球——你来我往，地位平等。
    而非对称协程更像是销售员和顾客——一个产出，一个消费。

    手动编译（需安装 Boost）：
      g++ -std=c++17 2_symmetric.cpp -lboost_coroutine -pthread
*/

// #include <boost/coroutine2/all.hpp>
// #include <iostream>
// #include <string>
//
// using namespace boost::coroutines2;

namespace demo_symmetric_basic {
/*
    1. 最简单的对称协程 —— A 和 B 互相让对方执行
*/

void demo() {
    // typedef symmetric_coroutine<int>::call_type coro_t;
    //
    // coro_t::yield_type* other = nullptr;
    //
    // // 协程 A
    // coro_t coro_a([&](coro_t::yield_type& yield) {
    //     for (int i = 1; i <= 3; ++i) {
    //         std::cout << "  [A]  "<< i << "\n";
    //         yield(i);  // 产出 + 切到 B
    //     }
    // });
    //
    // // 协程 B
    // coro_t coro_b([&](coro_t::yield_type& yield) {
    //     for (int i = 10; i <= 12; ++i) {
    //         std::cout << "  [B]  "<< i << "\n";
    //         yield(i);  // 产出 + 切到 A
    //     }
    // });
    //
    // std::cout << "A 和 B 交替执行：\n";
    // // 启动 A，A 会自动切到 B，B 再切回 A...
    // // 注意：需要手动管理协程的销毁顺序
}
}  // namespace demo_symmetric_basic

namespace demo_ping_pong {
/*
    2. 乒乓球模式
        两个协程通过 yield 互相传数据。
*/

void demo() {
    // typedef symmetric_coroutine<std::string>::call_type coro_t;
    //
    // coro_t player_a([&](coro_t::yield_type& yield) {
    //     std::cout << "  A 发球：\"ping\"\n";
    //     yield("ping");        // 发给 B，切到 B
    //     auto response = yield.get();  // 从 B 接收
    //     std::cout << "  A 收到：\"" << response << "\"\n";
    //     yield("pong");        // 再发给 B
    // });
    //
    // coro_t player_b([&](coro_t::yield_type& yield) {
    //     auto ball = yield.get();  // 从 A 接收
    //     std::cout << "  B 收到：\"" << ball << "\"\n";
    //     std::cout << "  B 回球：\"pong\"\n";
    //     yield("pong");        // 回给 A，切到 A
    //     ball = yield.get();   // 再次从 A 接收
    //     std::cout << "  B 再次收到：\"" << ball << "\"\n";
    // });
}
}  // namespace demo_ping_pong

namespace demo_symmetric_vs_asymmetric {
/*
    3. 什么时候用对称协程？
        一般情况下非对称协程（pull/push）足够用了。
        对称协程适合以下场景：
        - 多个协程需要以非层次化的方式互相切换（如状态机网络）
        - 协程之间地位平等，需要任意两个之间直接通信
        - 实现自定义调度器，协程自主选择下一个执行谁

        注意：C++20 标准库没有提供对称协程。要实现对称切换，
        通常用 await_suspend 返回另一个 coroutine_handle<>。
*/

void demo() {
    // 略 —— 实际场景较少用到对称协程
    std::cout << "对称协程在 C++20 中通过 await_suspend 返回 handle 模拟\n";
}
}  // namespace demo_symmetric_vs_asymmetric

/*
    注意事项：
    1. Boost 的 symmetric_coroutine 从 1.56 开始标记为 deprecated，
       1.60+ 中已移除。新项目请使用 asymmetric_coroutine（coroutine<T>）。
       上面的代码示例基于 Boost 1.55 的 API，仅作概念参考。
    2. C++20 中用 await_suspend 返回 coroutine_handle<> 可以实现等价的对称转移。
    3. 对于 Boost.Coroutine2 用户，建议全部使用 asymmetric_coroutine，
       它已涵盖大部分实际用法。
*/

// int main() {
//     std::cout << "===== Boost.Coroutine2 对称协程（概念参考）=====\n\n";
//     demo_symmetric_basic::demo();
//     demo_ping_pong::demo();
//     return 0;
// }
