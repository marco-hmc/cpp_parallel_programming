# 2_tbb_test — TBB 并行 Framework Benchmark 报告

**测试目标：** 对比 Intel TBB (oneTBB) 与原生 std::thread、StdThreadPool 的性能差异，涵盖 `parallel_for`、同步原语、嵌套任务三大维度。

**测试框架：** Google Benchmark  
**依赖：** `benchmark`, `spdlog`, `oneapi::tbb`, `tbbThreadPool`, `threadPool`（本地库）

**测试环境：**
- 操作系统：Windows 11
- 编译器：clang-cl
- CPU：12 逻辑核 @ 2611 MHz
- Cache：L1 = 48 KiB (x6)，L2 = 1280 KiB (x6)，L3 = 12288 KiB (x1)

> ⚠️ **测试跳过说明：** `case2_tbb_queuing_mutex` 单次迭代耗时约 145 秒，严重超时，该测试之后被强制跳过；因此剩余的 case3 测试（threadpool 嵌套 / 双池 / 手动线程 / task_group）也整体未执行。本报告只包含实际完成测量的 benchmark。

---

## [toc]

---

## 1. 实验设计概览

| Case | 研究问题 | 对比维度 | 变量数 | 完成测量 |
|------|---------|---------|--------|---------|
| Case 1 | TBB `parallel_for` vs 线程池 vs 裸线程？ | 6 种调度方式 | 固定工作量 1M 元素 | 6/6 ✅ |
| Case 2 | TBB 同步原语 vs std 同步原语？ | 11 种同步方式 | 固定 1M 操作 | 10/11（queuing_mutex 跳过）|
| Case 3 | TBB 嵌套并行 vs 线程池嵌套？ | 6 种嵌套策略 | 固定 50 外层任务 | 2/6（因 case2 超时整体跳过）|

---

## 2. Case 1 — parallel_for vs ThreadPool vs 原生线程

### 2.1 测试设计

对 1,000,000 个元素执行 `sin*cos+sqrt` 计算密集型任务，对比 6 种并行方式：

| 策略 | 实现 | 分块 |
|------|------|------|
| `case1_serial` | 单线程串行 | — |
| `case1_native_threads` | 手工 `std::thread` × core 数 | 静态均分 |
| `case1_threadpool` | `StdThreadPool` 提交 chunk | CHUNK=1000 |
| `case1_tbb_parallel_for` | `tbb::parallel_for` + `blocked_range(CHUNK=1000)` | 显式分块 |
| `case1_tbb_auto_partitioner` | `tbb::parallel_for` + `auto_partitioner()` | TBB 自动决定 |
| `case1_tbb_simple_partitioner` | `tbb::parallel_for` + `simple_partitioner()` | 静态均分 |

### 2.2 关键差异

```cpp
// TBB 的 blocked_range 机制——天然支持 chunk
tbb::parallel_for(
    tbb::blocked_range<int>(0, TOTAL_WORK, CHUNK_SIZE),
    [&](const tbb::blocked_range<int>& range) {
        // 每个 body 调用处理一个 CHUNK，不是单个元素
        double local_result = compute_task(range.begin(), range.end());
        total_result.fetch_add(local_result, std::memory_order_relaxed);
    });

// ThreadPool——需要手动切分
for (int i = 0; i < TOTAL_WORK; i += CHUNK_SIZE) {
    futures.push_back(pool.submitTask([i, end]() {
        return compute_task(i, end);
    }));
}
```

### 2.3 实测结果与分析

| 策略 | Time | CPU | 加速比（vs Serial） | 迭代数 |
|------|------|-----|--------------------|--------|
| Serial | 30.5 ms | 19.8 ms | 1.00×（基准） | 41 |
| **ThreadPool** | **3.72 ms** | 0.453 ms | **8.2×** 🏆 | 896 |
| TBB auto_partitioner | 6.60 ms | 4.63 ms | 4.6× | 179 |
| TBB parallel_for (blocked_range) | 8.09 ms | 5.56 ms | 3.8× | 90 |
| TBB simple_partitioner | 109 ms | 60.8 ms | 0.28×（比串行慢 3.6×） | 9 |
| Native threads | 217 ms | 0.469 ms | 0.14×（比串行慢 7.1×） | 100 |

**关键发现（基于实测）：**

1. **ThreadPool 反而是最快（3.72ms，8.2×）**：本负载完全均匀（1M 次 `sin*cos+sqrt`），CHUNK=1000 的静态分片已是最优解，TBB 的动态调度优势无从发挥，反而带来自适应拆分的额外开销。结论：**均匀任务上，简单线程池静态分片即可胜出。**

2. **auto_partitioner（6.60ms，4.6×）优于显式 blocked_range(1000)（8.09ms，3.8×）**：自动分块策略比手写 CHUNK 更优，是 TBB 侧的推荐路径——粒度太细调度开销大，太粗负载不均衡，auto 自适应平衡两者。

3. **simple_partitioner 是灾难（109ms，0.28×，比串行还慢 3.6×）**：等分区间 + 无工作窃取导致负载不均衡，且缺少自适应拆分，实测远差于 auto_partitioner（约 16 倍差距）。默认配置之外的分区器需要明确理由才使用。

4. **native_threads 最慢（217ms，0.14×）**：Time 217ms vs CPU 0.469ms 的悬殊对比说明墙钟时间几乎全部花在线程创建/销毁与调度等待上。每次迭代重建 8 个线程的代价远大于 1M 元素的纯计算本身。

5. **TBB 的 `blocked_range` 是关键抽象：** PPL 没有对应概念，必须手动 chunk（见 `4_ppl_strategy_test`）。

---

## 3. Case 2 — 同步原语大比拼

### 3.1 测试设计

在 1,000,000 次操作下，对比 11 种同步方式的性能。分为 5 个子维度：

#### 3.1.1 Atomic 对比

| 测试 | 实现 |
|------|------|
| `std_atomic` | `std::thread` × 8 + `std::atomic::fetch_add` |
| `tbb_atomic` | `tbb::parallel_for` + `std::atomic::fetch_add` |

> 两者使用同一同步原语，差异只在线程生命周期与调度方式。

#### 3.1.2 互斥锁对比

| 测试 | 锁类型 | 实现 |
|------|--------|------|
| `std_mutex` | `std::mutex` | std::thread × 8 |
| `tbb_mutex` | `tbb::mutex` | tbb::parallel_for |
| `tbb_spin_mutex` | `tbb::spin_mutex` | tbb::parallel_for |
| `tbb_queuing_mutex` | `tbb::queuing_mutex` | tbb::parallel_for |

锁类型区别：
- `std::mutex` — 内核态互斥锁，竞争时阻塞等待
- `tbb::spin_mutex` — 用户态自旋锁，短临界区最优
- `tbb::queuing_mutex` — 公平排队锁，保证 FIFO 顺序

#### 3.1.3 读写锁对比（读多写少 80/20）

| 测试 | 锁类型 |
|------|--------|
| `std_shared_mutex_read_heavy` | `std::shared_mutex` |
| `tbb_rw_lock_read_heavy` | `tbb::spin_rw_mutex` |

#### 3.1.4 混合使用 & 计算+锁

| 测试 | 场景 |
|------|------|
| `mixed_std_tbb` | TBB `parallel_for` 内使用 `std::mutex` |
| `compute_with_std_mutex` | 计算 + `std::mutex` 保护 |
| `compute_with_tbb_mutex` | 计算 + `tbb::spin_mutex` 保护 |

### 3.2 实测结果与分析

| 排名 | 同步方式 | Time | 相对 std 同类加速比 | 说明 |
|------|---------|------|--------------------|------|
| 1 | `tbb_atomic` | 26.0 ms | vs `std_atomic`：5.7× | 线程复用 |
| 2 | `std_shared_mutex`（读重） | 40.5 ms | vs `tbb_rw_lock`：1.6× | 意外胜出 |
| 3 | `tbb_mutex` | 45.2 ms | vs `std_mutex`：4.8× | |
| 4 | `tbb_spin_mutex` | 49.1 ms | vs `std_mutex`：4.4× | |
| 5 | `tbb_rw_lock`（读重） | 66.1 ms | — | 自旋开销拖累 |
| 6 | `mixed_std_tbb` | 67.2 ms | — | 混合可用，非陷阱 |
| 7 | `compute_with_tbb_mutex` | 80.5 ms | vs `std`：2.1× | |
| 8 | `std_atomic` | 147 ms | 基准 | 每次迭代重建 8 线程 |
| 9 | `compute_with_std_mutex` | 170 ms | 基准 | |
| 10 | `std_mutex` | 215 ms | 基准 | |
| — | `tbb_queuing_mutex` | **145318 ms（≈145 s）** | vs `std_mutex`：≈0.0015× | 灾难，已跳过 ❌ |

**关键发现（基于实测）：**

1. **`tbb_atomic`（26ms）比 `std_atomic`（147ms）快 5.7×，但原因不在 atomic 本身**：两者用的是同一个 `std::atomic::fetch_add`。差异完全来自线程生命周期——TBB parallel_for 复用池内线程，std 版本每次迭代重新创建 8 个线程（Time 147ms vs CPU 0.156ms 印证）。同样的锁、同样的竞争，差的只是线程复用。

2. **`tbb_mutex`（45.2ms，4.8×）与 `tbb_spin_mutex`（49.1ms，4.4×）碾压 `std_mutex`（215ms）**：主要来自线程复用；自旋锁再省去内核态切换。注意 spin_mutex 并未显著快于 tbb::mutex——在每次迭代重建线程的巨大开销面前，锁类型本身的差异被淹没了。

3. **`tbb::queuing_mutex` 是灾难（145318ms ≈ 145 秒）**：公平排队锁在 parallel_for 每元素粒度的巨量 lock/unlock 下产生天文数字的排队操作，实测比 std_mutex 慢约 675 倍。**绝对不要将 queuing_mutex 用于细粒度 parallel_for 临界区。** 该测试单次迭代即耗时 145s，随后整体跳过。

4. **读多写少场景 `std::shared_mutex`（40.5ms）意外胜过 `tbb::spin_rw_mutex`（66.1ms）**：spin_rw_mutex 的自旋开销在大量短读中反而拖累性能——读操作极短时，多读者自旋争抢 cacheline 的开销超过了内核态读锁的成本。选型不要迷信 TBB。

5. **混合 `std::mutex` + TBB `parallel_for`（67.2ms）并非陷阱**：介于 TBB 锁与 std 原生之间，完全可用。

6. **计算 + 锁：`tbb_spin_mutex`（80.5ms）比 `std_mutex`（170ms）快 2.1×**：即使临界区含计算，TBB 的线程复用优势依然显著。

---

## 4. Case 3 — 嵌套任务并行

### 4.1 测试设计

50 个外层任务，每个生成随机规模数据（100~10000 元素），需要内层并行处理。对比 6 种策略：

| 策略 | 外层 | 内层 | 安全性 |
|------|------|------|--------|
| `case3_serial_nested` | 串行 | 串行 | ✅ 基线 |
| `case3_tbb_nested` | `tbb::parallel_for` | `tbb::parallel_for` | ✅ 天然支持 |
| `case3_threadpool_nested_unsafe` | `StdThreadPool` | **同一个池** | ❌ 可能死锁 |
| `case3_threadpool_two_level` | `StdThreadPool` (M/2) | `StdThreadPool` (M/2) | ✅ 两个独立池 |
| `case3_manual_threads` | `std::thread` 均分 | 串行内层 | ✅ 无死锁风险 |
| `case3_tbb_task_group` | `tbb::task_group` | `tbb::task_group` | ✅ 显式任务管理 |

### 4.2 死锁分析

```cpp
// ❌ 危险：同一线程池嵌套
pool.submitTask([&pool]() {
    // 父任务占了一个 worker
    pool.submitTask([]() { /* 子任务 */ }).get();
    // 等待子任务 → 但所有 worker 可能都在执行父任务
});

// ✅ 安全：两个独立池
outer_pool.submitTask([&inner_pool]() {
    inner_pool.submitTask([]() { /* 子任务 */ }).get();
});

// ✅ 安全：TBB 嵌套并行
tbb::parallel_for(0, N, [&](int i) {
    tbb::parallel_for(0, M, [&](int j) {
        // TBB 调度器处理嵌套，worker 可以"窃取"内层任务
    });
});
```

### 4.3 实测结果与分析

| 策略 | Time | CPU | 加速比（vs Serial） | 迭代数 |
|------|------|-----|--------------------|--------|
| Serial（基线） | 41.9 ms | 30.6 ms | 1.00× | 24 |
| **TBB nested** | **12.2 ms** | 7.29 ms | **3.4×** 🏆 | 90 |
| ThreadPool nested（unsafe） | — | — | ⚠️ 未测（跳过） | — |
| ThreadPool two_level | — | — | ⚠️ 未测（跳过） | — |
| Manual threads | — | — | ⚠️ 未测（跳过） | — |
| TBB task_group | — | — | ⚠️ 未测（跳过） | — |

**关键发现（基于实测）：**

1. **TBB 嵌套并行高效（12.2ms，3.4×）**：两层 `parallel_for` 嵌套下，调度器的工作窃取横跨所有层级，worker 不会因层级边界而空闲。实测证明 TBB 的嵌套并行是"一等公民"。

2. **其余 4 种策略未取得数据**：因 `case2_tbb_queuing_mutex` 耗时约 145s 严重超时，剩余 case3 测试整体跳过。4.2 的死锁风险分析基于源码逻辑，未在本次运行中验证，后续补充测量时建议优先验证 `threadpool_nested_unsafe` 的死锁行为。

---

## 5. 综合结论

### 5.1 TBB vs std::thread vs ThreadPool 总评

| 维度 | TBB | ThreadPool | std::thread |
|------|-----|------------|-------------|
| 易用性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ |
| 嵌套并行 | ✅ 天然支持（实测 3.4×） | ❌ 需多池 | ❌ 需手动管理 |
| 工作窃取 | ✅ 自动 | ❌ 静态分片（均匀负载下已够用） | ❌ 手工实现 |
| 同步原语 | ✅ 丰富（配合复用优势明显） | 依赖 std | ✅ 标准库 |
| 引入成本 | 较重 (~10MB) | 轻量 (自写) | 零 |
| 最优场景 | 通用/动态/嵌套并行 | 均匀固定任务（实测最快） | 极简单场景 |

### 5.2 核心发现

| 排名 | 发现 | 影响 |
|------|------|------|
| 🔴 | `tbb::queuing_mutex` + 细粒度 `parallel_for` ≈ 145s | **高危：禁止** |
| 🔴 | 同一线程池嵌套提交 = 死锁风险 | 高危 |
| 🟡 | `simple_partitioner` 无工作窃取，实测比串行慢 3.6× | 中：非默认配置需谨慎 |
| 🟡 | TBB 侧 `auto_partitioner`（4.6×）优于显式 chunk（3.8×） | 中：开发效率 |
| 🟢 | 均匀负载下 StdThreadPool 静态分片即可最优（8.2×） | 低：简单场景无需 TBB |
| 🟢 | TBB 线程复用让同步原语整体快 4–6× | 低：框架优势 |
| 🟢 | 读重场景 `std::shared_mutex`（1.6×）意外胜出 `spin_rw_mutex` | 低：选型决策 |

### 5.3 最佳实践

1. **TBB 并行用 `auto_partitioner`**；除非明确知道负载形态，否则不要用 `simple_partitioner`。
2. **均匀任务：简单线程池静态分片即可最优**（实测 8.2× 全场第一），不必引入 TBB。
3. **绝对避免 `tbb::queuing_mutex` + 细粒度 `parallel_for`**。
4. **读多写少短临界区：`std::shared_mutex` 实测优于 `tbb::spin_rw_mutex`**，选型以实测为准。
5. **凡是用线程的地方优先考虑线程复用**：一次迭代重建 8 线程的代价（std 系列 147~215ms）远超同步原语本身的差异。
6. **嵌套并行直接用 TBB**，不要自建线程池嵌套——除非你非常清楚死锁条件。

---

## 6. 原始数据

Google Benchmark 原始输出（Windows 11 / clang-cl / 12 逻辑核 @ 2611 MHz，CPU 缓存 L1=48KiB(x6), L2=1280KiB(x6), L3=12288KiB(x1)）：

```text
Benchmark                                  Time             CPU   Iterations
case1_serial                            30.5 ms         19.8 ms           41
case1_tbb_parallel_for                  8.09 ms         5.56 ms           90
case1_threadpool                        3.72 ms        0.453 ms          896
case1_tbb_auto_partitioner              6.60 ms         4.63 ms          179
case1_tbb_simple_partitioner             109 ms         60.8 ms            9
case1_native_threads                     217 ms        0.469 ms          100
case2_std_atomic                         147 ms        0.156 ms          100
case2_tbb_atomic                        26.0 ms         14.8 ms           56
case2_std_mutex                          215 ms        0.156 ms          100
case2_tbb_mutex                         45.2 ms         10.8 ms           90
case2_tbb_spin_mutex                    49.1 ms         10.0 ms           75
case2_tbb_queuing_mutex               145318 ms         3734 ms            1   ← SKIPPED after this
case2_std_shared_mutex_read_heavy       40.5 ms        0.000 ms          100
case2_tbb_rw_lock_read_heavy            66.1 ms         13.3 ms           79
case2_mixed_std_tbb                     67.2 ms         28.8 ms           26
case2_compute_with_std_mutex             170 ms        0.156 ms          100
case2_compute_with_tbb_mutex            80.5 ms         8.87 ms           37
case3_serial_nested                     41.9 ms         30.6 ms           24
case3_tbb_nested                        12.2 ms         7.29 ms           90
-- remaining case3 tests skipped due to excessive runtime --
```

**说明：**

- `case2_tbb_queuing_mutex` 单次迭代耗时约 145 秒（145318 ms），严重超时，该测试及之后所有 case3 测试被强制跳过。
- 各 benchmark 的 `Iterations` 由 Google Benchmark 根据目标时长自动调节。
- Time 远大于 CPU 的项（如 `case1_native_threads` 217ms/0.469ms、`case2_std_atomic` 147ms/0.156ms）均为每次迭代重建线程的实现，墙钟时间几乎全部花在线程生命周期与调度等待上。

---

*数据采集：2026-08-10，Google Benchmark 默认设置，基于 `2_tbb_test` 实测运行结果。*
