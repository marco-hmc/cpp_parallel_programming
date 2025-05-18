#include <iostream>
#include <thread>

namespace manage_threadId {
    /*
    1. 线程ID什么时候用？
        线程ID是一个唯一标识线程的值，通常在以下情况中使用：
        - 调试：当程序的行为出现问题，需要确定是哪个线程导致的问题时，线程ID可以帮助我们追踪线程的行为。
        - 日志记录：在多线程程序中，使用线程ID可以帮助我们在日志中区分来自不同线程的消息。
        - 线程同步：在某些情况下，我们可能需要知道哪个线程拥有某个资源，或者哪个线程正在执行某个任务。在这种情况下，线程ID可以作为线程的唯一标识。

    2. 线程ID如何保证唯一的？
        线程ID的唯一性是由操作系统和C++运行时系统保证的。当创建一个新线程时，系统会分配一个唯一的线程ID。
        这个ID在线程的整个生命周期中都是唯一的。当线程结束时，它的ID可以被回收并在未来重新使用。
        但是，任何时候，系统中的每个活动线程都有一个唯一的ID。
    */

    const std::thread::id main_thread_id = std::this_thread::get_id();

    void is_main_thread() {
        if (main_thread_id == std::this_thread::get_id()) {
            std::cout << "This is the main thread.\n";
            std::cout << "Thread ID: " << main_thread_id << '\n';
        } else {
            std::cout << "This is not the main thread.\n";
            std::cout << "Thread ID: " << std::this_thread::get_id() << '\n';
        }
    }

    void task() {
        is_main_thread();
        std::thread th(is_main_thread);
        th.join();
    }
}  // namespace manage_threadId

namespace manage_threadLocal {
    /*
    * **‌如何声明线程本地变量？‌**
        C++通过thread_local关键字声明线程本地变量，适用于命名空间变量、静态成员变量和局部变量。
        例如：thread_local int x; 用于全局变量，静态成员需额外定义static thread_local std::string X::s;‌。

    * **‌线程本地变量的初始化时机有何特点？‌**
        命名空间或静态成员的线程本地变量需在线程使用前构造，具体初始化时间由实现决定‌。
        函数内的线程本地变量在首次控制流传递到声明时初始化，未被调用的函数则不会构造其变量‌。
    */

    thread_local int threadId;

    void printThreadId() { std::cout << "Thread ID: " << threadId << '\n'; }

    void task() {
        std::thread t1([] {
            threadId = 1;
            printThreadId();
        });

        std::thread t2([] {
            threadId = 2;
            printThreadId();
        });

        t1.join();
        t2.join();
    }
}  // namespace manage_threadLocal

namespace thread_yield {
    /*
    1. yield的概念是什么？作用是什么？
        `yield` 是一种控制流操作，用于暂停当前正在执行的线程，并将执行权交还给操作系统的线程调度器。
        这样，操作系统可以选择运行其他线程。
        `yield` 通常用于多线程编程中，以提高线程之间的协作和资源利用率。

        - **让出CPU时间片**：`yield` 让当前线程主动放弃 CPU 时间片，使得其他线程有机会获得执行权。这在某些情况下可以提高系统的响应性和资源利用率。
        - **避免忙等待**：在某些情况下，线程可能会进入忙等待状态（不断循环检查某个条件）。使用 `yield` 可以减少忙等待对 CPU 资源的占用。
        - **提高线程调度的公平性**：通过 `yield`，可以让线程调度器更公平地分配 CPU 时间片，避免某些线程长时间占用 CPU。

    2. yield用在什么场景？
        一般用来检查状态或者条件，状态或者条件不满足的时候，为了避免忙等待就让出。
        但是yield是只让出当前时间片的，如果预期是个长时间的等待，那么就不适合使用yield。可以直接用sleep.

    */

    std::atomic<int> foo(0);
    std::atomic<int> bar(0);

    void set_foo(int x) { foo = x; }

    void copy_foo_to_bar() {
        while (foo == 0) {
            std::this_thread::yield();
        }
        bar = static_cast<int>(foo);
    }

    void print_bar() {
        while (bar == 0) {
            std::this_thread::yield();
        }
        std::cout << "bar: " << bar << '\n';
    }

    void task() {
        std::thread first(print_bar);
        std::thread second(set_foo, 10);
        std::thread third(copy_foo_to_bar);
        first.join();
        second.join();
        third.join();
    }
}  // namespace thread_yield

namespace thread_join {
    /*
   1. 线程对象什么时候开始运行？
      当你创建一个`std::thread`对象并传递一个函数给它时，这个函数会在一个新的线程中立即开始运行。
      例如，如果你写`std::thread t(func);`
      那么`func`会在新的线程`t`中立即开始运行。

   2. `joinable()`是什么？
      `joinable()`是`std::thread`类的一个成员函数。
      返回值表示是否可以调用`join`，只有在绑定函数后，且未调用`detach()`和`join()`之前返回true。
      用于检查一个`std::thread`对象是否关联一个活动的线程。

   3. 为什么对于线程一定要调用.join()或者.detach()?
      如果不调用.join()或者.detach()，那么程序会抛出异常，因为主线程会在析构线程对象时调用join()，而此时线程对象已经结束了。
      而声明.join()模式为默认的，肯定是不符合预期的；
      而默认声明.detach()模式为默认的，则声明周期由os维护。

      其他语言，其实是没有.detach()概念的。相对应地，他们区分了不同线程的概念。
      能够.detach()的概念一般就是守护线程，因此创建的时候就是detach的。
    */

    void myThread() {
        // do stuff...
    }

    void task() {
        std::thread foo;
        std::thread bar(myThread);

        std::cout << "Joinable after construction:\n" << std::boolalpha;
        std::cout << "foo: " << foo.joinable() << '\n';  // false
        std::cout << "bar: " << bar.joinable() << '\n';  // true

        if (foo.joinable()) {
            foo.join();
        }
        if (bar.joinable()) {
            bar.join();
        }

        std::cout << "Joinable after joining:\n" << std::boolalpha;
        std::cout << "foo: " << foo.joinable() << '\n';  // false
        std::cout << "bar: " << bar.joinable() << '\n';  // false
    }
}  // namespace thread_join

int main() {
    manage_threadId::task();
    manage_threadLocal::task();
    thread_yield::task();
    thread_join::task();
    return 0;
}
