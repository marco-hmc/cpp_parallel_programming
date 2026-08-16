#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ============================================================
namespace tbb_concurrent_vector {
    /*
    1. tbb::concurrent_vector 是什么？
        线程安全的动态数组，支持多线程并发 push_back 而无需外部加锁。
        与 std::vector + mutex 相比，concurrent_vector 有更好的并发性能。

    2. 特点：
        - 多线程可以同时 push_back
        - 元素的地址在 push_back 后保持不变（不会因为 reallocation 而移动）
        - 不支持在中间插入/删除
        - grow_by() 可以一次性添加多个元素
    */

    void task() {
        tbb::concurrent_vector<int> cv;

        const int num_threads = 4;
        const int items_per_thread = 100;

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&cv, t, items_per_thread] {
                for (int i = 0; i < items_per_thread; ++i) {
                    cv.push_back(t * items_per_thread + i);
                }
            });
        }

        for (auto& th : threads) th.join();

        std::cout << "并发 push_back 后大小: " << cv.size()
                  << " (预期: " << num_threads * items_per_thread << ")\n";
        std::cout << "前 5 个元素: ";
        for (size_t i = 0; i < 5 && i < cv.size(); ++i) {
            std::cout << cv[i] << " ";
        }
        std::cout << "\n";
    }
}  // namespace tbb_concurrent_vector

// ============================================================
namespace tbb_concurrent_queue {
    /*
    1. tbb::concurrent_queue 是什么？
        线程安全的 FIFO 队列。支持多生产者-多消费者模式，
        不需要外部同步。底层使用 fine-grained locking 或 lock-free 实现。

    2. 常用操作：
        - push(item): 入队
        - try_pop(item): 尝试出队，成功返回 true
        - pop(item): 阻塞直到有元素（某些版本）
    */

    void task() {
        tbb::concurrent_queue<int> q;

        const int total_items = 1000;
        int consumed_count = 0;

        // 生产者线程：放入数字
        std::thread producer([&q, total_items] {
            for (int i = 0; i < total_items; ++i) {
                q.push(i);
            }
        });

        // 消费者线程：取出数字
        std::thread consumer([&q, total_items, &consumed_count] {
            int val;
            int received = 0;
            while (received < total_items) {
                if (q.try_pop(val)) {
                    ++received;
                    ++consumed_count;
                } else {
                    std::this_thread::yield();
                }
            }
        });

        producer.join();
        consumer.join();

        std::cout << "生产 " << total_items << " 个，消费 " << consumed_count
                  << " 个\n";
    }
}  // namespace tbb_concurrent_queue

// ============================================================
namespace tbb_concurrent_hash_map {
    /*
    1. tbb::concurrent_hash_map 是什么？
        线程安全的哈希表，允许多线程并发读写。
        使用 fine-grained locking（分段锁）实现高并发。

    2. 访问方式：
        使用 accessor 进行读写。accessor 在构造时获取对应 bucket 的锁，
        析构时释放，遵循 RAII 原则。

        - const_accessor：只读访问，允许多个线程同时持有
        - accessor：读写访问，独占该 key 的访问权
    */

    void task() {
        // 定义：Key=std::string, Value=int
        typedef tbb::concurrent_hash_map<std::string, int> StringTable;
        StringTable table;

        // 插入
        {
            StringTable::accessor a;
            table.insert(a, "apple");
            a->second = 100;

            StringTable::accessor b;
            table.insert(b, "banana");
            b->second = 200;
        }

        // 并发更新
        std::thread updater([&table] {
            StringTable::accessor a;
            if (table.find(a, "apple")) {
                a->second += 50;
                std::cout << "[更新线程] apple += 50\n";
            }
        });

        // 并发读取
        std::thread reader([&table] {
            StringTable::const_accessor a;
            if (table.find(a, "apple")) {
                std::cout << "[读取线程] apple = " << a->second << "\n";
            }
        });

        updater.join();
        reader.join();

        // 最终结果
        std::cout << "最终: apple = ";
        {
            StringTable::const_accessor a;
            if (table.find(a, "apple")) {
                std::cout << a->second;
            }
        }
        std::cout << "\n";
    }
}  // namespace tbb_concurrent_hash_map

// ============================================================
int main() {
    std::cout << "===== 1. concurrent_vector =====\n";
    tbb_concurrent_vector::task();

    std::cout << "\n===== 2. concurrent_queue =====\n";
    tbb_concurrent_queue::task();

    std::cout << "\n===== 3. concurrent_hash_map =====\n";
    tbb_concurrent_hash_map::task();

    return 0;
}
