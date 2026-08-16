#include <iostream>
#include <string>
#include <vector>

// ============================================================
namespace manual_sm_basic {
/*
    1. 编译器在背后做了什么？
        C++20 的 co_yield/co_await 看起来是"魔法"，但编译器做的事很朴实：
        - 把协程函数变成一个结构体（这就是"协程帧"）
        - 局部变量变成结构体的成员变量
        - 每个 co_yield 点变成一个 case 标签
        - resume() 就是执行 switch(state) 跳到对应的 case

    2. 这个例子演示什么？
        手写一个最简单的生成器（1→2→3），展示编译器协程转换的本质。
        理解了这个，你就理解了 C++20 协程的底层实现。
*/

// 手写的"协程帧"——编译器生成的等价物
struct SimpleGenerator {
    // ---- 局部变量变成成员 ----
    int i;          // for 循环的 i
    int max_val;    // 参数 n
    int result;     // 当前产出的值

    // ---- 状态：记录"执行到哪了" ----
    enum State { START = 0, YIELD_1, YIELD_2, DONE };
    State state = START;

    // ---- 构造函数（相当于协程函数的开头） ----
    SimpleGenerator(int n) : max_val(n) {}

    // ---- resume() = 协程的 coroutine_handle::resume() ----
    // 每次调用执行到下一个 co_yield（这里用 return true 表示）
    bool next() {
        switch (state) {
            case START:
                // for (i = 1; i <= max_val; ++i) 的第一轮
                i = 1;
                if (i > max_val) { state = DONE; return false; }
                result = i;
                state = YIELD_1;  // 标记"下次从哪里继续"
                return true;      // 有值可拿

            case YIELD_1:
                // ++i，然后检查条件
                i = 2;
                if (i > max_val) { state = DONE; return false; }
                result = i;
                state = YIELD_2;
                return true;

            case YIELD_2:
                // ++i，然后检查条件
                i = 3;
                if (i > max_val) { state = DONE; return false; }
                result = i;
                state = DONE;  // 不会有下一个了
                return true;

            case DONE:
                return false;
        }
        return false;
    }

    int current() const { return result; }
};

/*
    这段代码的等效 C++20 协程：
    Generator<int> count_to(int n) {
        for (int i = 1; i <= n; ++i) {
            co_yield i;
        }
    }
    编译器会把上面的协程转换为和 SimpleGenerator 等价的代码。
    不同的是编译器生成的版本能处理任意 n（通过循环展开 + 状态机优化），
    我们这个手写版本只能硬编码每个挂起点。
*/

void task() {
    std::cout << "手写状态机生成器（1 到 3）：\n  ";
    SimpleGenerator gen(3);
    while (gen.next()) {
        std::cout << gen.current() << " ";
    }
    std::cout << "\n";
}
}  // namespace manual_sm_basic

// ============================================================
namespace manual_sm_generator {
/*
    1. 通用手写生成器 —— 用 goto 模拟任意循环的挂起
        switch+case 只能处理固定数量的挂起点。
        要处理任意长度的循环，有两种办法：
        - 用 Duff's device（下个文件会讲）
        - 把"循环变量"存在成员里，每次 next() 模拟一轮迭代

    2. 这个例子用"一轮一轮跑"的方式模拟通用的 for 循环生成器。
       虽然不如编译器生成的 efficient（恢复时直接 jump 到 case），
       但它展示了"局部变量 → 成员"和"指令指针 → state 枚举"的映射。
*/

class FibonacciGen {
    // 局部变量存为成员
    long long a_ = 0, b_ = 1;
    long long current_ = 0;
    int count_;
    int max_;
    bool first_ = true;

  public:
    FibonacciGen(int max) : max_(max), count_(0) {}

    bool next() {
        if (count_ >= max_) return false;

        if (first_) {
            current_ = 0;
            first_ = false;
        } else {
            current_ = a_;
            auto next = a_ + b_;
            a_ = b_;
            b_ = next;
        }
        ++count_;
        return true;
    }

    long long current() const { return current_; }
};

/*
    等效 C++20：
    Generator<long long> fibonacci(int max) {
        long long a = 0, b = 1;
        for (int i = 0; i < max; ++i) {
            co_yield a;
            auto next = a + b;
            a = b;
            b = next;
        }
    }
*/

void task() {
    std::cout << "手写斐波那契生成器（前 12 项）：\n  ";
    FibonacciGen fib(12);
    while (fib.next()) {
        std::cout << fib.current() << " ";
    }
    std::cout << "\n";
}
}  // namespace manual_sm_generator

// ============================================================
namespace manual_sm_pipeline {
/*
    1. 手写管道 —— 两个手写生成器串联
        和 C++20 协程管道的模式一样：上游产数据 → 下游处理 → 输出。
*/

// 上游：生产 2 的幂次
class PowersOfTwo {
    int current_ = 1;
    int count_;
    int max_;
  public:
    PowersOfTwo(int max) : max_(max), count_(0) {}
    bool next() {
        if (count_ >= max_) return false;
        int result = current_;
        current_ *= 2;
        ++count_;
        return true;
    }
    int current() const { return current_ / 2; /* 返回上一个值 */ }
};

// 下游：只保留能被 16 整除的
class DivisibleBy16 {
    PowersOfTwo& upstream_;
    int current_val_ = 0;
    bool has_value_ = false;
  public:
    DivisibleBy16(PowersOfTwo& upstream) : upstream_(upstream) {}
    bool next() {
        while (upstream_.next()) {
            int v = upstream_.current();
            if (v >= 16 && v % 16 == 0) {
                current_val_ = v;
                has_value_ = true;
                return true;
            }
        }
        has_value_ = false;
        return false;
    }
    int current() const { return current_val_; }
};

void task() {
    PowersOfTwo pow2(10);  // 1, 2, 4, 8, 16, 32, 64, 128, 256, 512
    DivisibleBy16 filtered(pow2);

    std::cout << "管道：2 的幂次 → 能被 16 整除：\n  ";
    while (filtered.next()) {
        std::cout << filtered.current() << " ";
    }
    std::cout << "\n";
}
}  // namespace manual_sm_pipeline

// ============================================================
int main() {
    std::cout << "===== 1. 手写 switch 状态机（编译器转换的核心）=====\n";
    manual_sm_basic::task();

    std::cout << "\n===== 2. 通用手写生成器（局部变量 → 成员）=====\n";
    manual_sm_generator::task();

    std::cout << "\n===== 3. 手写管道（流式处理）=====\n";
    manual_sm_pipeline::task();

    return 0;
}
