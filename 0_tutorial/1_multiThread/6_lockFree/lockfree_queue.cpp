#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

// ============================================================
namespace spsc_queue {
    /*
    1. SPSC 无锁队列是什么？
        SPSC = Single Producer, Single Consumer
        单生产者-单消费者的无锁队列，不需要 mutex，性能极高。

    2. 核心思想：
        - 使用固定大小的环形缓冲区（ring buffer）
        - 用两个原子变量: write_pos（写位置）和 read_pos（读位置）
        - 生产者只写 write_pos，消费者只写 read_pos，没有竞争
        - 使用 memory_order_acquire/release 保证可见性

    3. 适用场景：
        - 线程间高效消息传递
        - 事件循环中的任务队列
        - audio/video 处理管线
    */

    template <typename T, size_t Capacity>
    class SPSCQueue {
        static_assert((Capacity & (Capacity - 1)) == 0,
                      "Capacity 必须是 2 的幂次");

      public:
        SPSCQueue()
            : buffer_(new T[Capacity]),
              write_pos_(0),
              read_pos_(0) {}

        ~SPSCQueue() { delete[] buffer_; }

        // 生产者调用: 尝试放入元素
        bool try_push(const T& item) {
            size_t w = write_pos_.load(std::memory_order_relaxed);
            size_t next = (w + 1) & (Capacity - 1);

            // 队列满？
            if (next == read_pos_.load(std::memory_order_acquire)) {
                return false;
            }

            buffer_[w] = item;
            write_pos_.store(next, std::memory_order_release);
            return true;
        }

        // 消费者调用: 尝试取出元素
        bool try_pop(T& item) {
            size_t r = read_pos_.load(std::memory_order_relaxed);

            // 队列空？
            if (r == write_pos_.load(std::memory_order_acquire)) {
                return false;
            }

            item = buffer_[r];
            read_pos_.store((r + 1) & (Capacity - 1),
                           std::memory_order_release);
            return true;
        }

        // 禁用拷贝
        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;

      private:
        T* buffer_;
        std::atomic<size_t> write_pos_;
        std::atomic<size_t> read_pos_;
        // padding 避免 false sharing（简化起见这里没加）
    };

    void task() {
        constexpr size_t Q_SIZE = 16;  // 必须是 2 的幂次
        SPSCQueue<int, Q_SIZE> q;

        const int total_items = 100;
        int consumed = 0;
        std::atomic<bool> done{false};

        // 生产者
        std::thread producer([&q, total_items] {
            for (int i = 0; i < total_items; ++i) {
                while (!q.try_push(i)) {
                    std::this_thread::yield();  // 队列满，等待
                }
                if (i % 20 == 0) {
                    std::cout << "[生产者] 已放入 " << i + 1 << " 个\n";
                }
            }
            std::cout << "[生产者] 完成，共放入 " << total_items << " 个\n";
        });

        // 消费者
        std::thread consumer([&q, total_items, &consumed, &done] {
            int val;
            while (consumed < total_items) {
                if (q.try_pop(val)) {
                    ++consumed;
                    if (consumed % 20 == 0) {
                        std::cout << "[消费者] 已取出 " << consumed << " 个\n";
                    }
                } else {
                    std::this_thread::yield();  // 队列空，等待
                }
            }
            std::cout << "[消费者] 完成，共取出 " << consumed << " 个\n";
        });

        producer.join();
        consumer.join();

        std::cout << "验证: 生产 " << total_items << ", 消费 " << consumed
                  << " → " << (total_items == consumed ? "成功!" : "失败!")
                  << "\n";
    }
}  // namespace spsc_queue

// ============================================================
namespace mpmc_queue {
    /*
    1. MPMC 无锁队列简要说明：
        MPMC (Multiple Producer, Multiple Consumer) 的无锁队列比 SPSC 复杂得多。
        典型的实现使用 compare_exchange 来原子地抢占槽位。

    2. 下面是一个简化版的多生产者-单消费者（MPSC）队列，
        使用 CAS 来协调多个生产者之间的竞争。
    */

    template <typename T>
    struct Node {
        T data;
        std::atomic<Node*> next;
        Node() : next(nullptr) {}
        explicit Node(const T& val) : data(val), next(nullptr) {}
    };

    template <typename T>
    class MpscQueue {
      public:
        MpscQueue() {
            // 哨兵节点
            Node<T>* dummy = new Node<T>();
            head_ = dummy;
            tail_ = dummy;
        }

        ~MpscQueue() {
            while (Node<T>* node = head_.load()) {
                head_.store(node->next);
                delete node;
            }
        }

        // 多生产者安全: 使用 CAS 竞争
        void push(const T& item) {
            Node<T>* node = new Node<T>(item);
            Node<T>* prev_tail = tail_.exchange(node, std::memory_order_acq_rel);
            prev_tail->next.store(node, std::memory_order_release);
        }

        // 单消费者（无需 CAS）
        bool try_pop(T& item) {
            Node<T>* h = head_.load(std::memory_order_acquire);
            Node<T>* next = h->next.load(std::memory_order_acquire);
            if (next == nullptr) {
                return false;  // 队列空
            }
            item = next->data;
            head_.store(next, std::memory_order_release);
            delete h;  // 删除旧哨兵
            return true;
        }

      private:
        std::atomic<Node<T>*> head_;
        std::atomic<Node<T>*> tail_;
    };

    void task() {
        MpscQueue<int> q;

        const int num_producers = 4;
        const int items_per_producer = 25;

        // 多个生产者
        std::vector<std::thread> producers;
        producers.reserve(num_producers);
        for (int t = 0; t < num_producers; ++t) {
            producers.emplace_back([&q, t, items_per_producer] {
                for (int i = 0; i < items_per_producer; ++i) {
                    q.push(t * 100 + i);
                }
            });
        }

        for (auto& t : producers) t.join();
        std::cout << num_producers << " 个生产者各放入 " << items_per_producer
                  << " 个元素\n";

        // 单消费者
        int consumed = 0;
        int val;
        while (q.try_pop(val)) {
            ++consumed;
        }

        std::cout << "消费者取出 " << consumed << " 个元素"
                  << " (预期 " << num_producers * items_per_producer << ")\n";
    }
}  // namespace mpmc_queue

// ============================================================
int main() {
    std::cout << "===== 1. SPSC 无锁队列（环形缓冲）=====\n";
    spsc_queue::task();

    std::cout << "\n===== 2. MPSC 无锁队列（链表式，CAS）=====\n";
    mpmc_queue::task();

    return 0;
}
