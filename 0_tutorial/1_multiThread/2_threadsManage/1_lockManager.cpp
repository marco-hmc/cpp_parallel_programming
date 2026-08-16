#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// ============================================================
namespace lock_guard_example {
    /*
    1. `std::lock_guard`的用途：
    `std::lock_guard`是一个简单的RAII包装器，用于管理互斥锁的生命周期。

    2. `std::lock_guard`能接受的模板参数：
    `std::lock_guard`是一个模板类，它的模板参数是一个互斥锁类型。
    这个类型需要满足BasicLockable的要求，也就是说，它需要提供`lock()`和`unlock()`两个成员函数。
    例如，`std::mutex`、`std::recursive_mutex`、`std::timed_mutex`和`std::recursive_timed_mutex`都可以作为`std::lock_guard`的模板参数。

    3. `std::lock_guard`的std::lock_policy有啥用？有哪些std::lock_policy？
        - `std::adopt_lock`:
            表示互斥量已经被锁定,不需要再尝试锁定.
        - `std::defer_lock`:
            表示不立即锁定互斥量,
            稍后可以手动调用 `lock()` 方法来锁定.
        - `std::try_to_lock`:
            尝试锁定互斥量,如果互斥量已经被锁定,则立即返回,不会阻塞.
    */

    std::mutex mtx;

    static void critical_section(int id) {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "线程 " << id << " 通过 lock_guard 进入临界区。"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "线程 " << id << " 通过 lock_guard 离开临界区。"
                  << std::endl;
    }

    void task() {
        std::thread t1(critical_section, 1);
        std::thread t2(critical_section, 2);
        t1.join();
        t2.join();
    }
}  // namespace lock_guard_example

// ============================================================
namespace unique_lock_example {
    /*
    1. `std::unique_lock`和`std::lock_guard`的区别：
        * `std::lock_guard`在构造时自动锁定互斥锁，在析构时自动解锁互斥锁，但是在其生命周期内不能改变锁的状态。
        * `std::unique_lock`在构造时可以选择是否锁定互斥锁，可以在其生命周期内多次锁定和解锁互斥锁，还可以转移所有权。

    2. `std::unique_lock`相较于`std::lock_guard`的额外功能：
        - 延迟锁定：
            `std::unique_lock`可以在构造时不锁定互斥锁，然后在需要的时候再锁定互斥锁。
        - 手动锁定和解锁：
            `std::unique_lock`可以在其生命周期内多次锁定和解锁互斥锁。
        - 锁所有权的转移：
            `std::unique_lock`可以将锁的所有权转移给另一个`std::unique_lock`对象。
        - 条件变量：
            `std::unique_lock`可以和`std::condition_variable`一起使用，用于等待条件或者通知条件。
    */

    std::mutex mtx;

    static void critical_section(int id) {
        std::unique_lock<std::mutex> lock(mtx, std::defer_lock);
        std::cout << "线程 " << id << " 创建了 unique_lock 但未锁定。"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        lock.lock();
        std::cout << "线程 " << id << " 通过 unique_lock 进入临界区。"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        lock.unlock();
        std::cout << "线程 " << id << " 手动解锁 unique_lock 离开临界区。"
                  << std::endl;
        lock.lock();  // unique_lock 离开作用域时会自动释放锁
    }

    void task() {
        std::thread t1(critical_section, 1);
        std::thread t2(critical_section, 2);

        t1.join();
        t2.join();
    }
}  // namespace unique_lock_example

// ============================================================
namespace scoped_lock_example {
    /*
        1. scoped_lock是什么？
            `std::scoped_lock` 是一个RAII类型的锁管理器，用于同时管理多个互斥锁。
            它可以避免死锁问题，因为它会按照一定的顺序锁定所有互斥锁。

        2. std::scoped_lock怎么用？
            * 构造函数
                `std::scoped_lock` 的构造函数可以接受多个互斥锁作为参数，并在构造时自动锁定这些互斥锁。
            * 析构函数
                在 `std::scoped_lock` 的生命周期结束时，它会自动解锁所有互斥锁。

        3. 使用场景
            适用于需要同时锁定多个互斥锁的场景，例如避免死锁的多资源访问。
    */
    std::mutex mtx1, mtx2;

    static void do_task(int id) {
        std::scoped_lock lock(mtx1, mtx2);
        std::cout << "线程 " << id << " 通过 scoped_lock 同时获取了两把锁。"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "线程 " << id << " 通过 scoped_lock 释放两把锁。"
                  << std::endl;
    }

    void task() {
        std::thread t1(do_task, 1);
        std::thread t2(do_task, 2);

        t1.join();
        t2.join();
    }
}  // namespace scoped_lock_example

// ============================================================
int main() {
    std::cout << "===== 1. lock_guard 基本用法 =====\n";
    lock_guard_example::task();

    std::cout << "\n===== 2. unique_lock 基本用法 =====\n";
    unique_lock_example::task();

    std::cout << "\n===== 3. scoped_lock（多锁管理）=====\n";
    scoped_lock_example::task();
    return 0;
}