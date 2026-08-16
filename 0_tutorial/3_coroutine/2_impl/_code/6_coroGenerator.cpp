#include <boost/coroutine2/all.hpp>
#include <iostream>

// 方法一
void foo(boost::coroutines2::coroutine<int>::pull_type& sink) {
    while (sink) {
        int value = sink.get();  // 获取当前值
        std::cout << "retrieve " << value << "\n";
        sink();  // 移动到下一个值
    }
}
// 方法二
void foo2(boost::coroutines2::coroutine<int>::pull_type& sink) {
    for (auto val : sink) {
        std::cout << "retrieve " << val << "\n";
    }
}

// 方法三
void foo3(boost::coroutines2::coroutine<int>::pull_type& sink) {
    for (int i = 0; i < 10; i++) {
        std::cout << "retrieve " << sink.get() << "\n";
        sink();
    }
}

int main() {
    // 使用方法一
    boost::coroutines2::coroutine<int>::push_type source1(foo);
    for (int i = 0; i < 10; i++) {
        source1(i);
    }

    // 使用方法二
    boost::coroutines2::coroutine<int>::push_type source2(foo2);
    for (int i = 0; i < 10; i++) {
        source2(i);
    }

    // 使用方法三
    boost::coroutines2::coroutine<int>::push_type source3(foo3);
    for (int i = 0; i < 10; i++) {
        source3(i);
    }

    return 0;
}