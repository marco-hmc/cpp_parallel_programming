#include <algorithm>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ============================================================
namespace scheduler_ready_queue {
/*
    1. 协程调度是什么意思？
        协程本身不解决"谁在什么时候执行"的问题，它只提供"挂起/恢复"的能力。
        调度器负责决定：哪些协程该被 resume、按什么顺序、是否分配线程。

    2. 协作式调度 vs 线程抢占式调度
        - 线程：操作系统按时钟中断强制切换（抢占式），程序员控制不了何时切
        - 协程：协程主动 co_await/yield 让出控制权（协作式），调度器在此时介入
        协作式的好处：只有 co_await 点才会切，不需要锁保护临界区（但也不允许某个协程霸占 CPU）。

    3. 这个例子演示什么？
        最简单的 round-robin 调度器：一个 ready 队列，每个协程执行一小段后
        主动 yield（co_await YieldAwaiter），调度器取下一个协程执行。
*/

struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit Task(handle_type h) : coro(h) {}
    Task(Task&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    ~Task() { if (coro) coro.destroy(); }
};

// ---- 调度器 ----
class SimpleScheduler {
  public:
    void add_task(Task t) { ready_queue_.push_back(std::move(t)); }

    void run() {
        while (!ready_queue_.empty()) {
            // 取队首
            Task current = std::move(ready_queue_.front());
            ready_queue_.erase(ready_queue_.begin());

            // 恢复协程
            current.coro.resume();

            // 如果协程还没结束，放回队尾
            if (!current.coro.done()) {
                ready_queue_.push_back(std::move(current));
            }
        }
    }

    // 给协程用的：把当前协程放回队列末尾，然后挂起
    void yield_current(std::coroutine_handle<> h) {
        // 注意：这里简化了。实际调度器需要能根据 h 找到对应的 Task
        // 更完整的实现会把 handle 和 Task 的对应关系存起来
        ready_queue_.push_back(
            Task{std::coroutine_handle<Task::promise_type>::from_address(
                h.address())});
    }

  private:
    std::list<Task> ready_queue_;
};

SimpleScheduler g_scheduler;

// ---- Yield awaiter ----
struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        // 交还给调度器——调度器会在下一轮 resume
        g_scheduler.yield_current(h);
    }
    void await_resume() noexcept {}
};

// ---- 两个协作式任务 ----
Task worker(const std::string& name, int steps) {
    for (int i = 1; i <= steps; ++i) {
        std::cout << "  [" << name << "] 第 " << i << " 步\n";
        co_await YieldAwaiter{};  // 主动让出
    }
}

void task() {
    std::cout << "两个任务轮流执行（round-robin）：\n";
    g_scheduler.add_task(worker("A", 5));
    g_scheduler.add_task(worker("B", 5));
    g_scheduler.run();
    std::cout << "所有任务完成\n";
}
}  // namespace scheduler_ready_queue

// ============================================================
namespace scheduler_timed_sleep {
/*
    1. 定时挂起 —— 模拟 setTimeout
        调度器不只管理 ready 队列，还有一个"延时等待"队列。
        协程请求 sleep(ms)，调度器把它放到延时队列，等到时间了再移回 ready 队列。

    2. 这样做的意义
        真正的异步 I/O 框架（比如 libuv、ASIO）就是这样工作的：
        所有协程在同一个线程里协作调度，I/O 等待期间不占线程资源。
*/

struct Task {
    struct promise_type {
        Task get_return_object() {
            return Task{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit Task(handle_type h) : coro(h) {}
    Task(Task&& other) noexcept : coro(other.coro) { other.coro = nullptr; }
    ~Task() { if (coro) coro.destroy(); }
};

struct DelayedEntry {
    std::chrono::steady_clock::time_point wakeup;
    Task task;
    bool operator<(const DelayedEntry& other) const {
        return wakeup > other.wakeup;  // 最小堆：最早的先出
    }
};

class TimedScheduler {
  public:
    void add_task(Task t) { ready_.push_back(std::move(t)); }

    void schedule_wakeup(Task t, std::chrono::milliseconds delay) {
        delayed_.push_back({std::chrono::steady_clock::now() + delay,
                            std::move(t)});
    }

    void run() {
        while (!ready_.empty() || !delayed_.empty()) {
            // 把到期的延时任务移到 ready 队列
            auto now = std::chrono::steady_clock::now();
            auto it = delayed_.begin();
            while (it != delayed_.end()) {
                if (it->wakeup <= now) {
                    ready_.push_back(std::move(it->task));
                    it = delayed_.erase(it);
                } else {
                    ++it;
                }
            }

            if (!ready_.empty()) {
                Task current = std::move(ready_.front());
                ready_.erase(ready_.begin());
                current.coro.resume();
                if (!current.coro.done()) {
                    ready_.push_back(std::move(current));
                }
            } else if (!delayed_.empty()) {
                // 没有 ready 任务，找最早到期的延时
                auto earliest = std::min_element(
                    delayed_.begin(), delayed_.end(),
                    [](const DelayedEntry& a, const DelayedEntry& b) {
                        return a.wakeup < b.wakeup;
                    });
                std::this_thread::sleep_for(earliest->wakeup - now);
            }
        }
    }

    std::list<Task> ready_;
    std::list<DelayedEntry> delayed_;
};

TimedScheduler g_timed_scheduler;

struct SleepAwaiter {
    std::chrono::milliseconds delay_;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        g_timed_scheduler.schedule_wakeup(
            Task{std::coroutine_handle<Task::promise_type>::from_address(
                h.address())},
            delay_);
    }
    void await_resume() noexcept {}
};

Task timed_worker(const std::string& name, int base_ms) {
    for (int i = 1; i <= 3; ++i) {
        int wait = base_ms + i * 50;
        std::cout << "  [" << name << "] sleep " << wait << "ms\n";
        co_await SleepAwaiter{std::chrono::milliseconds(wait)};
        std::cout << "  [" << name << "] 醒来，完成第 " << i << " 步\n";
    }
}

void task() {
    std::cout << "两个任务以不同间隔 sleep（总耗时 ≈ max，不是 sum）：\n";
    g_timed_scheduler.add_task(timed_worker("快", 100));
    g_timed_scheduler.add_task(timed_worker("慢", 300));
    g_timed_scheduler.run();
    std::cout << "所有任务完成\n";
}
}  // namespace scheduler_timed_sleep

// ============================================================
int main() {
    std::cout << "===== 1. Round-Robin 协作式调度 =====\n";
    scheduler_ready_queue::task();

    std::cout << "\n===== 2. 定时挂起调度器 =====\n";
    scheduler_timed_sleep::task();

    return 0;
}
