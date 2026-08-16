#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_scheduler_observer.h>
#include <tbb/blocked_range.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// ============================================================
namespace tbb_task_arena_basic {
    /*
    1. tbb::task_arena 是什么？
        task_arena 代表一个独立的线程工作域。
        可以显式控制任务在哪个线程池中执行。

    2. 基本用法：
        tbb::task_arena arena(4);  // 最多 4 个线程
        arena.execute([] {
            tbb::parallel_for(...);  // 在 arena 的线程池中执行
        });

    3. 使用场景：
        - 隔离不同优先级的工作负载
        - 为特定子系统保留专用线程
        - 嵌套并行场景下控制线程数
    */

    void heavy_work(int id) {
        double sum = 0;
        for (int i = 0; i < 1'000'000; ++i) {
            sum += std::sin(static_cast<double>(i)) * 0.001;
        }
        std::cout << "工作 " << id << " 完成, sum = " << sum << "\n";
    }

    void task() {
        // 默认 arena: 使用所有可用线程
        std::cout << "=== 默认 arena（所有线程）===\n";
        tbb::parallel_for(0, 4, [](int i) { heavy_work(i); });

        // 限制为 2 个线程的 arena
        std::cout << "\n=== 限制 arena（2 线程）===\n";
        tbb::task_arena limited(2);
        limited.execute([] {
            tbb::parallel_for(0, 4, [](int i) { heavy_work(i); });
        });
    }
}  // namespace tbb_task_arena_basic

// ============================================================
namespace tbb_task_arena_isolation {
    /*
    1. task_arena 隔离：
        不同 arena 之间的工作负载不会互相干扰。
        可以在一个 arena 中执行长时间任务，另一个 arena 中执行低延迟任务。
    */

    void task() {
        tbb::task_arena arena_a(2);  // 低延迟任务区，2 线程
        tbb::task_arena arena_b(1);  // 后台任务区，1 线程

        // 在 arena_a 中执行快速任务
        auto future_a = std::async(std::launch::async, [&arena_a] {
            arena_a.execute([] {
                std::cout << "[arena_a] 低延迟任务开始\n";
                tbb::parallel_for(
                    0, 5, [](int i) {
                        std::cout << "[arena_a] 快速任务 " << i << "\n";
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(50));
                    });
                std::cout << "[arena_a] 低延迟任务完成\n";
            });
        });

        // 在 arena_b 中执行耗时任务
        auto future_b = std::async(std::launch::async, [&arena_b] {
            arena_b.execute([] {
                std::cout << "[arena_b] 后台任务开始\n";
                tbb::parallel_for(
                    0, 3, [](int i) {
                        std::cout << "[arena_b] 后台任务 " << i << "\n";
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(200));
                    });
                std::cout << "[arena_b] 后台任务完成\n";
            });
        });

        future_a.wait();
        future_b.wait();
    }
}  // namespace tbb_task_arena_isolation

// ============================================================
namespace tbb_observer {
    /*
    1. task_scheduler_observer 是什么？
        用于监控 TBB 线程的进入和退出。
        可以观察线程何时进入/离开 TBB 的线程池。

    2. 用法：
        继承 task_scheduler_observer，实现 on_scheduler_entry() 和
        on_scheduler_exit() 回调。调用 observe(true) 开始观察。

    3. 使用场景：
        - 为 TBB 工作线程设置 thread-local 状态
        - 性能监控和 profiling
        - 调试和日志记录
    */

    class MyObserver : public tbb::task_scheduler_observer {
      public:
        void on_scheduler_entry(bool is_worker) override {
            // 工作线程进入 TBB 调度器时调用
            if (is_worker) {
                static std::atomic<int> count{0};
                int id = ++count;
                // 可以在这里设置 thread_local 变量、绑定 NUMA 节点等
            }
        }

        void on_scheduler_exit(bool is_worker) override {
            // 工作线程退出 TBB 调度器时调用
            if (is_worker) {
                // 清理 thread_local 资源
            }
        }
    };

    void task() {
        MyObserver observer;
        observer.observe(true);

        std::cout << "启动 observer，执行一些 TBB 任务...\n";
        tbb::parallel_for(0, 10, [](int i) {
            // 工作线程执行时 observer 会接到通知
            volatile double x = std::sin(static_cast<double>(i));
            (void)x;
        });

        observer.observe(false);
        std::cout << "Observer 已停止。\n";
        std::cout << "task_scheduler_observer 用于监控和定制 TBB 线程行为。\n";
    }
}  // namespace tbb_observer

// ============================================================
int main() {
    std::cout << "===== 1. task_arena 基本用法 =====\n";
    tbb_task_arena_basic::task();

    std::cout << "\n===== 2. task_arena 隔离 =====\n";
    tbb_task_arena_isolation::task();

    std::cout << "\n===== 3. task_scheduler_observer =====\n";
    tbb_observer::task();

    return 0;
}
