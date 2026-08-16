#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
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

// 对齐数据的乒乓缓存测试
void case2_pingPongAlignedWithOverhead(benchmark::State& state) {
    const int iterations = state.range(0);
    const int write_delay_ns = state.range(1);  // 每次写操作的延迟（纳秒）

    SharedDataAligned data;
    data.value1.store(0);
    data.value2.store(0);

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value1.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    write_delay_ns));  // 模拟写操作开销
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value2.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    write_delay_ns));  // 模拟写操作开销
            }
        });

        t1.join();
        t2.join();
    }
}

// 未对齐数据的乒乓缓存测试 (with overhead to dilute false-sharing impact)
void case2_pingPongUnalignedWithOverhead(benchmark::State& state) {
    const int iterations = state.range(0);
    const int write_delay_ns = state.range(1);  // 每次写操作的延迟（纳秒）
    SharedDataUnaligned data;
    data.value1.store(0);
    data.value2.store(0);

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value1.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    write_delay_ns));  // 模拟写操作开销
            }
        });

        std::thread t2([&]() {
            for (int i = 0; i < iterations; ++i) {
                data.value2.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::nanoseconds(
                    write_delay_ns));  // 模拟写操作开销
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
BENCHMARK(case2_pingPongAlignedWithOverhead)
    ->Args({1'000, 1})        // 10,000 次写操作，每次延迟 1 纳秒
    ->Args({100, 1'000})      // 10,000 次写操作，每次延迟 1 微秒
    ->Args({100, 1'000'000})  // 10,000 次写操作，每次延迟 1 毫秒
    ->Unit(benchmark::kMillisecond);
BENCHMARK(case2_pingPongUnalignedWithOverhead)
    ->Args({1'000, 1})        // 10,000 次写操作，每次延迟 1 纳秒
    ->Args({100, 1'000})      // 10,000 次写操作，每次延迟 1 微秒
    ->Args({100, 1'000'000})  // 10,000 次写操作，每次延迟 1 毫秒
    ->Unit(benchmark::kMillisecond);