#include <boost/coroutine2/all.hpp>
#include <iostream>

int main() {
    // 定义协程类型
    typedef boost::coroutines2::coroutine<int> coro_t;

    // 创建协程
    coro_t::pull_type source([&](coro_t::push_type& sink) {
        std::cout << "coroutine 1" << std::endl;
        sink(1);  // push {1} back to main-context
        std::cout << "coroutine 2" << std::endl;
        sink(2);  // push {2} back to main-context
        std::cout << "coroutine 3" << std::endl;
        sink(3);  // push {3} back to main-context
    });

    // 循环获取协程发送过来的值
    while (source) {
        int ret = source.get();  // 获取由sink()推送的数据
        std::cout << "move to coroutine-function " << ret << std::endl;
        source();  // 切换回协程函数
        std::cout << "back from coroutine-function" << std::endl;
    }

    return 0;
}