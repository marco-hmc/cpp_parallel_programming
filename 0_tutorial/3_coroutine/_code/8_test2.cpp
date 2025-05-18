#include <boost/coroutine2/all.hpp>
#include <iostream>
#include <vector>

// 打印向量内容的函数
void print(const std::vector<int>& vec) {
    for (int num : vec) {
        std::cout << num << " ";
    }
    std::cout << std::endl;
}

std::vector<int> merge(const std::vector<int>& a, const std::vector<int>& b) {
    std::vector<int> c;
    std::size_t idx_a = 0;
    std::size_t idx_b = 0;
    boost::coroutines2::asymmetric_coroutine<void>::push_type* other_a =
        nullptr;
    boost::coroutines2::asymmetric_coroutine<void>::push_type* other_b =
        nullptr;

    boost::coroutines2::asymmetric_coroutine<void>::push_type coro_a(
        [&](boost::coroutines2::asymmetric_coroutine<void>::pull_type& yield) {
            while (idx_a < a.size()) {
                if (idx_b < b.size() &&
                    b[idx_b] < a[idx_a])  // 检查b中的元素是否小于a中的元素
                    yield();              // 切换到协程coro_b
                c.push_back(a[idx_a++]);  // 将元素添加到最终数组
            }
            // 添加数组b中剩余的元素
            while (idx_b < b.size()) c.push_back(b[idx_b++]);
        });

    boost::coroutines2::asymmetric_coroutine<void>::push_type coro_b(
        [&](boost::coroutines2::asymmetric_coroutine<void>::pull_type& yield) {
            while (idx_b < b.size()) {
                if (idx_a < a.size() &&
                    a[idx_a] < b[idx_b])  // 检查a中的元素是否小于b中的元素
                    yield();              // 切换到协程coro_a
                c.push_back(b[idx_b++]);  // 将元素添加到最终数组
            }
            // 添加数组a中剩余的元素
            while (idx_a < a.size()) c.push_back(a[idx_a++]);
        });

    other_a = &coro_a;
    other_b = &coro_b;

    coro_a();  // 进入协程coro_a

    return c;
}

int main() {
    std::vector<int> a = {1, 5, 6, 10};
    std::vector<int> b = {2, 4, 7, 8, 9, 13};
    std::vector<int> c = merge(a, b);
    print(a);
    print(b);
    print(c);

    return 0;
}