#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

// ============================================================
namespace deadlock_classic {
    /*
    1. 死锁（deadlock）是什么？
        两个或多个线程互相等待对方持有的锁，导致所有线程永久阻塞。

    2. 典型场景：
        线程 A: lock(mtx1) → lock(mtx2)
        线程 B: lock(mtx2) → lock(mtx1)
        两个线程以相反顺序获取锁，形成循环等待。

    3. 如何避免？
        - 总是以相同顺序获取锁
        - 使用 std::lock() 或 std::scoped_lock 同时锁定多个锁
        - 使用 try_lock 并回退
        - 使用锁层级（hierarchical locking）
    */

    std::mutex mtx1, mtx2;
    int shared_data = 0;

    // 线程 A: 先锁 mtx1, 再锁 mtx2
    void thread_a_bad() {
        std::lock_guard<std::mutex> lock1(mtx1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 增加死锁概率
        std::lock_guard<std::mutex> lock2(mtx2);
        shared_data++;
        std::cout << "[A] 成功完成操作\n";
    }

    // 线程 B: 先锁 mtx2, 再锁 mtx1 — 与 A 相反，导致死锁！
    void thread_b_bad() {
        std::lock_guard<std::mutex> lock2(mtx2);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 增加死锁概率
        std::lock_guard<std::mutex> lock1(mtx1);
        shared_data++;
        std::cout << "[B] 成功完成操作\n";
    }

    void task() {
        std::cout << "⚠ 此演示会发生死锁，代码已注释。请阅读源码理解死锁原理。\n";
        std::cout << "  取消注释下面的代码来观察死锁效果（程序会 hang）:\n\n";
        std::cout << "  // std::thread t1(thread_a_bad);\n";
        std::cout << "  // std::thread t2(thread_b_bad);\n";
        std::cout << "  // t1.join(); t2.join();  // 永远不会执行到这里\n";

        // 实际执行的话会死锁，所以注释掉了
        // std::thread t1(thread_a_bad);
        // std::thread t2(thread_b_bad);
        // t1.join(); t2.join();
    }
}  // namespace deadlock_classic

// ============================================================
namespace deadlock_fix_scoped_lock {
    /*
    1. 使用 std::scoped_lock 避免死锁：
        std::scoped_lock (C++17) 使用死锁避免算法（类似 std::lock()），
        保证同时锁定多个 mutex 时不会发生死锁。

    2. 原理：
        内部使用 try_lock + 回退的策略，或者按统一顺序获取锁。
    */

    std::mutex mtx1, mtx2;
    int shared_data = 0;

    void safe_worker(int id) {
        // scoped_lock 同时锁定两个 mutex，避免死锁
        std::scoped_lock lock(mtx1, mtx2);
        shared_data++;
        std::cout << "[线程 " << id << "] 安全完成操作, shared_data = "
                  << shared_data << "\n";
    }

    void task() {
        std::cout << "使用 std::scoped_lock 避免死锁:\n";
        std::thread t1(safe_worker, 1);
        std::thread t2(safe_worker, 2);
        t1.join();
        t2.join();
    }
}  // namespace deadlock_fix_scoped_lock

// ============================================================
namespace deadlock_fix_std_lock {
    /*
    1. 使用 std::lock() 避免死锁：
        std::lock() 是一个函数（不是类），可以同时锁定多个 mutex。
        配合 std::lock_guard + std::adopt_lock 使用。

    2. 用法：
        std::lock(mtx1, mtx2);  // 同时锁定
        std::lock_guard<std::mutex> lock1(mtx1, std::adopt_lock);  // 接管所有权
        std::lock_guard<std::mutex> lock2(mtx2, std::adopt_lock);
    */

    std::mutex mtx1, mtx2;
    int shared_data = 0;

    void safe_worker(int id) {
        std::lock(mtx1, mtx2);  // 同时锁定两个 mutex
        std::lock_guard<std::mutex> lock1(mtx1, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(mtx2, std::adopt_lock);

        shared_data++;
        std::cout << "[线程 " << id << "] 通过 std::lock 安全完成操作\n";
    }

    void task() {
        std::cout << "使用 std::lock() + std::adopt_lock 避免死锁:\n";
        std::thread t1(safe_worker, 1);
        std::thread t2(safe_worker, 2);
        t1.join();
        t2.join();
    }
}  // namespace deadlock_fix_std_lock

// ============================================================
namespace deadlock_hierarchical {
    /*
    1. 层级锁（Hierarchical Locking）：
        给每个 mutex 分配一个层级值，线程只能按照层级升序（或降序）获取锁。
        如果违反层级规则，说明可能有死锁风险。

    2. 这是一个简化的演示，展示了设计模式而不是完整实现。
    */

    class HierarchicalMutex {
      public:
        explicit HierarchicalMutex(int level) : level_(level) {}

        void lock() {
            static thread_local int current_level = -1;
            if (current_level >= level_) {
                std::cerr << "错误: 违反锁层级! 当前层级 " << current_level
                          << " 试图获取层级 " << level_ << " 的锁\n";
                std::terminate();
            }
            int prev_level = current_level;
            current_level = level_;
            mtx_.lock();
            current_level = prev_level;
        }

        void unlock() { mtx_.unlock(); }

      private:
        std::mutex mtx_;
        int level_;
    };

    HierarchicalMutex high_level(1000);   // 高优先级锁（后获取）
    HierarchicalMutex medium_level(500);  // 中优先级锁
    HierarchicalMutex low_level(10);      // 低优先级锁（先获取）

    void good_worker() {
        std::lock_guard<HierarchicalMutex> l1(low_level);     // 先低级
        std::lock_guard<HierarchicalMutex> l2(medium_level);  // 再中级
        std::lock_guard<HierarchicalMutex> l3(high_level);    // 再高级
        std::cout << "层级锁: 正确的升序获取成功\n";
    }

    void task() {
        std::cout << "层级锁（Hierarchical Locking）模式:\n";
        std::cout << "层级: low_level(10) < medium_level(500) < high_level(1000)\n";
        good_worker();

        std::cout << "\n如果以错误顺序获取（先高后低），会触发 terminate。\n";
        std::cout << "注释中保留了错误示例供学习参考。\n";
    }
}  // namespace deadlock_hierarchical

// ============================================================
int main() {
    std::cout << "===== 1. 经典死锁场景 =====\n";
    deadlock_classic::task();

    std::cout << "\n===== 2. 用 std::scoped_lock 避免死锁 =====\n";
    deadlock_fix_scoped_lock::task();

    std::cout << "\n===== 3. 用 std::lock() + adopt_lock 避免死锁 =====\n";
    deadlock_fix_std_lock::task();

    std::cout << "\n===== 4. 层级锁 (Hierarchical Locking) =====\n";
    deadlock_hierarchical::task();

    return 0;
}
