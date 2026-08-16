// ...existing code...
#include <boost/coroutine2/all.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

/*
====================================================================================================
                                Boost 对称协程 (symmetric_coroutine) 教学文档
====================================================================================================

1. 什么是Boost对称协程？
   - Boost.Coroutine2 的对称协程支持
   - 协程之间可以相互调用，类似于协作式多任务处理
   - 由调用者控制协程的执行和调度

2. 核心概念：
   - Coroutine: 协程对象，可以暂停和恢复执行
   - Yield: 暂停执行并可选地传递数据
   - Call: 从协程接收数据并恢复执行

3. 主要优势：
   - 更灵活的协程调度
   - 适合实现协作式多任务
   - 简化某些类型的状态机实现

4. 应用场景：
   - 协作式任务调度
   - 状态机实现
   - 流水线处理

====================================================================================================
*/

using namespace std::chrono_literals;

// ================================================================================================
// 1. 对称协程示例 - 简单生成器
// ================================================================================================

namespace SymmetricCoroutines {

    // 如果系统的 Boost 不支持 symmetric_coroutine，使用
    // asymmetric_coroutine 来实现相同的生成/消费语义。
    using coro_t = boost::coroutines2::asymmetric_coroutine<int>;
    using pull_t = coro_t::pull_type;
    using push_t = coro_t::push_type;

    // 简单生成器：协程向调用者 yield 整数值
    void simple_generator(push_t& yield_) {
        for (int i = 1; i <= 5; ++i) {
            std::cout << "[simple_generator] yield " << i << std::endl;
            yield_(i);
            // 可以在这里做更多工作，恢复时继续
        }
    }

    // 两个协程分别做各自工作，main 交替调用实现 ping-pong 效果
    void ping_worker(push_t& yield_) {
        for (int i = 1; i <= 3; ++i) {
            std::cout << "[ping_worker] step " << i << std::endl;
            yield_(i);
        }
    }

    void pong_worker(push_t& yield_) {
        for (int i = 1; i <= 3; ++i) {
            std::cout << "[pong_worker] step " << i << std::endl;
            yield_(i + 100);
        }
    }

    // 演示：simple_generator 的使用
    void demonstrateSimpleGenerator() {
        std::cout << "\n=== Symmetric: Simple Generator ===" << std::endl;
        pull_t gen(simple_generator);
        for (auto v : gen) {
            std::cout << "[main] received: " << v << std::endl;
        }
    }

    // 演示：两个协程由 main 交替驱动（ping-pong）
    void demonstratePingPong() {
        std::cout << "\n=== Symmetric: Ping-Pong (main orchestration) ==="
                  << std::endl;
        pull_t ping(ping_worker);
        pull_t pong(pong_worker);

        // 交替调用直到两者都完成
        while (ping || pong) {
            if (ping) {
                int v = ping.get();
                std::cout << "[main] got from ping: " << v << std::endl;
                ping();
            }
            if (pong) {
                int v = pong.get();
                std::cout << "[main] got from pong: " << v << std::endl;
                pong();
            }
        }
    }

    // 演示：生产者->main->消费者 的链式处理（main 起到中介）
    void demonstrateChainViaMain() {
        std::cout << "\n=== Symmetric: Chain via main ===" << std::endl;
        pull_t producer([](push_t& yield_) {
            for (int i = 1; i <= 6; ++i) {
                std::cout << "[producer] produce " << i << std::endl;
                yield_(i);
                std::this_thread::sleep_for(50ms);
            }
        });

        std::vector<int> collected;
        while (producer) {
            int v = producer.get();
            // main 作为消费者或中间处理者
            std::cout << "[consumer/main] consume " << v << std::endl;
            collected.push_back(v);
            producer();
        }

        std::cout << "[consumer/main] collected " << collected.size()
                  << " items" << std::endl;
    }

    void demonstrateAll() {
        demonstrateSimpleGenerator();
        demonstratePingPong();
        demonstrateChainViaMain();
    }

}  // namespace SymmetricCoroutines

// ================================================================================================
// 主函数 - 运行所有演示
// ================================================================================================

// show_boost.cpp
#include <boost/version.hpp>
#include <iostream>

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Boost 对称协程 (symmetric_coroutine) 示例" << std::endl;
    std::cout << "========================================" << std::endl;

    SymmetricCoroutines::demonstrateAll();

    return 0;
}
// ...existing code...