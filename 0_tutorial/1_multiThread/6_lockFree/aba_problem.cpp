#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

// ============================================================
namespace aba_problem_demo {
    /*
    1. ABA 问题是什么？
        ABA 问题是无锁编程中一个经典陷阱，发生在 CAS 操作中：
        线程 1 读取值 A
        → 线程 2 将 A 改为 B，然后又改回 A
        → 线程 1 执行 CAS，发现还是 A，认为没有变化，操作"成功"
        但实际上数据结构可能已经被修改过了。

    2. 为什么会出问题？
        CAS 只检查"值是否等于期望值"，不知道中间发生过变化。
        在无锁栈/队列中，这可能导致释放了还在使用中的内存（use-after-free）。

    3. 解决方案：
        - 使用 tagged pointer（指针 + 版本号）
        - 使用 hazard pointers
        - 使用 RCU (Read-Copy-Update)
        - 使用 epoch-based reclamation

    4. 下面用无锁栈演示 ABA 问题的发生和解决
    */

    // ========== 有 ABA 问题的无锁栈 ==========
    template <typename T>
    class UnsafeLockFreeStack {
        struct Node {
            T data;
            Node* next;
            explicit Node(const T& val) : data(val), next(nullptr) {}
        };

        std::atomic<Node*> head_{nullptr};

      public:
        void push(const T& val) {
            Node* node = new Node(val);
            node->next = head_.load(std::memory_order_relaxed);
            while (!head_.compare_exchange_weak(node->next, node,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
            }
        }

        bool pop(T& val) {
            Node* node = head_.load(std::memory_order_acquire);
            while (node) {
                if (head_.compare_exchange_weak(node, node->next,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
                    val = node->data;
                    delete node;  // ABA 风险: 其他线程可能还在用这个 node
                    return true;
                }
            }
            return false;
        }

        ~UnsafeLockFreeStack() {
            T dummy;
            while (pop(dummy)) {
            }
        }
    };

    void task() {
        std::cout << "ABA 问题说明:\n\n";
        std::cout << "假设无锁栈当前状态: Top → A → B → C\n\n";

        std::cout << "线程 1 (pop): 读取 head = A, head->next = B\n";
        std::cout << "线程 2 (pop): 弹出 A，删除 A\n";
        std::cout << "线程 2 (pop): 弹出 B，删除 B\n";
        std::cout << "线程 2 (push): 压入 A (新分配的节点，恰好在同一地址!)\n";
        std::cout << "    栈当前状态: Top → A(新) → C\n\n";
        std::cout << "线程 1 (pop): CAS(head, A, B)  →  成功！\n";
        std::cout << "    但 B 已经被删除了！undefined behavior!\n\n";

        // 简化的单线程演示，展示基本用法
        UnsafeLockFreeStack<int> stack;
        stack.push(1);
        stack.push(2);
        stack.push(3);

        int val;
        while (stack.pop(val)) {
            std::cout << "弹出: " << val << "\n";
        }
        std::cout << "（单线程下连续 push/pop 不会有 ABA 问题）\n";
    }
}  // namespace aba_problem_demo

// ============================================================
namespace tagged_pointer_solution {
    /*
    1. Tagged Pointer 解决方案：
        在指针的高位（或独立字段）附加一个版本计数器。
        每次修改时递增计数器。CAS 比较指针+计数器，这样即使
        地址相同，计数器不同也会导致 CAS 失败。

    2. 实现方式：
        - 在 64 位系统上，指针实际只用 48 位，剩余 16 位可用作 tag
        - 或使用 double-width CAS (DWCAS) 操作 128 位的数据
        - C++ 使用 std::atomic<std::pair<...>> 或自定义结构体
    */

    // 使用 128-bit CAS 的 tagged pointer (需要平台支持)
    template <typename T>
    struct TaggedPointer {
        T* ptr;
        uintptr_t tag;

        bool operator==(const TaggedPointer& other) const {
            return ptr == other.ptr && tag == other.tag;
        }
    };

    template <typename T>
    class SafeLockFreeStack {
        struct Node {
            T data;
            Node* next;
            explicit Node(const T& val) : data(val), next(nullptr) {}
        };

        // 使用 double-width CAS (16 字节对齐，需要平台支持)
        struct alignas(16) Head {
            Node* ptr;
            uintptr_t tag;
        };
        std::atomic<Head> head_{{nullptr, 0}};

      public:
        void push(const T& val) {
            Node* node = new Node(val);
            Head old_head = head_.load(std::memory_order_relaxed);
            Head new_head;
            do {
                node->next = old_head.ptr;
                new_head = {node, old_head.tag + 1};
            } while (!head_.compare_exchange_weak(
                old_head, new_head, std::memory_order_release,
                std::memory_order_relaxed));
        }

        bool pop(T& val) {
            Head old_head = head_.load(std::memory_order_acquire);
            Head new_head;
            while (old_head.ptr) {
                new_head = {old_head.ptr->next, old_head.tag + 1};
                if (head_.compare_exchange_weak(old_head, new_head,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
                    val = old_head.ptr->data;
                    delete old_head.ptr;
                    return true;
                }
            }
            return false;
        }

        ~SafeLockFreeStack() {
            T dummy;
            while (pop(dummy)) {
            }
        }
    };

    void task() {
        SafeLockFreeStack<int> stack;
        stack.push(1);
        stack.push(2);
        stack.push(3);

        int val;
        while (stack.pop(val)) {
            std::cout << "弹出: " << val << "\n";
        }
        std::cout << "Tagged pointer 保证了即使地址相同，tag 不同 CAS 也会失败。\n";
    }
}  // namespace tagged_pointer_solution

// ============================================================
namespace aba_concurrent_demo {
    /*
    多线程环境下容易触发 ABA 问题的演示：
    多个线程同时 push/pop，可能触发 use-after-free。
    这个演示展示有 tag 保护的版本，不会 crash。
    */

    template <typename T>
    class SafeStack {
        struct Node {
            T data;
            Node* next;
            explicit Node(const T& val) : data(val), next(nullptr) {}
        };

        struct alignas(16) Head {
            Node* ptr;
            uintptr_t tag;
        };
        std::atomic<Head> head_{{nullptr, 0}};

      public:
        void push(const T& val) {
            Node* node = new Node(val);
            Head old_head = head_.load(std::memory_order_relaxed);
            Head new_head;
            do {
                node->next = old_head.ptr;
                new_head = {node, old_head.tag + 1};
            } while (!head_.compare_exchange_weak(
                old_head, new_head, std::memory_order_release,
                std::memory_order_relaxed));
        }

        bool pop(T& val) {
            Head old_head = head_.load(std::memory_order_acquire);
            Head new_head;
            while (old_head.ptr) {
                new_head = {old_head.ptr->next, old_head.tag + 1};
                if (head_.compare_exchange_weak(old_head, new_head,
                                                std::memory_order_release,
                                                std::memory_order_relaxed)) {
                    val = old_head.ptr->data;
                    delete old_head.ptr;
                    return true;
                }
            }
            return false;
        }
    };

    void task() {
        SafeStack<int> stack;
        const int num_threads = 4;
        const int ops = 100;

        std::vector<std::thread> threads;
        threads.reserve(num_threads * 2);

        // 生产者
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&stack, t, ops] {
                for (int i = 0; i < ops; ++i) {
                    stack.push(t * 1000 + i);
                }
            });
        }

        // 消费者
        std::atomic<int> total_popped{0};
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&stack, &total_popped] {
                int val;
                while (total_popped.load() < num_threads * ops) {
                    if (stack.pop(val)) {
                        ++total_popped;
                    }
                }
            });
        }

        for (auto& t : threads) t.join();

        std::cout << "多线程无锁栈（tagged pointer）: push " << num_threads * ops
                  << " 个, pop " << total_popped << " 个 → "
                  << (num_threads * ops == total_popped ? "成功!" : "失败!")
                  << "\n";
    }
}  // namespace aba_concurrent_demo

// ============================================================
int main() {
    std::cout << "===== 1. ABA 问题说明 =====\n";
    aba_problem_demo::task();

    std::cout << "\n===== 2. Tagged Pointer 解决方案 =====\n";
    tagged_pointer_solution::task();

    std::cout << "\n===== 3. 多线程无锁栈（tagged pointer）=====\n";
    aba_concurrent_demo::task();

    return 0;
}
