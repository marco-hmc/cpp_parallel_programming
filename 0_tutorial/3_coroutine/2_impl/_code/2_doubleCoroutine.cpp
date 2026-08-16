#include <boost/coroutine2/all.hpp>
#include <iostream>

int main() {
    boost::coroutines2::coroutine<int>::pull_type pullCoroutine(
        [](boost::coroutines2::coroutine<int>::push_type &coroutinePush) {
            for (int i = 0; i < 10; i++) {
                std::cout << "---------------------" << "coroutine 1" << '\n';
                coroutinePush(1);
            }
        });

    boost::coroutines2::coroutine<int>::pull_type pullCoroutine2(
        [](boost::coroutines2::coroutine<int>::push_type &coroutinePush) {
            for (int i = 0; i < 10; i++) {
                std::cout << "---------------------" << "coroutine 2" << '\n';
                coroutinePush(2);
            }
        });

    for (int i = 0; i < 10; ++i) {
        std::cout << pullCoroutine.get() << '\n';
        pullCoroutine();
        std::cout << pullCoroutine2.get() << '\n';
        pullCoroutine2();
    }

    std::cout << "continue>>>" << '\n';

    return 0;
}
