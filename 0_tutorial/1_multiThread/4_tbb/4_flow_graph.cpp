#include <tbb/flow_graph.h>

#include <iostream>
#include <string>
#include <thread>

/*
  TBB Flow Graph 是一个用于构建数据流/任务图的高级 API。
  它允许你以声明式方式描述节点和边，TBB 自动处理并行调度。

  核心概念：
    - 节点（Node）: 执行具体操作的单元
    - 边（Edge）: 节点之间的数据/控制依赖
    - 消息（Message）: 沿边传递的数据
*/

// ============================================================
namespace flow_graph_basic {
    /*
    1. Flow Graph 基本节点类型：
        - source_node: 数据源，生成消息
        - function_node: 接收消息，处理后发出新消息
        - continue_node: 接收控制信号（无数据）
        - join_node: 等待多个输入到达后一起发出
        - split_node: 将消息分发到多个输出
        - buffer_node: 缓冲消息
        - limiter_node: 限制并发消息数量

    2. 最简单的流水线：
        source → function_node1 → function_node2
    */

    void task() {
        using namespace tbb::flow;

        graph g;

        // function_node: 接收 int, 返回 int
        function_node<int, int> doubler(g, tbb::flow::unlimited,
            [](int x) -> int {
                int result = x * 2;
                std::cout << "  doubler: " << x << " → " << result << "\n";
                return result;
            });

        function_node<int, int> adder(g, tbb::flow::unlimited,
            [](int x) -> int {
                int result = x + 10;
                std::cout << "  adder:   " << x << " → " << result << "\n";
                return result;
            });

        function_node<int, int> printer(g, tbb::flow::unlimited,
            [](int x) -> int {
                std::cout << "  结果:    " << x << "\n";
                return 0;
            });

        // 连接边: doubler → adder → printer
        make_edge(doubler, adder);
        make_edge(adder, printer);

        // 发送消息
        for (int i = 1; i <= 5; ++i) {
            std::cout << "输入: " << i << "\n";
            doubler.try_put(i);
        }

        g.wait_for_all();
        std::cout << "流水线完成。\n";
    }
}  // namespace flow_graph_basic

// ============================================================
namespace flow_graph_parallel {
    /*
    1. function_node 的并发度控制：
        第二个参数指定最大并行度。
        - tbb::flow::serial: 一次只处理一个消息（保证顺序）
        - tbb::flow::unlimited: 无限制并行
        - 具体数字 N: 最多 N 个并发

    2. 多个输入源和 join：
        使用 join_node 等待多个输入都到达后才发出。
    */

    void task() {
        using namespace tbb::flow;

        graph g;

        // 两个数据源
        function_node<int, int> source_a(g, tbb::flow::serial,
            [](int) -> int {
                static int v = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::cout << "[源A] 产出: " << ++v << "\n";
                return v;
            });

        function_node<int, int> source_b(g, tbb::flow::serial,
            [](int) -> int {
                static int v = 10;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                std::cout << "[源B] 产出: " << ++v << "\n";
                return v;
            });

        // join_node: 等待 A 和 B 都到达
        using join_type = join_node<std::tuple<int, int>, queueing>;
        join_type joiner(g);

        // 合并处理器
        function_node<std::tuple<int, int>, int> merger(
            g, tbb::flow::unlimited,
            [](const std::tuple<int, int>& inputs) -> int {
                int a = std::get<0>(inputs);
                int b = std::get<1>(inputs);
                int sum = a + b;
                std::cout << "[合并] " << a << " + " << b << " = " << sum
                          << "\n";
                return sum;
            });

        // 连接
        make_edge(source_a, tbb::flow::input_port<0>(joiner));
        make_edge(source_b, tbb::flow::input_port<1>(joiner));
        make_edge(joiner, merger);

        // 触发
        source_a.try_put(0);
        source_a.try_put(0);
        source_b.try_put(0);
        source_b.try_put(0);

        g.wait_for_all();
        std::cout << "并行 join 完成。\n";
    }
}  // namespace flow_graph_parallel

// ============================================================
namespace flow_graph_limiter {
    /*
    1. limiter_node:
        限制在途（in-flight）消息的数量。
        适合需要背压（back pressure）的场景。

    2. 使用场景：
        - 限制对下游有限资源的并发访问
        - 防止内存无限增长
    */

    void task() {
        using namespace tbb::flow;

        graph g;

        // limiter_node: 最多允许 2 个消息同时在处理中
        limiter_node<int> limiter(g, 2);

        // 处理节点（故意慢一些）
        function_node<int, int> processor(g, 2,  // 最多 2 个并发
            [](int x) -> int {
                std::cout << "  处理 " << x << " 开始...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                std::cout << "  处理 " << x << " 完成。\n";
                return x;
            });

        function_node<int, int> decrementer(g, tbb::flow::serial,
            [&limiter](int x) -> int {
                limiter.decrement.try_put(1);  // 释放一个 limiter 槽位
                return x;
            });

        make_edge(limiter, processor);
        make_edge(processor, decrementer);
        make_edge(decrementer, limiter.decrement);

        // 发送 5 个消息，但同时只有 2 个在途
        std::cout << "发送 5 个消息（limiter=2）:\n";
        for (int i = 1; i <= 5; ++i) {
            limiter.try_put(i);
        }

        g.wait_for_all();
        std::cout << "limiter 演示完成。\n";
    }
}  // namespace flow_graph_limiter

// ============================================================
int main() {
    std::cout << "===== 1. Flow Graph 基本流水线 =====\n";
    flow_graph_basic::task();

    std::cout << "\n===== 2. 多源 join =====\n";
    flow_graph_parallel::task();

    std::cout << "\n===== 3. limiter_node（背压控制）=====\n";
    flow_graph_limiter::task();

    return 0;
}
