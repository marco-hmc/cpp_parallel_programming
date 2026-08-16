# 0_basic_test — 基础多线程开销 Benchmark 报告

**测试目标：** 量化多线程编程中的基础开销——线程创建、伪共享、加速比、过量线程、锁与原子竞争。

**测试框架：** Google Benchmark  
**依赖：** `benchmark`, `fmt`, `spdlog`

**测试平台：** Windows 11，编译器 clang-cl  
**硬件：** 12 × 2611 MHz；L1 Data 48 KiB (x6)、L1 Instruction 32 KiB (x6)、L2 Unified 1280 KiB (x6)、L3 Unified 12288 KiB (x1)

---

## [toc]

---

## 1. 实验设计概览

| Case | 研究问题 | 变量 | 关键指标 |
|------|---------|------|---------|
| Case 1 | 线程创建/销毁的开销有多大？ | 线程数 ∈ {10, 50, 100, 500, 1000}，join vs detach | 创建+回收耗时、每线程耗时 |
| Case 2 | 伪共享 (False Sharing) 对性能的影响？ | 对齐 vs 未对齐，写入延迟 | 两线程并发写入耗时 |
| Case 3 | 多线程的加速比能达到多少？ | 串行 / 单线程 / 多线程 / 线程池 | 耗时对比、加速比 |
| Case 4 | 线程过多时性能如何退化？ | 串行 / 最优线程数 / 1000 线程 | 耗时对比 |
| Case 5 | 锁与原子在竞争下的开销有多大？ | 无锁 / 有锁 / 锁竞争 / 原子竞争 | 计数耗时、每迭代成本 |

---

## 2. Case 1 — 线程创建开销

### 2.1 测试设计

创建 N 个线程（N=10, 50, 100, 500, 1000），每个线程执行空函数后结束。分别用两种回收方式测量：`join`（主线程等待线程结束并回收）与 `detach`（分离，由 OS 后台回收）。测量的是 **创建 + 调度 + 回收** 的综合开销。

```cpp
    // case1: spawn N threads, each sleeps 1ms
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(empty_task);
    }
    for (auto& t : threads) {
        t.join();
    }
```

### 2.2 实测结果与分析

| 线程数 | join 总耗时 | join 每线程 | detach 总耗时 | detach 每线程 |
|--------|------------|------------|--------------|--------------|
| 10     | 0.687 ms   | ~69 μs     | 0.410 ms     | ~41 μs       |
| 50     | 4.34 ms    | ~87 μs     | 2.32 ms      | ~46 μs       |
| 100    | 7.71 ms    | ~77 μs     | 6.20 ms      | ~62 μs       |
| 500    | 39.8 ms    | ~80 μs     | 81.2 ms      | ~162 μs      |
| 1000   | 67.5 ms    | ~67 μs     | 84.2 ms      | ~84 μs       |

**关键发现：**
- 线程创建不是免费的——**create+join 模式下每线程稳定消耗 ~67-87 μs** 的内核态开销（栈分配、TCB 初始化、调度器注册）。
- **低线程数（10-100）时 detach 明显更快**：省去了 join 的等待，每线程仅 ~41-62 μs。
- **高线程数（500+）时 detach 反而更慢**（500 时每线程 ~162 μs，1000 时 ~84 μs）：detach 线程由 OS 后台回收，线程数增多时回收队列积压，开销在创建循环期间体现出来。
- **教训：** 频繁创建/销毁线程的模式（per-request thread）在高吞吐场景下不可取，应使用线程池复用——每线程 ~70 μs 的创建成本是不可忽略的。

---

## 3. Case 2 — 伪共享 (False Sharing)

### 3.1 测试设计

两个线程分别递增两个相邻原子变量。当它们在同一 cache line（64 字节）上时，即使逻辑上互不干扰，硬件缓存一致性协议也会导致相互失效——即"乒乓缓存"。

```cpp
struct SharedDataAligned {
    alignas(64) std::atomic<int> value1;  // 各占独立 cache line
    alignas(64) std::atomic<int> value2;
};

struct SharedDataUnaligned {
    std::atomic<int> value1;  // 可能在同一 cache line
    std::atomic<int> value2;
};
```

子测试：
- `case2_pingPongAligned` — 对齐（理论上无伪共享），10M 次迭代
- `case2_pingPongUnaligned` — 未对齐（可能伪共享），10M 次迭代
- `case2_pingPong*WithOverhead` — 附加不同写入延迟（1ns / 1μs / 1ms，各 10000 次迭代）模拟不同计算密度

### 3.2 实测结果与分析

```
------------------------------------------------------------------------------------------
Benchmark                                                Time             CPU   Iterations
------------------------------------------------------------------------------------------
case2_pingPongAligned/10000000                        49.1 ms        0.000 ms          100
case2_pingPongUnaligned/10000000                       307 ms        0.000 ms           10
case2_pingPongAlignedWithOverhead/1000/1              6356 ms        0.000 ms            1
case2_pingPongAlignedWithOverhead/100/1000            1557 ms        0.000 ms           10
case2_pingPongAlignedWithOverhead/100/1000000         1555 ms        0.000 ms           10
case2_pingPongUnalignedWithOverhead/1000/1            6657 ms        0.000 ms            1
case2_pingPongUnalignedWithOverhead/100/1000          1559 ms        0.000 ms           10
case2_pingPongUnalignedWithOverhead/100/1000000       1556 ms        0.000 ms           10
```

### 3.3 伪共享原理

---

## 4. Case 3 — 加速比测量 (Amdahl's Law)

### 4.1 测试设计

对比四种执行模式完成**相同总工作量**（`cpu核心数 × 240M 次计数`）的耗时：

| 模式 | 执行方式 |
|------|---------|
| `case3_task_cost` | 单次任务（240M 计数）— 基线单位 |
| `case3_single_thread_cost` | 单线程串行执行 core 次 |
| `case3_multi_thread_cost` | 每个任务一个线程，并行执行 |
| `case3_thread_pool_cost` | 将任务提交给线程池并行执行 |

### 4.2 实测结果与分析

| 模式 | 实测耗时 | CPU 时间 | 说明 |
|------|---------|---------|------|
| 单次任务 (task_cost) | 99.4 ms | 95.5 ms | 基线单位 T |
| 单线程串行 (single_thread) | 1137 ms | 1125 ms | ≈ 11.4×T，接近理论 12× |
| 多线程并行 (multi_thread) | 397 ms | 0.156 ms | 相对串行加速 **2.86×** |
| 线程池并行 (thread_pool) | 407 ms | 95.5 ms | 与多线程基本持平（+2.5%） |

**关键发现：**
- 单线程串行 1137 ms ≈ 单次任务 99.4 ms × 11.4，与理论 12× 相当（无额外开销，实测略低于 12× 属正常测量差异）。
- 多线程 397 ms 相对串行加速 **2.86×**——**远低于理想 12×**。加速比受限的原因是：每次 benchmark 迭代都重新创建线程并 join 同步，这部分是**无法并行的串行部分（Amdahl 定律）**；且单任务规模（240M 计数 ≈ 99 ms）相对线程创建开销（~70 μs/个）不够大。
- 注意 CPU 时间列：`multi_thread_cost` 的 CPU 仅为 0.156 ms——Google Benchmark 只统计**调用线程**的 CPU 时间，实际计算发生在工作线程上，不计入调用线程。Time 列为墙钟时间。
- 线程池版本（407 ms）与裸多线程（397 ms）几乎相同——任务队列的入队/出队、条件变量通知开销在本任务规模下可忽略（约 +2.5%），但换来了线程复用。
- 这是**强扩展性（strong scaling）**场景，实测加速比远小于核心数，说明并行化的串行部分（创建+同步）是主要瓶颈。

---

## 5. Case 4 — 过量线程的代价

### 5.1 测试设计

1000 个相同任务（每个约 120M 次计数），每种策略执行方式不同：

| 模式 | 线程数 | 执行方式 |
|------|--------|---------|
| `case4_serial_execution` | 1 | 串行执行全部 1000 个任务 |
| `case4_optimal_threads` | N（核心数=12） | 每个线程分担 tasks/N 个任务 |
| `case4_too_many_threads` | **1000** | 每个任务创建一个线程 |

### 5.2 实测结果与分析

| 模式 | 实测耗时 | 相对串行 |
|------|---------|---------|
| 串行 | 55847 ms | 1× |
| 最优线程数 (12) | 17191 ms | 3.25× |
| 过量线程 (1000) | 17033 ms | 3.28× |

**关键发现（意外结果）：**
- **1000 线程与 12 线程几乎一样快**（17033 ms vs 17191 ms，差异在噪声范围内）。原预期的"上下文切换风暴"和"1GB 虚拟内存"代价并未体现。
- **原因：** 每个任务约 120M 次计数（单任务 ≈ 56 ms），线程创建开销 ~70 μs 占比 < 0.2%，被完全摊薄；1000 个 CPU-bound 线程在 12 核上虽然会切换，但每次调度切片都很长，切换开销占总时间比例极小。
- 另一个值得注意的点：**并行相对串行仅 3.25×**——即使 12 线程，也远低于理想 12×，提示瓶颈在共享资源（如内存带宽/缓存）而非线程管理本身。
- **教训：** 线程数 ≈ 核心数是充分条件而非必要条件——任务足够大时线程创建/切换开销可忽略；但盲目创建 1000 线程**也不会更快**。真正的收益上限由共享资源决定。

---

## 6. Case 5 — 锁与原子竞争开销

### 6.1 测试设计

对比无锁、有锁串行（无竞争）与竞争场景（锁/原子，线程数 ∈ {1, 2, 4, 12}）：

```cpp
// 有锁：每次 ++ 都加锁
void countNumberWithLock(int counter) {
    std::mutex mtx;
    for (int i = 0; i < counter; ++i) {
        std::lock_guard<std::mutex> lock(mtx);  // 锁/解锁 counter 次！
        ++value;
    }
}

// 无锁：直接 ++
void countNumberWithoutLock(int counter) {
    for (int i = 0; i < counter; ++i) {
        ++value;
    }
}
```

注意：串行版本（无锁/有锁）总迭代 **120M 次**；竞争版本（锁/原子）总迭代 **1.2M 次**，两者工作量不同，**不可直接比较耗时**，只能比较每迭代成本与退化趋势。

### 6.2 实测结果与分析

**串行（120M 次）：**

| 模式 | 实测耗时 | 每次计数成本 |
|------|---------|-------------|
| 无锁 | 55.0 ms | ~0.46 ns |
| 有锁（无竞争） | 2023 ms | ~16.9 ns（**37× 慢**） |

**竞争（总 1.2M 次，每次计数成本 = 耗时 / 1.2M）：**

| 线程数 | 锁耗时 | 锁每迭代 | 原子耗时 | 原子每迭代 |
|--------|--------|---------|---------|-----------|
| 1      | 18.7 ms  | ~15.6 ns | 11.1 ms  | ~9.3 ns |
| 2      | 21.4 ms  | ~17.8 ns | 27.2 ms  | ~22.7 ns |
| 4      | 32.1 ms  | ~26.8 ns | 49.5 ms  | ~41.3 ns |
| 12     | 63.9 ms  | ~53.3 ns | 37.4 ms  | ~31.2 ns |

**关键发现：**
- **锁本身的开销极大**：即使没有竞争（单线程），每次 `std::mutex::lock/unlock` 约 **17 ns**，120M 次累计使总耗时从 55 ms 膨胀到 2023 ms（**37×**）。锁粒度必须粗。
- **竞争下两者都退化**：锁从 1→12 线程退化 3.4×（18.7 → 63.9 ms）；原子从 1 线程的 11.1 ms 一路恶化到 4 线程的 49.5 ms。
- **12 线程时原子 (37.4 ms) 明显优于锁 (63.9 ms)**——高竞争下 lock-free 的 `std::atomic` 优势显著。
- 反常点：原子 4 线程（49.5 ms）反而比 12 线程（37.4 ms）慢。可能为测量噪声（benchmark 迭代次数不同：4 线程与 12 线程各 100 次）或调度抖动，需更多样本来确认。
- 单线程无竞争时原子（9.3 ns）也已优于锁（15.6 ns）。
- **教训：**
  - 锁的粒度要粗——批量操作后只锁一次
  - 共享计数器竞争激烈时应使用 `std::atomic`（lock-free），本平台实测明显快于 `std::mutex`
  - 本 case 的串行有锁版本是**反模式**示范——绝不要在循环体内反复加锁

---

## 7. 综合结论

### 7.1 核心发现排名

| 排名 | 发现 | 影响程度 |
|------|------|---------|
| 🔴 | 无竞争锁开销 ~17 ns/次，120M 次累计膨胀 37×——必须粗粒度加锁 | 高 |
| 🔴 | 并行加速比远低于核心数（Case 3: 2.86×，Case 4: 3.25× @ 12 核）——线程创建/同步是串行部分，共享写瓶颈可能在内存带宽 | 高 |
| 🟡 | 线程创建开销 ~67-84 μs/个，频繁创建应改用线程池 | 中 |
| 🟡 | 过量线程（1000）与最优线程数（12）性能几乎相同——任务够大时创建/切换开销被摊薄，但也不带来额外收益 | 低 |
| 🟢 | 竞争下 `std::atomic` 明显优于 `std::mutex`（12 线程 37.4 ms vs 63.9 ms） | 低（已知结论） |

### 7.2 最佳实践

1. **永远使用线程池**，避免 per-task 创建线程（每线程 ~70 μs 创建开销）
2. **共享数据结构布局优化以实测为准**——`alignas(64)` 不是银弹，本平台上对齐版本反而更慢
3. **线程数 = `std::thread::hardware_concurrency()`** 对 CPU-bound 任务足够；更多线程无收益也无灾难
4. **锁的粒度要粗**，绝不在 tight loop 内反复加锁（无竞争 ~17 ns/次）
5. **共享计数器优先使用 `std::atomic`** 替代 `std::mutex`，竞争激烈时优势更明显

---

## 8. 原始数据 (Raw Data)

完整 benchmark 输出如下（Google Benchmark 原生格式）：

```
System: Run on (12 X 2611 MHz CPU s), L1 Data 48 KiB (x6), L1 Instruction 32 KiB (x6), L2 Unified 1280 KiB (x6), L3 Unified 12288 KiB (x1)

Benchmark                                                  Time             CPU   Iterations
case1_create_and_join/10                               0.687 ms        0.222 ms         3446
case1_create_and_join/50                                4.34 ms         1.79 ms          498
case1_create_and_join/100                               7.71 ms         3.77 ms          166
case1_create_and_join/500                               39.8 ms         15.1 ms           64
case1_create_and_join/1000                              67.5 ms         25.5 ms           30
case1_create_and_detach/10                             0.410 ms        0.351 ms         2358
case1_create_and_detach/50                              2.32 ms         1.78 ms          448
case1_create_and_detach/100                             6.20 ms         3.56 ms          224
case1_create_and_detach/500                             81.2 ms         68.8 ms           10
case1_create_and_detach/1000                            84.2 ms         45.8 ms           15
case2_pingPongAligned/10000000                          49.1 ms        0.000 ms          100
case2_pingPongUnaligned/10000000                         307 ms        0.000 ms           10
case2_pingPongAlignedWithOverhead/1000/1                6356 ms        0.000 ms            1
case2_pingPongAlignedWithOverhead/100/1000              1557 ms        0.000 ms           10
case2_pingPongAlignedWithOverhead/100/1000000           1555 ms        0.000 ms           10
case2_pingPongUnalignedWithOverhead/1000/1              6657 ms        0.000 ms            1
case2_pingPongUnalignedWithOverhead/100/1000            1559 ms        0.000 ms           10
case2_pingPongUnalignedWithOverhead/100/1000000         1556 ms        0.000 ms           10
case3_task_cost                                         99.4 ms         95.5 ms            9
case3_single_thread_cost                                1137 ms         1125 ms            1
case3_multi_thread_cost                                  397 ms        0.156 ms          100
case3_thread_pool_cost                                   407 ms         95.5 ms            9
case4_serial_execution                                 55847 ms        54453 ms            1
case4_optimal_threads                                  17191 ms        0.000 ms            1
case4_too_many_threads                                 17033 ms         15.6 ms            1
case5_without_lock_cost                                 55.0 ms         55.4 ms           11
case5_with_lock_serial                                  2023 ms         2031 ms            1
case5_lock_contended_1thread                            18.7 ms        0.031 ms         1000
case5_lock_contended_2threads                           21.4 ms        0.031 ms         1000
case5_lock_contended_4threads                           32.1 ms        0.156 ms          100
case5_lock_contended_hwthreads                          63.9 ms        0.469 ms          100
case5_atomic_contended_1thread                          11.1 ms        0.078 ms         1000
case5_atomic_contended_2threads                         27.2 ms        0.156 ms          100
case5_atomic_contended_4threads                         49.5 ms        0.000 ms          100
case5_atomic_contended_hwthreads                        37.4 ms        0.469 ms          100
```

---

*报告基于 `0_basic_test` 实际 benchmark 运行数据生成（Windows 11 + clang-cl，12 核 2611 MHz）。*
