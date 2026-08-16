#include <barrier>
#include <chrono>
#include <iostream>
#include <latch>
#include <semaphore>
#include <thread>
#include <vector>

/*
  C++20 引入了三个重要的同步原语：std::latch、std::barrier、std::semaphore。
  这些是对传统 mutex + condition_variable 组合的重要补充。
*/

// ============================================================
namespace cpp20_latch {
    /*
    1. std::latch 是什么？
        latch（门闩）是一个一次性倒计数器。线程调用 arrive_and_wait() 或
        count_down() 来减少计数。当计数到达 0 时，所有等待的线程被释放。

    2. 和 CountDownLatch 的关系：
        std::latch 就是 C++ 标准版的 CountDownLatch，Java 中也有同名概念。

    3. 使用场景：
        - 启动门：等待所有工作线程初始化完成后一起开始
        - 结束门：等待所有工作线程完成后主线程继续
    */

    void worker(int id, std::latch& start_latch, std::latch& done_latch) {
        std::cout << "工作线程 " << id << " 已就绪，等待启动信号...\n";
        start_latch.arrive_and_wait();  // 等待启动门打开

        // 模拟工作
        std::this_thread::sleep_for(std::chrono::milliseconds(100 * id));
        std::cout << "工作线程 " << id << " 完成工作。\n";

        done_latch.count_down();  // 通知结束门
    }

    void task() {
        const int num_workers = 5;
        std::latch start_latch(1);       // 1 次 count_down 即打开启动门
        std::latch done_latch(num_workers);  // 等待所有工人完成

        std::vector<std::thread> threads;
        threads.reserve(num_workers);
        for (int i = 1; i <= num_workers; ++i) {
            threads.emplace_back(worker, i, std::ref(start_latch),
                                std::ref(done_latch));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "\n主线程: 所有线程就绪，开始！\n\n";
        start_latch.count_down();  // 打开启动门

        done_latch.wait();  // 等待所有工作完成
        std::cout << "\n主线程: 所有工作线程已完成。\n";

        for (auto& t : threads) t.join();
    }
}  // namespace cpp20_latch

// ============================================================
namespace cpp20_barrier {
    /*
    1. std::barrier 是什么？
        barrier（屏障）是一个可复用的同步点。所有线程到达 barrier 后被阻塞，
        直到指定数量的线程到达，然后所有线程被同时释放，进入下一阶段。

    2. 和 latch 的区别：
        - latch 是一次性的，barrier 可以重复使用
        - barrier 支持完成回调（completion function）

    3. 使用场景：
        - 分阶段并行计算（每个阶段结束同步一次）
        - 迭代式算法中的同步点
    */

    void worker(int id, std::barrier<>& barrier, int num_phases) {
        for (int phase = 1; phase <= num_phases; ++phase) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30 * id));
            std::cout << "线程 " << id << " 完成第 " << phase << " 阶段\n";

            // 等待所有线程完成当前阶段
            barrier.arrive_and_wait();
        }
    }

    void task() {
        const int num_threads = 4;
        const int num_phases = 3;

        // 回调函数：每次所有线程到达时执行
        auto on_completion = [phase = 0]() mutable {
            ++phase;
            std::cout << "--- 所有线程已完成第 " << phase
                      << " 阶段，进入下一阶段 ---\n";
        };

        std::barrier barrier(num_threads, on_completion);

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int i = 1; i <= num_threads; ++i) {
            threads.emplace_back(worker, i, std::ref(barrier), num_phases);
        }

        for (auto& t : threads) t.join();
    }
}  // namespace cpp20_barrier

// ============================================================
namespace cpp20_semaphore {
    /*
    1. std::counting_semaphore / std::binary_semaphore 是什么？
        - counting_semaphore<N>: 计数信号量，维护一个内部计数器。
          计数器 > 0 时 acquire() 成功（减 1），= 0 时阻塞等待。
          release() 增加计数器。
        - binary_semaphore: 就是 counting_semaphore<1>，只有 0 和 1 两种状态。

    2. 使用场景：
        - 限流：限制同时访问某个资源的线程数量
        - 生产者-消费者：比条件变量更简洁
        - 互斥锁：binary_semaphore 可以替代 mutex（但不推荐，缺少所有权概念）
    */

    // 用 counting_semaphore 实现一个简单的限流示例
    std::counting_semaphore<3> limiter(3);  // 最多同时 3 个线程访问

    void limited_worker(int id) {
        // 先获取信号量许可
        limiter.acquire();
        std::cout << "线程 " << id << " 获得许可，开始工作...\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "线程 " << id << " 完成工作，释放许可。\n";

        limiter.release();  // 归还许可
    }

    void task() {
        std::cout << "最多允许 3 个线程同时工作：\n";

        std::vector<std::thread> threads;
        threads.reserve(10);
        for (int i = 1; i <= 10; ++i) {
            threads.emplace_back(limited_worker, i);
        }

        for (auto& t : threads) t.join();
        std::cout << "所有线程完成。\n";
    }
}  // namespace cpp20_semaphore

// ============================================================
int main() {
    std::cout << "===== 1. std::latch（一次性门闩）=====\n";
    cpp20_latch::task();

    std::cout << "\n===== 2. std::barrier（可复用的阶段屏障）=====\n";
    cpp20_barrier::task();

    std::cout << "\n===== 3. std::counting_semaphore（限流信号量）=====\n";
    cpp20_semaphore::task();

    return 0;
}
