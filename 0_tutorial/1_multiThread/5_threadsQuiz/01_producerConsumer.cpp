#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace producer_consumer {

    struct pCout : public std::stringstream {
        static inline std::mutex cout_mutex;
        ~pCout() override {
            std::lock_guard<std::mutex> l{cout_mutex};
            std::cout << rdbuf();
        }
    };

    std::queue<size_t> q;
    std::mutex q_mutex;
    bool production_stopped{false};

    std::condition_variable go_produce;
    std::condition_variable go_consume;

    static void producer(size_t id, size_t items, size_t stock) {
        for (size_t i = 0; i < items; ++i) {
            std::unique_lock<std::mutex> lock(q_mutex);
            go_produce.wait(lock, [&] { return q.size() < stock; });
            q.push(id * 100 + i);

            pCout{} << " Producer " << id << " --> item " << std::setw(3)
                    << q.back() << '\n';

            go_consume.notify_all();
            std::this_thread::sleep_for(90ms);
        }

        pCout{} << "EXIT: Producer " << id << '\n';
    }

    static void consumer(size_t id) {
        while (!production_stopped || !q.empty()) {
            std::unique_lock<std::mutex> lock(q_mutex);
            if (go_consume.wait_for(lock, 1s, [] { return !q.empty(); })) {
                pCout{} << " item " << std::setw(3) << q.front()
                        << " --> Consumer " << id << '\n';
                q.pop();

                go_produce.notify_all();
                std::this_thread::sleep_for(130ms);
            }
        }

        pCout{} << "EXIT: Consumer " << id << '\n';
    }

    void task() {
        std::vector<std::thread> workers;
        workers.reserve(3);
        for (size_t i = 0; i < 3; ++i) {
            workers.emplace_back(producer, i, 15, 5);
        }

        std::vector<std::thread> consumers;
        consumers.reserve(5);
        for (size_t i = 0; i < 5; ++i) {
            consumers.emplace_back(consumer, i);
        }

        for (auto &t : workers) {
            t.join();
        }
        {
            std::lock_guard<std::mutex> lock(q_mutex);
            production_stopped = true;
        }
        go_consume.notify_all();  // 唤醒等待中的消费者,让它们检查 production_stopped
        for (auto &t : consumers) {
            t.join();
        }
    }
}  // namespace producer_consumer

namespace producer_consumer2 {

    // 单槽位（1-slot）缓冲区: 生产和消费严格交替进行
    // cargo 只能存一个值，生产者在 cargo 非空时等待，消费者在 cargo 为空时等待
    // 这不同于上面的有界队列模型——这里是一种简单的握手协议

    std::mutex mtx;
    std::condition_variable produce, consume;
    int cargo = 0;

    void consumer() {
        std::unique_lock<std::mutex> lck(mtx);
        while (cargo == 0) {
            consume.wait(lck);
        }
        std::cout << cargo << '\n';
        cargo = 0;
        produce.notify_one();
    }

    void producer(int id) {
        std::unique_lock<std::mutex> lck(mtx);
        while (cargo != 0) {
            produce.wait(lck);
        }
        cargo = id;
        consume.notify_one();
    }

    void task() {
        std::thread consumers[10];
        std::thread producers[10];

        for (int i = 0; i < 10; ++i) {
            consumers[i] = std::thread(consumer);
            producers[i] = std::thread(producer, i + 1);
        }

        for (int i = 0; i < 10; ++i) {
            producers[i].join();
            consumers[i].join();
        }
    }
}  // namespace producer_consumer2

int main() {
    std::cout << "\n=== 多生产者-多消费者（有界队列） ===\n";
    producer_consumer::task();

    std::cout << "\n=== 单生产者-单消费者（单槽位） ===\n";
    producer_consumer2::task();
    return 0;
}