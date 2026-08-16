#include <algorithm>
#include <functional>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

// ============================================================
// 一个最简单的"任务"抽象：有一个 run()，返回是否已完成
// 不依赖任何协程机制——可以是宏协程、手写状态机、或任何实现了 run()/done() 的东西
// ============================================================

// ---- 宏协程定义（自包含，每个文件自己定义） ----
// 注意：crYield 中 _line_ = __LINE__ 和 case __LINE__ 必须在同一行，
// 否则它们会得到不同的行号，导致恢复时跳不到正确位置。
#define crBegin          \
    switch (_line_) {    \
        case 0:

#define crYield          do { _line_ = __LINE__; return false; case __LINE__:; } while (0)

#define crFinish         \
    }                    \
    _done_ = true;       \
    return true

// ============================================================
namespace scheduler_round_robin {
/*
    1. Round-Robin 调度 —— 最朴素的协作式调度器
        - 每个"任务"只需要实现 run() → 返回 true 表示已完成
        - run() 返回 false 表示还有工作，放回队尾
        - 调度器挨个 run，直到全部完成

    2. 和 C++20 协程调度器的对比
        见 1_cpp20/6_scheduler.cpp。原理完全一样，区别是：
        - 这里用宏协程实现任务
        - C++20 用 co_await + coroutine_handle
*/

// 一个用宏协程实现的简单任务
class MacroTask {
    int _line_ = 0;
    bool _done_ = false;
    std::string name_;
    int steps_;
    int current_;

  public:
    MacroTask(std::string name, int steps)
        : name_(std::move(name)), steps_(steps), current_(0) {}

    bool done() const { return _done_; }
    const std::string& name() const { return name_; }

    // run() 返回 true 表示完成，false 表示还有工作
    bool run() {
        switch (_line_) {
            case 0:;
            for (current_ = 1; current_ <= steps_; ++current_) {
                std::cout << "  [" << name_ << "] 第 " << current_ << " 步\n";
                crYield;  // 执行一步，返回 false（"还有工作"）
            }
            crFinish;  // 全部完成，返回 true
    }
};

void task() {
    std::vector<MacroTask> tasks;
    tasks.emplace_back("A", 3);
    tasks.emplace_back("B", 3);
    tasks.emplace_back("C", 3);

    std::cout << "3 个任务轮流执行（Round-Robin）：\n";

    // 调度循环
    size_t completed = 0;
    while (completed < tasks.size()) {
        for (auto& t : tasks) {
            if (!t.done()) {
                if (t.run()) {
                    ++completed;
                }
            }
        }
        std::cout << "  --- 一轮结束 ---\n";
    }

    std::cout << "全部任务完成\n";
}
}  // namespace scheduler_round_robin

// ============================================================
namespace scheduler_priority {
/*
    1. 优先级调度
        每个任务有一个优先级（数字越大越优先）。
        调度器总是选择当前优先级最高的 ready 任务去执行。

    2. 实现：用 priority_queue
*/

class PriorityTask {
    int _line_ = 0;
    bool _done_ = false;
    std::string name_;
    int priority_;
    int steps_;
    int current_;

  public:
    PriorityTask(std::string name, int priority, int steps)
        : name_(std::move(name)), priority_(priority), steps_(steps), current_(0) {}

    bool done() const { return _done_; }
    int priority() const { return priority_; }
    const std::string& name() const { return name_; }

    bool run() {
        switch (_line_) {
            case 0:;
            for (current_ = 1; current_ <= steps_; ++current_) {
                std::cout << "  [" << name_ << " (优先级 " << priority_ << ")] 第 "
                          << current_ << " 步\n";
                crYield;
            }
            crFinish;
    }
};

void task() {
    std::vector<PriorityTask> tasks;
    tasks.emplace_back("低优先级", 1, 2);
    tasks.emplace_back("高优先级", 10, 3);
    tasks.emplace_back("中优先级", 5, 2);

    std::cout << "优先级调度（高优先级任务优先执行）：\n";

    size_t completed = 0;
    while (completed < tasks.size()) {
        // 找未完成中优先级最高的
        int best_idx = -1;
        int best_prio = -1;
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (!tasks[i].done() && tasks[i].priority() > best_prio) {
                best_prio = tasks[i].priority();
                best_idx = (int)i;
            }
        }
        if (best_idx >= 0 && tasks[best_idx].run()) {
            ++completed;
        }
    }

    std::cout << "全部任务完成\n";
}
}  // namespace scheduler_priority

// ============================================================
namespace scheduler_event_driven {
/*
    1. 事件驱动调度
        任务不主动轮询，而是等待"事件"被触发。
        调度器检查哪些任务的等待条件已满足，只运行那些。

    2. 这模拟了现实中的事件循环：
        - 事件源：计时器到期、网络数据到达、用户输入等
        - 调度器：检查事件 → 运行对应的任务 → 任务 yield → 检查下一个事件
*/

// "事件"的简化模型
enum EventType { NONE, TIMER, DATA_READY };

class EventTask {
    int _line_ = 0;
    bool _done_ = false;
    std::string name_;
    EventType waiting_for_;
    int current_;

  public:
    EventTask(std::string name)
        : name_(std::move(name)), waiting_for_(NONE), current_(0) {}

    bool done() const { return _done_; }
    EventType waiting_for() const { return waiting_for_; }
    const std::string& name() const { return name_; }

    // 外部"触发"事件
    void notify(EventType event) {
        if (waiting_for_ == event) {
            waiting_for_ = NONE;  // 事件被消费
        }
    }

    bool run() {
        switch (_line_) {
            case 0:;
            std::cout << "  [" << name_ << "] 启动，等待 TIMER 事件...\n";
            waiting_for_ = TIMER;
            crYield;  // 挂起，等 TIMER 事件
            waiting_for_ = NONE;

            std::cout << "  [" << name_ << "] TIMER 到了，干活... 等待 DATA_READY 事件...\n";
            waiting_for_ = DATA_READY;
            crYield;  // 挂起，等 DATA_READY 事件
            waiting_for_ = NONE;

            std::cout << "  [" << name_ << "] 数据就绪，完成\n";
            crFinish;
    }
};

void task() {
    EventTask worker("Worker");

    std::cout << "事件驱动调度：\n";
    std::cout << "  main: 启动 worker\n";
    worker.run();  // 执行到第一个 crYield（等待 TIMER）

    std::cout << "  main: worker 在等待 TIMER...\n";
    std::cout << "  main: （模拟过了 1 秒）TIMER 触发！\n";
    worker.notify(TIMER);
    worker.run();  // 继续执行到第二个 crYield（等待 DATA_READY）

    std::cout << "  main: worker 在等待 DATA_READY...\n";
    std::cout << "  main: （模拟数据到达）DATA_READY 触发！\n";
    worker.notify(DATA_READY);
    worker.run();  // 完成

    std::cout << "\n对比：如果不用协程，事件驱动的代码需要把每个步骤" "拆成独立的回调函数，代码会变得支离破碎（\"回调地狱\"）。\n" "协程（哪怕是宏协程）让我们用顺序的代码描述异步流程。\n";
}
}  // namespace scheduler_event_driven

// ============================================================
int main() {
    std::cout << "===== 1. Round-Robin 调度 =====\n";
    scheduler_round_robin::task();

    std::cout << "\n===== 2. 优先级调度 =====\n";
    scheduler_priority::task();

    std::cout << "\n===== 3. 事件驱动调度 =====\n";
    scheduler_event_driven::task();

    return 0;
}
