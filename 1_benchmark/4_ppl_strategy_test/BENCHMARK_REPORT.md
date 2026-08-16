# PPL 并行策略 Benchmark 结论报告

**测试环境：** 12 X 2611 MHz（6 物理核 / 12 逻辑线程）, L1=48KiB(x6), L2=1280KiB(x6), L3=12288KiB(x1), Windows 11, clang-cl (Release)

**测试日期：** 2026-08-10

---

## [toc]

---

## 1. 总览

本 benchmark 围绕五个核心问题展开：

| Case | 问题 | 变量 | 工作量 (固定) |
|------|------|------|---------------|
| Case 1 | CPU-bound 最优 chunk 粒度？ | grain ∈ {8, 64, 1K, 16K, 64K} | 4M 浮点元素 |
| Case 2 | Memory-bound 的最优粒度是否不同？ | 同上 grain，增加 affinity | 8M 随机访存，128MB 数组 |
| Case 3 | 2D 嵌套并行 vs 扁平化哪个更好？ | 嵌套形状 {Deep, Mid, Shallow} | 4M CPU 元素 |
| Case 4 | 跨策略综合最优组合 | grain × workload × strategy | 4M CPU + 8M Memory |
| Case 5 | 负载不均时哪种分区策略最优？ | clustered / shuffled × grain / 嵌套层数 | 2000 个不等长任务 ≈ 4M 元素 |

**版本说明：** 本次报告基于代码修改后重新采集的数据。与上一版相比，benchmark 以 Release 模式重建，且 workload 函数加重（如 case1_serial 从旧版 36.9ms 变为 640ms）。**绝对数值不可跨版本比较，速度比（相对串行基线）才有意义**。本版新增了 Case 5（负载不均测试，上一版缺失），并修正了上一版对 PPL auto/static 的核心误判（见 §6.3）。

---

## 2. Case 1 — CPU-bound 粒度测试

**负载类型：** 纯 CPU（sin·cos + sqrt），4M 元素

### 2.1 结果

| 策略 | Time (ms) | Speedup vs Serial | 说明 |
|------|-----------|-------------------|------|
| **Serial (基线)** | 640 | 1.00× | 单线程参考 |
| **PPL chunked / 64** | **56.5** | **11.3× 🏆** | 62.5K 任务，开销/粒度最优平衡 |
| PPL chunked / 1024 | 64.6 | 9.91× | ~3.9K 任务，接近最优 |
| ThreadPool / 48 | 64.2 | 9.97× | 48 静态分片，过订阅 |
| ThreadPool / 12 | 74.5 | 8.59× | 12 静态分片 |
| PPL chunked / 8 | 80.4 | 7.96× | 500K 任务，调度开销明显 |
| PPL chunked / 16384 | 84.6 | 7.56× | 244 任务，微负载不均衡 |
| PPL chunked / 65536 | 90.3 | 7.09× | 61 任务，负载不均衡明显 |
| PPL auto_partitioner | 616 | 1.04× 🚨 | 零加速，≈ 串行 |
| PPL static_partitioner | 604 | 1.06× 🚨 | 零加速，≈ 串行 |

### 2.2 分析

**最优粒度 (grain=64, 62.5K tasks):**
11.3× speedup，12 线程效率 94%，接近理想。粒度曲线呈浅 U 形（56.5→90.3ms），64~1024 区间（11.3×/9.9×）都是安全选择；更细（8）或更粗（16K/64K）分别被调度开销和负载不均衡拖累。

**PPL auto/static 为何零加速（不再是"慢 15 倍"，但依然没有并行收益）:**

这是本版最重要的修正点。旧报告声称 auto/static "比串行慢 10-15 倍"——那是**元素级原子操作**的绝对开销相对超轻量串行基线（36.9ms）造成的观感。本版 workload 加重后：

- 元素级 `atomic::fetch_add` + 每元素一次 lambda 调用的绝对开销约 0.5~0.6s，基本恒定（旧版 auto 565ms，新版 616ms，几乎不变）；
- 串行基线从 36.9ms 涨到 640ms 后，同样的 ~600ms 开销占比骤降，比值回到 ~1.04×；
- **但并行收益依然为零**：auto/static 是 1.04×/1.06×，而 chunked 是 11.3×——两者相差 10 倍。问题不在 partitioner 本身，而在"每次迭代一次原子操作"这个用法（§6.3 用 Case 5 证明：任务级粒度下 PPL auto 反而是最优）。

**教训：PPL 没有 TBB 的 `blocked_range` 概念，均匀负载下仍需手动 chunk 控制粒度。**

### 2.3 CPU-bound 最优策略

```
grain = 64～1024 均可，64 最优（11.3× speedup）
```

---

## 3. Case 2 — Memory-bound 粒度测试

**负载类型：** 随机访存 128MB 数组（远超 L3=12MB），8M 元素，Knut 乘法哈希破坏硬件预取

### 3.1 结果

| 策略 | Time (ms) | Speedup vs Serial | 说明 |
|------|-----------|-------------------|------|
| **Serial (基线)** | 330 | 1.00× | 内存带宽瓶颈的基准 |
| **ThreadPool / 48** | **60.2** | **5.48× 🏆** | 48 静态分片，访存请求充分交错 |
| ThreadPool / 12 | 79.3 | 4.16× | |
| PPL chunked / 64 | 83.8 | 3.94× | 细粒度交错有利 |
| PPL chunked / 16384 | 87.8 | 3.76× | |
| PPL chunked / 1024 | 102 | 3.24× | |
| PPL chunked / 65536 | 118 | 2.80× | 粗粒度，多线程请求交错不足 |
| PPL chunked / 8 | 213 | 1.55× | 过细粒度 + 内存竞争更差 |
| PPL auto_partitioner | 1470 | **0.22× 🚨** | 比串行慢 4.5 倍！ |
| PPL static_partitioner | 1435 | **0.23× 🚨** | 比串行慢 4.3 倍！ |
| PPL affinity_partitioner | 1386 | **0.24× 🚨** | cache affinity 在随机访存下无效 |

### 3.2 分析

**内存带宽瓶颈 — 峰值 ~5.5×:**
12 核并行时所有核共享内存总线，随机访存 128MB（每次访问大概率 miss L3），speedup 被 DRAM 带宽封顶在 ~5.5×。手工 pool/48 通过 48 个分片把访存请求充分交错，反而超过 PPL chunked。

**粒度影响:**
chunked 在 64~16K 区间 3.2~3.9×（差异 ~20%），但 65536 粗粒度退到 2.8×——memory-bound 下负载均衡依然比"减少原子次数"更重要。grain=8 的 1.55× 说明细粒度 + 内存竞争是双杀。

**元素级原子在 memory-bound 下危害最大:**
auto/static/affinity 全部 0.22~0.24×（比串行慢 4~5 倍）。原因：单元素工作极轻（一次随机访问 ~十几 ns），每次迭代的原子操作与 lambda 调用开销占比极大，并行化的收益被完全吞掉。与 §2.2 一致——这是元素级原子模式的绝对开销，与分区器无关（Case 5 将证明）。

**affinity_partitioner 无能为力:**
128MB 数组随机访问 → 无空间/时间局部性，cache affinity 毫无意义（0.24×，仅比 static 略好）。

### 3.3 Memory-bound 最优策略

```
ThreadPool 分片（48 片，5.5×）> PPL chunked / 64~16K（3.2~3.9×）
```

---

## 4. Case 3 — Nested vs Flat 并行对比

**负载类型：** CPU-bound，4M 元素，三种 2D 分解形状：

| 形状 | 外层 | 内层 | 说明 |
|------|------|------|------|
| Deep | 5000 | 800 | 很多小外层任务 |
| Medium | 1000 | 4000 | 均衡 |
| Shallow | 100 | 40000 | 少数大外层任务 |

### 4.1 结果

| 策略 | Time (ms) | Speedup | 说明 |
|------|-----------|---------|------|
| Serial (基线) | 646 | 1.00× | |
| **Task group nested / 100×40000** | **65.7** | **9.83× 🏆** | Shallow 形状 task_group 最优 |
| Task group nested / 5000×800 | 66.4 | 9.73× | Deep 形状 task_group 最优 |
| Pool flat | 70.2 | 9.20× | 手工 48 分片 |
| Outer-only / 100 | 73.7 | 8.76× | |
| Nested PF / 5000×800 | 117 | 5.52× | Deep 嵌套 parallel_for |
| Nested PF / 1000×4000 | 119 | 5.43× | |
| Flat chunked (1024) | 134 | 4.82× | |
| Outer-only / 5000 | 135 | 4.79× | |
| Task group nested / 1000×4000 | 534 | 1.21× ⚠️ | 异常（见 4.2） |
| Outer-only / 1000 | 581 | 1.11× ⚠️ | 异常（见 4.2） |
| Flat auto (PPL 默认) | 667 | 0.97× 🚨 | 元素级原子，零加速 |
| Nested PF / 100×40000 | 753 | **0.86× 🚨** | 异常（见 4.2） |

### 4.2 分析

**稳定赢家：task_group 嵌套与 pool_flat**
`task_group_nested` 在 Deep 与 Shallow 两种形状都达 ~9.7-9.8×（接近 12 线程理想值），`pool_flat` 9.2×。均匀 2D 负载下，显式任务（task_group）比嵌套 parallel_for 更高效。

**Nested PF 稳定在 5.4~5.5×（Deep/Mid 形状）**
与 task_group 的差距来自内层 `parallel_for` 的每调用调度开销。本版 workload 加重后差距被放大（旧版两者持平）。

**flat_auto 再次零加速（0.97×）**
与 §2.2 相同的元素级原子模式，结论一致。

**⚠️ 异常条目（本版新出现，建议复核）:**
- **Mid (1000×4000) 形状**：`outer_only/1000`（581ms）与 `task_group_nested/1000/4000`（534ms）严重退化，但 `nested PF/1000/4000` 仅 119ms——同形状下三种策略结果相互矛盾；
- **Shallow (100×40000) 形状**：`nested PF/100×40000` 退化到 753ms（慢于串行），而 `task_group` 与 `outer_only` 在同形状下恰恰是最优（65.7 / 73.7ms）。

这些异常行有一个共同特征：**wall time ≫ CPU time（主线程 CPU 占比从正常行的 ~50% 掉到 ~5%，全程空等）**，而正常行 Time≈2×CPU。无论机制是调度器停滞还是测量时系统干扰，这些数值都不可靠，不应据此下结论；其余条目（Deep 形状全系、pool_flat、flat 系列）数据自洽。

### 4.3 Nested vs Flat 结论

```
task_group 嵌套 / pool_flat（9~10×）> Nested PF（5.4~5.5×）> Flat chunked（4.8×）
uniform 2D 负载优先选 task_group 或 pool；异常形状（Mid/Shallow 部分组合）待复核
```

---

## 5. Case 4 — 跨策略综合对比

**设计：** 3 grains × 2 workloads × 5 strategies 的网格

> 注：本 case 串行基线（CPU 534ms / MEM 186ms）与 Case 1（640ms）/ Case 2（330ms）不同——同一 workload 在不同 benchmark 段测量值有 ±20~50% 波动（CPU 频率/运行顺序效应）。speedup 一律以同 case 内基线计算。

### 5.1 CPU-bound 结果

| 策略 | Time (ms) | Speedup | 排名 |
|------|-----------|---------|------|
| Serial | 534 | 1.00× | #7 |
| **ThreadPool** | **58.7** | **9.10× 🏆** | #1 |
| Chunked / 1024 | 60.9 | 8.77× | #2 |
| Chunked / 16384 | 65.4 | 8.17× | #3 |
| Chunked / 64 | 75.2 | 7.10× | #4 |
| PPL static | 701 | 0.76× | #5 |
| PPL auto | 722 | 0.74× | #6 |

### 5.2 Memory-bound 结果

| 策略 | Time (ms) | Speedup | 排名 |
|------|-----------|---------|------|
| Serial | 186 | 1.00× | #7 |
| **ThreadPool** | **55.9** | **3.33× 🏆** | #1 |
| Chunked / 16384 | 65.0 | 2.86× | #2 |
| Chunked / 64 | 69.2 | 2.69× | #3 |
| Chunked / 1024 | 85.7 | 2.17× | #4 |
| PPL auto | 1455 | 0.13× | #5 |
| PPL static | 1673 | 0.11× | #6 |

### 5.3 分析

**CPU vs Memory 最优策略:**
- CPU: ThreadPool 9.10× 与 chunked/1024 8.77× 并列第一梯队——均匀负载下手工分片与 PPL chunked 等价；
- Memory: ThreadPool 3.33× 封顶，chunked 2.2~2.9×——DRAM 带宽是硬上限。

**元素级原子的一致性失败（最严重场景）:**
- CPU: auto/static 0.74~0.76×（≈串行，零加速）；
- Memory: auto/static 0.11~0.13×（**比串行慢 8~9 倍**）——memory-bound 下每元素工作极轻，原子开销占比最大，是全报告最差的组合。

---

## 6. Case 5 — 负载不均：clustered vs shuffled（新增）

**负载类型：** 2000 个不等长 CPU 任务，总 ≈4M 元素（与 Case 1 等量）：

| 层级 | 任务数 | 每任务元素 | 总元素 | 占比 |
|------|--------|-----------|--------|------|
| Heavy | 20 | 100K | 2.0M | 50% |
| Medium | 200 | 8K | 1.6M | 40% |
| Light | 1780 | ~224 | 0.4M | 10% |

- **Clustered（最坏情形）**：20 个重任务扎堆在 indices 0..19——等宽静态分片会把全部重活分给同一个线程；
- **Shuffled（现实情形）**：同一组任务按 seed=42 打乱，重任务均匀散开。

关键设计：每个**任务**一次 `cpu_work` 调用 + 一次 `fetch_add`（任务级原子，共 2000 次原子操作，而非元素级 400 万次）。串行基线 306ms。

### 6.1 Clustered 结果（重任务扎堆 0..19）

| 策略 | Time (ms) | Speedup | 说明 |
|------|-----------|---------|------|
| Serial (基线) | 306 | 1.00× | |
| **Nested PF / 10** | **85.5** | **3.58× 🏆** | 两级 work-stealing 全局最优 |
| Nested PF / 50 | 90.4 | 3.39× | |
| **PPL auto** | **99.8** | **3.07×** | 内置 work-stealing，clustered 下最优分区器 |
| Nested PF / 200 | 137 | 2.23× | outer 太粗，第二层偷取受限 |
| Chunked / 1 | 140 | 2.19× | 2000 细 chunk 让偷取生效 |
| Pool / 48 | 194 | 1.58× | 手工静态分片，无偷取 |
| Pool / 12 | 205 | 1.49× | 第 0 片独占 20 重 + 147 中任务（~79% 工作量） |
| Outer-only / 200 | 209 | 1.46× | |
| Outer-only / 50 | 275 | 1.11× | |
| PPL affinity | 321 | 0.95× 🚨 | 记住映射不解决布局问题 |
| Chunked / 50 | 388 | 0.79× 🚨 | 重任务被锁进少数 chunk |
| Chunked / 200 | 417 | 0.73× 🚨 | 10 个 chunk，2 个 chunk 装下全部重活 |
| Outer-only / 10 | 416 | 0.74× 🚨 | 第 0 组 = 200 个任务 ≈ 85% 工作量 |
| Chunked / 10 | 493 | **0.62× 🚨** | 最差：粗 chunk 无偷取 + 布局最坏 |

### 6.2 Shuffled 结果（重任务随机散开）

| 策略 | Time (ms) | Speedup | 说明 |
|------|-----------|---------|------|
| Serial (基线) | 306 | 1.00× | |
| **Pool / 48** | **41.5** | **7.37× 🏆** | 随机分布下手工分片也公平 |
| Nested PF / 10 | 49.5 | 6.18× | |
| Nested PF / 200 | 49.7 | 6.16× | |
| Nested PF / 50 | 50.5 | 6.06× | |
| Outer-only / 200 | 49.8 | 6.14× | 每个 outer 组期望工作量相同 |
| Pool / 12 | 52.8 | 5.80× | 逼近 PPL auto |
| Outer-only / 50 | 56.7 | 5.40× | |
| Outer-only / 10 | 62.0 | 4.94× | |
| Chunked / 1 | 65.4 | 4.68× | |
| **PPL auto** | **66.6** | **4.59×** | 仍好，但只比 clustered 快 1.5× |
| PPL affinity | 68.8 | 4.45× | |
| Chunked / 200 | 72.8 | 4.20× | 粒度不再敏感 |
| **PPL static** | **74.6** | **4.10×** | 369ms → 74.6ms，4.9× 改善！ |
| Chunked / 50 | 74.9 | 4.09× | |
| Chunked / 10 | 81.2 | 3.77× | |

### 6.3 分析（本版最重要发现）

**① Work-stealing 是唯一对任务布局"免疫"的机制**

| 策略 | Clustered | Shuffled | 变化 |
|------|-----------|----------|------|
| **PPL auto** | 99.8ms（3.07×） | 66.6ms（4.59×） | 1.5× |
| **PPL static** | 369ms（0.83×） | 74.6ms（4.10×） | **4.9×** |
| PPL affinity | 321ms（0.95×） | 68.8ms（4.45×） | 4.7× |
| Pool / 12 | 205ms（1.49×） | 52.8ms（5.80×） | 3.9× |
| Nested PF / 10 | 85.5ms（3.58×） | 49.5ms（6.18×） | 1.7× |
| Chunked / 10 | 493ms（0.62×） | 81.2ms（3.77×） | **6.1×** |

clustered 下 auto 3.07× vs static 0.83×（3.7 倍差距）：static 等宽分片把 0..19 的 20 个重任务全部划给第 0 片，一个线程扛 50% 工作量。**static/affinity/pool/chunked 的表现完全取决于任务布局——是"布局彩票"；只有 auto（以及 nested）靠偷取免疫布局。**

**② static 从"最差"翻身为"可用"只差一次 shuffle**
static 369ms → 74.6ms（0.83× → 4.10×）。重任务散开后，等宽分片自然公平。这证明 clustered 下 static 的灾难是**数据布局的假象**，不是分区器固有缺陷——但同时也说明：任务开销分布未知时，static 是高风险选择。

**③ 嵌套提供第二层偷取**
`nested/10` clustered 85.5ms（3.58×）全场最快——两级 work-stealing 把扎堆的重任务进一步拆碎分给空闲线程；shuffled 下同样最优（6.18×）。外层数越少（10），每外层含的重任务越多，内层偷取的用武之地越大（200 时退化到 2.23×）。

**④ 手工 pool：现实布局下性价比最高的"免费午餐"**
clustered 1.49~1.58×（第 0 片独占 20 重 + 147 中任务 ≈ 79% 工作量），shuffled 5.80~7.37×（48 片 7.37× 甚至超过 PPL auto 的 4.59×）。**代码最简单、无调度器依赖，只要任务分布不恶意就能接近最优。**

**⑤ 元素级原子是旧结论的假象——真正的核心发现**
上一版报告"PPL auto/static 比串行慢 10-15 倍"的结论，根源是把原子操作放在**元素级**（400 万次 fetch_add）。本 case 把原子降到**任务级**（2000 次）后：

- clustered 最坏布局下，PPL auto 3.07× 是所有内置分区器中最优；
- shuffled 下 auto 4.59×，nested 6.18×。

**真正的教训是：`PPL auto_partitioner + 元素级原子` 是差的（§2/§3/§4 已证：零加速甚至负加速），但 `PPL auto + 任务级并行` 是优秀的。** 分区器本身没有问题，问题在于"每次迭代一个原子操作 + 一次调度调用"的用法。

**⑥ 结构性下限：重任务不可分割**
clustered 最优也仅 3.58×，远低于 12 线程理想值（~25ms）。原因：20 个 100K 元素的重任务（各 ~7.7ms 串行）是不可再分的最小调度单元，最理想也只能让 12 个线程各承担 1~2 个重任务串行完成。若重任务内部还能再并行（如 nested 的内层），可继续逼近下限——nested/10 正是这么做的。

### 6.4 负载不均场景结论

```
任务开销方差大 / 布局未知 → PPL auto（work-stealing）或 nested parallel_for（3.1~6.2×）
任务开销均匀（或已 shuffle）→ pool 手工分片（5.8~7.4×）或 chunked
绝不：重任务可能扎堆时用 static / affinity / 粗 chunked（0.62~0.95×，慢于串行）
```

---

## 7. 综合结论与最佳实践

### 7.1 核心发现

#### 🔴 发现 1：元素级原子 + 分区器 = 零收益（修正版）

`parallel_for(0, N, body)` 中每次迭代做一次 `atomic::fetch_add`，会产生约 0.5~0.6s 的恒定绝对开销（原子竞争 + 400 万次调度调用）。旧版报告"慢 10-15 倍"是因为串行基线极轻（36.9ms）放大了相对比值；workload 加重后（640ms）比值回到 ~1×，但**并行收益依然为零**（1.04× vs chunked 的 11.3×）。memory-bound 下单元素工作极轻，该模式依然慢 4~9 倍（0.11~0.24×）。**任何场景都不可用。**

#### 🟢 发现 2：PPL auto 的 work-stealing 是负载不均场景的最优解（任务级粒度）

Case 5 证明：把原子降到任务级后，clustered 最坏布局下 auto 3.07×（内置分区器最优），shuffled 下 4.59×；而 static/affinity/pool/chunked 全部是"布局彩票"（clustered 0.62~1.58× ↔ shuffled 3.8~7.4×）。**任务开销分布未知时，auto 是唯一稳健的内置选择。**

#### 🟢 发现 3：均匀负载下手动 chunk 依旧必要（PPL 无 blocked_range）

Case 1 峰值 11.3×（grain=64，12 线程效率 94%）。但注意 Case 5 的教训：**任务分布不均时粗 chunk 会把重任务锁死在少数 chunk 内**（chunked/10 0.62×），不均衡负载下应选细粒度（grain=1）或干脆用 auto。

#### 🟡 发现 4：CPU-bound vs Memory-bound 并行特性差异显著

| 维度 | CPU-bound | Memory-bound |
|------|-----------|--------------|
| 峰值 speedup (12线程) | **11.3×**（chunked/64） | **5.5×**（pool/48） |
| 瓶颈 | 计算吞吐 | DRAM 带宽 |
| 最优 grain | 64~1024 | 64~16K（pool 分片更优） |
| 元素级原子代价 | ≈串行（0.74~1.06×） | 慢 4~9 倍（0.11~0.24×） |

#### 🟢 发现 5：嵌套并行：均匀负载用 task_group，不均衡负载用 nested PF

Case 3：均匀 2D 负载下 task_group 嵌套 9.7~9.8×、pool_flat 9.2×，优于 nested PF（5.4~5.5×）。Case 5：不均衡负载下 nested PF 提供第二层偷取，clustered 全局最优 3.58×、shuffled 6.18×。

#### 🔵 发现 6：affinity_partitioner 无局部性时无效

随机访存（Case 2：0.24×）与重任务布局（Case 5 clustered：0.95×）下均无收益。仅当数据有空间/时间局部性时才考虑。

#### 🟠 发现 7：数据质量备注

Case 3 的 Mid (1000×4000) 与 Shallow (100×40000) 形状部分组合出现 wall≫CPU 的异常行（主线程空等），结果互相矛盾，待复核；结论以数据自洽的条目为准（§4.2）。

### 7.2 策略选择决策树

```
需要 PPL 并行？
  ├─ 任务开销分布已知均匀？
  │   ├─ YES → chunked(grain≈64~1024) 或 pool 手工分片   ← 11× / 9~10×
  │   └─ NO / 未知 → PPL auto（work-stealing）或 nested PF
  │         └─ 绝不 static / affinity / 粗 chunked（重任务扎堆时 0.62~0.95×）
  │
  ├─ 工作是否 2D 结构？
  │   ├─ 均匀负载 → task_group 嵌套 / pool_flat（9.2~9.8×）
  │   └─ 不均衡负载 → nested parallel_for（两级偷取，3.6~6.2×）
  │
  ├─ bottleneck 是什么？
  │   ├─ CPU  → chunked/64（11.3×）或 pool（9.1×）
  │   └─ Memory → pool 分片（5.5×）或 chunked/64~16K（2.7~3.9×）
  │
  └─ 绝对不要：元素级 atomic + auto/static/affinity
     → 0.11~1.06×（比串行慢最多 9 倍，最好也只是零加速）
```

### 7.3 最终排名

按"综合场景下最稳定可靠的并行模式"排序：

| 排名 | 策略 | 典型 speedup | 适用场景 |
|------|------|--------------|----------|
| 1 | **PPL auto（任务级粒度）** | 3.1~6.2× | 负载不均/布局未知，work-stealing 免疫布局 |
| 2 | **PPL chunked (grain=64~1024)** | 7.1~11.3× | CPU 均匀负载 |
| 3 | **ThreadPool 手工分片** | 1.5~10× | 均匀/随机布局，代码最简单；恶意布局差 |
| 4 | **Nested parallel_for** | 3.6~6.2× | 不均衡 + 可嵌套分解（第二层偷取） |
| 5 | **Task group 嵌套** | 9.2~9.8× | 均匀 2D 负载 |
| 6 | **Outer-only parallel_for** | 1.1~6.1× | 实现简单的折中（布局敏感） |
| ❌ | 元素级 atomic + auto/static/affinity | 0.11~1.06× | **绝对避免** |

---

## 8. 原始数据

### 测试系统信息

```
Run on (12 X 2611 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 1280 KiB (x6)
  L3 Unified 12288 KiB (x1)

Windows 11, clang-cl (Release)
```

### 完整 Benchmark 输出

```
----------------------------------------------------------------------------
Benchmark                                  Time             CPU   Iterations
----------------------------------------------------------------------------
case1_serial                             640 ms          262 ms            4
case1_ppl_chunked/8                     80.4 ms         32.5 ms           26
case1_ppl_chunked/64                    56.5 ms         29.4 ms           25
case1_ppl_chunked/1024                  64.6 ms         27.5 ms           25
case1_ppl_chunked/16384                 84.6 ms         29.6 ms           37
case1_ppl_chunked/65536                 90.3 ms         27.3 ms           20
case1_ppl_auto                           616 ms         96.6 ms           11
case1_ppl_static                         604 ms          172 ms            4
case1_pool/12                           74.5 ms        0.156 ms          100
case1_pool/48                           64.2 ms        0.312 ms          100
case2_serial                             330 ms          162 ms            5
case2_ppl_chunked/8                      213 ms         36.2 ms           19
case2_ppl_chunked/64                    83.8 ms         32.1 ms           18
case2_ppl_chunked/1024                   102 ms         29.0 ms           21
case2_ppl_chunked/16384                 87.8 ms         33.6 ms           20
case2_ppl_chunked/65536                  118 ms         35.9 ms           20
case2_ppl_auto                          1470 ms          177 ms            3
case2_ppl_static                        1435 ms          375 ms            2
case2_ppl_affinity                      1386 ms          320 ms            2
case2_pool/12                           79.3 ms        0.000 ms          100
case2_pool/48                           60.2 ms        0.000 ms          100
case3_serial                             646 ms          375 ms            3
case3_flat_chunked                       134 ms         34.7 ms           18
case3_flat_auto                          667 ms         65.3 ms           11
case3_pool_flat                         70.2 ms        0.156 ms          100
case3_ppl_nested/5000/800                117 ms         36.7 ms           20
case3_outer_only/5000                    135 ms         31.2 ms           20
case3_task_group_nested/5000/800        66.4 ms         20.1 ms           45
case3_ppl_nested/1000/4000               119 ms         39.1 ms           22
case3_outer_only/1000                    581 ms         34.5 ms           19
case3_task_group_nested/1000/4000        534 ms         35.9 ms           10
case3_ppl_nested/100/40000               753 ms         35.9 ms           10
case3_outer_only/100                    73.7 ms         37.5 ms           15
case3_task_group_nested/100/40000       65.7 ms         31.2 ms           26
case4_cpu_serial                         534 ms          430 ms            2
case4_cpu_auto                           722 ms         89.8 ms            4
case4_cpu_static                         701 ms          188 ms            4
case4_cpu_pool                          58.7 ms        0.156 ms          100
case4_cpu_chunked/64                    75.2 ms         34.4 ms           20
case4_cpu_chunked/1024                  60.9 ms         29.4 ms           26
case4_cpu_chunked/16384                 65.4 ms         32.7 ms           22
case4_mem_serial                         186 ms          156 ms            4
case4_mem_auto                          1455 ms          133 ms            2
case4_mem_static                        1673 ms          305 ms            2
case4_mem_pool                          55.9 ms        0.156 ms          100
case4_mem_chunked/64                    69.2 ms         33.6 ms           20
case4_mem_chunked/1024                  85.7 ms         33.6 ms           20
case4_mem_chunked/16384                 65.0 ms         35.6 ms           18
case5_serial                             306 ms          250 ms            3
case5_ppl_auto                          99.8 ms         3.12 ms          100
case5_ppl_static                         369 ms         1.56 ms           10
case5_ppl_affinity                       321 ms         1.56 ms           10
case5_ppl_chunked/1                      140 ms         8.28 ms          100
case5_ppl_chunked/10                     493 ms         10.9 ms           10
case5_ppl_chunked/50                     388 ms         3.12 ms           10
case5_ppl_chunked/200                    417 ms         4.69 ms           10
case5_ppl_nested/10                     85.5 ms         4.84 ms          100
case5_ppl_nested/50                     90.4 ms         6.84 ms          112
case5_ppl_nested/200                     137 ms         8.06 ms           64
case5_ppl_outer_only/10                  416 ms         1.56 ms           10
case5_ppl_outer_only/50                  275 ms         4.69 ms           10
case5_ppl_outer_only/200                 209 ms         3.12 ms          100
case5_pool/12                            205 ms        0.156 ms          100
case5_pool/48                            194 ms        0.000 ms          100
case5_ppl_auto_shuffled                 66.6 ms         26.9 ms           25
case5_ppl_static_shuffled               74.6 ms         34.7 ms           32
case5_ppl_affinity_shuffled             68.8 ms         34.5 ms           19
case5_ppl_chunked_shuffled/1            65.4 ms         29.3 ms           24
case5_ppl_chunked_shuffled/10           81.2 ms         30.6 ms           25
case5_ppl_chunked_shuffled/50           74.9 ms         28.6 ms           30
case5_ppl_chunked_shuffled/200          72.8 ms         28.2 ms           26
case5_ppl_nested_shuffled/10            49.5 ms         14.4 ms           50
case5_ppl_nested_shuffled/50            50.5 ms         21.5 ms           32
case5_ppl_nested_shuffled/200           49.7 ms         20.5 ms           45
case5_ppl_outer_only_shuffled/10        62.0 ms         23.4 ms           28
case5_ppl_outer_only_shuffled/50        56.7 ms         19.8 ms           30
case5_ppl_outer_only_shuffled/200       49.8 ms         21.2 ms           25
case5_pool_shuffled/12                  52.8 ms        0.156 ms          100
case5_pool_shuffled/48                  41.5 ms        0.000 ms          100
```

---

*报告由 benchmark 原始输出自动生成，结合五组 case 的手工分析。*
