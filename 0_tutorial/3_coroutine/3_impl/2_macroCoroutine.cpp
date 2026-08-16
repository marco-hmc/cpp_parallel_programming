#include <iostream>
#include <string>

// ============================================================
// 宏协程 —— 用 C 预处理器实现"可暂停的函数"
//
// 原理：利用 switch 语句的一个鲜为人知的特性——
// 在 switch 块内可以放 case 标签到任何子作用域中。
// 只要整个 switch 在一个大括号里，case 标签就可以穿插在循环、if 语句之间。
//
// 本质：switch(_line) 根据"执行到哪行"来跳转到对应的 case，
// 因为 __LINE__ 在每行都是唯一的，所以每个 crYield 产生唯一的 case 标签。
//
// 这是1983年 Duff's device 的技巧，2000年 Simon Tatham 将其用于实现协程。
// C++20 编译器做的事情在概念上与此完全相同——只是用结构化方式实现。
// ============================================================

// ---- 宏定义 ----
// 注意：crYield 中 _line_ = __LINE__ 和 case __LINE__ 必须在同一行，
// 否则它们会得到不同的行号，导致恢复时跳不到正确位置。
#define crBegin          \
    switch (_line_) {    \
        case 0:

#define crYield(x)       do { _line_ = __LINE__; return (x); case __LINE__:; } while (0)

#define crFinish         \
    }                    \
    _done_ = true;       \
    return 0

// ============================================================
namespace macro_coro_basic {
/*
    1. 宏协程的最简示例
        用 crBegin/crYield/crFinish 实现一个生成 1,2,3 的协程。
        看代码就像在写普通函数，但每次调用 next() 会从上次 return 的地方继续。

    2. __LINE__ 是什么？
        C/C++ 预处理器宏，展开为当前行号（整数）。每一行代码都有唯一行号。
        编译器在 case 标签中看到常量整数，生成跳转表。

    3. 调用方式
        int v = demo_coro(); // 调用一次，执行到下一个 crYield
        反复调用直到 _done_ 为 true
*/

int macro_coro_counter() {
    static int _line_ = 0;
    static bool _done_ = false;
    if (_done_) return -1;

    crBegin;
    crYield(1);
    crYield(2);
    crYield(3);
    crFinish;
}

void task() {
    std::cout << "宏协程产出：\n  ";
    int v;
    while ((v = macro_coro_counter()) != -1) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}
}  // namespace macro_coro_basic

// ============================================================
namespace macro_coro_fibonacci {
/*
    1. 带循环的宏协程
        因为 case 标签可以放在循环里，crYield 可以出现在 for 循环中。
        这就是编译器实现 co_yield 在 for 循环里的底层等价物。

    2. 局限性
        不能在同一个 crYield 的同一行放两个 yield（__LINE__ 相同导致重复 case）。
        但实际不会把两个 yield 写在同一行。
*/

int fibonacci(int max_count) {
    static int _line_ = 0;
    static bool _done_ = false;
    static long long a, b;
    static int count, max_n;

    if (_done_) {
        // 重置状态以便复用
        _line_ = 0;
        _done_ = false;
        return -1;
    }

    crBegin;
    max_n = max_count;
    a = 0;
    b = 1;
    for (count = 0; count < max_n; ++count) {
        crYield(a);
        long long next = a + b;
        a = b;
        b = next;
    }
    crFinish;
}

void task() {
    std::cout << "宏协程斐波那契（前 12 项）：\n  ";
    int v;
    while ((v = fibonacci(12)) != -1) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}
}  // namespace macro_coro_fibonacci

// ============================================================
namespace macro_coro_class {
/*
    1. 把宏协程封装成类
        上面的写法用 static 局部变量存状态，不够优雅且不能实例化多个。
        把 _line_ 和局部变量变成类成员，就可以创建多个独立的协程实例。
*/
class MacroGenerator {
    int _line_ = 0;
    bool _done_ = false;

    // "局部变量"都变成成员
    int i_, n_;
    int current_val_;

  public:
    MacroGenerator(int n) : n_(n) {}

    bool done() const { return _done_; }

    int next() {
        switch (_line_) {
            case 0:;
            i_ = 1;
            for (; i_ <= n_; ++i_) {
                current_val_ = i_;
                _line_ = __LINE__; return current_val_; case __LINE__:;
            }
            _done_ = true;
            return 0;
        }
        return 0;
    }
};

/*
    等效 C++20：
    Generator<int> range(int n) {
        for (int i = 1; i <= n; ++i) {
            co_yield i;
        }
    }
*/

void task() {
    std::cout << "类封装宏协程 range(1,5)：\n  ";
    MacroGenerator gen(5);
    while (!gen.done()) {
        std::cout << gen.next() << " ";
    }
    std::cout << "\n";
}
}  // namespace macro_coro_class

// ============================================================
namespace macro_coro_limits {
/*
    1. 宏协程的局限性（也是 C++20 协程为什么需要编译器支持的原因）
        a) 不能声明非静态局部变量（必须手动提升为成员或 static）
        b) 不能跨函数——goto/case 不能跳出当前函数
        c) 不能在同一个 crYield 中写两个 yield（重复 case 标签）
        d) 不能和局部变量的析构函数配合（跳过构造/析构 = RAII 失效）
        e) 没有异常安全——yield 时栈不会被正确展开
        f) __LINE__ 在同一行只能出现一次

    2. C++20 协程解决了这些问题
        - 自动把局部变量提升到协程帧（堆上）
        - 正确处理析构和异常展开
        - 允许嵌套（co_await 子协程）
        - 不需要宏，纯语言特性
*/

void task() {
    std::cout << "宏协程是理解编译器实现的关键垫脚石，但实际项目请使用 C++20 协程。\n";
    std::cout << "宏协程的局限：\n";
    std::cout << "  - 不支持 RAII / 异常安全\n";
    std::cout << "  - 非静态局部变量需手动提升到成员\n";
    std::cout << "  - 不能跨函数\n";
    std::cout << "  - __LINE__ 在一行只能出现一次\n";
}
}  // namespace macro_coro_limits

// ============================================================
int main() {
    std::cout << "===== 1. 最简宏协程 =====\n";
    macro_coro_basic::task();

    std::cout << "\n===== 2. 宏协程 + for 循环 =====\n";
    macro_coro_fibonacci::task();

    std::cout << "\n===== 3. 类封装的宏协程（可实例化多个）=====\n";
    macro_coro_class::task();

    std::cout << "\n===== 4. 宏协程的局限性 =====\n";
    macro_coro_limits::task();

    return 0;
}
