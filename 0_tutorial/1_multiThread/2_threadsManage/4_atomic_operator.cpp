#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace regularOper {
    /*
    1. 原子变量的store和load操作是什么意思，有什么用？
        * store操作：将一个值存储到原子变量中，可以指定内存顺序(memory order)。
        * load操作：从原子变量中加载一个值，可以指定内存顺序(memory order)。
        * fetch_add操作：原子地将一个值加到原子变量中，返回原子变量的旧值。

        这些操作和相关运算符的操作本质是一直的，只是函数的方式可以指定内存徐，而运算符的方式是默认的内存顺序（最安全的）。
    */

    std::atomic<int> count = {0};

    int test_1() {
        std::thread t1([]() { count.fetch_add(1); });
        std::thread t2([]() {
            count++;     // same as count.fetch_add(1)
            count += 1;  // same as count.fetch_add(1)
        });

        t1.join();
        t2.join();
        std::cout << count << '\n';
        return 0;
    }
}  // namespace regularOper

/////////////////////////////////////////////////////////////

namespace lock_free {
    /*
    1. 自定义原子类型哪些是 lock-free 的，哪些不是？
        * 基本数据类型：大多数平台上的基本数据类型（如int、float、double等）通常是无锁的。
        * 小型结构体：包含少量基本数据类型的结构体，通常也是无锁的，前提是它们的大小和对齐方式适合平台的原子操作。
        * 大型结构体和数组：包含大量数据或复杂数据结构的类型，通常不是无锁的，因为它们的大小超出了平台原子操作的能力。

        注意：std::atomic<T> 的 is_lock_free() 方法返回 true 并不意味着 std::atomic<T>
        的所有操作都是无锁的，只是表示 std::atomic<T> 的 lock-free 特性是平台支持的。
    */
    struct A {
        int a[100];
    };
    struct B {
        int x, y;
    };
    void task() {
        std::cout << std::boolalpha << "std::atomic<A> is lock free? "
                  << std::atomic<A>{}.is_lock_free() << '\n'
                  << "std::atomic<B> is lock free? "
                  << std::atomic<B>{}.is_lock_free() << '\n';
    }
}  // namespace lock_free

namespace atomic_bool {
    std::atomic<bool> ready(false);
    std::atomic<bool> winner(false);

    void count1m(int id) {
        while (!ready) {
        }

        for (int i = 0; i < 1000000; ++i) {
        }

        if (!winner.exchange(true)) {
            std::cout << "thread #" << id << " won!\n";
        }
    };

    void task() {
        std::cout << "spawning 10 threads that count to 1 million...\n";
        std::vector<std::thread> threads;
        for (int i = 1; i <= 10; ++i) {
            threads.emplace_back(count1m, i);
        }

        ready = true;
        for (auto &th : threads) {
            th.join();
        }
    }
}  // namespace atomic_bool

namespace atomic_compare_exchange_wear {
    /*
    1. compare_exchange_weak怎么用的？
    `compare_exchange_weak` 是 C++ 标准库中 `std::atomic`
    类提供的一种原子操作，用于实现无锁编程。它的主要作用是比较并交换值，通常用于实现复杂的同步原语，如无锁队列、无锁栈等。

    ### 函数签名

    ```cpp
    bool compare_exchange_weak(T& expected, T desired, std::memory_order order =
    std::memory_order_seq_cst) noexcept; bool compare_exchange_weak(T& expected, T
    desired, std::memory_order success, std::memory_order failure) noexcept;
    ```

    ### 参数解释

    - `expected`：一个引用参数，表示预期的值。如果当前原子对象的值与 `expected`
    相等，则将其更新为 `desired`；否则，将当前原子对象的值写入 `expected`。
    - `desired`：表示希望设置的新值。
    - `order`：内存顺序，默认为 `std::memory_order_seq_cst`，表示顺序一致性。
    - `success` 和 `failure`：分别表示成功和失败时的内存顺序。

    ### 返回值

    - 如果当前原子对象的值与 `expected` 相等，则返回 `true`，并将原子对象的值更新为
    `desired`。
    - 如果当前原子对象的值与 `expected` 不相等，则返回
    `false`，并将当前原子对象的值写入 `expected`。

    ### 总结

    - `compare_exchange_weak`
    是一种原子操作，用于比较并交换值，常用于实现无锁数据结构。
    - 它通过比较当前值与预期值，如果相等则更新为新值，否则更新预期值为当前值。
    - 该操作在实现复杂的同步原语时非常有用，能够避免锁的使用，提高并发性能。

    */
    struct Node {
        int value;
        Node *next;
    };
    std::atomic<Node *> list_head(nullptr);

    void append(int val) {
        Node *oldHead = list_head;
        Node *newNode = new Node{val, oldHead};

        while (!list_head.compare_exchange_weak(oldHead, newNode)) {
            newNode->next = oldHead;
        }
    }

    void printFunc() {
        Node *it;
        while (it = list_head) {
            list_head = it->next;
            delete it;
        }
    }

    void task() {
        std::vector<std::thread> threads;
        threads.reserve(10);
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back(append, i);
        }
        for (auto &th : threads) {
            th.join();
        }

        for (Node *it = list_head; it != nullptr; it = it->next) {
            std::cout << ' ' << it->value;
        }
        std::cout << '\n';

        printFunc();
    }

}  // namespace atomic_compare_exchange_wear

namespace AtomicFlag {
    /*
    ### 1. `atomic_flag` 和一般的 `atomic` 有什么区别？
    atomic_flag一定是无锁的。
    atomic_bool是否无锁取决于架构。
    两者的作用都是类似的，但是atomic_flag更加轻量级，多用于实现自定义的atomic类型或者无锁编程的。

    ### 2. `atomic_flag` 的初始化方式是什么？常见的成员函数有哪些？

    - **初始化方式**：
    - `atomic_flag` 可以通过 `ATOMIC_FLAG_INIT` 宏进行初始化。
    - 也可以使用默认构造函数进行初始化。

    ```cpp
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    std::atomic_flag flag2;
    ```

    - **常见的成员函数**：
    -[`test_and_set()`]：设置标志位为 `true`，并返回之前的值。
        用于设置atomic_flag对象的值为true，并返回之前的值。
        如果之前的值为true，则返回true，否则返回false。
    -[`clear()`]：清除标志位，将其设置为 `false`。

    */
    std::atomic_flag lock_stream = ATOMIC_FLAG_INIT;
    std::stringstream stream;

    void append_number(int x) {
        while (lock_stream.test_and_set()) {
        }
        stream << "thread #" << x << '\n';
        lock_stream.clear();
    }

    void task() {
        std::vector<std::thread> threads;
        for (int i = 1; i <= 10; ++i) {
            threads.emplace_back(append_number, i);
        }
        for (auto &th : threads) {
            th.join();
        }

        std::cout << stream.str();
    }
}  // namespace AtomicFlag

namespace CallOnce {
    /*
    1. call_once是什么？
        `std::call_once`是一个C++标准库函数，用于确保一个函数或代码块只被执行一次。
        `std::call_once`接受一个`std::once_flag`对象和一个函数或代码块作为参数。
        `std::call_once`会检查`std::once_flag`对象是否已经被调用过，如果没有，那么它会调用传入的函数或代码块。
    */

    int winner;
    std::once_flag winner_flag;

    void set_winner(int x) { winner = x; }
    void wait_1000ms(int id) {
        for (int i = 0; i < 1000; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::call_once(winner_flag, set_winner, id);
    }

    void task() {
        std::vector<std::thread> threads;
        threads.reserve(10);
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back(wait_1000ms, i + 1);
        }

        std::cout
            << "waiting for the first among 10 threads to count 1000ms...\n";

        for (auto &th : threads) {
            th.join();
        }
        std::cout << "winner thread: " << winner << '\n';
    }
}  // namespace CallOnce

namespace atomic_thread_fence {
    /*

    ### 1. 所以原子变量都有 `exchange()` 方法和 `store` 吗？解释一下这两个方法怎么使用？

    C++ 标准库中的 `std::atomic` 类提供了多种方法来操作原子变量，其中包括 `exchange()` 和 `store()` 方法。

    - **`exchange()` 方法**：
    - 用于将原子变量的当前值替换为新值，并返回旧值。
    - 语法：`atomic_var.exchange(new_value, memory_order)`
    - 示例：
        ```cpp
        std::atomic<int> atomic_var(0);
        int old_value = atomic_var.exchange(1); // 将 atomic_var 的值设置为 1，并返回旧值 0
        ```

    - **`store()` 方法**：
    - 用于将新值存储到原子变量中。
    - 语法：`atomic_var.store(new_value, memory_order)`
    - 示例：
        ```cpp
        std::atomic<int> atomic_var(0);
        atomic_var.store(1); // 将 atomic_var 的值设置为 1
        ```

    ### 2. 为什么 `lock()` 函数中要使用 `while` 循环？会导致忙等待吗？

    - **使用 `while` 循环**：
    - `lock()` 函数中的 `while` 循环用于实现自旋锁。自旋锁会不断尝试获取锁，直到成功为止。
    - 代码示例：
        ```cpp
        void lock() {
            while (flag.exchange(true, std::memory_order_relaxed)) {
                // 忙等待
            }
            std::atomic_thread_fence(std::memory_order_acquire);
        }
        ```

    - **忙等待**：
    - 是的，`while` 循环会导致忙等待（busy-waiting），即线程在循环中不断检查条件，而不进行任何有意义的工作。
    - 忙等待会消耗 CPU 资源，但在某些情况下（如锁持有时间很短），自旋锁可能比阻塞锁更高效。

    ### 3. 为什么 `unlock()` 函数中要使用 `std::atomic_thread_fence(std::memory_order_release)`？

    - **`std::atomic_thread_fence(std::memory_order_release)`**：
    - 用于确保在释放锁之前，所有先前的内存操作不会被重排序到这个操作之后。
    - 这可以确保在释放锁之前，所有对共享数据的修改都对其他线程可见。
    - 代码示例：
        ```cpp
        void unlock() {
            std::atomic_thread_fence(std::memory_order_release);
            flag.store(false, std::memory_order_relaxed);
        }
        ```

    ### 4. 解释一下 `std::atomic_thread_fence` 怎么用？

    - **`std::atomic_thread_fence`**：
    - 是一个全局内存栅栏，用于控制内存操作的顺序。
    - 它不会对特定的原子变量进行操作，而是对所有内存操作生效。
    - 常用的内存顺序有：
        - `std::memory_order_relaxed`：不进行任何同步或排序操作。
        - `std::memory_order_acquire`：确保后续的内存操作不会被重排序到这个操作之前。
        - `std::memory_order_release`：确保先前的内存操作不会被重排序到这个操作之后。
    - 代码示例：
        ```cpp
        std::atomic<int> atomic_var(0);

        // 确保在获取锁之后的内存操作不会被重排序到获取锁之前
        std::atomic_thread_fence(std::memory_order_acquire);

        // 确保在释放锁之前的内存操作不会被重排序到释放锁之后
        std::atomic_thread_fence(std::memory_order_release);
        ```

    希望这些解释对你有所帮助！如果有其他问题，请随时提问。
    */

    class mutex {
        std::atomic<bool> flag{false};

      public:
        void lock() {
            while (flag.exchange(true, std::memory_order_relaxed)) {
            }
            std::atomic_thread_fence(std::memory_order_acquire);
        }

        void unlock() {
            std::atomic_thread_fence(std::memory_order_release);
            flag.store(false, std::memory_order_relaxed);
        }
    };

    void task() {
        int a = 0;
        mutex mtx_a;
        std::thread t1([&]() {
            mtx_a.lock();
            a += 1;
            mtx_a.unlock();
        });
        std::thread t2([&]() {
            mtx_a.lock();
            a += 2;
            mtx_a.unlock();
        });

        t1.join();
        t2.join();

        std::cout << a << '\n';
    }
}  // namespace atomic_thread_fence

int main() {
    regularOper::test_1();

    return 0;
}
