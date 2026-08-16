#include <cctype>
#include <coroutine>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// 共用 Generator<T> (复用 4_generator.cpp 的设计)
template <typename T>
struct Generator {
    struct promise_type {
        T current_value_;
        Generator get_return_object() {
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T v) {
            current_value_ = v;
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;
    explicit Generator(handle_type h) : coro(h) {}
    Generator(Generator&& other) noexcept : coro(other.coro) {
        other.coro = nullptr;
    }
    ~Generator() { if (coro) coro.destroy(); }

    struct iterator {
        handle_type coro;
        bool done;
        iterator& operator++() {
            coro.resume();
            done = coro.done();
            return *this;
        }
        T operator*() const { return coro.promise().current_value_; }
        bool operator!=(const iterator& other) const {
            return done != other.done;
        }
    };
    iterator begin() {
        if (coro) {
            coro.resume();
            if (coro.done()) return {nullptr, true};
            return {coro, false};
        }
        return {nullptr, true};
    }
    iterator end() { return {nullptr, true}; }
};

// ============================================================
namespace quiz_state_machine {
/*
    1. 用协程实现文本解析状态机
        传统状态机需要写 switch(state) 和大量的 case 分支。
        协程把"状态"编码在挂起点位置——代码跑到哪，状态就在哪。
        比如解析 "k=v;k=v" 格式，key 和 value 之间的分号就是状态切换点。

    2. 协程做状态机的好处
        - 状态隐式存在代码位置上，不需要显式的 state 变量
        - 流程看起来和同步代码一样清晰
*/

struct KVPair {
    std::string key;
    std::string value;
};

Generator<KVPair> parse_kv_pairs(const std::string& input) {
    std::string current_key;
    std::string current_value;
    bool in_value = false;

    for (char c : input) {
        if (c == '=') {
            in_value = true;
            continue;
        }
        if (c == ';') {
            co_yield KVPair{current_key, current_value};
            current_key.clear();
            current_value.clear();
            in_value = false;
            continue;
        }
        if (!in_value) {
            current_key += c;
        } else {
            current_value += c;
        }
    }
    // 最后一段（没有尾随分号的情况）
    if (!current_key.empty()) {
        co_yield KVPair{current_key, current_value};
    }
}

void task() {
    std::string input = "name=Alice;age=30;city=NYC";
    std::cout << "解析 \"" << input << "\"：\n";
    for (auto kv : parse_kv_pairs(input)) {
        std::cout << "  " << kv.key << " = " << kv.value << "\n";
    }
}
}  // namespace quiz_state_machine

// ============================================================
namespace quiz_producer_consumer {
/*
    1. 协作式生产者-消费者（无锁！）
        生产者协程产出数据，消费者协程消费数据。因为是单线程协作式调度，
        不存在多个协程同时访问队列的问题——不需要锁。

    2. 和线程版的区别
        线程版（见 5_threadsQuiz/01_producerConsumer.cpp）需要 mutex + condition_variable。
        协程版用 yield 传递控制权，天然互斥。

    3. 注意
        协作式 + 无限队列 = 简单。有限队列（backpressure）需要额外的协程同步机制。
*/

std::queue<int> shared_queue;

Generator<int> producer(int count) {
    for (int i = 1; i <= count; ++i) {
        std::cout << "  [生产] " << i << "\n";
        shared_queue.push(i);
        co_yield i;  // 让出控制权给消费者
    }
}

Generator<int> consumer() {
    while (true) {
        if (!shared_queue.empty()) {
            int v = shared_queue.front();
            shared_queue.pop();
            std::cout << "  [消费] " << v << "\n";
            co_yield v;
        } else {
            // 队列空了，让出控制权等生产者
            co_yield -1;
        }
    }
    // unreachable, Generator does not need co_return
}

void task() {
    std::cout << "生产者产 5 个，消费者挨个消费：\n";
    auto prod = producer(5);
    auto cons = consumer();

    // 手动交替驱动两个协程（模拟调度器）
    auto pi = prod.begin();
    auto ci = cons.begin();

    while (pi != prod.end() || !shared_queue.empty()) {
        if (shared_queue.empty() && pi != prod.end()) {
            ++pi;  // 生产一个
        } else {
            ++ci;  // 消费一个
        }
    }
    std::cout << "队列已空，生产者已完成\n";
}
}  // namespace quiz_producer_consumer

// ============================================================
namespace quiz_round_robin {
/*
    1. 三个协程交替打印 A/B/C —— 线程版 vs 协程版
        线程版（02_printOrder.cpp）：需要 mutex + condition_variable + 共享 state
        协程版：用 yield 自然交替

    2. 协作式的优势
        不需要同步原语。代码意图——"我先打 A，然后你打 B，然后他打 C"——
        直接写在执行流程里。
*/

Generator<char> print_char(char c, int count) {
    for (int i = 0; i < count; ++i) {
        std::cout << c;
        if (i < count - 1) {
            co_yield c;
        }
    }
}

void task() {
    std::cout << "协作式交替打印（A→B→C→A→B→C→...）：\n  ";

    auto a = print_char('A', 5);
    auto b = print_char('B', 5);
    auto c = print_char('C', 5);

    auto ai = a.begin();
    auto bi = b.begin();
    auto ci = c.begin();

    for (int round = 0; round < 5; ++round) {
        if (ai != a.end()) ++ai;
        if (bi != b.end()) ++bi;
        if (ci != c.end()) ++ci;
    }
    std::cout << "\n";
}
}  // namespace quiz_round_robin

// ============================================================
namespace quiz_pipeline {
/*
    1. 词频统计管道
        模拟：raw text → tokenize（分词）→ lowercase（小写化）→ count（统计）
        每一步都是一个 Generator，数据从上一步流到下一步。

    2. 管道模式的好处
        - 每步独立测试
        - 内存恒定（不存储中间结果全集）
        - 可以轻松插入新步骤（stop words 过滤、stemming 等）
*/

Generator<std::string> tokenize(const std::string& text) {
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        co_yield word;
    }
}

Generator<std::string> to_lower(Generator<std::string> source) {
    for (auto word : source) {
        for (auto& c : word) c = std::tolower(c);
        co_yield word;
    }
}

Generator<std::string> remove_punct(Generator<std::string> source) {
    for (auto word : source) {
        std::string cleaned;
        for (char c : word) {
            if (!std::ispunct(c)) cleaned += c;
        }
        if (!cleaned.empty()) co_yield cleaned;
    }
}

void task() {
    std::string text = "C++ Coroutines are NOT just for I/O. They are for ANY async workflow!";

    std::cout << "原文：\n  " << text << "\n\n";

    auto tokens = tokenize(text);
    auto lower = to_lower(std::move(tokens));
    auto cleaned = remove_punct(std::move(lower));

    std::cout << "管道处理结果（tokenize → lowercase → remove_punct）：\n  ";
    for (auto w : cleaned) {
        std::cout << "[" << w << "] ";
    }
    std::cout << "\n";
}

/*
    课后练习：
    1. 实现 stop_words 过滤：过滤掉 "the", "is", "are", "a", "an" 等常见词
    2. 实现词频统计：把词和出现次数输出为结构体
    3. 实现简单的指数退避重试：网络请求失败后等待 100ms/200ms/400ms... 重试
*/
}  // namespace quiz_pipeline

// ============================================================
int main() {
    std::cout << "===== 1. 状态机：解析 k=v 对 =====\n";
    quiz_state_machine::task();

    std::cout << "\n===== 2. 生产者-消费者（协作式无锁）=====\n";
    quiz_producer_consumer::task();

    std::cout << "\n===== 3. 三协程交替打印 A/B/C =====\n";
    quiz_round_robin::task();

    std::cout << "\n===== 4. 词频统计管道 =====\n";
    quiz_pipeline::task();

    return 0;
}
