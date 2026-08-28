#include <benchmark/benchmark.h>
#include <oneapi/tbb.h>
#include <oneapi/tbb/mutex.h>  // tbb::mutex 不在 umbrella 内，需显式包含

#include <atomic>
#include <mutex>
#include <shared_mutex>

#include "bench_common.h"

// Case 2 — 锁原语 × 调度器 × 临界区粒度。
//
// 研究问题：std::mutex / tbb::mutex / spin_mutex / queuing_mutex / 读写锁
// 在 8 线程、临界区粒度 K ∈ {1, 64, 4096} 下的真实差异是什么？
//
// 配对对比：锁次数精确 = N/K（K 整除 N），两侧逐位对齐——
//   - TBB 侧统一 static_partitioner + 8 线程 arena（auto 会额外拆分导致锁次数漂移）；
//   - std 侧 PersistentTeam(8) 持久线程组，静态切片，构造在计时窗外。
// 控制组：team 与 parallel_for 同一把 std::mutex 的 2×2 交叉，
//   把「锁质量」与「调度器/线程生命周期」两个效应拆开。
//
// 旧版设计缺陷（已修正）：
// - 旧 case2_tbb_mutex 实为 spin_mutex，真 tbb::mutex 从未被测——v2 补测；
// - 旧 std 系每迭代重建 8 线程，锁对比被线程创建成本主导（旧「快 4.8x」的真相）——
//   v2 用持久线程组控制组剥离生命周期效应；
// - 旧 queuing_mutex 每元素粒度 145s/迭代拖垮整套件——v2 默认只跑 intended use
//   （粗粒度公平队列），K=1 误用场景移入 RUN_QUEUING_FINE=1 门控。

namespace {

    // 操作总数：2^20 = 1048576，可被 K ∈ {1, 64, 4096} 整除
    constexpr int N = 1 << 20;
    constexpr int NUM_THREADS = 8;
    constexpr int N_PER_THREAD = N / NUM_THREADS;

    // 80/20 读重负载：i % 5 == 0 为写，写操作数 = 1 + (N-1)/5
    constexpr int WRITE_COUNT = 1 + (N - 1) / 5;

    std::atomic<int> atomic_counter{0};
    int mutex_counter = 0;
    int shared_data = 0;

    std::mutex std_mutex;
    std::shared_mutex std_shared_mutex;
    tbb::mutex tbb_mutex;          // 真 tbb::mutex（旧版误标为 spin_mutex 的对象）
    tbb::spin_mutex tbb_spin_mutex;
    tbb::queuing_mutex tbb_queuing_mutex;
    tbb::spin_rw_mutex tbb_rw_lock;

    // 持久线程组控制组：8 线程常驻，构造在计时窗外（与 TBB 常驻池同生命周期）。
    PersistentTeam gTeam(NUM_THREADS);

    // 每 worker 加锁成功次数（仅门控公平性测量使用）
    std::atomic<int> gLockCounts[NUM_THREADS];

    void resetLockCounts() {
        for (auto& c : gLockCounts) {
            c.store(0);
        }
    }

    // 串行基线的递增循环必须对编译器不透明（optnone 强制真实循环），
    // 否则「counter += N」一次加法就能完成整个基准。
    NO_OPTIMIZE int countUp(int start, int n) {
        for (int i = 0; i < n; ++i) {
            ++start;
        }
        return start;
    }

    // 精确锁次数配对（v2 实测教训）：不能依赖分区器的叶子行为——
    // static_partitioner 按线程数等分，2^20 范围只产生 12 个叶子，
    // 锁次数只有 ~12 次而非 N/K（旧版全部 TBB 行因此测了个寂寞）。
    // 正确做法：以 N/K 个 batch 直接作为 parallel_for 的索引域，
    // 锁次数由构造保证精确 = N/K，与 team 侧静态切片逐位对齐。
    template <typename Body>  // Body(int batch_index)
    inline void tbbBatchFor8(int batches, Body&& body) {
        arena8().execute([&] {
            tbb::parallel_for(0, batches, std::forward<Body>(body));
        });
    }

    void countCurrentWorkerLock() {
        const int idx = static_cast<int>(tbb::this_task_arena::current_thread_index());
        if (idx >= 0 && idx < NUM_THREADS) {
            gLockCounts[idx].fetch_add(1, std::memory_order_relaxed);
        }
    }

    void printFairnessRatio() {
        int mx = 0, mn = INT32_MAX;
        for (auto& c : gLockCounts) {
            mx = std::max(mx, c.load());
            mn = std::min(mn, c.load());
        }
        std::printf("[fairness] lock acquisitions per worker: max=%d min=%d ratio=%.2f\n",
                    mx, mn, mn > 0 ? static_cast<double>(mx) / mn : -1.0);
    }

}  // namespace

// 串行基线：无锁递增 N 次，量化临界区操作本身（无争用）的成本下限。
void case2_serial_baseline(benchmark::State& state) {
    for (auto _ : state) {
        int counter = countUp(0, N);
        VERIFY_EXACT(counter, N);
        benchmark::DoNotOptimize(counter);
    }
}

// ---- std::mutex × {team, parallel_for} 2×2 交叉（配对对比核心） ----

void case2_std_mutex_team(benchmark::State& state) {
    const int K = static_cast<int>(state.range(0));

    for (auto _ : state) {
        mutex_counter = 0;
        gTeam.run([&](int tid) {
            const int begin = tid * N_PER_THREAD;
            const int end = begin + N_PER_THREAD;
            for (int i = begin; i < end; i += K) {
                std::lock_guard<std::mutex> lock(std_mutex);
                const int stop = std::min(i + K, end);
                for (int x = i; x < stop; ++x) {
                    ++mutex_counter;
                }
            }
        });
        VERIFY_EXACT(mutex_counter, N);
        benchmark::DoNotOptimize(mutex_counter);
    }
}

void case2_std_mutex_parallel_for(benchmark::State& state) {
    const int K = static_cast<int>(state.range(0));

    for (auto _ : state) {
        mutex_counter = 0;
        tbbBatchFor8(N / K, [&](int c) {
            std::lock_guard<std::mutex> lock(std_mutex);
            const int begin = c * K;
            for (int i = begin; i < begin + K; ++i) {
                ++mutex_counter;
            }
        });
        VERIFY_EXACT(mutex_counter, N);
        benchmark::DoNotOptimize(mutex_counter);
    }
}

// ---- TBB 锁（同一调度器 tbbStaticFor8，与上两行配对） ----

void case2_tbb_mutex(benchmark::State& state) {
    const int K = static_cast<int>(state.range(0));

    for (auto _ : state) {
        mutex_counter = 0;
        tbbBatchFor8(N / K, [&](int c) {
            tbb::mutex::scoped_lock lock(tbb_mutex);
            const int begin = c * K;
            for (int i = begin; i < begin + K; ++i) {
                ++mutex_counter;
            }
        });
        VERIFY_EXACT(mutex_counter, N);
        benchmark::DoNotOptimize(mutex_counter);
    }
}

void case2_tbb_spin_mutex(benchmark::State& state) {
    const int K = static_cast<int>(state.range(0));

    for (auto _ : state) {
        mutex_counter = 0;
        tbbBatchFor8(N / K, [&](int c) {
            tbb::spin_mutex::scoped_lock lock(tbb_spin_mutex);
            const int begin = c * K;
            for (int i = begin; i < begin + K; ++i) {
                ++mutex_counter;
            }
        });
        VERIFY_EXACT(mutex_counter, N);
        benchmark::DoNotOptimize(mutex_counter);
    }
}

// queuing_mutex：FIFO 公平队列锁，intended use 是线程级粗粒度公平等待
// （K=64 每 chunk 一次、K=4096 每大块一次），K=1 误用见门控基准。
void case2_tbb_queuing_mutex(benchmark::State& state) {
    const int K = static_cast<int>(state.range(0));

    for (auto _ : state) {
        mutex_counter = 0;
        tbbBatchFor8(N / K, [&](int c) {
            tbb::queuing_mutex::scoped_lock lock(tbb_queuing_mutex);
            const int begin = c * K;
            for (int i = begin; i < begin + K; ++i) {
                ++mutex_counter;
            }
        });
        VERIFY_EXACT(mutex_counter, N);
        benchmark::DoNotOptimize(mutex_counter);
    }
}

// ---- 读写锁（80/20 读重，每元素加锁——细粒度是读写锁的典型场景） ----

void case2_std_shared_mutex_team(benchmark::State& state) {
    for (auto _ : state) {
        shared_data = 0;
        gTeam.run([&](int tid) {
            const int begin = tid * N_PER_THREAD;
            const int end = begin + N_PER_THREAD;
            for (int i = begin; i < end; ++i) {
                if (i % 5 == 0) {
                    std::unique_lock<std::shared_mutex> lock(std_shared_mutex);
                    ++shared_data;
                } else {
                    std::shared_lock<std::shared_mutex> lock(std_shared_mutex);
                    benchmark::DoNotOptimize(shared_data);
                }
            }
        });
        VERIFY_EXACT(shared_data, WRITE_COUNT);
        benchmark::DoNotOptimize(shared_data);
    }
}

void case2_tbb_rw_lock(benchmark::State& state) {
    for (auto _ : state) {
        shared_data = 0;
        tbbBatchFor8(N, [&](int i) {
            if (i % 5 == 0) {
                tbb::spin_rw_mutex::scoped_lock lock(tbb_rw_lock, /*write=*/true);
                ++shared_data;
            } else {
                tbb::spin_rw_mutex::scoped_lock lock(tbb_rw_lock, /*write=*/false);
                benchmark::DoNotOptimize(shared_data);
            }
        });
        VERIFY_EXACT(shared_data, WRITE_COUNT);
        benchmark::DoNotOptimize(shared_data);
    }
}

// ---- 锁自由对照：原子操作 × {team, parallel_for} ----
// 旧版「tbb_atomic 快 5.7x」的真相是 std 侧重建线程；两行同为常驻线程时应当持平。

void case2_atomic_std_team(benchmark::State& state) {
    for (auto _ : state) {
        atomic_counter.store(0);
        gTeam.run([&](int tid) {
            const int begin = tid * N_PER_THREAD;
            const int end = begin + N_PER_THREAD;
            for (int i = begin; i < end; ++i) {
                atomic_counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
        VERIFY_EXACT(atomic_counter.load(), N);
        benchmark::DoNotOptimize(atomic_counter.load());
    }
}

void case2_atomic_parallel_for(benchmark::State& state) {
    for (auto _ : state) {
        atomic_counter.store(0);
        arena8().execute([&] {
            tbb::parallel_for(0, N, [&](int i) {
                atomic_counter.fetch_add(1, std::memory_order_relaxed);
            });
        });
        VERIFY_EXACT(atomic_counter.load(), N);
        benchmark::DoNotOptimize(atomic_counter.load());
    }
}

// ---- 门控：queuing_mutex 细粒度误用复现 ----
// 旧版该行 145s/迭代拖垮整套件。默认不注册；单独运行：
//   RUN_QUEUING_FINE=1 ./tbb_benchmark \
//       --benchmark_filter=case2_tbb_queuing_mutex_fine_gated
void case2_tbb_queuing_mutex_fine_gated(benchmark::State& state) {
    constexpr int N_FINE = N / 20;  // 减量 20 倍，仍足以展示灾难级差距

    for (auto _ : state) {
        mutex_counter = 0;
        resetLockCounts();
        tbbBatchFor8(N_FINE, [&](int i) {
            tbb::queuing_mutex::scoped_lock lock(tbb_queuing_mutex);
            ++mutex_counter;
            countCurrentWorkerLock();
        });
        printFairnessRatio();
        VERIFY_EXACT(mutex_counter, N_FINE);
        benchmark::DoNotOptimize(mutex_counter);
    }
}

#define CASE2_K_1_64_4096(FN) \
    FN->Args({1})->Args({64})->Args({4096})->Unit(benchmark::kMillisecond)
#define CASE2_K_64_4096(FN) \
    FN->Args({64})->Args({4096})->Unit(benchmark::kMillisecond)

BENCHMARK(case2_serial_baseline)->Unit(benchmark::kMillisecond);
CASE2_K_1_64_4096(BENCHMARK(case2_std_mutex_team));
CASE2_K_1_64_4096(BENCHMARK(case2_std_mutex_parallel_for));
CASE2_K_1_64_4096(BENCHMARK(case2_tbb_mutex));
CASE2_K_1_64_4096(BENCHMARK(case2_tbb_spin_mutex));
CASE2_K_64_4096(BENCHMARK(case2_tbb_queuing_mutex));
BENCHMARK(case2_std_shared_mutex_team)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_tbb_rw_lock)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_atomic_std_team)->Unit(benchmark::kMillisecond);
BENCHMARK(case2_atomic_parallel_for)->Unit(benchmark::kMillisecond);

// 环境变量门控注册：全局作用域只能写声明（BENCHMARK 宏展开为声明），
// 语句必须包在 lambda 初始化器里（1_threadPool 同款教训）。
static bool g_register_queuing_fine = []() {
    if (std::getenv("RUN_QUEUING_FINE")) {
        BENCHMARK(case2_tbb_queuing_mutex_fine_gated)->Unit(benchmark::kMillisecond);
    }
    return true;
}();
