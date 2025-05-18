#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace cv_wait {
    /*
    1. wait的概念是什么？有什么陷阱？
        - wait的概念：
            `std::condition_variable`的`wait`函数用于等待条件变量的通知。
        - 陷阱：
            `std::condition_variable`的`wait`函数需要传入一个`std::unique_lock`对象，这个对象会在`wait`函数内部解锁互斥锁，然后等待条件变量的通知。
            但是，如果在`wait`函数内部等待的过程中，有其他线程修改了条件变量，那么`wait`函数会返回，但是这个时候条件可能已经不满足了。
            所以，`wait`函数需要和`std::unique_lock`一起使用，用于在等待条件变量的过程中解锁互斥锁，等到条件满足后再重新加锁。
        - 使用：
            先上锁，然后调用`wait`函数，`wait`函数会解锁互斥锁，然后等待条件变量的通知，当条件满足时，`wait`函数会重新加锁，然后返回。

    2. wait函数一般不与std::lock_guard使用，因为这个不能手动再次解锁。
        - `std::lock_guard`是一个RAII类型的锁管理器，它在构造时自动锁定互斥锁，在析构时自动解锁互斥锁。
        - 但是，`std::condition_variable`的`wait`函数需要传入一个`std::unique_lock`对象，这个对象会在`wait`函数内部解锁互斥锁，然后等待条件变量的通知。
        - 所以，`std::condition_variable`的`wait`函数不能和`std::lock_guard`一起使用，因为它不能手动再次解锁互斥锁。
    */

    std::condition_variable cv;
    std::mutex cv_m;
    int i = 0;

    void waits() {
        std::unique_lock<std::mutex> lk(cv_m);
        std::cerr << "Waiting... \n";
        cv.wait(lk, [] { return i == 1; });
        std::cerr << "...finished waiting. i == 1\n";
    }

    void signals() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::lock_guard<std::mutex> lk(cv_m);
            std::cerr << "Notifying...\n";
        }
        cv.notify_all();

        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::lock_guard<std::mutex> lk(cv_m);
            i = 1;
            std::cerr << "Notifying again...\n";
        }
        cv.notify_all();
    }

    void task() {
        std::thread t1(waits);
        std::thread t2(waits);
        std::thread t3(waits);
        std::thread t4(signals);

        t1.join();
        t2.join();
        t3.join();
        t4.join();
    }
}  // namespace cv_wait

namespace cv_waitFor {
    /*
    1. wait_for怎么用？和wait相比的区别是什么？
        - wait_for的概念：
            `std::condition_variable`的`wait_for`函数用于等待条件变量的通知，但是可以设置一个超时时间。
        - 区别：
            `std::condition_variable`的`wait_for`函数和`wait`函数的区别在于，`wait_for`函数可以设置一个超时时间，如果超时时间到了，那么`wait_for`函数会返回。
        - 使用：
            先上锁，然后调用`wait_for`函数，`wait_for`函数会解锁互斥锁，然后等待条件变量的通知，如果超时时间到了，那么`wait_for`函数会返回false。
    */
    std::condition_variable cv;
    std::mutex cv_m;
    int i;

    void waits(int idx) {
        std::unique_lock<std::mutex> lk(cv_m);
        if (cv.wait_for(lk, idx * std::chrono::milliseconds(100),
                        [] { return i == 1; })) {
            std::cerr << "Thread " << idx << " finished waiting. i == " << i
                      << '\n';
        } else {
            std::cerr << "Thread " << idx << " timed out. i == " << i << '\n';
        }
    }

    void signals() {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        std::cerr << "Notifying...\n";
        cv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            std::lock_guard<std::mutex> lk(cv_m);
            i = 1;
        }
        std::cerr << "Notifying again...\n";
        cv.notify_all();
    }

    void task() {
        std::thread t1(waits, 1);
        std::thread t2(waits, 2);
        std::thread t3(waits, 3);
        std::thread t4(signals);
        t1.join(), t2.join(), t3.join(), t4.join();
    }
}  // namespace cv_waitFor

namespace NotifyAllAtThreadExit {
    /*
        1. `std::notify_all_at_thread_exit`的用途：
            `std::notify_all_at_thread_exit`用于在当前线程退出时通知所有等待的线程。
            它可以用于在当前线程退出时自动通知所有等待的线程，而不需要手动调用`notify_all()`。
        2. `std::notify_all_at_thread_exit`的参数：
            - `std::condition_variable& cv`: 要通知的条件变量。
            - `std::unique_lock<std::mutex>&& lock`: 要锁定的互斥锁。
        3. `std::notify_all_at_thread_exit`的使用场景：
            - 当一个线程需要在退出时通知所有等待的线程时，可以使用`std::notify_all_at_thread_exit`。
            - 例如，当一个线程需要在退出时释放资源或者通知其他线程时，可以使用`std::notify_all_at_thread_exit`。
    */
    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;

    void print_id(int id) {
        std::unique_lock<std::mutex> lck(mtx);
        while (!ready) {
            cv.wait(lck);
        }

        std::cout << "thread " << id << '\n';
    }

    void go() {
        std::unique_lock<std::mutex> lck(mtx);
        std::notify_all_at_thread_exit(cv, std::move(lck));
        ready = true;
    }

    void task() {
        std::vector<std::thread> threads;
        threads.reserve(10);
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back(print_id, i);
        }
        std::cout << "10 threads ready to race...\n";

        std::thread(go).detach();
        for (auto &th : threads) {
            th.join();
        }
    }
}  // namespace NotifyAllAtThreadExit
int main() {
    cv_wait::task();
    cv_waitFor::task();
    return 0;
}
