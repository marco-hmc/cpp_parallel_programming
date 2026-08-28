#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

// 模拟共享数据结构
struct SharedDataAligned {
    alignas(64) std::atomic<int> value1;  // 使用对齐避免伪共享
    alignas(64) std::atomic<int> value2;  // 使用对齐避免伪共享
};

struct SharedDataUnaligned {
    std::atomic<int> value1;  // 未对齐，可能导致伪共享
    std::atomic<int> value2;  // 未对齐，可能导致伪共享
};

// 对齐数据的乒乓缓存测试
void case2_pingPongAligned(benchmark::State& state) {
    const int iterations = state.range(0);
    SharedDataAligned data;
    data.value1.store(0);
    data.value2.store(0);

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value1.fetch_add(1, std::memory_order_relaxed);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value2.fetch_add(1, std::memory_order_relaxed);
            }
        });

        t1.join();
        t2.join();
    }
}

// 未对齐数据的乒乓缓存测试
void case2_pingPongUnaligned(benchmark::State& state) {
    const int iterations = state.range(0);
    SharedDataUnaligned data;
    data.value1.store(0);
    data.value2.store(0);

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value1.fetch_add(1, std::memory_order_relaxed);
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value2.fetch_add(1, std::memory_order_relaxed);
            }
        });

        t1.join();
        t2.join();
    }
}

// 忙等：每次写之后执行 busy_iters 次依赖链计算（LCG），模拟真实业务开销。
// 依赖链使编译器无法折叠或向量化；DoNotOptimize 防止整段计算被删除。
// 注意：不用 sleep 模拟延迟——sleep 让线程让出 CPU，两个线程的写不再重叠，
// 伪共享的乒乓在机制上消失，测到的不是"惩罚被稀释"，而是"竞争不存在"。
inline void busyWork(int busy_iters, int seed) {
    uint64_t acc = static_cast<uint64_t>(seed) + 1;
    for (int j = 0; j < busy_iters; ++j) {
        acc = (acc * 6364136223846793005ULL) + 1442695040888963407ULL;
    }
    benchmark::DoNotOptimize(acc);
}

// 忙等版：每次写之后执行一段真实计算，模拟"带业务"的写入场景。
// 两个线程持续并发写，只是写入频率随 busy_iters 下降——可测出伪共享惩罚的稀释曲线。
void case2_pingPongAlignedWithBusyWork(benchmark::State& state) {
    const int iterations = state.range(0);  // 每个线程的写次数
    const int busy_iters = state.range(1);  // 每次写之后的忙等迭代数

    SharedDataAligned data;
    data.value1.store(0);
    data.value2.store(0);

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value1.fetch_add(1, std::memory_order_relaxed);
                busyWork(busy_iters, i);  // 模拟写操作开销
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value2.fetch_add(1, std::memory_order_relaxed);
                busyWork(busy_iters, i);  // 模拟写操作开销
            }
        });

        t1.join();
        t2.join();
    }
}

// 未对齐数据的忙等版（同 AlignedWithBusyWork，仅结构体不同）
void case2_pingPongUnalignedWithBusyWork(benchmark::State& state) {
    const int iterations = state.range(0);  // 每个线程的写次数
    const int busy_iters = state.range(1);  // 每次写之后的忙等迭代数

    SharedDataUnaligned data;
    data.value1.store(0);
    data.value2.store(0);

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value1.fetch_add(1, std::memory_order_relaxed);
                busyWork(busy_iters, i);  // 模拟写操作开销
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value2.fetch_add(1, std::memory_order_relaxed);
                busyWork(busy_iters, i);  // 模拟写操作开销
            }
        });

        t1.join();
        t2.join();
    }
}

// 注册基准测试
BENCHMARK(case2_pingPongAligned)
    ->Arg(10'000'000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case2_pingPongUnaligned)
    ->Arg(10'000'000)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case2_pingPongAlignedWithBusyWork)
    ->Args({1'000'000, 0})        // 100 万次写，写后 0 次忙等（纯写对照）
    ->Args({1'000'000, 10})       // 写后 10 次 LCG 忙等
    ->Args({1'000'000, 100})      // 写后 100 次 LCG 忙等
    ->Args({1'000'000, 1'000})    // 写后 1000 次 LCG 忙等（实测 ~79 ns/写，见报告 3.2）
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case2_pingPongUnalignedWithBusyWork)
    ->Args({1'000'000, 0})        // 100 万次写，写后 0 次忙等（纯写对照）
    ->Args({1'000'000, 10})       // 写后 10 次 LCG 忙等
    ->Args({1'000'000, 100})      // 写后 100 次 LCG 忙等
    ->Args({1'000'000, 1'000})    // 写后 1000 次 LCG 忙等
    ->Unit(benchmark::kMillisecond);