#include <tbb/parallel_for.h>
#include <tbb/task_group.h>

#include <exception>
#include <functional>
#include <iostream>
#include <string>

static std::atomic_int a = 0;
static std::atomic_int b = 0;
static std::atomic_int c = 0;
static std::atomic_int d = 0;

namespace {
    static int i = 0;
    class Guard {
      public:
        Guard(std::function<void()> enter, std::function<void()> exit)
            : exit_(exit) {
            enter();
        }
        ~Guard() { exit_(); }

      private:
        std::function<void()> exit_;
    };
}  // namespace

// extern "C" 的 c（不抛异常）
extern "C" void c_no_throw(int i) {
    Guard g([]() { c++; }, []() { c--; });
    // 外部 C 函数，不抛异常
    (void)i;
}

// function_d：会抛异常（示例条件：v % 7 == 0）
void function_d(int v) {
    if (v % 5 == 0) {
        throw std::runtime_error(std::string("function_d failed for v=") +
                                 std::to_string(v));
    }
    // 否则正常工作（不抛）
}

// function_b：在内部 parallel_for 中调用 c_no_throw 和 function_d。
// 当 function_d 抛出时在 b 内部捕获并收集异常，b 自身不抛出。
void function_b(int base) {
    Guard g([]() { b++; }, []() { b--; });
    tbb::parallel_for(0, 100, [base](int j) {
        c_no_throw(base + j);  // c 不抛异常
        function_d(base + j);  // 可能抛出
    });
}

// function_a：通过 parallel_for 调用 function_b（a -> parallel_for -> b）
void function_a() {
    Guard g([]() { a++; },
            []() {
                a--;
                a--;
            });
    tbb::parallel_for(0, 500, [](int k) { function_b(k * 10); });
}

// 示例：d 会抛出，但 b 已处理异常，外层不应捕获到异常
void example_nested_exception_handled_by_b() {
    function_a();
    std::cout
        << "function_a completed without throwing (b handled exceptions)\n";
}

int main() {
    try {
        example_nested_exception_handled_by_b();
        std::cout << "Final counts: a=" << a.load() << ", b=" << b.load()
                  << ", c=" << c.load() << ", d=" << d.load() << '\n';
    } catch (const std::exception &e) {
        std::cout << "Unexpected: outer caught exception: " << e.what() << '\n';
        std::cout << "Final counts: a=" << a.load() << ", b=" << b.load()
                  << ", c=" << c.load() << ", d=" << d.load() << '\n';
    } catch (...) {
        std::cout << "Unexpected: outer caught non-std exception\n";
    }
    return 0;
}