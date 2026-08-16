#include <benchmark/benchmark.h>
#include <oneapi/tbb.h>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

// 共享数据和测试参数
constexpr int NUM_OPERATIONS = 1000000;
constexpr int NUM_THREADS = 8;

// 全局变量用于测试
std::atomic<int> atomic_counter{0};
int mutex_counter = 0;
int tbb_mutex_counter = 0;
int shared_data = 0;

std::mutex std_mutex;
std::shared_mutex std_shared_mutex;
tbb::spin_mutex tbb_mutex;
tbb::spin_mutex tbb_spin_mutex;
tbb::queuing_mutex tbb_queuing_mutex;
tbb::spin_rw_mutex tbb_rw_lock;

// 1. std::atomic vs TBB parallel_for with atomic
void case2_std_atomic(benchmark::State& state) {
    for (auto _ : state) {
        atomic_counter.store(0);
        
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < NUM_OPERATIONS / NUM_THREADS; ++i) {
                    atomic_counter.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        benchmark::DoNotOptimize(atomic_counter.load());
    }
}

void case2_tbb_atomic(benchmark::State& state) {
    for (auto _ : state) {
        atomic_counter.store(0);
        
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            atomic_counter.fetch_add(1, std::memory_order_relaxed);
        });
        
        benchmark::DoNotOptimize(atomic_counter.load());
    }
}

// 2. std::mutex vs tbb::mutex
void case2_std_mutex(benchmark::State& state) {
    for (auto _ : state) {
        mutex_counter = 0;
        
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < NUM_OPERATIONS / NUM_THREADS; ++i) {
                    std::lock_guard<std::mutex> lock(std_mutex);
                    ++mutex_counter;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        benchmark::DoNotOptimize(mutex_counter);
    }
}

void case2_tbb_mutex(benchmark::State& state) {
    for (auto _ : state) {
        tbb_mutex_counter = 0;
        
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            tbb::spin_mutex::scoped_lock lock(tbb_mutex);
            ++tbb_mutex_counter;
        });
        
        benchmark::DoNotOptimize(tbb_mutex_counter);
    }
}

// 3. tbb::spin_mutex 性能对比
void case2_tbb_spin_mutex(benchmark::State& state) {
    for (auto _ : state) {
        tbb_mutex_counter = 0;
        
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            tbb::spin_mutex::scoped_lock lock(tbb_spin_mutex);
            ++tbb_mutex_counter;
        });
        
        benchmark::DoNotOptimize(tbb_mutex_counter);
    }
}

// 4. tbb::queuing_mutex 性能对比
void case2_tbb_queuing_mutex(benchmark::State& state) {
    for (auto _ : state) {
        tbb_mutex_counter = 0;
        
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            tbb::queuing_mutex::scoped_lock lock(tbb_queuing_mutex);
            ++tbb_mutex_counter;
        });
        
        benchmark::DoNotOptimize(tbb_mutex_counter);
    }
}

// 5. 读写锁对比 - 读多写少场景
void case2_std_shared_mutex_read_heavy(benchmark::State& state) {
    for (auto _ : state) {
        shared_data = 0;
        
        std::vector<std::thread> threads;
        
        // 80% 读操作，20% 写操作
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < NUM_OPERATIONS / NUM_THREADS; ++i) {
                    if ((t * NUM_OPERATIONS / NUM_THREADS + i) % 5 == 0) {
                        // 写操作
                        std::unique_lock<std::shared_mutex> lock(std_shared_mutex);
                        ++shared_data;
                    } else {
                        // 读操作
                        std::shared_lock<std::shared_mutex> lock(std_shared_mutex);
                        benchmark::DoNotOptimize(shared_data);
                    }
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        benchmark::DoNotOptimize(shared_data);
    }
}

void case2_tbb_rw_lock_read_heavy(benchmark::State& state) {
    for (auto _ : state) {
        shared_data = 0;
        
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            if (i % 5 == 0) {
                // 写操作
                tbb::spin_rw_mutex::scoped_lock lock(tbb_rw_lock, /*write=*/true);
                ++shared_data;
            } else {
                // 读操作
                tbb::spin_rw_mutex::scoped_lock lock(tbb_rw_lock, /*write=*/false);
                benchmark::DoNotOptimize(shared_data);
            }
        });
        
        benchmark::DoNotOptimize(shared_data);
    }
}

// 6. 混合使用 std 同步原语与 TBB parallel_for
void case2_mixed_std_tbb(benchmark::State& state) {
    for (auto _ : state) {
        mutex_counter = 0;
        
        // 在 TBB parallel_for 中使用 std::mutex
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            std::lock_guard<std::mutex> lock(std_mutex);
            ++mutex_counter;
        });
        
        benchmark::DoNotOptimize(mutex_counter);
    }
}

// 7. 计算密集型任务 + 同步开销对比
void case2_compute_with_std_mutex(benchmark::State& state) {
    for (auto _ : state) {
        mutex_counter = 0;
        
        std::vector<std::thread> threads;
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < NUM_OPERATIONS / NUM_THREADS; ++i) {
                    // 模拟计算
                    double result = std::sin(i) * std::cos(i);
                    benchmark::DoNotOptimize(result);
                    
                    // 同步更新
                    std::lock_guard<std::mutex> lock(std_mutex);
                    mutex_counter += static_cast<int>(result * 1000) % 10;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        benchmark::DoNotOptimize(mutex_counter);
    }
}

void case2_compute_with_tbb_mutex(benchmark::State& state) {
    for (auto _ : state) {
        tbb_mutex_counter = 0;
        
        tbb::parallel_for(0, NUM_OPERATIONS, [&](int i) {
            // 模拟计算
            double result = std::sin(i) * std::cos(i);
            benchmark::DoNotOptimize(result);
            
            // 同步更新
            tbb::spin_mutex::scoped_lock lock(tbb_mutex);
            tbb_mutex_counter += static_cast<int>(result * 1000) % 10;
        });
        
        benchmark::DoNotOptimize(tbb_mutex_counter);
    }
}

// 注册基准测试
BENCHMARK(case2_std_atomic)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_tbb_atomic)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_std_mutex)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_tbb_mutex)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_tbb_spin_mutex)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_tbb_queuing_mutex)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_std_shared_mutex_read_heavy)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_tbb_rw_lock_read_heavy)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_mixed_std_tbb)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_compute_with_std_mutex)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_compute_with_tbb_mutex)->Unit(benchmark::kMillisecond);