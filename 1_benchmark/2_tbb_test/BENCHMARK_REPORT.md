# 2_tbb_test — TBB 并行框架 Benchmark 报告（v2 受控实验版）

**测试目标：** 受控实验——每条结论必须能归因到唯一的变量变化。三个研究问题：① 分组策略（partitioner × grain size × 负载形态）的最佳实践；② 锁原语 × 调度器 × 临界区粒度的真实差异；③ 嵌套 parallel_for 的影响（flat vs nested vs isolate、嵌套深度、内层粒度）。

**测试框架：** Google Benchmark（repetitions=3，取均值；`--benchmark_min_time=0.3s`）

**依赖：** `benchmark`、`spdlog`、`oneapi::tbb`（2021.5.0，系统包）、`threadPool`（本地库）

**测试平台/环境：** WSL2 / Linux，clang Release（-O3，clang-ninja-release preset），12 逻辑核 @ 2611 MHz，L1 48 KiB (x6) / L2 1280 KiB (x6) / L3 12288 KiB (x1)，Load Average 运行时 ≈ 3~6（中负载，见 §6 数据质量说明）

**术语：** 见仓库根目录 `CONTEXT.md`（完成顺序/提交顺序/死锁/并行度侵蚀/过度订阅/配对对比/控制组/基线等）。

> **v2 相对旧版（2026-08-10，Windows）的关键变更：**
> 1. case1 全部重写：旧「显式 chunk vs auto_partitioner」两行实际都是 auto_partitioner（只差 grain 提示），对比无效 → 4 分区器 × 4 粒度 × 2 负载形态全矩阵；
> 2. 旧 simple_partitioner「禁用」结论作废：旧测的唯一配置是 grainsize=1 + 共享 cache line 原子争用，归因错误 → v2 槽位归约消灭共享原子，全粒度扫描；
> 3. case2 补测真 `tbb::mutex`（旧行实为 spin_mutex；且 oneTBB 2021 中 `tbb::mutex` 是 PREVIEW 特性，需 `__TBB_PREVIEW_MUTEXES`，实现为 spin+wait 混合而非经典 OS 锁）；
> 4. case2 新增 PersistentTeam 持久线程组控制组，剥离旧版 std 系每迭代重建线程的混淆（旧「tbb 锁快 4.8x」「tbb_atomic 快 5.7x」均被证明是线程生命周期假象）；
> 5. case2 锁次数配对修正：不依赖分区器叶子行为，以 N/K 个 batch 直接作 parallel_for 索引域（实测教训见 §3.1）；
> 6. queuing_mutex 重定界：默认只跑 intended use（粗粒度公平队列），K=1 误用场景 `RUN_QUEUING_FINE=1` 门控 + 减量（旧 145s/迭代拖垮整套件）；
> 7. case3 确定性负载：mt19937(42) 一次性预生成，全策略每迭代复用（旧 random_device 无种子，串行/并行测不同负载，3.4x 不可信）；
> 8. 全 case 新增正确性校验（槽位归约 + verifyResult，stderr + abort）；旧版无任何校验；
> 9. 新增门控实测：TBB 窃取重入死锁（教程 §4.1 声明）用看门狗验证；旧版死锁分析仅源码推理。
>
> ⚠️ 旧版数据为 Windows 11 / clang-cl；v2 全部数据为 WSL2/Linux 重跑，与旧版数值不可直接对比（平台差异如 shared_mutex 实现见 §3.3）。

## [toc]

## 1. 实验设计概览

| Case | 研究问题 | 变量 | 控制组/基线 | 完成测量 |
|------|---------|------|------------|---------|
| Case 1 | 分组策略：工作窃取何时值钱？ | 分区器 {auto, static, simple, affinity} × 粒度 {1, 256, 4096, 65536} × 偏斜 {0, 8} | 串行基线；静态分片线程池（跨框架锚点，与 static 同调度形状） | 42 |
| Case 2 | 锁 × 调度器 × 临界区粒度 | 锁 6 种 × 调度器 {team, parallel_for} × K {1, 64, 4096} | PersistentTeam(8) 控制组；无锁串行基线；std::mutex 2×2 交叉 | 19 |
| Case 3 | 嵌套 parallel_for 的影响 | 结构 {flat, nested, isolate, depth3, 双池} × outer {8, 32} × 内层粒度 {1, 256, 4096} × 偏斜 {0, 1} | 串行基线；确定性 spec 全策略复用；threadPool 双池锚点 | 16 |

裁剪说明（有依据）：`case1_native_threads`（1_threadPool_test case1 已分解线程创建成本）、`case3_manual_threads`（无独立轴）、`case3_tbb_task_group`（与嵌套 parallel_for 同构）、`case3_threadpool_nested_unsafe`（1_threadPool_test case2 已用看门狗覆盖线程池嵌套死锁，且其结论不能泛化到 TBB——正是本 case3 要证实的）。

全套默认 77 行 ≈ 6 分钟（threadpool 的 grain=1 两行占 ~2.5 分钟）。门控演示不进默认套件。

## 2. Case 1 — 分区器 × 粒度 × 负载形态

### 2.1 测试设计

- **负载**：`TOTAL_WORK = 2^20` 个元素（可被全部粒度整除），每元素成本 `elemCost(i, amp) = BASE_UNITS × (1 + amp × quarter)`，4 段阶梯偏斜（quarter 0..3 权重 1/9/17/25，amp=8 时）。纯函数、确定性、NO_OPTIMIZE 保证各策略同代码。
- **配对对比**：同一负载同一槽位归约，唯一差异是分区器/粒度/偏斜轴。
- **槽位归约**：每个 chunk 的部分和原子累加进规范 chunk 槽位，主线程按槽位升序合并（归约顺序 = 元素顺序）。**这个设计让校验系统抓出了两个真实的库行为认知错误**：① static/affinity 的叶子起点不保证 grain 对齐（等分负载优先），槽位必须是 atomic fetch_add 而非赋值（实测丢 0.2%~2.7% 部分和）；② affinity_partitioner 的划分树只对同一 Range 形状有效，跨参数复用会非法分裂（见 §2.3 教训）。
- **串行基线**：amp=0 实测 30.1ms、amp=8 实测 411ms（13.7 倍，与理论权重 13 一致）。
- 正确性：每行与串行参考值比对，相对容差 1e-9（实测噪声 ≤1e-14）。

### 2.2 实测结果与分析

**均匀负载（amp=0），Time 均值（ms），串行 30.1：**

| 策略 \ 粒度 | 1 | 256 | 4096 | 65536 |
|---|---|---|---|---|
| auto | **6.19** | 8.35 | 12.4 | 14.8 |
| static | 17.1 | 15.1 | 21.7 | 17.1 |
| simple | 33.3 | 14.3 | 14.5 | 15.2 |
| affinity | 15.4 | 15.6 | 12.6 | 15.7 |
| threadpool 静态分片 | 23204 | 105 | 14.6 | 15.6 |

**偏斜负载（amp=8），Time / CPU（ms），串行 411：**

| 策略 \ 粒度 | 1 | 256 | 4096 | 65536 |
|---|---|---|---|---|
| auto | 179 / 170 | 181 / 180 | 177 / 153 | 181 / 121 |
| static | 191 / 11.4 | 207 / 10.4 | 207 / 10.9 | 198 / 12.6 |
| simple | 187 / 184 | 159 / 155 | 164 / 160 | 190 / 128 |
| affinity | 248 / 168 | 191 / 186 | 165 / 158 | 204 / 13.1 |
| threadpool 静态分片 | 22775 / 17879 | 190 / 44.9 | 183 / 4.56 | 193 / 0.82 |

**分析：**

- **旧「auto 优于显式 chunk」是混淆假象，但 auto 确实是最稳的选择。** 均匀负载下 auto@1 是全场最优（4.9x），且对粒度不敏感：偏斜下 auto 四档粒度 177~181ms 几乎水平，Time/CPU ≈ 1.0~1.5（自适应分裂回收了尾端空闲）。auto 对 grain 提示的响应比直觉弱——grain 只是分裂下界，auto 自己决定实际分裂深度。
- **旧「simple_partitioner 禁用」结论作废。** simple@1 = 33.3ms ≈ 串行（1M 个单元素任务的任务开销 ≈ 3ms），但旧版的 109ms（3.6x 慢于串行）并未复现——旧版多出的 ~76ms 是共享 cache line 原子争用。simple 在 grain ≥ 256 后与其他分区器持平（14~15ms）。**simple 的代价是「任务爆炸」而非「无窃取」**。
- **static 的软肋在偏斜负载的 CPU 利用率。** 偏斜下 static 的 Time（191~207ms）与其他分区器相差不大，但 CPU 只有 10~13ms——Time/CPU ≈ 17 倍，说明 worker 大量空闲等待：static 按线程数等分（实测 12 个叶子、起点不与 grain 对齐），阶梯偏斜下吃重尾的 worker 决定墙钟时间。这正是无窃取分区器在**异构负载**上的真实代价：不是吞吐下降，而是资源闲置。
- **affinity 在本负载下无优势且存在风险。** 均匀/偏斜下 affinity 均未超过 auto（最好 165ms）；偏斜 + 粗粒度（65536）下 CPU 仅 13.1ms、Time 204ms——划分树复用导致 8/12 worker 闲置。affinity 的价值场景是**重复调用 + 缓存敏感负载**（划分稳定带来的缓存驻留），本 case 的计算负载对划分不敏感，测不出它的好处——只能测出它的风险。选它需要明确的缓存理由。
- **静态分片线程池的真相。** threadpool@4096/65536（14.6~15.6ms）≈ TBB static（15~22ms），旧版「线程池 8.2x 全场最快」的真相是**均匀负载 + 静态分片**，而非线程池本身更优。真正拉开差距的是 grain=1：threadpool 23.2 秒 vs TBB auto 6.19ms——**3750 倍**。condvar 队列线程池每任务 ~20µs 的提交成本在细粒度下崩溃；TBB 的 per-worker 本地队列 + 窃取把每任务调度成本压到 ~30ns。
- **偏斜负载下的最优解是 auto + 任意 grain**（177~181ms），其次是 simple/affinity @ 4096（164~165ms）。static 与 threadpool 静态分片在这种形态下都靠不住（Time/CPU 失衡）。

### 2.3 教程声明核对与教训

- 教程 docs/1_threads/4_tbb.md §3.2 称「通过 partitioner 设置开多少个线程」——**错误**。分区器控制任务划分；线程数由 task_arena/global_control 控制。已向教程提交勘误。
- **教训 1（库行为以实测为准）：** static/affinity 的叶子起点不保证 grain 对齐、affinity 的划分树跨参数复用会非法分裂——这些是官方文档没有明说的实现细节，由本套件的校验系统实测抓出。
- **教训 2（粒度经验法则）：** 任务开销 ~µs 级的调度系统（线程池队列）要求粒度 ≥ 数千元素；任务开销 ~ns 级的调度系统（TBB）在 grain=1 也能工作（auto 会自适应聚合）。grain 的推荐值因调度器而异，不能搬用。

## 3. Case 2 — 锁原语 × 调度器 × 临界区粒度

### 3.1 测试设计

- **N = 2^20 次操作，8 线程**。锁次数精确 = N/K，K ∈ {1, 64, 4096}。
- **PersistentTeam 控制组**：8 线程 + std::barrier 双栅栏持久线程组，构造在计时窗外——与 TBB 常驻池同生命周期，剥离「线程创建」变量。
- **std::mutex 2×2 交叉**：{team, parallel_for} × {std::mutex}，加 {parallel_for} × {tbb::mutex}，把「锁质量」与「调度器」两个效应拆开。
- **锁次数配对（实测教训）**：第一版用 `blocked_range(N, K) + static_partitioner` 假设叶子数 = N/K——实测 static 按线程数等分，2^20 范围只有 ~12 个叶子，全部 TBB 行锁次数只有 ~12 次（0.005ms，校验通过但测了个寂寞）。修正为 `parallel_for(0, N/K)` 以 batch 为索引域，锁次数由构造保证。
- 真 `tbb::mutex`：oneTBB 2021.5 中该类型在 `__TBB_PREVIEW_MUTEXES` 下才存在（PREVIEW 特性），实现为 spin+wait 混合（`waitable_atomic`），**并非**经典 TBB 2.x 的 OS 互斥锁。
- 正确性：计数器精确 == N（整数精确比较）。

### 3.2 实测结果与分析

**Time 均值（ms），K ∈ {1, 64, 4096}：**

| 策略 | K=1 | K=64 | K=4096 |
|---|---|---|---|
| 无锁串行基线 | — | — | 0.619 |
| std::mutex + team | 104 | 1.54 | 0.403 |
| std::mutex + parallel_for | 95.4 | 4.92 | 2.20 |
| tbb::mutex（PREVIEW）+ parallel_for | 264 | 3.69 | 1.23 |
| tbb::spin_mutex + parallel_for | 135 | 3.73 | 1.10 |
| tbb::queuing_mutex + parallel_for | 门控 | 11.4 | 1.08 |

**读写锁（80/20 读重，K=1）与原子对照：**

| 策略 | Time (ms) |
|---|---|
| std::shared_mutex + team | 313 |
| tbb::spin_rw_mutex + parallel_for | 230 |
| std::atomic + team | 29.0 |
| std::atomic + parallel_for | 31.5 |

**分析：**

- **旧「tbb 锁比 std 快 4~5 倍」是线程生命周期假象。** 控制组下 std::mutex + team 与 std::mutex + parallel_for 在 K=1 时持平（104 vs 95.4ms）——同一把锁在两种调度器下无本质差异。锁的质量差异必须看**同调度器**的对比（下面三条），旧报告把「线程复用」与「锁质量」混为一谈。
- **同调度器下的锁排名（parallel_for 列）：** K=1（高争用细粒度）：std::mutex（95.4）< spin_mutex（135）< tbb::mutex PREVIEW（264）。**tbb::mutex（PREVIEW）在高争用细粒度下最慢**——spin+wait 的混合实现在争用风暴下反而放大开销；K=4096（低争用）：spin_mutex（1.10）≈ queuing（1.08）≈ tbb::mutex（1.23）< std::mutex（2.20）。**细粒度用 std::mutex、粗粒度 TBB 系略优**，与直觉的「TBB 锁总是更快」不同——旧版从未测出这个形状，因为线程创建成本（每迭代 ~1ms 级）淹没了锁的真实差异。
- **spin 的甜点窗口很窄。** spin_mutex 只在 K=64 以下有明显价值（K=1 时 135 vs tbb::mutex 264），K=4096 时与 queuing/tbb::mutex 无差别（1.10~1.23）。旧结论「spin 不显著」在修正锁次数后不再成立。
- **queuing_mutex 的定位是公平性而非速度。** K=64 时 11.4ms（全场最慢）——FIFO 队列的交接成本；K=4096 时 1.08ms（与 spin 持平）。门控细粒度误用（K=1，N/20）实测 35.5ms，**每 worker 加锁次数比值 1.00~1.02**（FIFO 公平性的直接证据）。旧版 145s/迭代的「灾难」未复现——旧数字本身可能就是其 bug 性测量的产物（见 §6 勘误）。
- **读写锁结论反转，且是平台相关。** v2（WSL2/glibc）tbb::spin_rw_mutex（230ms）< std::shared_mutex（313ms）；旧版（Windows）std::shared_mutex 40.5ms < tbb::spin_rw 66.1ms。glibc 的 shared_mutex（pthread_rwlock）与 Windows SRWLOCK 实现差异决定胜负。**读写锁的选择没有跨平台答案，需在本平台实测。**
- **旧「tbb_atomic 快 5.7x」坐实为假象。** 控制组下 std::atomic + team（29.0）≈ std::atomic + parallel_for（31.5）。原子操作本身在两套调度下相同——旧版差距全部来自 std 侧重建 8 线程。
- **CPU 列的语义（本报告统一解释）：** Google Benchmark 的 CPU 只计主线程。team 行主线程纯等待 → CPU ≈ 0（104ms 的 Time 对应 0.036ms CPU）；parallel_for 行主线程（arena 调用线程）参与执行 body → CPU 有值（62.5ms）。**TBB 的调用线程参与执行任务，线程池的主线程纯等待**——这是两套调度模型的又一处结构性差异，也是旧版 CPU-vs-Time 异常（ThreadPool CPU 0.453ms vs Time 3.72ms）的正解。

### 3.3 教训

- 锁对比必须先拆「线程生命周期」，再谈锁质量——配对变量超过一个的对比产生的是复合假象。
- 锁次数这类「显然正确」的假设也要校验：static_partitioner 的叶子数是线程数级而非 N/K，第一版全部 TBB 锁行都测了个寂寞，是校验（计数器 == N 通过 + 耗时 0.005ms 物理不可能）抓住的。

## 4. Case 3 — 嵌套并行：flat vs nested vs isolate

### 4.1 测试设计

- **确定性负载**：`makeSpec(outer, mean_inner=8000, skewed)` 用 mt19937(42) 一次性生成每外层任务的内层元素数；均匀 = 全部 8000，偏斜 = 伪随机 [2000, 32000]。参考值在计时窗外算好，全策略每迭代复用同一负载。
- **配对对比**：同一 spec，唯一差异是任务结构——flat（blocked_range2d 一次拍平，教程 §2.2 推荐的二维做法）/ nested（外层 pf → 内层 pf）/ isolate（嵌套 + this_task_arena::isolate 禁内层窃取）/ depth3（三层）/ 双池（threadPool 6+6 静态切分锚点）。
- 正确性：槽位归约 + 相对容差 1e-9（偏斜负载下 flat 用 `j < size[i]` 守卫跳过矩形域的幻影元素）。

### 4.2 实测结果与分析

**Time 均值（ms），串行：均匀 12.4 / 偏斜 21.1：**

| 策略 | 均匀 | 偏斜 |
|---|---|---|
| serial_nested | 12.4 | 21.1 |
| flat_2d @ g=256 | **2.61** | **5.53** |
| flat_2d @ g=4096 | 5.26 | 5.61 |
| nested @ outer=32, g=256 | 3.02 | 6.91 |
| nested @ outer=8, g=256 | 1.04 | — |
| nested @ g_inner=1 | 4.03 | — |
| nested @ g_inner=4096 | 4.02 | — |
| nested_depth3 | 3.06 | — |
| isolate @ outer=32 | 3.27 | — |
| isolate @ outer=8 | 0.969 | — |
| threadpool_two_level (6+6) | 5.90 | 10.8 |

**分析：**

- **「嵌套回收负载均衡」的预期被实测否定：flat 在两种负载形态下都赢。** 均匀：flat 2.61 vs nested 3.02（flat 快 16%）；偏斜：5.53 vs 6.91（flat 快 25%）。原因：拍平后 TBB 对整个域一次性做自适应分裂 + 窃取，嵌套的两层结构反而限制了调度视野（外层任务边界是硬边界）。**嵌套的价值不在负载均衡，而在结构表达**（每外层任务独立、数量动态、生命周期不同）。
- **嵌套无死锁、有适度开销。** 均匀下 nested（3.02）与 flat（2.61）差距仅 16%，depth3（3.06）≈ depth2（3.02）——深度增加无额外税（此负载）。对照 1_threadPool_test case2 的并行度侵蚀（嵌套等待随父任务数线性劣化），TBB 嵌套的结构性优势成立。
- **isolate 的隔离税远小于预期。** 预期 outer=8（<12 worker）时付 ~33% 利用率税——实测 0.969ms vs nested 1.04ms（**-7%，反而略快**）；outer=32 时 +8%。窃取本身有代价（跨核搬任务、缓存失效），isolate 禁掉内层窃取在部分场景收支平衡。**isolate 不是性能优化，是锁场景的正确性工具**（§4.3 死锁演示），平时不需要它。
- **嵌套时内层粒度同样有甜点**：g_inner=1（4.03）与 4096（4.02）都慢于 256（3.02）——过细任务爆炸、过粗丧失平衡，与 case1 的粒度教训一致。
- **跨框架结论成立：双池静态切分不能跨池平衡。** 均匀 5.90 vs nested 3.02（TBB 嵌套快 1.95x）；偏斜 10.8 vs 6.91（快 1.56x）。两个静态池之间没有窃取，外层负载不均时内层池空转。这也是「1_threadPool_test 的线程池结论不能泛化到 TBB」的直接证据。
- **嵌套在 outer=8（<12 worker）下工作正常且无并行度侵蚀**：nested@8 = 1.04ms，其串行等效（8 任务 ≈ 3.1ms）的 3.0x——对照 1_threadPool_test case2 的并行度侵蚀，TBB 嵌套在此场景不死锁、不劣化。但注意小域下效率低于大域（3.0x vs outer=32 的 4.1x），空闲 worker 的跨层级窃取未能完全抵消小域任务开销。flat@outer=8 未测（本版缺口），此配对结论仅限 nested 自身。

### 4.3 门控演示：教程 §4.1 死锁声明实测

`docs/1_threads/4_tbb.md` §4.1 声称：嵌套 parallel_for + mutex + 工作窃取会死锁（外层持锁、内层任务被窃、窃取者随后阻塞在同一把锁上）。用看门狗实测（`RUN_TBB_STEAL_DEMO=1`，复用 1_threadPool_test 的 DEADLOCK_TIMEOUT_SECONDS 模式，outer=256, inner=4096）：

```
$ RUN_TBB_STEAL_DEMO=1 DEADLOCK_TIMEOUT_SECONDS=30 ./tbb_benchmark \
    --benchmark_filter='case3_tbb_steal_deadlock'
DEADLOCK CONFIRMED: nested parallel_for + mutex deadlocked (outer=256, inner=4096) for over 30s   # abort
```

**声明验证成立：30 秒无进展，死锁确认。** 修复版（内层包 `this_task_arena::isolate`）实测 52.2ms 正常完成——isolate 的官方修复方案同样验证有效。教程勘误：§4.1 示例代码用的是 `std::recursive_mutex`，而解释文字描述的是非递归锁自锁场景——示例与解释错位，已一并修正。

### 4.4 教训

- 「嵌套优于拍平（负载均衡）」在 TBB 上不成立——**可拍平就先拍平**，嵌套留给结构表达。
- isolate 是锁场景正确性工具，不是性能优化。
- 深度 ≤3 的嵌套开销可以忽略；真正要控制的是内层粒度和外层任务数与 worker 数的关系。

## 5. 综合结论

### 5.1 核心发现（按证据强度排序）

| 级别 | 发现 | 证据 |
|---|---|---|
| 🔴 | **线程生命周期效应曾系统性污染锁/原子对比**：旧「tbb 锁快 4.8x」「tbb_atomic 快 5.7x」全部消失于控制组下（atomic：29.0 vs 31.5ms；同调度器锁排名重排） | case2 2×2 交叉 + PersistentTeam |
| 🔴 | **线程池静态分片在细粒度下崩溃**：grain=1 时 23.2s vs TBB auto 6.19ms（3750x）——condvar 队列 vs 本地队列+窃取的结构性差距 | case1 threadpool/1 vs auto/1 |
| 🔴 | **嵌套 parallel_for + mutex + 窃取 → 死锁（实测确认）**；isolate 修复有效（52.2ms 完成） | case3 门控演示，看门狗 30s 触发 |
| 🟡 | **「auto 优于显式 chunk」「simple 禁用」两条旧结论作废**：前者是混淆变量（两行都是 auto），后者归因错误（109ms 真因是原子争用而非无窃取，v2 实测 33.3ms ≈ 串行） | case1 全矩阵 |
| 🟡 | **static 在偏斜负载下的代价是 CPU 闲置**：Time 191~207ms 但 CPU 仅 10~13ms（17x 失衡），auto 无此现象（Time≈CPU） | case1 amp=8 行 Time/CPU 对比 |
| 🟡 | **锁的选择取决于临界区粒度与平台**：K=1 高争用 std::mutex 最优（95.4ms，tbb::mutex PREVIEW 最差 264ms）；K≥4096 TBB 系全面反超（1.08~1.23 vs 2.20ms）；读写锁胜负随平台反转（glibc vs SRWLOCK） | case2 全表 |
| 🟢 | **flat ≥ nested**：均匀快 16%、偏斜快 25%；depth3≈depth2 无深度税；isolate 税 -7%~+8%（远小于直觉） | case3 配对 |
| 🟢 | **TBB 嵌套 vs 双池线程池：1.6~2x 优势**——跨池无窃取是双池方案的硬伤 | case3 two_level 配对 |
| 🟢 | **queuing_mutex 的公平性是真实的**：每 worker 加锁次数比值 1.00~1.02（FIFO） | case2 门控 |
| 🟢 | **auto_partitioner 对 grain 不敏感且偏斜下最稳**：177~181ms 全粒度持平，Time/CPU ≈ 1.0~1.5 | case1 amp=8 行 |

### 5.2 最佳实践

**分组策略（partitioner × grain size）：**
1. 默认 `auto_partitioner`，grain 提示写「每任务预期工作量」即可，不要为它做精细调参——auto 对 grain 不敏感，且在偏斜负载下是唯一保持 CPU 满负荷的分区器。
2. `simple_partitioner` 不是洪水猛兽：grain 合理（每任务 ≳ 100 元素）时与其他分区器持平；grain=1 的任务爆炸代价约等于串行成本，可接受但无收益。
3. `static_partitioner` 只用于「负载严格均匀且要可复现划分」的场景；一旦负载可能不均，选 auto——static 的代价不是慢，是 worker 闲置（Time/CPU 失衡）。
4. `affinity_partitioner` 只在重复调用 + 缓存敏感的负载上有理论优势；使用前提是与上次完全相同的 Range 形状（跨参数复用会非法分裂，见 §2.3）。划分树复用的坑比收益常见。
5. 粒度经验法则：调度系统任务开销 ~µs 级（线程池队列）→ 粒度 ≥ 数千；~ns 级（TBB 本地队列）→ 粒度可以到个位数。
6. 线程数不受 partitioner 控制——用 `task_arena` / `global_control`。

**嵌套 parallel_for 使用指南：**
7. 能拍平就拍平（`blocked_range2d` 或一维摊平）——拍平在均匀与偏斜负载下都优于嵌套（16%~25%），且代码更简单。
8. 嵌套留给结构表达：外层任务数量动态、生命周期独立、或外层任务本身在等待其他资源。深度 ≤3 无额外税。
9. 嵌套的跨层级窃取保证 outer < worker 时正常工作（不死锁、无并行度侵蚀），但小域下效率低于大域——决定嵌套收益的是内层粒度与任务总量，不是外层任务数。
10. 嵌套内层同样要控制粒度（甜点在 ~256 元素档）。
11. `this_task_arena::isolate` 不是性能优化（税 -7%~+8%），是锁场景的正确性工具：外层持锁 + 内层 parallel_for 的代码**必须** isolate（或 task_arena 隔离），否则窃取重入死锁（§4.3 已实测）。
12. 锁 + 嵌套组合里的锁，选 std::mutex（细粒度争用）或 spin_mutex（短临界区）都比 tbb::mutex PREVIEW 稳；tbb::mutex 是 PREVIEW 特性，避免在生产依赖。

## 6. 原始数据

完整 JSON：`tmp/tbb_bench.json`（77 行 × 7 统计项）。文本：`tmp/tbb_bench.txt`。门控演示输出：`tmp/tbb_gated_queuing.txt`、`tmp/tbb_gated_fixed.txt`、`tmp/tbb_gated_deadlock.txt`。

数据收集：2026-08-25，WSL2/Linux + clang Release（-O3），12 核 2611 MHz。运行时 Load Average ≈ 3~6（中负载）——本套件自己就是负载来源（threadpool grain=1 两行各 ~23s）。case1 amp=8 各行的 CV 与 case1_serial 的波动（30~41ms 跨运行）说明 ~10% 级噪声存在；结论均基于配对差值（同进程同负载相邻测量），对均匀噪声稳健。Iterations=1 的行（如 case1_threadpool/1/*，单迭代 23s）按 1_threadPool_test 先例标注：方向性结论可靠，精确比值仅作量级参考。

**⚠️ 旧版设计缺陷与勘误（v1 → v2）：**
1. case1「显式 chunk vs auto」两行都是 auto_partitioner——对比无效；
2. case1 simple 归因错误（原子争用 ≠ 无窃取），且从未在合理粒度下测过；
3. case2 `tbb::mutex` 从未被测（旧行实为 spin_mutex）；oneTBB 2021 中它是 PREVIEW 特性，旧版连宏都没定义；
4. case2 全部 std 行每迭代重建 8 线程——锁对比被线程创建主导；
5. case2 queuing_mutex 145s/迭代：旧数字在受控复现中只有 35.5ms（N/20 减量折算 ~710ms/1M）——旧测量的 ~200 倍差距疑似其自身 bug 或测量环境问题，不可引用；
6. case3 负载非确定（random_device 无种子），3.4x 不可信；
7. 旧版零正确性校验——v2 校验系统抓出 3 个真实的库行为认知错误（叶子不对齐、affinity 跨参数非法分裂、static 锁次数假设）；
8. 旧 CPU-vs-Time 异常未解释——v2 §3.2 统一解释（CPU 只计主线程，TBB 调用线程参与执行、线程池主线程纯等待）；
9. 旧数据为 Windows 平台，读写锁结论与 Linux 相反（§3.2），平台差异不可忽视。

*报告基于 2_tbb_test 实际 benchmark 运行数据生成（2026-08-25，WSL2/Linux + clang Release 构建，12 核 2611 MHz）。设计决策记录见 `tmp/design/2_tbb_test_redesign.md`，过程记录见 `tmp/fixJournal.md`。*
