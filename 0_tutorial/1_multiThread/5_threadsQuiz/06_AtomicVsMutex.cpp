#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    template <typename Func, typename... Args>
    inline void measure_time(Func&& func, Args&&... args) {
        auto start = std::chrono::high_resolution_clock::now();
        std::forward<Func>(func)(std::forward<Args>(args)...);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double milliseconds = duration.count() / 1e6;
        if (milliseconds < 1000) {
            std::cout << std::scientific << std::setprecision(2) << milliseconds
                      << " ms" << std::endl;
        } else {
            std::cout << std::scientific << std::setprecision(2)
                      << milliseconds / 1000 << " s" << std::endl;
        }
    }

    constexpr int NUM_THREADS = 4;
    constexpr int NUM_ITERATIONS = 1'000'000;

    void atomic_increment() {
        std::atomic<int> counter{0};
        auto task = [&counter]() {
            for (int i = 0; i < NUM_ITERATIONS; ++i) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(task);
        }
        for (auto& t : threads) {
            t.join();
        }

        std::cout << "Final counter value (atomic): " << counter.load()
                  << std::endl;
    }

    void mutex_increment() {
        int counter = 0;
        std::mutex mtx;
        auto task = [&counter, &mtx]() {
            for (int i = 0; i < NUM_ITERATIONS; ++i) {
                std::lock_guard<std::mutex> lock(mtx);
                ++counter;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(task);
        }
        for (auto& t : threads) {
            t.join();
        }

        std::cout << "Final counter value (mutex): " << counter << std::endl;
    }

    void task() {
        std::cout << "Testing atomic increment:" << std::endl;
        measure_time(atomic_increment);

        std::cout << "\nTesting mutex increment:" << std::endl;
        measure_time(mutex_increment);
    }
}  // namespace

int main() {
    task();
    return 0;
}