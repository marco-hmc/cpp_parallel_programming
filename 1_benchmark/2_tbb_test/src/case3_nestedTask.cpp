#include <benchmark/benchmark.h>
#include <oneapi/tbb.h>

#include <atomic>
#include <future>
#include <random>
#include <thread>
#include <vector>

#include "threadPool.h"

// 模拟计算密集型任务，返回值影响内层任务规模
struct TaskResult {
    std::vector<double> data;
    int nested_task_count;
    
    TaskResult(int size) : data(size), nested_task_count(size / 100) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.1, 10.0);
        
        for (int i = 0; i < size; ++i) {
            data[i] = dis(gen);
        }
    }
};

// 外层任务：计算量波动很大（100到10000个元素）
TaskResult outer_task(int task_id) {
    // 任务规模随机波动
    std::random_device rd;
    std::mt19937 gen(rd() + task_id);
    std::uniform_int_distribution<> size_dis(100, 10000);
    
    int size = size_dis(gen);
    TaskResult result(size);
    
    // 模拟复杂计算
    for (int i = 0; i < size; ++i) {
        result.data[i] = std::sin(result.data[i]) * std::cos(task_id + i) + std::sqrt(i + 1);
    }
    
    return result;
}

// 内层任务：处理外层任务的结果
double inner_task(const std::vector<double>& data, int start, int end) {
    double sum = 0.0;
    for (int i = start; i < end; ++i) {
        sum += data[i] * std::log(i + 1) + std::pow(data[i], 1.1);
    }
    return sum;
}

constexpr int OUTER_TASKS = 50;

// 1. 串行版本 - 基准
void case3_serial_nested(benchmark::State& state) {
    for (auto _ : state) {
        double total_result = 0.0;
        
        for (int i = 0; i < OUTER_TASKS; ++i) {
            // 外层任务
            TaskResult outer_result = outer_task(i);
            
            // 内层任务（串行）
            double inner_sum = 0.0;
            int chunk_size = std::max(1, static_cast<int>(outer_result.data.size()) / outer_result.nested_task_count);
            
            for (int j = 0; j < outer_result.nested_task_count; ++j) {
                int start = j * chunk_size;
                int end = std::min(start + chunk_size, static_cast<int>(outer_result.data.size()));
                inner_sum += inner_task(outer_result.data, start, end);
            }
            
            total_result += inner_sum;
        }
        
        benchmark::DoNotOptimize(total_result);
    }
}

// 2. TBB nested parallelism - 推荐方式
void case3_tbb_nested(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> total_result{0.0};
        
        // 外层并行
        tbb::parallel_for(0, OUTER_TASKS, [&](int i) {
            // 外层任务
            TaskResult outer_result = outer_task(i);
            
            // 内层并行
            std::atomic<double> inner_sum{0.0};
            int chunk_size = std::max(1, static_cast<int>(outer_result.data.size()) / outer_result.nested_task_count);
            
            tbb::parallel_for(0, outer_result.nested_task_count, [&](int j) {
                int start = j * chunk_size;
                int end = std::min(start + chunk_size, static_cast<int>(outer_result.data.size()));
                double local_sum = inner_task(outer_result.data, start, end);
                inner_sum.fetch_add(local_sum, std::memory_order_relaxed);
            });
            
            total_result.fetch_add(inner_sum.load(), std::memory_order_relaxed);
        });
        
        benchmark::DoNotOptimize(total_result.load());
    }
}

// 3. ThreadPool 嵌套 - 可能导致死锁
void case3_threadpool_nested_unsafe(benchmark::State& state) {
    const int num_threads = std::thread::hardware_concurrency();
    StdThreadPool::ThreadPool pool(num_threads);
    
    for (auto _ : state) {
        std::vector<std::future<double>> outer_futures;
        
        // 外层任务提交到线程池
        for (int i = 0; i < OUTER_TASKS; ++i) {
            outer_futures.push_back(pool.submitTask([i, &pool]() -> double {
                // 外层任务
                TaskResult outer_result = outer_task(i);
                
                // 内层任务也提交到同一个线程池（危险！）
                std::vector<std::future<double>> inner_futures;
                int chunk_size = std::max(1, static_cast<int>(outer_result.data.size()) / outer_result.nested_task_count);
                
                for (int j = 0; j < outer_result.nested_task_count; ++j) {
                    int start = j * chunk_size;
                    int end = std::min(start + chunk_size, static_cast<int>(outer_result.data.size()));
                    
                    inner_futures.push_back(pool.submitTask([&outer_result, start, end]() {
                        return inner_task(outer_result.data, start, end);
                    }));
                }
                
                // 等待内层任务完成
                double inner_sum = 0.0;
                for (auto& future : inner_futures) {
                    inner_sum += future.get();
                }
                
                return inner_sum;
            }));
        }
        
        // 等待外层任务完成
        double total_result = 0.0;
        for (auto& future : outer_futures) {
            total_result += future.get();
        }
        
        benchmark::DoNotOptimize(total_result);
    }
}

// 4. ThreadPool 两层 - 安全的方式
void case3_threadpool_two_level(benchmark::State& state) {
    const int num_threads = std::thread::hardware_concurrency();
    StdThreadPool::ThreadPool outer_pool(num_threads / 2);  // 外层池
    StdThreadPool::ThreadPool inner_pool(num_threads / 2);  // 内层池
    
    for (auto _ : state) {
        std::vector<std::future<double>> outer_futures;
        
        // 外层任务提交到外层线程池
        for (int i = 0; i < OUTER_TASKS; ++i) {
            outer_futures.push_back(outer_pool.submitTask([i, &inner_pool]() -> double {
                // 外层任务
                TaskResult outer_result = outer_task(i);
                
                // 内层任务提交到内层线程池
                std::vector<std::future<double>> inner_futures;
                int chunk_size = std::max(1, static_cast<int>(outer_result.data.size()) / outer_result.nested_task_count);
                
                for (int j = 0; j < outer_result.nested_task_count; ++j) {
                    int start = j * chunk_size;
                    int end = std::min(start + chunk_size, static_cast<int>(outer_result.data.size()));
                    
                    inner_futures.push_back(inner_pool.submitTask([&outer_result, start, end]() {
                        return inner_task(outer_result.data, start, end);
                    }));
                }
                
                // 等待内层任务完成
                double inner_sum = 0.0;
                for (auto& future : inner_futures) {
                    inner_sum += future.get();
                }
                
                return inner_sum;
            }));
        }
        
        // 等待外层任务完成
        double total_result = 0.0;
        for (auto& future : outer_futures) {
            total_result += future.get();
        }
        
        benchmark::DoNotOptimize(total_result);
    }
}

// 5. std::thread 手动管理
void case3_manual_threads(benchmark::State& state) {
    const int num_threads = std::thread::hardware_concurrency();
    
    for (auto _ : state) {
        std::vector<std::thread> outer_threads;
        std::vector<double> results(OUTER_TASKS);
        
        // 将外层任务分配给线程
        int tasks_per_thread = OUTER_TASKS / num_threads;
        int remaining_tasks = OUTER_TASKS % num_threads;
        
        for (int t = 0; t < num_threads; ++t) {
            int start_task = t * tasks_per_thread;
            int end_task = start_task + tasks_per_thread;
            if (t < remaining_tasks) {
                end_task++;
            }
            
            outer_threads.emplace_back([&results, start_task, end_task]() {
                for (int i = start_task; i < end_task; ++i) {
                    // 外层任务
                    TaskResult outer_result = outer_task(i);
                    
                    // 内层任务（串行，避免线程爆炸）
                    double inner_sum = 0.0;
                    int chunk_size = std::max(1, static_cast<int>(outer_result.data.size()) / outer_result.nested_task_count);
                    
                    for (int j = 0; j < outer_result.nested_task_count; ++j) {
                        int start = j * chunk_size;
                        int end = std::min(start + chunk_size, static_cast<int>(outer_result.data.size()));
                        inner_sum += inner_task(outer_result.data, start, end);
                    }
                    
                    results[i] = inner_sum;
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& thread : outer_threads) {
            thread.join();
        }
        
        double total_result = 0.0;
        for (double result : results) {
            total_result += result;
        }
        
        benchmark::DoNotOptimize(total_result);
    }
}

// 6. TBB task_group - 显式任务管理
void case3_tbb_task_group(benchmark::State& state) {
    for (auto _ : state) {
        std::atomic<double> total_result{0.0};
        
        tbb::task_group outer_group;
        
        for (int i = 0; i < OUTER_TASKS; ++i) {
            outer_group.run([i, &total_result]() {
                // 外层任务
                TaskResult outer_result = outer_task(i);
                
                // 内层任务组
                tbb::task_group inner_group;
                std::atomic<double> inner_sum{0.0};
                
                int chunk_size = std::max(1, static_cast<int>(outer_result.data.size()) / outer_result.nested_task_count);
                
                for (int j = 0; j < outer_result.nested_task_count; ++j) {
                    int start = j * chunk_size;
                    int end = std::min(start + chunk_size, static_cast<int>(outer_result.data.size()));
                    
                    inner_group.run([&outer_result, start, end, &inner_sum]() {
                        double local_sum = inner_task(outer_result.data, start, end);
                        inner_sum.fetch_add(local_sum, std::memory_order_relaxed);
                    });
                }
                
                inner_group.wait();
                total_result.fetch_add(inner_sum.load(), std::memory_order_relaxed);
            });
        }
        
        outer_group.wait();
        benchmark::DoNotOptimize(total_result.load());
    }
}

// 注册基准测试
BENCHMARK(case3_serial_nested)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_tbb_nested)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_threadpool_nested_unsafe)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_threadpool_two_level)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_manual_threads)->Unit(benchmark::kMillisecond);
BENCHMARK(case3_tbb_task_group)->Unit(benchmark::kMillisecond);