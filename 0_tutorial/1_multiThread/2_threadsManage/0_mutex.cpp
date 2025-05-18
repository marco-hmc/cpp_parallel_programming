#include <chrono>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace Mutex {
    /*
        1. mutex是什么？
            互斥锁（mutex）是一种用于保护共享资源的同步机制。当一个线程需要访问共享资源时，它需要先获取互斥锁，然后才能访问这个资源。
            当线程访问完共享资源后，它需要释放互斥锁，以便其他线程可以访问这个资源。
            互斥锁可以防止多个线程同时访问共享资源，从而避免数据竞争和其他并发问题。
        
        2. std::mutex怎么用？
            * 构造函数
                `std::mutex`是一个类，它的构造函数没有形参，用于创建一个互斥锁对象。
            * 主要成员函数
                - `lock()`: 用于获取互斥锁。如果当前没有其他线程持有这个锁，那么这个函数会立即获取锁并返回。
                    如果锁已经被其他线程持有，那么这个函数会阻塞当前线程，直到锁变得可用。
                - `unlock()`: 用于释放互斥锁。如果当前线程持有这个锁，那么这个函数会释放锁。
                    如果当前线程没有持有这个锁，那么这个函数的行为是未定义的。
            * 其他成员函数
                - `try_lock()`: 尝试获取互斥锁。如果互斥锁当前没有被其他线程持有，那么这个函数会立即获取锁并返回`true`。
                    如果互斥锁已经被其他线程持有，那么这个函数不会阻塞，而是立即返回`false`。
    
    */
    std::mutex mtx;

    void critical_section(int id) {
        mtx.lock();
        std::cout << "Thread " << id << " entered critical section."
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Thread " << id << " leaving critical section."
                  << std::endl;
        mtx.unlock();
    }

    void run() {
        std::thread t1(critical_section, 1);
        std::thread t2(critical_section, 2);

        t1.join();
        t2.join();
    }
}  // namespace Mutex

namespace TryLockExample {
    std::mutex mtx;

    void critical_section(int id) {
        if (mtx.try_lock()) {
            std::cout << "Thread " << id
                      << " entered critical section using try_lock."
                      << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "Thread " << id
                      << " leaving critical section using try_lock."
                      << std::endl;
            mtx.unlock();
        } else {
            std::cout << "Thread " << id
                      << " could not enter critical section using try_lock."
                      << std::endl;
        }
    }

    void run() {
        std::thread t1(critical_section, 1);
        std::thread t2(critical_section, 2);

        t1.join();
        t2.join();
    }
}  // namespace TryLockExample

namespace RecursiveMutex {
    /*
        1. recursive_mutex是什么？
            `std::recursive_mutex` 是一种特殊的互斥锁，允许同一个线程多次获取同一个锁，而不会导致死锁。
            这种锁适用于递归函数或需要多次锁定的场景。

        2. std::recursive_mutex怎么用？
            * 构造函数
                `std::recursive_mutex` 的构造函数没有形参，用于创建一个递归互斥锁对象。
            * 主要成员函数
                - `lock()`: 获取锁。如果当前线程已经持有锁，则允许再次获取。
                - `unlock()`: 释放锁。如果当前线程多次获取了锁，则需要调用相同次数的 `unlock()` 才能完全释放锁。
                - `try_lock()`: 尝试获取锁。如果锁可用，则获取锁并返回 `true`，否则返回 `false`。

        3. 使用场景
            适用于递归调用或需要多次锁定的场景，例如递归函数中需要保护共享资源。
    */

    std::recursive_mutex rmtx;

    void recursive_function(int depth) {
        if (depth <= 0) {
            return;
        }

        rmtx.lock();
        std::cout << "Thread " << std::this_thread::get_id()
                  << " acquired lock at depth " << depth << std::endl;

        // 递归调用
        recursive_function(depth - 1);

        std::cout << "Thread " << std::this_thread::get_id()
                  << " releasing lock at depth " << depth << std::endl;
        rmtx.unlock();
    }

    void task() {
        std::thread t1(recursive_function, 3);
        std::thread t2(recursive_function, 3);

        t1.join();
        t2.join();
    }
}  // namespace RecursiveMutex

namespace SharedMutex {
    /*
        1. shared_mutex是什么？
            `std::shared_mutex` 是一种共享互斥锁，允许多个线程同时读取共享资源，但在写入时需要独占锁。
            它适用于读多写少的场景。

        2. std::shared_mutex怎么用？
            * 构造函数
                `std::shared_mutex` 的构造函数没有形参，用于创建一个共享互斥锁对象。
            * 主要成员函数
                - `lock()`: 获取独占锁，用于写操作。
                - `unlock()`: 释放独占锁。
                - `lock_shared()`: 获取共享锁，用于读操作。
                - `unlock_shared()`: 释放共享锁。

        3. 使用场景
            适用于读多写少的场景，例如缓存系统或配置文件读取。
    */
    std::vector<int> cache_data;
    std::shared_mutex rw_mutex;

    std::vector<int> query_database() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return {10, 20, 30, 40};
    }

    void update_cache() {
        std::unique_lock lock(rw_mutex);

        std::cout << "[" << std::this_thread::get_id() << "] 开始更新缓存... ";

        cache_data = query_database();
        std::cout << "完成，新数据：";
        for (auto v : cache_data) std::cout << v << " ";
        std::cout << "\n";
    }

    void read_cache(int user_id) {
        std::shared_lock lock(rw_mutex);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::cout << "[" << std::this_thread::get_id() << "] 用户" << user_id
                  << "读取：";
        if (cache_data.empty()) {
            std::cout << "缓存为空（等待更新）\n";
            return;
        }
        for (auto v : cache_data) std::cout << v << " ";
        std::cout << "\n";
    }

    void task() {
        std::cout << "初始化：缓存为空，启动 5 个读线程 + 1 个写线程\n\n";

        std::thread writer(update_cache);
        std::thread readers[5];
        for (int i = 0; i < 5; ++i) {
            readers[i] = std::thread(read_cache, i + 1);
        }

        writer.join();
        for (auto& t : readers) t.join();

        std::cout
            << "\n最终状态：所有操作完成，缓存数据未被修改（写仅执行一次）\n";
    }
}  // namespace SharedMutex

namespace TimeMutex {
    /*
        1. timed_mutex是什么？
            `std::timed_mutex` 是一种支持定时功能的互斥锁，允许线程在指定时间内尝试获取锁。
            如果锁在指定时间内可用，则获取锁；否则，线程会超时并继续执行其他操作。

        2. std::timed_mutex怎么用？
            * 构造函数
                `std::timed_mutex` 的构造函数没有形参，用于创建一个定时互斥锁对象。
            * 主要成员函数
                - `lock()`: 获取锁。如果锁不可用，则阻塞直到锁可用。
                - `unlock()`: 释放锁。
                - `try_lock()`: 尝试立即获取锁。如果锁可用，则获取锁并返回 `true`，否则返回 `false`。
                - `try_lock_for()`: 在指定的时间段内尝试获取锁。如果锁在时间段内可用，则获取锁并返回 `true`，否则返回 `false`。
                - `try_lock_until()`: 在指定的时间点之前尝试获取锁。如果锁在时间点之前可用，则获取锁并返回 `true`，否则返回 `false`。

        3. 使用场景
            适用于需要在超时时间内尝试获取锁的场景，例如避免线程长时间阻塞。
    */
    std::timed_mutex tmtx;

    void critical_section(int id) {
        if (tmtx.try_lock_for(std::chrono::milliseconds(100))) {
            std::cout << "Thread " << id << " entered critical section."
                      << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "Thread " << id << " leaving critical section."
                      << std::endl;
            tmtx.unlock();
        } else {
            std::cout << "Thread " << id << " could not enter critical section."
                      << std::endl;
        }
    }

    void run() {
        std::thread t1(critical_section, 1);
        std::thread t2(critical_section, 2);

        t1.join();
        t2.join();
    }
}  // namespace TimeMutex

namespace SharedTimeMutex {
    /*
        1. shared_timed_mutex是什么？
            `std::shared_timed_mutex` 是一种共享互斥锁，允许多个线程同时读取共享资源，但在写入时需要独占锁。
            它还支持定时功能，可以在指定时间内尝试获取锁。

        2. std::shared_timed_mutex怎么用？
            * 构造函数
                `std::shared_timed_mutex` 的构造函数没有形参，用于创建一个共享定时互斥锁对象。
            * 主要成员函数
                - `lock()`: 获取独占锁，用于写操作。
                - `unlock()`: 释放独占锁。
                - `lock_shared()`: 获取共享锁，用于读操作。
                - `unlock_shared()`: 释放共享锁。
                - `try_lock_for()` 和 `try_lock_until()`: 在指定时间内尝试获取独占锁。
                - `try_lock_shared_for()` 和 `try_lock_shared_until()`: 在指定时间内尝试获取共享锁。

        3. 使用场景
            适用于读多写少的场景，例如缓存系统或配置文件读取。
    */
    void task() {}
}  // namespace SharedTimeMutex

int main() {
    Mutex::run();
    RecursiveMutex::task();
    SharedMutex::task();
    TimeMutex::run();
    SharedTimeMutex::task();

    return 0;
}
