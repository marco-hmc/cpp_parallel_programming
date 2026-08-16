#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/parallel_scan.h>
#include <tbb/blocked_range.h>

#include <cmath>
#include <iostream>
#include <vector>

// ============================================================
namespace tbb_parallel_for {
    /*
    1. tbb::parallel_for 是什么？
        TBB 提供的并行 for 循环，自动将迭代空间切分为多个 block，
        分配到线程池中的工作线程执行。比手动分块方便很多。

    2. 基本用法：
        tbb::parallel_for(tbb::blocked_range<size_t>(0, n),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    // 对每个 i 执行操作
                }
            });

    3. 特点：
        - 自动负载均衡：通过 work stealing 实现
        - 支持嵌套并行
        - 比 OpenMP 的 parallel for 更 C++ 风格
    */

    void task() {
        const size_t N = 100;
        std::vector<double> a(N), b(N), c(N);

        // 初始化
        for (size_t i = 0; i < N; ++i) {
            a[i] = std::sin(static_cast<double>(i));
            b[i] = std::cos(static_cast<double>(i));
        }

        // TBB 并行 for
        tbb::parallel_for(tbb::blocked_range<size_t>(0, N),
            [&](const tbb::blocked_range<size_t>& r) {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    c[i] = a[i] * b[i] + a[i] + b[i];
                }
            });

        // 验证结果
        std::cout << "前 5 个结果: ";
        for (size_t i = 0; i < 5; ++i) {
            std::cout << c[i] << " ";
        }
        std::cout << "\n";
    }
}  // namespace tbb_parallel_for

// ============================================================
namespace tbb_parallel_reduce {
    /*
    1. tbb::parallel_reduce 是什么？
        并行归约操作。将数据分块并行计算局部结果，最后合并。

    2. 基本用法：
        tbb::parallel_reduce(
            tbb::blocked_range<size_t>(0, n),
            identity_value,
            [&](const tbb::blocked_range<size_t>& r, T local_value) -> T {
                // 局部累加
                return local_value;
            },
            [](T x, T y) -> T { return x + y; }  // 合并函数
        );
    */

    void task() {
        const size_t N = 10'000'000;
        std::vector<double> data(N);

        for (size_t i = 0; i < N; ++i) {
            data[i] = 1.0 / (i + 1);
        }

        // 并行求和
        double sum = tbb::parallel_reduce(
            tbb::blocked_range<size_t>(0, N),
            0.0,  // 初始值
            [&](const tbb::blocked_range<size_t>& r, double local_sum) -> double {
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    local_sum += data[i];
                }
                return local_sum;
            },
            std::plus<double>()  // 合并两个局部结果
        );

        std::cout << "调和级数前 " << N << " 项和: " << sum << "\n";
    }
}  // namespace tbb_parallel_reduce

// ============================================================
namespace tbb_parallel_scan {
    /*
    1. tbb::parallel_scan 是什么？
        并行前缀和（prefix sum / scan）算法。
        用于需要累积前序结果的计算，比 sequential scan 快很多。

    2. 两阶段算法：
        - 第一阶段：各块独立计算局部 scan
        - 第二阶段：用上一块的最终值修正本块结果
    */

    void task() {
        const size_t N = 20;
        std::vector<int> input(N, 1);   // 全 1
        std::vector<int> output(N, 0);  // 输出前缀和

        tbb::parallel_scan(
            tbb::blocked_range<size_t>(0, N),
            0,  // 初始值
            [&](const tbb::blocked_range<size_t>& r, int sum,
                bool is_final_scan) -> int {
                int temp = sum;
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    temp += input[i];
                    if (is_final_scan) {
                        output[i] = temp;
                    }
                }
                return temp;
            },
            [](int left, int right) { return left + right; });

        std::cout << "输入:  ";
        for (auto v : input) std::cout << v << " ";
        std::cout << "\n前缀和: ";
        for (auto v : output) std::cout << v << " ";
        std::cout << "\n";
    }
}  // namespace tbb_parallel_scan

// ============================================================
int main() {
    std::cout << "===== 1. tbb::parallel_for =====\n";
    tbb_parallel_for::task();

    std::cout << "\n===== 2. tbb::parallel_reduce =====\n";
    tbb_parallel_reduce::task();

    std::cout << "\n===== 3. tbb::parallel_scan =====\n";
    tbb_parallel_scan::task();
    return 0;
}
