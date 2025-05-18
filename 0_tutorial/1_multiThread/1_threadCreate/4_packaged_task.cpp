#include <future>
#include <iostream>
#include <thread>

/*
p.s: 请先看future_promise部分，再看async

    1. 有了async, 有了thread，为什么还要有packaged_task？packaged_task有什么能力？
        * thread 的特点与局限：
            最简单，最直接的。但是std::thread 本身不提供直接获取任务执行结果的机制。
            如果需要获取线程函数的返回值，需要手动使用共享变量、锁等机制来实现，这增加了编程的复杂性和出错的可能性。
        * async 的特点与局限：
            可以获得任务执行结果，但是管理机制太过简单，不够灵活。
        * packaged_task 的独特作用
            packaged_task 则提供了一种更方便的方式来处理异步任务以及获取任务的返回结果，它可以将一个可调用对象（比如函数、函数对象、lambda 表达式等）包装起来，
            使得这个可调用对象可以被异步地执行（比如放到线程池中或者配合其他异步执行机制），并且能够方便地获取其返回值。
            弥补了thread不能够获取任务执行结果，async不好管理的问题，提供了一种相对简洁且功能聚焦的任务包装和结果获取机制。

    2. 怎么理解packaged_task？
        * packaged_task 是一个模板类，它的模板参数是一个可调用对象的类型，比如函数指针、函数对象、lambda 表达式等。
        * packaged_task 的主要作用是将一个可调用对象包装起来，使得这个可调用对象可以被异步地执行，并且能够方便地获取其返回值。
        * packaged_task 的构造函数接受一个可调用对象作为参数，然后可以通过调用其 operator() 方法来执行这个可调用对象。
        * packaged_task 的 operator() 方法可以通过调用 get_future() 方法来获取一个 std::future 对象，通过这个 std::future 对象可以获取异步任务的返回值。
        * packaged_task 本身并不具备执行异步任务的能力，它需要配合其他机制（比如线程、线程池等）来实现异步执行。

    3. 注意事项
        * packaged_task 对象只能被移动，不能被复制。
        * packaged_task 对象只能被调用一次，即只能执行一次包装的可调用对象。
        * packaged_task 对象在执行完包装的可调用对象后，会自动释放资源，不需要手动释放。
        * packaged_task 对象的 operator() 方法可以被多次调用，但只有第一次调用会执行包装的可调用对象，后续调用会直接返回之前的结果。
        * 顺序一定是先packaged_task，再future，再thread，最后get()。
        * 
    4. packaged_task的构造是不是和thread的不一样？
        thread的构造函数的参数是可调用对象，后面可以带上参数；
        packaged_task的构造函数的参数就只能够是可调用对象，不能够接参数。
        packaged_task的构造函数在以后更高级的c++标准是有可能可以带上参数的。
*/

namespace packagedTask_basic {

    int triple(int x) { return x * 3; }

    void task() {
        std::packaged_task<int(int)> tsk(triple);
        tsk(33);
        std::future<int> fut = tsk.get_future();
        std::cout << "The triple of 33 is " << fut.get() << ".\n";
        tsk.reset();

        fut = tsk.get_future();
        tsk(55);
        std::cout << "The triple of 33 is " << fut.get() << ".\n";
        tsk.reset();

        fut = tsk.get_future();
        std::thread(std::move(tsk), 99).detach();
        std::cout << "The triple of 99 is " << fut.get() << ".\n";
    }

}  // namespace packagedTask_basic

namespace packagedTask_withThread_noParam {
    void task() {
        std::packaged_task<int()> task([]() { return 7; });
        std::future<int> result = task.get_future();
        std::thread(std::move(task)).detach();

        std::cout << "waiting...";
        result.wait();

        std::cout << "done!" << std::endl
                  << "future result is " << result.get() << '\n';
    }
}  // namespace packagedTask_withThread_noParam

///////////////////////////////////////////////////////////////////////

namespace packagedTask_withThread_withParam {
    std::future<int> launcher(std::packaged_task<int(int)> &tsk, int arg) {
        if (tsk.valid()) {
            std::future<int> ret = tsk.get_future();
            std::thread(std::move(tsk), arg).detach();
            return ret;
        }
        return std::future<int>();
    }

    void task() {
        std::packaged_task<int(int)> tsk([](int x) { return x * 2; });
        std::future<int> fut = launcher(tsk, 25);
        std::cout << "The double of 25 is " << fut.get() << ".\n";
    }
}  // namespace packagedTask_withThread_withParam

//////////////////////////////////////////////////////////////////////////////

int main() {
    packagedTask_basic::task();
    packagedTask_withThread_noParam::task();
    packagedTask_withThread_withParam::task();
    return 0;
}
