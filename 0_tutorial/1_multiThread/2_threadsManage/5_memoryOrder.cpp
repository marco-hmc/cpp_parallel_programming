#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace identifier2 {
    std::atomic<int> foo(0);

    void set_foo(int x) {
        foo.store(x, std::memory_order_relaxed);  // 等价于 foo = x;
    }

    void print_foo() {
        int x = 0;
        do {
            x = foo.load(std::memory_order_relaxed);  // 等价于 x = foo;
        } while (x == 0);
        std::cout << "foo: " << x << '\n';
    }

    void test_2() {
        std::thread first(print_foo);
        std::thread second(set_foo, 10);
        first.join();
        second.join();
    }
}  // namespace identifier2

using namespace std::chrono;

const int N = 10000;

// ============================================================
namespace relaxed_order_bench {
    /*
    1. memory_order_relaxed 是什么？
        relaxed 是最宽松的内存顺序，只保证原子性，不保证任何顺序关系。
        适用于简单的计数器场景。

    2. 注意：
        下面的 benchmark 创建了 10000 个线程来测量操作开销，
        这实际上测量的是线程创建/销毁开销远多于 memory order 本身的差异。
        真正的 memory order 性能对比需要用更精细的 micro-benchmark（如单线程反复操作、减少线程数等）。
    */
    void task() {
        std::cout << "relaxed_order: " << std::endl;

        std::atomic<int> counter = {0};
        std::vector<std::thread> vt;
        vt.reserve(N);
        for (int i = 0; i < N; ++i) {
            vt.emplace_back(
                [&]() { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        auto t1 = high_resolution_clock::now();
        for (auto& t : vt) {
            t.join();
        }
        auto t2 = high_resolution_clock::now();
        auto duration = (t2 - t1).count();
        std::cout << "relaxed order speed: " << duration / N << "ns"
                  << std::endl;
    }
}  // namespace relaxed_order_bench

// ============================================================
namespace release_consume_order {
    void task() {
        std::cout << "release_consume_order: " << std::endl;

        std::atomic<int*> ptr{};
        int v = 0;
        std::thread producer([&]() {
            int* p = new int(42);
            v = 1024;
            ptr.store(p, std::memory_order_release);
        });
        std::thread consumer([&]() {
            int* p = nullptr;
            while ((p = ptr.load(std::memory_order_consume)) == nullptr) {
                ;
            }

            std::cout << "p: " << *p << std::endl;
            std::cout << "v: " << v << std::endl;
        });
        producer.join();
        consumer.join();
    }
}  // namespace release_consume_order

// ============================================================
namespace release_acquire_order {
    void task() {
        std::cout << "release_acquire_order: " << std::endl;

        int v;
        std::atomic<int> flag = {0};
        std::thread release([&]() {
            v = 42;
            flag.store(1, std::memory_order_release);
        });
        std::thread acqrel([&]() {
            int expected = 1;  // must before compare_exchange_strong
            while (!flag.compare_exchange_strong(expected, 2,
                                                 std::memory_order_acq_rel)) {
                expected = 1;  // must after compare_exchange_strong
            }
            // flag has changed to 2
        });
        std::thread acquire([&]() {
            while (flag.load(std::memory_order_acquire) < 2) {
                ;
            }

            std::cout << "v: " << v << std::endl;  // must be 42
        });
        release.join();
        acqrel.join();
        acquire.join();
    }

}  // namespace release_acquire_order

// ============================================================
namespace seq_cst_order_bench {
    /*
    注意：同 relaxed_order_bench，这里也主要测量线程创建开销。
    */
    void task() {
        std::cout << "sequential_consistent_order: " << std::endl;

        std::atomic<int> counter = {0};
        std::vector<std::thread> vt;
        vt.reserve(N);
        for (int i = 0; i < N; ++i) {
            vt.emplace_back(
                [&]() { counter.fetch_add(1, std::memory_order_seq_cst); });
        }
        auto t1 = high_resolution_clock::now();
        for (auto& t : vt) {
            t.join();
        }
        auto t2 = high_resolution_clock::now();
        auto duration = (t2 - t1).count();
        std::cout << "sequential consistent speed: " << duration / N << "ns"
                  << std::endl;
    }
}  // namespace seq_cst_order_bench

// ============================================================
int main() {
    std::cout << "===== 1. memory_order_relaxed =====\n";
    relaxed_order_bench::task();

    std::cout << "\n===== 2. release-consume =====\n";
    release_consume_order::task();

    std::cout << "\n===== 3. release-acquire =====\n";
    release_acquire_order::task();

    std::cout << "\n===== 4. sequential_consistent =====\n";
    seq_cst_order_bench::task();
    return 0;
}