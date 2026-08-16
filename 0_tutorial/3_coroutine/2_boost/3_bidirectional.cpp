/*
    Boost.Coroutine2 双向数据传递参考
    =================================
    本文件仅供阅读学习，不参与构建（本机未安装 Boost）。

    核心概念：push_type + pull_type&
    前面学了两种单向模式：
    - pull_type：协程 → caller（pull 模式）
    - push_type：caller → 协程（push 模式）
    但如果协程既要收数据也要发数据呢？把 push_type 和 pull_type 组合起来。

    模式：caller 持有 push_type，协程体接收 pull_type&。
    caller 通过 push(x) 向协程推数据，协程通过 pull.get() 接收。
    如果协程还需要向 caller 返回结果，可以在 pull_type& 上做二次包装。

    手动编译（需安装 Boost）：
      g++ -std=c++17 3_bidirectional.cpp -lboost_coroutine -pthread
*/

// #include <boost/coroutine2/all.hpp>
// #include <iostream>
// #include <iomanip>
// #include <string>
// #include <vector>
//
// using namespace boost::coroutines2;

namespace demo_bidirectional {
/*
    1. caller 推数据进协程，协程处理后返回给 caller

    这个例子：caller 有一串名字，推给协程做格式化（补空格到固定宽度），
    协程每收到一个就处理一个，格式化后返回给 caller 打印。
    本质上是"协程当过滤器"的模式。
*/

// coroutine<int>::push_type make_processor(
//     coroutine<int>::pull_type& input,
//     std::vector<int>& output) {
//
//     return coroutine<int>::push_type([&](auto& sink) {
//         // 协程体：从 pull (caller 推来的数据) 读取，处理后用 sink 返回
//         while (input) {
//             int value = input.get();  // 从 caller 接收
//             int processed = value * 2;
//             output.push_back(processed);
//             sink(processed);  // 返回给 caller
//         }
//     });
// }

void demo() {
    // 这里的 API 在实际 Boost.Coroutine2 中比较复杂，上面的伪代码展示了思想。
    // 更实用的方式是：caller 持有 push，协程持有 pull，
    // 处理结果不通过 sink 返回，而是存入共享容器（caller 在协程结束后读取）。
    //
    // coroutine<int>::pull_type processor([](auto& sink) {
    //     // 协程：这里收到的不是数据，而是 caller 的"喂入"
    //     // 实际上 coroutine<T> 的推/拉是单向的，不支持双向
    //     // 如果需要双向，需要两个协程配合。
    // });
}
}  // namespace demo_bidirectional

namespace demo_pipeline_bidirectional {
/*
    2. 用两个协程实现双向管道

    协程 A（生产者）→ 产出数据 → 主力（main/调度器）→ 喂给 →
    协程 B（消费者）→ 处理 → 主力 → 协程 A 接收反馈

    主力（main）充当"交换机"角色，连接两个单向协程。
*/

void demo() {
    // // 管道的数据存放处
    // std::vector<int> pipeline_data;
    //
    // // 生产者：产出数据到 pipeline
    // coroutine<int>::pull_type producer([](auto& sink) {
    //     for (int i = 1; i <= 5; ++i) {
    //         std::cout << "  [生产] " << i << "\n";
    //         sink(i);
    //     }
    // });
    //
    // // 消费者：从 pipeline 读取并处理
    // // （这里用 pull_type 直接从 producer 拉取，main 只负责连接）
    //
    // // main 充当调度器：从 producer 拉，推给 consumer
    // coroutine<int>::push_type consumer([](auto& pull) {
    //     while (pull) {
    //         int value = pull.get();
    //         std::cout << "  [消费] " << value << " → 翻倍：" << value * 2 << "\n";
    //     }
    // });
    //
    // // 连接：main 从 producer 拉，推给 consumer
    // for (int v : producer) {
    //     consumer(v);
    // }
    //
    // std::cout << "管道处理完成\n";
}
}  // namespace demo_pipeline_bidirectional

namespace demo_asio_pattern {
/*
    3. Boost.Asio 的协程模式（C++ 异步编程的最终形态）

    Boost.Asio 提供了与协程的集成：
    - 用 co_await（C++20）或 yield_context（Boost）等待异步操作
    - I/O 操作不阻塞线程，协程自动挂起/恢复
    - 这是目前最成熟的 C++ 异步编程范式之一

    对应关系：
    - Boost.Asio + boost::asio::awaitable<T> + co_await  ≈ C++20 协程 + asio
    - Boost.Asio + boost::asio::yield_context          ≈ C++14 协程 + asio

    注：需要 C++20 协程支持的编译器 + Boost 1.78+。
    Asio 现在也支持通过 asio::use_awaitable 与 C++20 协程配合。
*/

void demo() {
    // // 伪代码：用 C++20 协程 + Asio 做异步 TCP echo server
    // asio::awaitable<void> echo_session(tcp::socket socket) {
    //     try {
    //         char data[1024];
    //         for (;;) {
    //             // co_await asio 异步读 —— 不阻塞线程
    //             size_t n = co_await socket.async_read_some(
    //                 asio::buffer(data), asio::use_awaitable);
    //             // co_await asio 异步写
    //             co_await async_write(socket, asio::buffer(data, n),
    //                                  asio::use_awaitable);
    //         }
    //     } catch (std::exception& e) {
    //         std::cerr << "echo session error: " << e.what() << "\n";
    //     }
    // }
    //
    // // 主协程：监听 + 接受连接
    // asio::awaitable<void> listener(tcp::acceptor& acceptor) {
    //     for (;;) {
    //         auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    //         // 为每个连接创建一个并发执行的协程
    //         asio::co_spawn(acceptor.get_executor(),
    //                        echo_session(std::move(socket)), asio::detached);
    //     }
    // }
}
}  // namespace demo_asio_pattern

/*
    总结：
    1. 双向数据传递本质上是通过两个单向协程 + 外部调度实现的。
       Boost.Coroutine2 的 pull_type/push_type 是单向的，但不妨碍构建双向交互。
    2. 实际工程中，Boost.Asio + C++20 协程是生产级别的异步 C++ 方案。
       Asio 提供了和协程的深度集成，有网 I/O、计时器、信号处理等全套异步原语。
    3. 如果不需要网络 I/O，只用 Boost.Coroutine2 做计算协程也足够好。
       它与 C++20 协程是互补关系，不是替代。
*/

// int main() {
//     std::cout << "===== Boost.Coroutine2 双向传递 / 管道 / Asio =====\n\n";
//     demo_bidirectional::demo();
//     demo_pipeline_bidirectional::demo();
//     demo_asio_pattern::demo();
//     return 0;
// }
