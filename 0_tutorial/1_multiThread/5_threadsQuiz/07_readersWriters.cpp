#include <condition_variable>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace cpp11 {
    class ReadWriteLock {
        std::mutex mtx;
        std::condition_variable cv;
        int readers = 0;
        bool writer = false;

      public:
        void lockRead() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]() { return !writer; });
            readers++;
        }

        void unlockRead() {
            std::unique_lock<std::mutex> lock(mtx);
            if (--readers == 0) {
                cv.notify_all();
            }
        }

        void lockWrite() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this]() { return !writer && readers == 0; });
            writer = true;
        }

        void unlockWrite() {
            std::unique_lock<std::mutex> lock(mtx);
            writer = false;
            cv.notify_all();
        }
    };

    void reader(int id, ReadWriteLock& rwLock) {
        rwLock.lockRead();
        std::cout << "C++11 Reader " << id << " is reading" << '\n';
        rwLock.unlockRead();
    }

    void writer(int id, ReadWriteLock& rwLock) {
        rwLock.lockWrite();
        std::cout << "C++11 Writer " << id << " is writing" << '\n';
        rwLock.unlockWrite();
    }
}  // namespace cpp11

namespace cpp17 {
    std::shared_mutex rw_mutex;

    void reader(int id) {
        std::shared_lock<std::shared_mutex> lock(rw_mutex);
        std::cout << "C++17 Reader " << id << " is reading" << '\n';
    }

    void writer(int id) {
        std::unique_lock<std::shared_mutex> lock(rw_mutex);
        std::cout << "C++17 Writer " << id << " is writing" << '\n';
    }
}  // namespace cpp17

int main() {
    const int numReaders = 5;
    const int numWriters = 2;

    std::vector<std::thread> threads;

    // C++11 example
    cpp11::ReadWriteLock rwLockC11;
    for (int i = 0; i < numReaders; i++) {
        threads.emplace_back(cpp11::reader, i, std::ref(rwLockC11));
    }
    for (int i = 0; i < numWriters; i++) {
        threads.emplace_back(cpp11::writer, i, std::ref(rwLockC11));
    }

    // C++17 example
    for (int i = 0; i < numReaders; i++) {
        threads.emplace_back(cpp17::reader, i);
    }
    for (int i = 0; i < numWriters; i++) {
        threads.emplace_back(cpp17::writer, i);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    return 0;
}