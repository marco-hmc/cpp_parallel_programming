#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

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

namespace sequentialPrintABC_cv {
    std::mutex myMutex;
    std::condition_variable cv;
    int flag = 0;

    void printA() {
        std::unique_lock<std::mutex> lk(myMutex);
        int count = 0;
        while (count < 10) {
            while (flag != 0) {
                cv.wait(lk);
            }
            std::cout << "\tcv_a" << std::endl;
            flag = 1;
            cv.notify_all();
            count++;
        }
    }

    void printB() {
        std::unique_lock<std::mutex> lk(myMutex);
        for (int i = 0; i < 10; i++) {
            while (flag != 1) {
                cv.wait(lk);
            }
            std::cout << "\tcv_b" << std::endl;
            flag = 2;
            cv.notify_all();
        }
    }

    void printC() {
        std::unique_lock<std::mutex> lk(myMutex);
        for (int i = 0; i < 10; i++) {
            while (flag != 2) {
                cv.wait(lk);
            }
            std::cout << "\tcv_c" << std::endl;
            flag = 0;
            cv.notify_all();
        }
    }

    void task() {
        std::thread th2(printA);
        std::thread th1(printB);
        std::thread th3(printC);

        th1.join();
        th2.join();
        th3.join();
    }
}  // namespace sequentialPrintABC_cv

namespace sequentialPrintABC_atomic {
    std::atomic<bool> a{true};
    std::atomic<bool> b{false};
    std::atomic<bool> c{false};

    void printA() {
        for (int i = 0; i < 10; ++i) {
            while (!a.load(std::memory_order_acquire)) {
                // busy-wait
            }
            std::cout << "\tatomic_a" << std::endl;
            a.store(false, std::memory_order_release);
            b.store(true, std::memory_order_release);
        }
    }

    void printB() {
        for (int i = 0; i < 10; ++i) {
            while (!b.load(std::memory_order_acquire)) {
                // busy-wait
            }
            std::cout << "\tatomic_b" << std::endl;
            b.store(false, std::memory_order_release);
            c.store(true, std::memory_order_release);
        }
    }

    void printC() {
        for (int i = 0; i < 10; ++i) {
            while (!c.load(std::memory_order_acquire)) {
                // busy-wait
            }
            std::cout << "\tatomic_c" << std::endl;
            c.store(false, std::memory_order_release);
            a.store(true, std::memory_order_release);
        }
    }

    void task() {
        std::thread t1(printA);
        std::thread t2(printB);
        std::thread t3(printC);

        t1.join();
        t2.join();
        t3.join();
    }
}  // namespace sequentialPrintABC_atomic

namespace sequentialPrintABC_future {
    void printA(std::promise<void>& pA, std::shared_future<void> fPrev) {
        fPrev.wait();
        for (int i = 0; i < 10; ++i) {
            std::cout << "\tfuture_a" << std::endl;
        }
        pA.set_value();
    }

    void printB(std::promise<void>& pB, std::shared_future<void> fPrev) {
        fPrev.wait();
        for (int i = 0; i < 10; ++i) {
            std::cout << "\tfuture_b" << std::endl;
        }
        pB.set_value();
    }

    void printC(std::shared_future<void> fPrev) {
        fPrev.wait();
        for (int i = 0; i < 10; ++i) {
            std::cout << "\tfuture_c" << std::endl;
        }
    }

    void task() {
        std::promise<void> pStart, pA, pB;
        auto fStart = pStart.get_future().share();
        auto fA = pA.get_future().share();
        auto fB = pB.get_future().share();

        std::thread t1(printA, std::ref(pA), fStart);
        std::thread t2(printB, std::ref(pB), fA);
        std::thread t3(printC, fB);

        pStart.set_value();
        t1.join();
        t2.join();
        t3.join();
    }
}  // namespace sequentialPrintABC_future

int main() {
    std::cout << "Testing sequentialPrintABC_cv:" << std::endl;
    measure_time(sequentialPrintABC_cv::task);

    std::cout << "\nTesting sequentialPrintABC_atomic:" << std::endl;
    measure_time(sequentialPrintABC_atomic::task);

    std::cout << "\nTesting sequentialPrintABC_future:" << std::endl;
    measure_time(sequentialPrintABC_future::task);

    return 0;
}