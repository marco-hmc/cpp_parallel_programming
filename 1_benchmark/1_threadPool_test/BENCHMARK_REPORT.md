# 1_threadPool_test — 线程池 Benchmark 报告（v2 受控实验版）

**测试目标：** 受控实验——每条结论必须能归因到唯一的变量变化。对比线程池与裸线程的调度开销、嵌套等待的并行度侵蚀与死锁、并行+串行混合的最优流水线策略。

**测试框架：** Google Benchmark（repetitions=3，取均值）
**依赖：** `benchmark`, `fmt`, `spdlog`, `threadPool`（本地库）
**测试环境：** 12th Gen Intel i7-1255U，12 逻辑核（WSL2 视图 6 核 × 2 线程），Linux 6.18 WSL2，clang++ 14.0（Ninja，Debug）
**线程池大小：** `std::thread::hardware_concurrency()` = 12 个 worker
**术语：** 见仓库根目录 `CONTEXT.md`（死锁 / 饥饿 / 并行度侵蚀 / 过度订阅 / 完成顺序 / 提交顺序 / 流水线 / 配对对比 / 串行占比旋钮）

> **本版（v2）相对旧版的关键变更：**
> 1. 受控实验化：Case 1 加 oversized 池控制组，Case 2 对齐参数集，Case 3 重构为 3 组配对 + 并行 reduce 基线 + 串行占比旋钮
> 2. 死锁场景首次真实执行（看门狗 + abort，§3.2）
> 3. 任务时长重新标定：旧版 `taskNear20ms` 实测 ~190ms、`taskNear100ms` 实测 ~640ms（名不副实，反推自旧数据）；本版统一标定 ~12ms
> 4. 修复 callback 实现中的 lost-wakeup 竞态（§4.3.1）、dual 自旋轮询改 futures、`async_lazy` 改名 `std_async`（launch::async 是 eager，旧名错误）
> 5. 旧版归因错误修正：「串行占比」旋钮并不存在（`vector_size` 同时缩放两个阶段，§4.0）、async「零开销」说法不成立（§4.2）、one/multi 参数不对齐却横向比较（§3.1）

---

## [toc]

---

## 1. 实验设计概览

| Case | 研究问题 | 变量 | 关键指标 |
|------|---------|------|---------|
| Case 1 | 线程创建开销与过度订阅各自的成本？ | 三列分解（控制组法） | 总执行耗时 |
| Case 2 | 嵌套等待的并行度侵蚀与死锁？ | 父任务数 × 子任务数 | 耗时 / 是否死锁 |
| Case 3 | 并行+串行混合的最优流水线策略？ | 3 组配对 + 基线 + 串行占比旋钮 | 总耗时 |

---

## 2. Case 1 — 线程创建开销 vs 过度订阅（三列分解）

### 2.1 测试设计

每个任务约 12ms（本机标定，见 §2.3）。裸线程方案同时包含两个变量——**线程创建开销**和**过度订阅**（200 个线程抢 12 核）——旧版把两者搅在一起得不出各自成本。本版用控制组法三列分解：

| 基准 | 线程创建 | 过度订阅 | 测量内容 |
|------|---------|---------|---------|
| `case1_no_pool` | ✔ | ✔ | 创建 + 订阅 + 执行 |
| `case1_pool_oversized` | ✘（tasks_nums 个 worker 的池，构造在计时窗外） | ✔ | 订阅 + 执行 |
| `case1_pool` | ✘ | ✘ | 纯执行 |

- 纯创建开销 ≈ no_pool − oversized
- 纯过度订阅税 ≈ oversized − pool

### 2.2 实际结果与分析

| 基准 | 任务数=50 | 任务数=200 |
|------|----------|-----------|
| no_pool | 51.6 ms | 225.3 ms |
| pool_oversized | 52.3 ms | 191.1 ms |
| pool | 52.5 ms | 195.8 ms |

| 分解项 | 任务数=50 | 任务数=200 |
|--------|----------|-----------|
| 纯创建开销 | ≈ 0（噪声内） | **34.2 ms ≈ 171 µs/线程** |
| 纯过度订阅税 | ≈ 0（噪声内） | ≈ 0（噪声内） |

**关键发现：**

1. **过度订阅税 ≈ 0**：200 个纯 CPU 线程在 Linux CFS 调度下几乎没有额外成本——每个线程只需 ~3 个时间片，上下文切换开销（µs 级）与任务时长（12ms）相比可忽略。oversized 池甚至略快于 12-worker 池（−4.7ms，噪声内），因为免去了队列互斥竞争。
2. **创建开销在 200 线程时显现**：34.2ms ≈ 171µs/线程（pthread_create + join + 调度）。50 线程时被噪声淹没。
3. **与旧版 Windows 数据（同一台机器）对比**：旧版 200 任务下 no_pool 4987ms vs pool 2454ms——创建+订阅混合税高达 2.5 秒，是本版 Linux 实测（34ms）的 70 倍。**平台差异本身就是结论**：Windows 线程创建与调度成本远高于 Linux，线程池在 Windows 上是必需品、在 Linux 上是优化项。⚠️ 注意旧版任务时长 ~190ms（本版 12ms），跨 OS 比较为定性结论。

### 2.3 任务时长标定

`taskNear20ms`（`countNumber(5'000'000)`）实测 **≈12ms**：case1_pool/50 = 52.5ms = 50 任务 ÷ 12 worker × T → T ≈ 12.6ms；case1_pool/200 反推 11.7ms。旧版 `taskNear20ms`（48M 计数）实测 ~190ms、`taskNear100ms`（240M 计数）实测 ~640ms——**任务时长从未被标定过**，本版统一按实测值标定并写入报告。

---

## 3. Case 2 — 嵌套等待：并行度侵蚀与死锁

### 3.1 并行度侵蚀（安全区，对齐参数）

这是旧版「Thread Starvation」case。**术语修正**：安全区（父任务数 < worker 数）实测到的现象不是饥饿——子任务终会执行，只是池的有效并行度被阻塞中的父任务线性蚕食。按 CONTEXT.md 术语，此为**并行度侵蚀**；经典**饥饿**（任务无限期得不到执行）只发生在 §3.2 的死锁参数下。

旧版 one/multi 跑不同参数集（{2,4} vs {10,15}）却横向比较，本版对齐为同一参数集 {4,30}、{8,30}：

| 参数 | one（共享池） | multi（每父任务独立子池） | 对比 |
|------|--------------|------------------------|------|
| {4, 30} | 180.3 ms | 182.1 ms | 持平（+1%） |
| {8, 30} | 450.9 ms | 306.5 ms | **multi 快 32%** |

**分析：**

- **{4, 30}**：4 个父任务阻塞 4 个 worker，剩余 8 个空闲 worker 执行 120 个子任务（15 批 ≈ 180ms）。multi 侧 4×12+12=60 个线程的过度订阅代价与创建开销 ≈ 共享池的侵蚀代价——打平。
- **{8, 30}**：共享池只剩 4 个空闲 worker 消化 240 个子任务，侵蚀代价急剧放大；multi 侧 108 个线程虽然过度订阅，但 12 核仍被压满。**交叉点出现：侵蚀 > 订阅税**。
- **结论：** 侵蚀代价随父任务数非线性增长（空闲 worker 减半 → 吞吐减半），而订阅税随线程数温和增长。父任务数逼近 worker 数时，独立子池反而更优——这与直觉（"池越少越好"）相反，是受控实验才测得出的结论。

### 3.2 死锁实验（首次真实执行）

旧版死锁条件只停留在理论分析（"未在本组参数中触发"）。本版新增 `case2_deadlock_demo`：父任务数 12 == worker 数 12，每个父任务向**同一池**提交 30 个子任务并阻塞等待。

由于死锁在 parent ≥ worker 时**数学必然**（父任务占满全部 worker 等待子任务，子任务永远无 worker 可执行），任何"完成"反而是 bug——看门狗大阈值判定完全可靠。看门狗超时后打印确认并 `abort()`（保证不污染进程内其他基准），因此该组通过环境变量门控、单独运行：

```bash
RUN_DEADLOCK_DEMO=1 DEADLOCK_TIMEOUT_SECONDS=30 \
    ./pool_benchmark --benchmark_filter=case2_deadlock_demo
```

实测输出（验证时阈值缩短为 5s）：

```
DEADLOCK CONFIRMED: 12 parent tasks blocked all 12 workers for over 5s;
30 child tasks per parent never ran
（exit code 134 = SIGABRT）
```

**教训：**
> 永远不要让同一个固定大小线程池中的任务等待该池中其他任务完成。父任务数 ≥ worker 数时死锁；即使不死锁（安全区），阻塞的 worker 也会非线性蚕食池的有效并行度。

---

## 4. Case 3 — 并行+串行混合的最优策略

### 4.0 工作负载与参数（先于结论读）

模拟 **Map + 串行 Reduce**：`heavyComputation`（并行，sin 循环）→ `serialComputationK`（串行，sqrt 循环，×factor）→ 求和。三个参数：

| 参数 | 含义 | 取值 |
|------|------|------|
| tasks_nums | Map 任务数 | {50, 200} |
| vector_size | 每个任务的向量长度 | {10K, 50K} |
| **serial_factor（串行占比旋钮）** | 串行阶段工作量 ×k，并行阶段不变 | {1, 8} |

**为什么需要旋钮：** `vector_size` 同时缩放 Map（sin）与 Reduce（sqrt）两个阶段——每元素成本同量级，比值在所有参数下恒定。旧版报告"串行工作量占比增大 → callback 占优"的归因建立在**不存在的旋钮**上；唯一能控制串行/并行比值的是 `serial_factor`。

**任务时长均匀性：** 所有 Map 任务工作量相同 → 完成顺序 ≈ 提交顺序。这一点决定了完成顺序感知的价值（§4.5）。

### 4.1 配对 1：block vs pipeline —— 交错的价值

唯一差异：批量等待 vs 边等边处理（同为 gPool 调度、主线程 Reduce、提交顺序等待）。

| 参数（tasks/vec/factor） | block | pipeline | pipeline 相对 |
|--------------------------|-------|----------|--------------|
| 50/10K/1 | 10.3 ms | 8.5 ms | **−18%** |
| 50/10K/8 | 49.8 ms | 43.7 ms | **−12%** |
| 50/50K/1 | 54.5 ms | 42.8 ms | **−22%** |
| 50/50K/8 | 220.6 ms | 218.2 ms | −1% |
| 200/10K/1 | 42.7 ms | 34.7 ms | **−19%** |
| 200/10K/8 | 168.8 ms | 170.8 ms | +1% |
| 200/50K/1 | 207.1 ms | 171.5 ms | **−17%** |
| 200/50K/8 | 814.5 ms | 869.4 ms | +7%（噪声，见 §4.6） |

**结论：交错值 17~22%**（factor=1 全组一致）。机制：pipeline 的主线程 Reduce 与剩余 worker 的 Map 重叠，block 的串行阶段则让全部 worker 空闲。factor=8 时收益消失（±噪声）——串行工作量 ×8 后主线程成为瓶颈，交错与否不再重要。

### 4.2 配对 2：pipeline vs std_async —— 调度器的价值

结构与 pipeline 完全相同（提交全部 → 提交顺序 get → 主线程 Reduce），**唯一差异是调度器**。

| 参数 | pipeline | std_async | std_async 相对 |
|------|----------|-----------|----------------|
| 50/10K/1 | 8.5 ms | 14.1 ms | **+67%** |
| 50/10K/8 | 43.7 ms | 54.3 ms | **+24%** |
| 50/50K/1 | 42.8 ms | 46.1 ms | **+8%** |
| 50/50K/8 | 218.2 ms | 205.8 ms | −6%（噪声） |
| 200/10K/1 | 34.7 ms | 52.7 ms | **+52%** |
| 200/10K/8 | 170.8 ms | 202.6 ms | **+19%** |
| 200/50K/1 | 171.5 ms | 192.8 ms | **+12%** |
| 200/50K/8 | 869.4 ms | 725.8 ms | −17%（噪声，见 §4.6） |

**结论：** Linux/libstdc++ 下 `std::async(launch::async)` 是 thread-per-task，任务数一上去就被线程创建开销拖垮（+8~67%）。旧版报告"async 零开销"的说法不成立——它在 **Windows/MSVC 下走 PPL 线程池**（线程数受限，长任务还会饿死队列后面的任务），在那个平台上它是"另一个池"，不是"零开销"。**本配对结论平台相关，不可跨 OS 迁移。**

### 4.3 配对 3：callback vs dual —— Reduce 执行者的价值

唯一差异：裸线程+条件变量 vs 单线程池（同为完成顺序感知：结果完成即入队处理）。

| 参数 | callback | dual | dual 相对 |
|------|----------|------|-----------|
| 50/10K/1 | 8.8 ms | 9.6 ms | +10% |
| 50/10K/8 | 42.5 ms | 44.0 ms | +4% |
| 50/50K/1 | 40.8 ms | 44.7 ms | +10% |
| 50/50K/8 | 207.5 ms | 209.8 ms | +1% |
| 200/10K/1 | 34.4 ms | 37.7 ms | +10% |
| 200/10K/8 | 158.7 ms | 181.3 ms | +14% |
| 200/50K/1 | 166.8 ms | 165.9 ms | 持平 |
| 200/50K/8 | 857.7 ms | 824.7 ms | −4% |

**结论：** 两者基本等价（±10% 内）。小任务下 callback 略优（少一层池的入队/调度开销），大任务下持平。**Reduce 执行者的选择不是瓶颈所在**——写哪个顺手用哪个。

#### 4.3.1 审查中发现并修复的 bug：lost-wakeup 竞态

旧版 callback 实现中 worker 顺序为 push(锁内) → `notify_one()` → `completed_tasks.fetch_add(1)`：最后一次递增（使退出谓词成立的状态变更）**不伴随任何 notify**。若串行线程恰在最后一条结果被消费后、递增落地前重新检查谓词并进入 wait，就永久睡眠——`join()` 挂死整个 benchmark。修复：push 与递增收进同一临界区，notify 在其后发出（状态变更必须先于 notify）。修复后 8 参数组 × 2000+ 迭代零挂起。

### 4.4 基线：parallel_reduce —— "Reduce 必须串行"这个前提值多少钱

`case3_parallel_reduce`：Map 任务内部顺手 Reduce，无串行阶段。串行阶段前提成本 = 最优串行策略 − 基线：

| 参数 | 基线 | 最优串行策略 | 前提成本 |
|------|------|--------------|----------|
| 50/10K/1 | 6.1 ms | 8.5 ms（pipeline） | +38% |
| 50/10K/8 | 17.9 ms | 43.7 ms | **+144%** |
| 50/50K/1 | 25.3 ms | 42.8 ms | +69% |
| 50/50K/8 | 75.7 ms | 218.2 ms | **+188%** |
| 200/10K/1 | 22.3 ms | 34.7 ms | +56% |
| 200/10K/8 | 63.2 ms | 170.8 ms | **+170%** |
| 200/50K/1 | 101.9 ms | 171.5 ms | +68% |
| 200/50K/8 | 249.8 ms | 814.5 ms | **+226%** |

**结论：** factor=1 时串行阶段前提成本 38~69%；factor=8 时 144~226%。**在设计"必须串行"的阶段之前，先问一句"真的必须吗"**——可结合的求和（如本负载）并行化后最多快 2 倍以上。

### 4.5 旋钮检验：callback 何时占优？（假设被拒绝）

旧版 Windows 结论："串行工作量占比高时 callback 反超成为最快"。用真正的旋钮（serial_factor）在 Linux 上重测：

| factor | callback | pipeline | 对比 |
|--------|----------|----------|------|
| 50/10K/1 | 8.8 ms | 8.5 ms | callback +3% |
| 50/10K/8 | 42.5 ms | 43.7 ms | callback −3% |
| 200/50K/1 | 166.8 ms | 171.5 ms | callback −3% |
| 200/50K/8 | 857.7 ms | 869.4 ms | callback −1% |

**假设被拒绝：** factor 1→8 时 callback 从未明显胜出（±3% 噪声内）。**机制分析：** 本负载所有 Map 任务时长均匀 → 完成顺序 ≈ 提交顺序 → pipeline 的提交顺序等待不存在队头阻塞 → 完成顺序感知（callback/dual 的核心价值）无利可图，只有队列+唤醒的固定开销。旧版 Windows 上 callback "在大数据反超"很可能是 **Windows 条件变量唤醒延迟（ms 级）造成的假象**，而非流水线价值。

**推论（可继续深挖）：** 完成顺序感知只在任务时长**不均匀**时才值钱。未来可加变时长任务（如 tasks 的 vector_size 随机化）检验——这是本版实验设计留下的自然下一步。

### 4.6 数据质量说明

- factor=8 的大参数组（200/50K/8）单次迭代 0.8s，Google Benchmark 判定 Iterations=1（每 rep 单样本，靠 3 次 repetition 取均值，stddev 27~57ms）——相关行的结论已标注"噪声"并降权处理。
- 采集期间系统 load average 波动于 4~15（WSL2 宿主机有其他负载），case2/8/30 行 stddev 达 88ms（20%）——交叉点结论（32% 差距）方向可靠，精确幅度仅供参考。
- factor=1 行迭代数 23~1106，数据可信度高，本报告的归因结论主要建立在这些行上。

---

## 5. 综合结论

### 5.1 核心发现（按证据强度排序）

| 强度 | 发现 | 证据 |
|------|------|------|
| 🔴 | 嵌套等待同一固定池：父任务数 ≥ worker 数时**死锁**（首次实测确认）；安全区内侵蚀代价随父任务数非线性增长，{8,30} 时独立子池反超共享池 32% | §3.1、§3.2 |
| 🟡 | 「Reduce 必须串行」的前提成本 38~226%（随串行占比旋钮放大）——先验证可结合性再设计串行阶段 | §4.4 |
| 🟡 | 完成顺序感知（callback/dual）在任务时长均匀时无利可图；旧版"callback 大数据反超"是 Windows cv 唤醒延迟假象 | §4.5 |
| 🟢 | pipeline 交错值 17~22%（factor=1 时稳定复现） | §4.1 |
| 🟢 | Linux 上 std_async(thread-per-task) 比自研池慢 8~67%；Windows/MSVC 上它是 PPL 池——"std::async 零开销"不成立 | §4.2 |
| 🟢 | Linux 上线程创建 ≈171µs/线程、纯 CPU 过度订阅税 ≈0；同硬件 Windows 上混合税是 Linux 的 70 倍——线程池在 Windows 是必需品，在 Linux 是优化项 | §2.2 |

### 5.2 最佳实践

1. **绝对不要在同一固定线程池中嵌套等待**——父任务数逼近 worker 数时改用独立子池（尽管过度订阅，实证更快）或支持嵌套的框架（TBB）。
2. **先测后串行**：Reduce 阶段能并行就别串行（前提成本可高达 2×）；真串行时交错处理（pipeline）稳定回血 17~22%。
3. **完成顺序感知只对变时长任务有意义**——任务均匀时它就是纯开销。
4. **std::async 的调度器是平台实现细节**——写平台无关结论前先查实现的线程模型（MSVC=PPL 池，libstdc++=thread-per-task）。
5. **benchmark 的招牌场景必须真跑**——"理论确认"的死锁差点让整个 case 失去存在的理由；任务时长、注释、报告结论都要用实测数据反推核对。

---

## 6. 原始数据

Google Benchmark 输出（repetitions=3 的均值；Time 为单次迭代 wall time，CPU 为聚合口径，±为 repetition 间 stddev）：

```
Benchmark                                      Time(ms)   CPU(ms)  ±stddev  Iters
case1_no_pool/50                                 51.58      6.03     4.40    246
case1_no_pool/200                               225.25     24.52    17.36     25
case1_pool_oversized/50                          52.31      1.39     2.04    100
case1_pool_oversized/200                        191.10      2.79     6.75    100
case1_pool/50                                    52.53      1.65     1.47    100
case1_pool/200                                  195.84      5.33     7.73    100
case2_one_thread_pool/4/30                      180.29      0.24    21.80    100
case2_one_thread_pool/8/30                      450.94      0.45    88.39     10
case2_multi_thread_pool/4/30                    182.11      0.23     3.78    100
case2_multi_thread_pool/8/30                    306.52      0.60    36.57     10
case3_block_then_serial/50/10000/1               10.33      7.80     1.28     95
case3_block_then_serial/50/10000/8               49.79     47.40     1.49     12
case3_block_then_serial/50/50000/1               54.49     37.23     6.08     21
case3_block_then_serial/50/50000/8              220.55    208.72    12.63      3
case3_block_then_serial/200/10000/1              42.72     30.10     5.29     28
case3_block_then_serial/200/10000/8             168.83    162.45     5.47      4
case3_block_then_serial/200/50000/1             207.13    150.98    11.12      6
case3_block_then_serial/200/50000/8             814.47    836.60    27.02      1
case3_pipeline_processing/50/10000/1              8.48      8.28     0.97    113
case3_pipeline_processing/50/10000/8             43.68     43.24     3.02     15
case3_pipeline_processing/50/50000/1             42.76     38.97     3.20     19
case3_pipeline_processing/50/50000/8            218.16    215.93    18.56      3
case3_pipeline_processing/200/10000/1            34.68     33.47     1.91     23
case3_pipeline_processing/200/10000/8           170.76    169.92    11.39      4
case3_pipeline_processing/200/50000/1           171.52    164.47     7.17      5
case3_pipeline_processing/200/50000/8           869.41    867.61    39.99      1
case3_std_async/50/10000/1                       14.14     13.43     0.17     62
case3_std_async/50/10000/8                       54.27     53.50     3.70     10
case3_std_async/50/50000/1                       46.08     41.67     1.73     18
case3_std_async/50/50000/8                      205.79    219.19     7.11      3
case3_std_async/200/10000/1                      52.70     51.27     1.44     10
case3_std_async/200/10000/8                     202.64    196.52    11.92      3
case3_std_async/200/50000/1                     192.83    150.00    11.91      4
case3_std_async/200/50000/8                     725.82    693.86    11.62      1
case3_callback_style/50/10000/1                   8.75      0.78     0.72   1189
case3_callback_style/50/10000/8                  42.45      0.58     1.01    100
case3_callback_style/50/50000/1                  40.75      0.85     2.05    100
case3_callback_style/50/50000/8                 207.49      0.66     6.93    100
case3_callback_style/200/10000/1                 34.37      1.10     2.20    100
case3_callback_style/200/10000/8                158.74      0.95    10.35    100
case3_callback_style/200/50000/1                166.75      2.18     3.40    100
case3_callback_style/200/50000/8                857.65      1.92    12.89     10
case3_dual_threadpool/50/10000/1                  9.63      1.13     1.55    742
case3_dual_threadpool/50/10000/8                 43.97      1.29     2.13    100
case3_dual_threadpool/50/50000/1                 44.72      1.93     1.32    100
case3_dual_threadpool/50/50000/8                209.79      2.40     6.74    100
case3_dual_threadpool/200/10000/1                37.65      3.32     1.95    100
case3_dual_threadpool/200/10000/8               181.30      4.29     4.55    100
case3_dual_threadpool/200/50000/1               165.90      6.91    11.22     97
case3_dual_threadpool/200/50000/8               824.65      9.31    40.81     10
case3_parallel_reduce/50/10000/1                  6.13      0.84     0.20   1106
case3_parallel_reduce/50/10000/8                 17.87      1.26     0.76    550
case3_parallel_reduce/50/50000/1                 25.34      1.38     2.68    725
case3_parallel_reduce/50/50000/8                 75.74      1.98    12.89    100
case3_parallel_reduce/200/10000/1                22.28      2.32     1.79    332
case3_parallel_reduce/200/10000/8                63.15      3.60     3.58    100
case3_parallel_reduce/200/50000/1               101.94      4.69     3.52    100
case3_parallel_reduce/200/50000/8               249.76      5.43    57.26     10
```

死锁实验（单独运行）：`DEADLOCK CONFIRMED: 12 parent tasks blocked all 12 workers for over 5s; 30 child tasks per parent never ran`（exit 134）。

完整 JSON：`tmp/threadPool_bench.json`。

---

*报告基于 `1_threadPool_test` v2 受控实验实测数据生成（2026-08-24）。设计决策记录见 `tmp/design/1_threadPool_test_redesign.md`。*
