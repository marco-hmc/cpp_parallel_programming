#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace directCnt {
    constexpr int runSeconds = 3;
    void increment(int &value, std::atomic<bool> &stop_flag) {
        while (!stop_flag.load()) {
            ++value;
        }
    }

    void task() {
        std::atomic<bool> stop_flag(false);
        int i = 0;
        std::thread t(increment, std::ref(i), std::ref(stop_flag));
        std::this_thread::sleep_for(std::chrono::seconds(runSeconds));
        stop_flag.store(true);
        t.join();
        std::cout << "Direct case: i = " << i / 1e8 << std::endl;
    }
}  // namespace directCnt

namespace MultiThread_with_Padded {
    struct PaddedInt {
        int value;
        char padding[64 - sizeof(int)];
    };

    constexpr int runSeconds = 3;

    void increment(PaddedInt &paddedInt, std::atomic<bool> &stop_flag) {
        while (!stop_flag.load()) {
            ++paddedInt.value;
        }
    }

    void countWithin3Sec() {
        PaddedInt i1 = {0}, i2 = {0}, i3 = {0}, i4 = {0};
        std::atomic<bool> stop_flag(false);

        std::thread t1(increment, std::ref(i1), std::ref(stop_flag));
        std::thread t2(increment, std::ref(i2), std::ref(stop_flag));
        std::thread t3(increment, std::ref(i3), std::ref(stop_flag));
        std::thread t4(increment, std::ref(i4), std::ref(stop_flag));

        std::this_thread::sleep_for(std::chrono::seconds(runSeconds));
        stop_flag.store(true);

        t1.join();
        t2.join();
        t3.join();
        t4.join();

        std::cout << "Padded case: i1 = " << i1.value / 1e8
                  << ", i2 = " << i2.value / 1e8 << ", i3 = " << i3.value / 1e8
                  << ", i4 = " << i4.value / 1e8 << std::endl;
    }

}  // namespace MultiThread_with_Padded

namespace MultiThread_no_Padded {

    constexpr int runSeconds = 3;
    void increment(int &i, std::atomic<bool> &stop_flag) {
        while (!stop_flag.load()) {
            ++i;
        }
    }

    void countWithin3Sec() {
        std::vector<int> data(4, 0);
        std::atomic<bool> stop_flag(false);

        std::thread t1(increment, std::ref(data[0]), std::ref(stop_flag));
        std::thread t2(increment, std::ref(data[1]), std::ref(stop_flag));
        std::thread t3(increment, std::ref(data[2]), std::ref(stop_flag));
        std::thread t4(increment, std::ref(data[3]), std::ref(stop_flag));

        std::this_thread::sleep_for(std::chrono::seconds(runSeconds));
        stop_flag.store(true);

        t1.join();
        t2.join();
        t3.join();
        t4.join();

        std::cout << "Pingpong Cache case B: data[0] = " << data[0] / 1e8
                  << ", data[1] = " << data[1] / 1e8
                  << ", data[2] = " << data[2] / 1e8
                  << ", data[3] = " << data[3] / 1e8 << std::endl;
    }

}  // namespace MultiThread_no_Padded

int main() {
    directCnt::task();
    MultiThread_with_Padded::countWithin3Sec();
    MultiThread_no_Padded::countWithin3Sec();
    return 0;
}