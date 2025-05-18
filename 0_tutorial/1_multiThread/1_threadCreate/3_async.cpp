#include <future>
#include <iostream>
#include <thread>

/*
    p.s: 请先看future_promise部分，再看async

    1. std::async
      是一个函数模板，它可以启动一个异步任务，并返回一个std::future对象。
      这个std::future对象可以用于获取异步任务的结果。

    2. std::async的std::launch参数是什么意思?
      `std::async`是C++中的一个函数，用于异步执行任务。它的`std::launch`参数决定了任务的执行策略。这个参数可以有两个值：
        1.`std::launch::async`：
            这个值表示任务将在一个新的线程上异步执行。这意味着任务的执行可能会立即开始。

        2.`std::launch::deferred`：
            这个值表示任务的执行将被延迟，直到你调用了返回的`std::future`对象的`get()`或`wait()`方法。
            这意味着任务的执行可能会被延迟，直到你真正需要任务的结果。

    如果你没有提供`std::launch`参数，那么`std::async`的默认行为是`std::launch::async|std::launch::deferred`
    这意味着它可能选择任何一种策略。具体选择哪种策略取决于库的实现。
*/

namespace async_withReturn {
    int factorial(int N) {
        int res = 1;
        for (int i = N; i > 1; i--) {
            res *= i;
        }
        std::cout << "Result is: " << res << '\n';
        return res;
    }

    void task() {
        std::future<int> fu = std::async(std::launch::deferred, factorial, 4);
        int x = fu.get();
        x;
    }
}  // namespace async_withReturn

namespace async_no_return {
    void print_ten(char c, int ms) {
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            std::cout << c;
        }
    }

    void task() {
        std::cout << "with launch::async:\n";
        std::future<void> foo =
            std::async(std::launch::async, print_ten, '*', 100);
        std::future<void> bar =
            std::async(std::launch::async, print_ten, '@', 200);
        foo.get();
        bar.get();
        std::cout << "\n\n";

        std::cout << "with launch::deferred:\n";
        foo = std::async(std::launch::deferred, print_ten, '*', 100);
        bar = std::async(std::launch::deferred, print_ten, '@', 200);
        foo.get();
        bar.get();
        std::cout << '\n';
    }

}  // namespace async_no_return

int main() {
    async_withReturn::task();
    async_no_return::task();
    return 0;
}
