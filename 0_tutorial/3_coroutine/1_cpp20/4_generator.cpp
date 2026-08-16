#include <coroutine>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// 完整的 Generator<T> —— 只定义一次，本文件内复用
// C++23 有 std::generator，这里用 C++20 自己实现
template <typename T>
struct Generator {
    struct promise_type {
        T current_value_;

        Generator get_return_object() {
            return Generator{
                std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // lazy: 创建时不立即执行，等 begin() 第一次 ++ 才启动
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield value 等价于 co_await promise.yield_value(value)
        std::suspend_always yield_value(T v) {
            current_value_ = v;
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;
    handle_type coro;

    explicit Generator(handle_type h) : coro(h) {}
    Generator(const Generator&) = delete;
    Generator(Generator&& other) noexcept : coro(other.coro) {
        other.coro = nullptr;
    }
    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (coro) coro.destroy();
            coro = other.coro;
            other.coro = nullptr;
        }
        return *this;
    }
    ~Generator() {
        if (coro) coro.destroy();
    }

    // ---- 迭代器支持 ----
    struct iterator {
        handle_type coro;
        bool done;

        iterator& operator++() {
            coro.resume();             // 执行到下一个 co_yield
            done = coro.done();        // 如果协程结束了，标记 done
            return *this;
        }
        T operator*() const { return coro.promise().current_value_; }
        bool operator!=(const iterator& other) const { return done != other.done; }
    };

    iterator begin() {
        if (coro) {
            coro.resume();  // 启动协程，执行到第一个 co_yield
            if (coro.done()) return {nullptr, true};
            return {coro, false};
        }
        return {nullptr, true};
    }
    iterator end() { return {nullptr, true}; }
};

// ============================================================
namespace generator_basic {
/*
    1. 生成器（Generator）是什么？
        一个可以"一个个地"产出值的协程。每次 co_yield 产出一个值并挂起，
        调用者通过 range-for 拿到值后，协程自动恢复继续产下一个。
        这本质上就是"惰性求值"——值在被需要的时候才计算。

    2. Generator 和普通返回 vector 有什么区别？
        - vector：一次性算完所有值，存进内存。数量小时方便，数量大时内存爆炸。
        - Generator：每次只算一个值，不存历史。适合无限序列、大文件逐行处理。
*/

Generator<int> count_to(int n) {
    for (int i = 1; i <= n; ++i) {
        co_yield i;
    }
}

void task() {
    std::cout << "1 到 5 的生成：\n  ";
    for (int v : count_to(5)) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}
}  // namespace generator_basic

// ============================================================
namespace generator_fibonacci {
/*
    1. 无限序列生成器
        因为 Generator 是惰性的，可以表达无限序列。
        调用者通过 break 控制何时停止，协程自然不会继续执行。
*/
Generator<long long> fibonacci() {
    long long a = 0, b = 1;
    while (true) {
        co_yield a;
        auto next = a + b;
        a = b;
        b = next;
    }
}

void task() {
    std::cout << "斐波那契前 15 项：\n  ";
    int count = 0;
    for (auto v : fibonacci()) {
        std::cout << v << " ";
        if (++count >= 15) break;
    }
    std::cout << "\n";
}
}  // namespace generator_fibonacci

// ============================================================
namespace generator_pipeline {
/*
    1. 生成器管道
        就像 Unix 管道 `cat file | grep foo | wc`，每个生成器处理上游的数据，
        产给下游。好处：每一步只处理当前元素，内存占用恒定。
*/

Generator<int> range(int from, int to) {
    for (int i = from; i <= to; ++i) {
        co_yield i;
    }
}

Generator<int> filter_even(Generator<int> source) {
    for (int v : source) {
        if (v % 2 == 0) {
            co_yield v;
        }
    }
}

Generator<int> multiply_by(Generator<int> source, int factor) {
    for (int v : source) {
        co_yield v * factor;
    }
}

void task() {
    // 管道：range(1,10) → 过滤偶数 → ×10
    auto even = filter_even(range(1, 10));
    auto result = multiply_by(std::move(even), 10);

    std::cout << "管道 —— range(1,10) → 偶数 → ×10：\n  ";
    for (int v : result) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}
}  // namespace generator_pipeline

// ============================================================
namespace generator_tree {
/*
    1. 递归生成器 —— 遍历树结构
        协程里可以递归调用其他协程吗？C++20 不能直接 co_yield 子协程
        （那是 C++23 std::generator 的特性），但可以手动遍历子生成器。
*/

Generator<int> traverse_tree(int depth) {
    if (depth > 3) co_return;

    co_yield depth;                        // 先产出当前节点
    for (int v : traverse_tree(depth + 1)) { co_yield v; }  // 再递归产出子节点
    for (int v : traverse_tree(depth + 1)) { co_yield v; }  // （模拟二叉树）
}

void task() {
    std::cout << "树遍历（深度优先，depth ≤ 3）：\n  ";
    for (int v : traverse_tree(0)) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}
}  // namespace generator_tree

// ============================================================
int main() {
    std::cout << "===== 1. 基本生成器 =====\n";
    generator_basic::task();

    std::cout << "\n===== 2. 无限序列（斐波那契）=====\n";
    generator_fibonacci::task();

    std::cout << "\n===== 3. 生成器管道 =====\n";
    generator_pipeline::task();

    std::cout << "\n===== 4. 递归生成器（树遍历）=====\n";
    generator_tree::task();

    return 0;
}
