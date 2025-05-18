#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

/*
  `CountDownLatch`是一个同步工具，它允许一个或多个线程等待其他线程完成一组操作。
  它在并发编程中非常有用，特别是当你需要在一个线程中等待一个或多个线程完成操作时
  以下是一些`CountDownLatch`的常见用途:
    1. **启动门**:如果你想在启动应用时同时启动多个线程，但是你希望所有线程都等待，直到应用完全准备好再开始执行，你可以使用一个`CountDownLatch`。
    2. **结束门**:如果你在一个线程中启动了多个工作线程，并且你希望等待所有工作线程都完成，你可以使用一个`CountDownLatch`。
    3. **周期栅栏**:如果你在一个循环中，每个循环迭代都需要等待多个线程完成，你可以使用一个`CountDownLatch`。
  在这些情况下，`CountDownLatch`提供了一种简单而灵活的方式来同步线程的行为。

  java直接有这个工具，c++没有。
  启动门可以用于同时执行，避免有偷跑的（有些场景，线程先后创建开销不可忽视）
  结束门可以用于知道什么时候都结束了，不用.join()是因为线程可能复用的。
*/

class CountDownLatch {
  public:
    explicit CountDownLatch(int count) : count_(count) {}

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (count_ > 0) {
            condition_.wait(lock);
        }
    }

    void countDown() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (--count_ == 0) {
            condition_.notify_all();
        }
    }

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    int count_;
};

void worker(CountDownLatch& latch, int id) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Worker " << id << " done." << std::endl;
    latch.countDown();
}

int main() {
    const int numWorkers = 5;
    CountDownLatch latch(numWorkers);

    std::vector<std::thread> threads;
    threads.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        threads.emplace_back(worker, std::ref(latch), i);
    }

    std::cout << "Main thread waiting for workers to finish..." << std::endl;
    latch.wait();
    std::cout << "All workers done. Main thread proceeding." << std::endl;

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}