#include <algorithm>
#include <chrono>
#include <execution>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

/*
  C++17 引入了并行算法（Parallel Algorithms），在 <execution> 中定义了执行策略。
  大多数 `<algorithm>` 和 `<numeric>` 中的算法都支持了并行版本。

  执行策略:
    - std::execution::seq         : 顺序执行（和普通调用一样）
    - std::execution::par         : 并行执行
    - std::execution::par_unseq   : 并行 + 向量化（SIMD）执行
    - std::execution::unseq       : 向量化（SIMD）执行（C++20）
*/

// ============================================================
namespace parallel_sort {
    /*
    1. std::sort 的并行版本:
        std::sort(std::execution::par, begin, end)
        自动使用多线程对数据进行排序。

    2. 适用条件:
        - 数据量大（通常 > 10000 个元素）
        - 元素比较开销较大
        - 注意：并行版本不会保证 stable sort
    */

    void task() {
        const size_t N = 5'000'000;
        std::vector<int> data(N);

        // 生成随机数据
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(1, 1'000'000);
        for (size_t i = 0; i < N; ++i) {
            data[i] = dist(rng);
        }

        // 复制两份用于对比
        auto data_seq = data;
        auto data_par = data;

        // 顺序排序
        auto t1 = std::chrono::high_resolution_clock::now();
        std::sort(data_seq.begin(), data_seq.end());
        auto t2 = std::chrono::high_resolution_clock::now();
        auto seq_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t2 - t1)
                          .count();

        // 并行排序
        auto t3 = std::chrono::high_resolution_clock::now();
        std::sort(std::execution::par, data_par.begin(), data_par.end());
        auto t4 = std::chrono::high_resolution_clock::now();
        auto par_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t4 - t3)
                          .count();

        // 验证结果一致
        bool same = (data_seq == data_par);
        std::cout << "排序 " << N << " 个随机整数:\n";
        std::cout << "  顺序: " << seq_ms << " ms\n";
        std::cout << "  并行: " << par_ms << " ms\n";
        std::cout << "  结果一致: " << (same ? "是" : "否") << "\n";
    }
}  // namespace parallel_sort

// ============================================================
namespace parallel_transform {
    /*
    1. std::transform 的并行版本:
        对每个元素进行同样的操作，非常适合并行化。
    */

    void task() {
        const size_t N = 10'000'000;
        std::vector<double> input(N);
        std::vector<double> output_seq(N);
        std::vector<double> output_par(N);

        for (size_t i = 0; i < N; ++i) {
            input[i] = static_cast<double>(i) * 0.001;
        }

        // 顺序
        auto t1 = std::chrono::high_resolution_clock::now();
        std::transform(input.begin(), input.end(), output_seq.begin(),
                       [](double x) { return std::sin(x) * std::cos(x); });
        auto t2 = std::chrono::high_resolution_clock::now();

        // 并行
        auto t3 = std::chrono::high_resolution_clock::now();
        std::transform(std::execution::par, input.begin(), input.end(),
                       output_par.begin(),
                       [](double x) { return std::sin(x) * std::cos(x); });
        auto t4 = std::chrono::high_resolution_clock::now();

        auto seq_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t2 - t1)
                          .count();
        auto par_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t4 - t3)
                          .count();

        std::cout << "transform(" << N << " 个 sin*cos):\n";
        std::cout << "  顺序: " << seq_ms << " ms\n";
        std::cout << "  并行: " << par_ms << " ms\n";
    }
}  // namespace parallel_transform

// ============================================================
namespace parallel_reduce {
    /*
    1. std::reduce 的并行版本:
        std::reduce (C++17) 是 std::accumulate 的并行友好版本。
        区别: reduce 不保证操作的顺序（允许并行化），accumulate 保证从左到右。

    2. std::transform_reduce:
        结合 transform 和 reduce，在单次遍历中完成，避免中间临时容器。
    */

    void task() {
        const size_t N = 10'000'000;
        std::vector<double> data(N);
        for (size_t i = 0; i < N; ++i) {
            data[i] = 1.0 / (i + 1);
        }

        // 顺序 reduce
        auto t1 = std::chrono::high_resolution_clock::now();
        double sum_seq = std::reduce(std::execution::seq, data.begin(),
                                     data.end(), 0.0);
        auto t2 = std::chrono::high_resolution_clock::now();

        // 并行 reduce
        auto t3 = std::chrono::high_resolution_clock::now();
        double sum_par = std::reduce(std::execution::par, data.begin(),
                                     data.end(), 0.0);
        auto t4 = std::chrono::high_resolution_clock::now();

        auto seq_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t2 - t1)
                          .count();
        auto par_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          t4 - t3)
                          .count();

        std::cout << "reduce(" << N << " 个元素求和):\n";
        std::cout << "  顺序: " << seq_ms << " ms, sum = " << sum_seq << "\n";
        std::cout << "  并行: " << par_ms << " ms, sum = " << sum_par << "\n";
    }
}  // namespace parallel_reduce

// ============================================================
namespace parallel_for_each {
    /*
    1. std::for_each 的并行版本:
        对范围中的每个元素执行操作，适合没有数据依赖的独立操作。
    */

    void task() {
        std::vector<int> data(20);
        std::iota(data.begin(), data.end(), 0);

        std::cout << "并行 for_each 输出（顺序不保证）:\n  ";
        std::for_each(std::execution::par, data.begin(), data.end(),
                      [](int& x) {
                          x *= x;  // 每个元素平方
                          // 注意：cout 输出顺序不保证
                      });

        for (size_t i = 0; i < data.size(); ++i) {
            std::cout << data[i] << " ";
        }
        std::cout << "\n";
    }
}  // namespace parallel_for_each

// ============================================================
int main() {
    std::cout << "===== 1. parallel sort =====\n";
    parallel_sort::task();

    std::cout << "\n===== 2. parallel transform =====\n";
    parallel_transform::task();

    std::cout << "\n===== 3. parallel reduce =====\n";
    parallel_reduce::task();

    std::cout << "\n===== 4. parallel for_each =====\n";
    parallel_for_each::task();

    return 0;
}
