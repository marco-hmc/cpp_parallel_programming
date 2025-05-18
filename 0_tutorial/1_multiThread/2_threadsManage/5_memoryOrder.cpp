#include <atomic>
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

void relaxed_order() {
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
    std::cout << "relaxed order speed: " << duration / N << "ns" << std::endl;
}

void release_consume_order() {
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

void release_acquire_order() {
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

void sequential_consistent_order() {
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

int main() {
    relaxed_order();
    release_consume_order();
    release_acquire_order();
    sequential_consistent_order();
    return 0;
}