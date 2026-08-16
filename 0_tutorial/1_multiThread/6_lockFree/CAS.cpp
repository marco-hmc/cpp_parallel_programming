#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

class AtomicCounter {
  public:
    AtomicCounter() : counter(0) {}

    bool compareAndSwap(int expected, int desired) {
        return counter.compare_exchange_strong(expected, desired);
    }

    int get() const { return counter.load(); }

  private:
    std::atomic<int> counter;
};

void increment(AtomicCounter& counter, int id) {
    for (int i = 0; i < 1000; ++i) {
        int expected = counter.get();
        while (!counter.compareAndSwap(expected, expected + 1)) {
            expected = counter.get();
        }
    }
    std::cout << "Thread " << id << " finished.\n";
}

int main() {
    AtomicCounter counter;
    std::vector<std::thread> threads;

    threads.reserve(10);
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(increment, std::ref(counter), i);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Final counter value: " << counter.get() << std::endl;
    return 0;
}