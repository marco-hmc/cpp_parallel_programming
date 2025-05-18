#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace args_is_override_func {
    /*
    1. 参数是同名函数，会怎样？
        使用静态类型转换来明确调用没有参数的function_1版本d
        如果不进行类型转换，编译器无法确定应该调用哪个function_1版本
        因为thread接受function_1是作为参数的，后面补上参数也不知道用哪个
    */

    void function_1() { std::cout << "null" << '\n'; }
    void function_1(int i) { std::cout << i << '\n'; }

    void task() {
        {
            // wrong
            // std::thread t1(function_1);
            // std::thread t2(function_1, 2);
            // t1.join();
            // t2.join();
        }
        {
            std::thread t1(static_cast<void (*)(int)>(function_1), 2);
            t1.join();
        }
        {
            std::thread t1(static_cast<void (*)()>(function_1));
            t1.join();
        }
    }
}  // namespace args_is_override_func

namespace args_is_member_func {

    /*
    1.  参数是成员函数，会怎样？
        线程传入的函数是成员函数时，需要传入对象指针和成员函数指针。
        其实成员函数和普通函数没有什么不同的，只是成员函数会有一个默认的`this`指针，指向调用它的对象。所以补上这个就好了。
    */
    class Foo {
        std::mutex mtx1, mtx2;
        std::unique_lock<std::mutex> lock_1, lock_2;

      public:
        Foo()
            : lock_1(mtx1, std::try_to_lock), lock_2(mtx2, std::try_to_lock) {}

        void doFirst(const std::function<void()> &printFirst) {
            printFirst();
            lock_1.unlock();
        }
        void doSecond(const std::function<void()> &printSecond) {
            std::lock_guard<std::mutex> guard(mtx1);
            printSecond();
            lock_2.unlock();
        }
        void doThird(const std::function<void()> &printThird) {
            std::lock_guard<std::mutex> guard(mtx2);
            printThird();
        }
    };

    void printFirst() {
        for (int i = 0; i < 5; i++) {
            std::cout << "first" << '\n';
        }
    }
    void printSecond() {
        for (int i = 0; i < 5; i++) {
            std::cout << "second" << '\n';
        }
    }
    void printThird() {
        for (int i = 0; i < 5; i++) {
            std::cout << "third" << '\n';
        }
    }

    void task() {
        Foo f;
        std::thread t1(&Foo::doFirst, &f, printFirst);
        std::thread t2(&Foo::doSecond, &f, printSecond);
        std::thread t3(&Foo::doThird, &f, printThird);
        t1.join();
        t2.join();
        t3.join();
    }
}  // namespace args_is_member_func

namespace args_is_ref {
    void modifyValue(int &num) {
        num *= 2;
        std::cout << "Modified value in thread: " << num << std::endl;
    }

    void task() {
        int value = 10;
        std::thread t(modifyValue, std::ref(value));
        // std::thread t(modifyValue, value); // 运行会出错
        t.join();
    }
}  // namespace args_is_ref

namespace ctor_by_move {
    /*
    1. 线程是可以移动的。
      线程是一种进入系统内核创建出来的一种系统资源，是有复用意义的。  
      而`std::thread`只是管理线程资源的对象而已，未绑定函数的时候是没有线程资源的。
      哪怕线程正在运行的时候，`std::thread`也是可以移动的。
      因为移动的是`std::thread`对象，而不是线程资源本身。
    */

    void task1() { std::cout << "Executing Task 1" << '\n'; }
    void task2() { std::cout << "Executing Task 2" << '\n'; }

    void task() {
        std::thread t1(task1);
        if (false) {
            std::thread t2 = std::move(t1);
            // std::thread t2 = t1; // compile error, not allowed to copy
            t2.join();
        } else {
            t1 = std::thread(task2);  // 重新赋值,接管新线程的控制权
            t1.join();
        }
    }
}  // namespace ctor_by_move

int main() {
    args_is_member_func::task();
    args_is_override_func::task();
    args_is_ref::task();
    ctor_by_move::task();

    return 0;
}
