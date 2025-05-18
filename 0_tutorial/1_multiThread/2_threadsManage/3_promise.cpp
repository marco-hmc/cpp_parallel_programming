#include <future>
#include <iostream>
#include <thread>

/*
    1. promise怎么用？讲解一下用法。
        std::promise是C++11引入的一个类模板，它可以用于在一个线程中存储一个值或异常，然后在另一个线程中获取这个值或异常。
        这样可以实现线程间数据的同步。

        std::promise<int> prom;
        std::future<int> fut = prom.get_future();
        prom.set_value(10);
        int x = fut.get();

    2. std::future
        是一个类模板，它表示一个异步操作的结果。你可以调用std::future::get()来获取异步操作的结果。
        如果异步操作还没有完成，std::future::get会阻塞，直到异步操作完成。

    3. promise和future的关系是什么？
        promise用于设置一个值或异常，而future用于获取这个值或异常。两者通常一起使用，用于实现多线程数据通信。
        一般来说，promise在一个线程中设置值，而future在另一个线程中获取这个值。
        而一般业务开发直接用future就可以了，promise一般用于底层实现。
  
    4. future有了get()，为什么还要wait()？
        - `std::future`的`get`函数用于获取异步任务的结果，它会阻塞当前线程，直到异步任务完成并返回结果。
        - `get`函数会等待异步任务完成并返回结果，如果异步任务抛出了异常，那么`get`函数会重新抛出这个异常。
        - 用于有返回函数

        - `std::future`的`wait`函数用于等待异步任务的完成，它会阻塞当前线程，直到异步任务完成。
        - `wait`函数只会等待异步任务完成，不会获取结果，也不会重新抛出异常。
        - 用于无返回函数。
*/

namespace promise_future_basic {

    int calculateSum(int a, int b) { return a + b; }

    void doCalculation(std::promise<int> &promiseObj, int a, int b) {
        int result = calculateSum(a, b);
        promiseObj.set_value(result);
    }

    void task() {
        std::promise<int> promiseObj;
        std::future<int> futureObj = promiseObj.get_future();

        doCalculation(promiseObj, 5, 3);
        int sum = futureObj.get();
        std::cout << "The sum of " << 5 << " and " << 3 << " is: " << sum
                  << std::endl;
    }
}  // namespace promise_future_basic

namespace promise_future_async {

    int add(int a, int b) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        return a + b;
    }

    void task() {
        std::future<int> result = std::async(std::launch::async, add, 3, 5);
        if (result.valid()) {
            int sum = result.get();
            std::cout << "异步操作完成，结果是: " << sum << std::endl;
        } else {
            std::cout << "异步操作无效。" << std::endl;
        }
    }
}  // namespace promise_future_async

namespace promise_future_thread {
    void print_int(std::future<int> &fut) {
        int x = fut.get();
        std::cout << "value: " << x << '\n';
    }

    void task() {
        std::promise<int> prom;
        std::future<int> fut = prom.get_future();
        std::thread th1(print_int, std::ref(fut));
        prom.set_value(10);
        th1.join();
    }

}  // namespace promise_future_thread

namespace future_waitFor {
    /*
    1. std::future::wait_for()怎么用？
        - `std::future`的`wait_for`函数用于等待`std::future`对象的状态变为`ready`，它的参数是一个`std::chrono::duration`对象，表示等待的时间。
        - 如果`std::future`对象的状态在指定的时间内变为`ready`，那么`wait_for`函数会返回`std::future_status::ready`。
        - 如果`std::future`对象的状态在指定的时间内没有变为`ready`，那么`wait_for`函数会返回`std::future_status::timeout`。
        - 如果`std::future`对象的状态在指定的时间内没有变为`ready`，并且在等待的过程中抛出了异常，那么`wait_for`函数会返回`std::future_status::deferred`。
        - `wait_for`函数的返回值是一个`std::future_status`枚举类型的值，表示`std::future`对象的状态。
*/
    bool is_prime(int x) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        for (int i = 2; i < x; ++i) {
            if (x % i == 0) {
                return false;
            }
        }
        return true;
    }

    void test() {
        std::future<bool> fut = std::async(is_prime, 3045348722);
        std::chrono::milliseconds span(10);
        while (fut.wait_for(span) == std::future_status::timeout) {
            std::cout << '.' << std::flush;
        }

        bool x = fut.get();
        std::cout << "\n3 " << (x ? "is" : "is not") << " prime.\n";
    }
}  // namespace future_waitFor

namespace future_waitUntil {

    bool is_prime(int x) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        for (int i = 2; i < x; ++i) {
            if (x % i == 0) {
                return false;
            }
        }
        return true;
    }

    void test() {
        std::future<bool> fut = std::async(is_prime, 3045348722);
        std::cout << "checking, please wait";

        std::chrono::milliseconds span(10);
        auto now = std::chrono::steady_clock::now();
        while (fut.wait_until(now + span) == std::future_status::timeout) {
            std::cout << '.' << std::flush;

            now = std::chrono::steady_clock::now();
        }

        bool x = fut.get();
        std::cout << "\n3045348722 " << (x ? "is" : "is not") << " prime.\n";
    }

}  // namespace future_waitUntil

namespace shared_future {
    /*
    1. `std::shared_future`的用途：
        `std::shared_future`是一个可以被多个线程共享的异步结果。
        它通常用于多个线程需要等待同一个任务完成的情况。
        当你有一个返回值的异步任务，并且你希望多个线程都能获取到这个返回值时，你可以使用`std::shared_future`。

    2. `std::shared_future`和`std::future`的区别：
        主要的区别在于`std::future`只能被移动，而`std::shared_future`可以被复制。
        这意味着一个`std::future`的结果只能被一个线程获取，而一个`std::shared_future`的结果可以被多个线程获取。
        另外，`std::future::get`会移动结果，使得`std::future`变为无效状态，而`std::shared_future::get`则会复制结果，不会影响`std::shared_future`的状态。

    3. `std::shared_future`的生命周期需要注意什么？
        `std::shared_future`的生命周期应该超过所有使用它的线程。
        如果一个线程还在等待`std::shared_future`的结果，而`std::shared_future`已经被销毁，那么这个线程将会遇到未定义的行为。
        因此，你需要确保`std::shared_future`在所有线程都获取到结果之后再销毁。
*/

    int factorial(const std::shared_future<int> &f) {
        int res = 1;
        int N = f.get();
        for (int i = N; i > 1; i--) {
            res *= i;
        }
        std::cout << "Result is: " << res << '\n';

        return res;
    }
    void task() {
        std::promise<int> p;
        std::future<int> f = p.get_future();
        std::shared_future<int> sf = f.share();

        std::future<int> fu = std::async(std::launch::async, factorial, sf);
        std::future<int> fu2 = std::async(std::launch::async, factorial, sf);
        std::future<int> fu3 = std::async(std::launch::async, factorial, sf);

        p.set_value(4);
    }
}  // namespace shared_future

int main() {
    promise_future_basic::task();
    promise_future_async::task();
    promise_future_thread::task();

    future_waitFor::test();
    future_waitUntil::test();
    return 0;
}
