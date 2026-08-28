# 0_basic_test — 基础多线程开销 Benchmark 报告

**测试目标：** 量化多线程编程中的基础开销——线程创建、伪共享、加速比、过量线程、锁与原子竞争。

**测试框架：** Google Benchmark  
**依赖：** `benchmark`, `fmt`, `spdlog`

**测试平台：** WSL2 (Ubuntu) / Linux，编译器 clang（`clang-ninja-release` 构建，-O2）  
**WSL 拓扑：** 4 × 2611 MHz（**2 物理核 × 2 SMT**）；L1 Data 48 KiB (x2)、L1 Instruction 32 KiB (x2)、L2 Unified 1280 KiB (x2)、L3 Unified 12288 KiB (x1)  
**Windows 宿主：** 12th Gen Intel i7-1255U（2 P 核 + 8 E 核，共 12 逻辑处理器）

> 注 1：WSL2 vCPU 是 Windows 宿主的普通调度线程，宿主可在物理层自由迁移它们——guest 内 `pthread_setaffinity_np` 钉核**不能**固定物理核。对物理拓扑敏感的测量（case2 伪共享 ping-pong）存在宿主级噪声：跨进程间惩罚呈双稳态跳变（详见 §3.2 噪声诊断）。CPU-bound 吞吐类测量（case3/4/5）受此影响较小（同工作量交叉验证一致，见 §4.2）。
>
> 注 2：软件层是 Linux——锁走 glibc futex（vs Windows SRWLOCK）、线程创建走 Linux clone（vs Windows 内核线程）。**case1（线程创建）与 case5（锁）的绝对数字与 Windows 会有平台差异，趋势结论通用。**
>
> 注 3：本报告数据为 2026-08-27 在 4 vCPU（2C4T）环境下实测。此前存在 12 vCPU 配置的旧数据（guest 内呈现「6C12T」拓扑——这是 Hyper-V 在 2P+8E 宿主上的伪造拓扑，物理基础已不存在），已全部替换。

---

## [toc]

---

## 1. 实验设计概览

| Case | 研究问题 | 变量 | 关键指标 |
|------|---------|------|---------|
| Case 1 | 线程创建的开销有多大？ | 线程数 ∈ {10, 50, 100, 500, 1000} | 启动延迟、每线程创建成本 |
| Case 2 | 伪共享 (False Sharing) 对性能的影响？ | 对齐 vs 未对齐，写间忙等计算量 | 两线程并发写入耗时 |
| Case 3 | 多线程的加速比能达到多少？ | 串行 / 单线程 / 多线程 | 耗时对比、加速比 |
| Case 4 | 线程数与性能的关系——甜点在哪、超量线程惩罚多大？ | 线程数 ∈ {1,2,3,4,6,8,12,16,24,48,100,1000} + 每任务一线程 | 耗时-线程数曲线、加速比 |
| Case 5 | 锁的开销是多少，无锁的开销又是多少？ | 计数方式 ∈ {无共享基线 / mutex / atomic}，线程数 ∈ {1, 2, 4, hw} | 锁开销、无锁开销（ns/次计数） |

---

## 2. Case 1 — 线程创建开销

### 2.1 测试设计

创建 N 个线程（N=10, 50, 100, 500, 1000），线程体一进入即通过原子计数器报告"已启动"。计时窗口只覆盖从第一次 `std::thread` 构造到 **所有线程都已开始执行** 的区间——即纯创建 + 首次调度开销（startup latency），不含任何回收成本。`join` 回收在计时窗口之外执行（`UseManualTime` + `SetIterationTime` 手动计时）。

```cpp
    // case1: time only creation + first scheduling
    std::atomic<int> started{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    const auto t0 = std::chrono::steady_clock::now();     // 计时开始
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(empty_task, std::ref(started));  // 线程体：进入即自增 started
    }
    while (started.load(std::memory_order_acquire) != thread_count) {
        std::this_thread::yield();
    }
    state.SetIterationTime(...);                          // 计时结束

    for (auto& t : threads) {
        t.join();                                         // 回收不计时
    }
```

### 2.2 实测结果与分析

| 线程数 | 启动延迟总耗时 | 每线程创建成本 |
|--------|--------------|--------------|
| 10     | 0.271 ms     | 27.1 μs      |
| 50     | 1.44 ms      | 28.8 μs      |
| 100    | 2.94 ms      | 29.4 μs      |
| 500    | 13.1 ms      | 26.2 μs      |
| 1000   | 23.1 ms      | 23.1 μs      |

**分析：**
- **每线程创建成本 ~23–29 μs**（WSL/Linux 的 `clone` 系统调用 + 内核线程初始化 + 首次调度），总耗时随线程数严格线性——创建成本恒定，无固定摊薄，也**无排队放大**（10→1000 线程每线程成本平坦，cv ≤ 8%）。
- 注意：本测量只含「创建 + 首次调度」；回收（join + 内核回收）在计时窗口外。真实 per-request 线程模式的总成本约为本数据的 2 倍。
- **交叉验证（case4）**：case4 的 `per_task_thread`（1000 次创建+回收在计时窗口内）比预创建 T=1000 慢 24 ms（§5.2），即 24 μs/对——与本节实测创建成本一致，两个独立实验互证。
- **教训：** 频繁创建线程的模式（per-request thread）在高吞吐场景下不可取——创建本身含内核态栈分配、TCB 初始化与首次调度，~25 μs 级固定成本不可忽略（10 万 QPS 下 25 μs × 10 万 = 2.5 s/s，即需 2.5 个核专职创建线程）。应使用线程池复用。

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
- `case2_pingPong*WithBusyWork` — 每次写之后执行忙等计算（依赖链 busy ∈ {0, 10, 100, 1000}，各 1M 次写），模拟不同计算密度的"带业务"写入

### 3.2 实测结果与分析

| 子测试 | 写次数/线程 | 忙等迭代/写 | 实测总耗时 | 每写惩罚* |
|--------|-----------|-----------|-----------|-----------|
| Aligned（纯写） | 10M | — | 46.8 ms | — |
| Unaligned（纯写） | 10M | — | 299 ms | 12.6 ns |
| AlignedWithBusyWork | 1M | 0 | 7.64 ms | — |
| 〃 | 1M | 10 | 6.47 ms | — |
| 〃 | 1M | 100 | 6.99 ms | — |
| 〃 | 1M | 1000 | 86.9 ms | — |
| UnalignedWithBusyWork | 1M | 0 | 36.5 ms | 14.4 ns |
| 〃 | 1M | 10 | 37.0 ms | 15.3 ns |
| 〃 | 1M | 100 | 42.0 ms | 17.5 ns |
| 〃 | 1M | 1000 | 106 ms | 9.6 ns |

\* 每写惩罚 = (Unaligned − Aligned) / 总写次数（纯写组总写次数 20M，busy 组 2M）。**看差值而非比值**：比值指标会被对齐组自身的「busy=0 RMW 仲裁」效应污染（见下）。

**分析：**
- **伪共享惩罚存在且量级 ~10–18 ns/写**：纯写组惩罚 12.6 ns/写（比值 6.4×）。惩罚来自缓存行跨核往返（实测往返 ~20–40 ns，见 §3.3）。
- **惩罚不随业务量增长——稀释前提成立**：busy=0/10/100 三档惩罚 14.4 / 15.3 / 17.5 ns/写，基本恒定（cv ~10%），证明业务密度不影响单次惩罚成本。**busy=1000 档惩罚降至 9.6 ns/写、比值 1.22×**——写间隔（~87 ns）超过往返成本后，行转移与另一线程的忙等重叠，惩罚被摊薄。稀释是连续的，不是阈值突变。
- **业务耗时实测标定**（Release -O2 下）：busy=1000 档对齐组 86.9 − 7.6 ≈ 79.3 ms / 1M 写 = **~79 ns/写 ≈ 0.08 ns/LCG 迭代**——远低于 O0 预期量级（~1.5 ns/迭代），因为 `busyWork` 未标 `NO_OPTIMIZE`，-O2 下编译器对 LCG 依赖链做了并行重组（affine 递推拆多条链）。各档业务耗时以实测差值为准。
- **反常点（真实但幅度小）：** aligned busy=0（7.64 ms）比 busy=10（6.47 ms）慢 ~18%。解释：busy=0 时两线程以最高频率（~7.6 ns/写）发原子 RMW，`lock` 指令的内存子系统仲裁在最高频下排队；加入忙等后写频率下降，单次 RMW 成本回落。旧 12 vCPU 数据中该效应曾达 44%（11.4 vs 7.89 ms）——大部分是环境噪声放大，真实幅度 ~10–20%。
- **对齐决策判据**：写间隔 vs 缓存行往返成本（本机实测 ~20–40 ns）。写间隔 ≪ 往返成本必须对齐/填充（惩罚 ~10–18 ns/写）；写间隔 ≫ 往返成本时收益摊薄（busy=1000 档惩罚占比 ~10%）。临界点不是固定 100 ns，而是本机往返成本的两倍量级。

#### 数据噪声诊断（WSL2 宿主效应）

伪共享测量对运行环境极其敏感——本次调查中，同一二进制、同一代码在数分钟内出现 **unaligned 组双稳态**：

| 实验 | aligned（控制组） | unaligned |
|------|-----------------|-----------|
| 独立探针 × 6 进程（同进程内先后测） | **6.56–7.34 ms，稳定 ±5%** | **21.8–42.0 ms，双稳态 ±45%** |
| guest 内 `pthread_setaffinity_np` 钉同核/异核 | 无差异 | 差异 ~4%，且与快慢态无相关性 |

- **结论 1：噪声特异放大伪共享测量。** aligned 控制组跨进程稳定，unaligned 在同一进程内即可翻转——若噪声来自负载或频率，两组应同步波动。unaligned 的敏感性是 ping-pong 路径（跨核缓存行往返）依赖宿主物理层调度的直接体现。
- **结论 2：guest 级钉核无效。** WSL2 vCPU 是宿主的普通线程，`sched_getcpu()` 返回的是 guest 编号，与物理核无稳定映射；宿主的迁移/调度 guest 不可见、不可控。
- **结论 3：suite 运行序可放大噪声。** 同一 suite 内 unaligned busy=10 在 15:58 与 16:19 两次运行中都是 49.0 ms（cv 0.6%），而干净孤立运行时为 37.0 ms——suite 前置基准把机器推到同一热状态，使噪声变成**确定性偏差**而非随机误差。这也是「重复运行 cv 很小」不能证明数据可靠的例子。
- **方法论启示：** 对物理拓扑敏感的基准（伪共享、NUMA、缓存行竞争），**必须携带控制组**（本 case 的 aligned 组），用差值/比值而非绝对值作结论；单次 suite 数据 + 小 cv 不足以支撑定量结论。绝对惩罚值应以区间（~10–18 ns/写）而非单点表述。

### 3.3 伪共享原理

CPU 以缓存行（本平台 64 字节）为单位在核心间搬运数据，MESI 一致性协议保证同一时刻只有一个核心能独占写某一行。当两个线程写同一行上的不同变量时：

1. 线程 1 写 → 该行在核心 1 为 Modified 态，核心 2 的副本失效
2. 线程 2 写 → 核心 2 必须先经互连总线从核心 1 取回该行（本机实测 ~20–40 ns/往返，依赖宿主物理层调度呈双稳态），写后核心 1 副本又失效
3. 反复往返即"乒乓缓存"

伪共享的成立前提是**两个核心几乎同时写**：
- 写间隔 ≫ 缓存行往返成本 → 惩罚被业务时间摊薄（稀释）
- 写间隔略大于往返成本 → 行转移与另一线程的业务重叠，惩罚部分隐藏（§3.2 busy=1000 档即此情形）
- 用 sleep 拉开写间隔 → 线程让出 CPU，写不再重叠，乒乓在机制上消失——这不是稀释，测不到伪共享（旧版 sleep 实验的错误所在）

注：教科书常引用 ~100 ns 作为跨核缓存行往返成本——那是多插槽服务器的量级。本机（单 die、2 物理核、共享 L2/L3）实测仅 ~20–40 ns，且存在双稳态（§3.2 诊断）。判据中的临界写间隔应基于**实测**的往返成本，而非教科书常数。

---

## 4. Case 3 — 加速比测量 (Amdahl's Law)

### 4.1 测试设计

> 旧版「线程池并行」变体已删除：其与裸多线程的对比只能说明「线程复用 vs 创建销毁」，而创建成本已由 case1 直接量化，该对比在本 case 中冗余。

对比三种执行模式完成**相同总工作量**（`cpu核心数 × 240M 次计数`）的耗时：

| 模式 | 执行方式 |
|------|---------|
| `case3_task_cost` | 单次任务（240M 计数）— 基线单位 |
| `case3_single_thread_cost` | 单线程串行执行 core 次 |
| `case3_multi_thread_cost` | 每个任务一个线程，并行执行 |

### 4.2 实测结果与分析

| 模式 | 实测耗时 | CPU 时间 | 说明 |
|------|---------|---------|------|
| 单次任务 (task_cost) | 91.2 ms | 91.3 ms | 基线单位 T |
| 单线程串行 (single_thread) | 355 ms | 373 ms | 3.89×T，≈理论 4× ✓ |
| 多线程并行 (multi_thread) | 190 ms | 0.215 ms | 相对串行加速 **1.87×**；为单任务的 **2.08×** |

**关键发现：**
- **串行一致性**：single_thread（355 ms）≈ task_cost（91.2 ms）× 3.9——与理论 4× 几乎一致（工作量为 `hardware_concurrency()` = 4 份），串行执行无额外框架开销。
- **加速比 ≈ 物理核数，并行效率 96%**：multi_thread 起 4 个线程跑在 2 个物理核上。2 核理想耗时为 4 × 91.2 / 2 = 182 ms，实测 190 ms——**多出的只有 4%**。旧 12 vCPU 数据里的「2.49× 与 6 核上限差 2.4× 的缺口」真相大白：guest 的「6C12T」是 Hyper-V 伪造拓扑，真实物理核只有 2 个，2× 就是上限。加速比受限的根因是**物理核数**，不是 Amdahl 串行部分、不是频率墙、更不是线程创建。
- **交叉验证（case3 ↔ case4 互相印证）**：case3 的 4 线程跑 960M 次计数（4 × 240M）耗时 190 ms = **5.05 G 计数/s**；case4 扫描的 T=4 跑 6G 次计数耗时 1179 ms = **5.09 G 计数/s**。两个独立实验归一化后仅差 0.8%——190 ms 就是「本机 4 线程满载完成该总工作量」的真实耗时。
- **线程创建不是瓶颈**：case1 实测 4 线程创建 ≈ 0.1 ms（~27 μs × 4），仅占 190 ms 的 **0.05%**。
- 注意 CPU 时间列：`multi_thread_cost` 的 CPU 仅为 0.215 ms——Google Benchmark 只统计**调用线程**的 CPU 时间，实际计算发生在工作线程上。Time 列（墙钟）才是有效口径。

---

## 5. Case 4 — 线程数与性能（重新设计）

### 5.1 测试设计

> 旧设计（串行 / 12 线程 / 每任务一线程三个点）已废弃，问题有三：① 只有 3 个采样点，回答不了"多少核开多少线程"；② 所谓"最优 = 12（逻辑核）"与 Case 3 结论矛盾——本机 2 物理核才是理论甜点，旧设计根本没测 T=2；③ `optimal`（线程复用）与 `too_many`（每任务创建）之间差异混杂了线程数效应与创建开销两个变量。

**新设计：线程数扫描。** 固定总工作量（1000 个任务 × 6M 次计数，串行 ≈ 2.5 s），唯一变量是线程数 T：

| 采样点 | 含义 |
|--------|------|
| 1, 2, 3 | 低于/等于物理核数（2） |
| **4** | **等于逻辑核数（SMT 满载）** |
| 6, 8, 12, 16 | 轻度超量 |
| 24, 48, 100, 1000 | 重度超量（oversubscribed） |

关键设计点：
- **线程预创建在计时窗口之外**（手动计时，同 Case 1）：测的是"T 个线程并行完成固定工作量"的纯耗时，创建开销不污染曲线
- **原子索引领取任务**（work-stealing 式）：负载均衡与 T 无关，`tasks % T ≠ 0` 时也不倾斜
- 对比变体 `case4_per_task_thread`：1000 个任务每任务创建一个线程（创建+回收在窗口内）——与扫描中 T=1000（预创建）对比，差值即"1000 次创建+回收+1GB 栈内存"的真实代价

```cpp
    // case4_thread_count：计时窗口只含 go 释放 → 全部任务完成
    auto worker = [&] {
        ready.fetch_add(1);                       // 创建+ready 同步：窗口外
        while (!go.load(std::memory_order_acquire)) {}
        for (;;) {
            const int t = next_task.fetch_add(1); // 原子索引领取
            if (t >= total_tasks) break;
            taskCntNumbers();
        }
        done.fetch_add(1);
    };
    // ...预创建 T 个线程，等 ready==T 后：
    const auto t0 = now();  go.store(true);       // 计时开始
    while (done.load() != T) {                    // 计时结束
        std::this_thread::yield();
    }
    // ...join：窗口外
```

### 5.2 实测结果与分析

| 线程数 T | 实测耗时 | 相对 T=1 加速比 | 备注 |
|---------|---------|---------------|------|
| 1（基线） | 2483 ms | 1.00× | 串行基线（单线程 boost 最高频，基线本身被"抬高"） |
| 2 | 1526 ms | 1.63× | 物理核满载 |
| 3 | 1261 ms | 1.97× | 轻微超量 |
| 4 | 1179 ms | 2.11× | 逻辑核（SMT）满载——SMT 增益 1.29× |
| 6 | 1102 ms | 2.25× | 超量 |
| 8 | 1148 ms | 2.16× | 超量 |
| 12 | 1091 ms | 2.28× | 超量 |
| 16 | 1052 ms | 2.36× | 超量 |
| 24 | 1106 ms | 2.24× | 超量 |
| **48** | **1022 ms** | **2.43×** | **实测最快点** |
| 100 | 1125 ms | 2.21× | 超量 |
| 1000 | 1079 ms | 2.30× | 极端超量 |
| per_task_thread（1000 次创建） | 1103 ms | — | 与 T=1000 差 24 ms（= 创建+回收代价） |

**关键发现：**
- **甜点是一个宽平台，不是一个点**：T=4 起（2.11×）到 T=1000（2.30×）全部落在 1022–1179 ms 内，最优与最差相差 ~15%（各点 cv 0.3–4.6%）。**超量线程的惩罚远小于直觉预期**——旧 12 vCPU 数据里「T=48 最差 +45%」的峰值曲线没有复现（那是 E 核混入的伪拓扑效应）。
- **SMT 有真实增益（+29%）**：T=4（1179 ms）比 T=2（1526 ms）快 1.29×。本负载 `countNumber` 含 `padding[128]` 栈数组（`DoNotOptimize`），带来内存停顿，SMT 靠双线程填充停顿槽。旧「纯 ALU → SMT 零增益」推论修正：**SMT 增益取决于负载的停顿特征，实测为准**。
- **T=2 只有 1.63× 而非 2×——基线被 boost 抬高**：T=1 单线程时 P 核 boost 至最高频（串行速率 2.42G 计数/s），T=2 起每核速率回落（1.97G/s）。T=1 不是"干净的 1 核参照"。同理，T=4 之后超过 2× 物理核上限（最高 2.43×）也不是幻数：`padding` 数组的内存停顿被更多线程的 memory-level parallelism 隐藏，单位时间总吞吐超过 2 核满载的 naive 估计。
- **per_task_thread（1103 ms）− T=1000（1079 ms）= 24 ms** = 1000 次创建+回收 ≈ **24 μs/对**——与 case1 实测创建成本（23–29 μs）精确互证。任务重（6M 计数 ≈ 2.5 ms/任务，T=1 串行口径）时创建占比仅 ~2%；任务越轻，创建占比越高（case1 教训）。
- **与 case3 互证**：T=4 归一化吞吐 5.09 G 计数/s ≈ case3 multi_thread 的 5.05 G/s（§4.2）。
- **教训（实测修正版）：** 2 物理核机器上，CPU-bound 任务从 T=2 起收益即大部分到手（1.63×），T=4（SMT）再赚 29%，再往后全是平台噪声级的小幅波动。超量 1000 线程代价 <10%。线程数选型：**逻辑核数起步，按负载停顿特征在 物理核~逻辑核×2 之间微调**；「中等超量最差」不成立，不值得为此过度优化。

---

## 6. Case 5 — 锁的开销与无锁的开销

### 6.1 测试设计

> 旧设计（无锁串行 120M / 有锁串行 120M / 锁竞争 1.2M / 原子竞争 1.2M 四组）已废弃，问题有二：① 串行组与竞争组工作量不同（120M vs 1.2M），无法直接相减得出开销；② 「无锁」语义混乱——纯局部计数（无共享）与 `atomic`（lock-free 实现）混在一起，且无同构 baseline 作参照，答不出「锁到底贵多少」。

**新设计：三组同构计数，直接相减。** 三组完成**完全相同**的总工作量（`kTotalWork` = 1.2M 次计数，N 线程均分），唯一变量是计数方式：

| 组 | 实现 | 含义 |
|----|------|------|
| `case5_baseline` | 每线程在自己的局部 `int` 上 `++` | 无共享、无同步——绝对底线 |
| `case5_mutex` | 所有线程共享一个 `int`，每次 `++` 都加 `std::mutex` | 锁实现 |
| `case5_atomic` | 所有线程共享 `std::atomic<int>`，`fetch_add(relaxed)` | 无锁实现 |

```cpp
// 三组的循环体同构，仅计数方式不同
NO_OPTIMIZE void countLocal(int per_thread) {            // baseline
    int value = 0;
    for (int i = 0; i < per_thread; ++i) ++value;
}
NO_OPTIMIZE void countWithMutex(int per_thread, std::mutex& mtx, int& counter) {
    for (int i = 0; i < per_thread; ++i) {
        std::lock_guard<std::mutex> lock(mtx);           // 每次 ++ 都锁一次（细粒度）
        ++counter;
    }
}
NO_OPTIMIZE void countWithAtomic(int per_thread, std::atomic<int>& counter) {
    for (int i = 0; i < per_thread; ++i)
        counter.fetch_add(1, std::memory_order_relaxed);
}
```

**两个核心问题的答案就是两次相减：**

- **锁的开销 = `case5_mutex` − `case5_baseline`**
- **无锁的开销 = `case5_atomic` − `case5_baseline`**

（同一线程数下相减；可再归一化为 ns/次计数 = 差值 / 1.2M。）

关键设计点：
- **线程数扫描 {1, 2, 4, hw}**（hw = `hardware_concurrency()`，注册参数用哨兵值 0，运行期解析，避免与 1/2/4 重名冲突）：单线程点给出**纯机制开销**（零竞争），2/4/hw 点给出**竞争下开销如何膨胀**。
- **手动计时（同 case1/case4 模式）**：线程创建、`ready` 同步、共享状态构建与 `join` 全部在计时窗口外，`SetIterationTime` 只覆盖 `go` 释放 → 全部线程完成计数——线程创建成本不污染「锁的开销」。
- 每轮迭代结束后断言计数结果 == 总工作量（`mutex`/`atomic` 组），保证测量的是真实完成的计数。
- 注意 `mutex` 组是**故意细粒度**加锁（每次 ++ 锁一次）——这正是本 case 要量化的反模式成本；真实业务应粗粒度（见教训）。

### 6.2 实测结果与分析

**单线程（无竞争）——纯机制开销（总工作量 1.2M 次计数）：**

| 组 | 实测耗时 | 减去 baseline 后的开销 | 归一化 |
|----|---------|---------------------|--------|
| baseline（纯局部计数） | 0.514 ms | —（参照零点） | — |
| atomic（无锁） | 8.27 ms | 7.76 ms = 无锁的开销 | **6.5 ns/次** |
| mutex（锁） | 15.1 ms | 14.6 ms = 锁的开销 | **12.2 ns/次** |

**竞争下（相同总工作量 1.2M 次，随线程数膨胀）：**

| 线程数 | baseline | atomic | mutex | 锁开销 | 无锁开销 |
|--------|----------|--------|-------|--------|---------|
| 1 | 0.514 ms | 8.27 ms | 15.1 ms | 12.2 ns/次 | 6.5 ns/次 |
| 2 | 0.326 ms | 18.1 ms | 52.1 ms | 43.1 ns/次 | 14.8 ns/次 |
| 4（hw） | 0.321 ms | 19.5 ms | 53.9 ms | 44.6 ns/次 | 16.0 ns/次 |

**关键发现：**
- **单线程（纯机制开销）**：锁 12.2 ns/次（glibc futex 无竞争快速路径），无锁 6.5 ns/次（`fetch_add` 原子 RMW）——**无锁实现比锁快 ~1.9×**。与旧 Windows 数据（SRWLOCK ~17 ns、atomic ~9 ns）同量级，绝对差异即平台锁实现差异。
- **竞争膨胀不对称，但比旧 12 vCPU 数据温和**：1→4 线程，锁开销膨胀 **3.7×**（12.2 → 44.6 ns/次，缓存行乒乓 + futex 内核仲裁 + 排队唤醒）；无锁开销膨胀 **2.5×**（6.5 → 16.0 ns/次，纯缓存行竞争，无内核态往返）。膨胀幅度与参与竞争的线程数相关（旧数据 12 线程 6.2× / 2.7×），趋势一致。
- **4 线程竞争下 atomic（19.5 ms）比 mutex（53.9 ms）快 2.8×**——竞争越激烈，lock-free 优势越大。
- 注意 2 线程与 4 线程的 mutex/atomic 数据几乎持平（52.1 vs 53.9；18.1 vs 19.5）——本机只有 2 物理核，2 个线程已把竞争推到饱和，再叠加 SMT 线程不再加剧缓存行争抢。
- baseline 随线程数基本不变（0.32–0.51 ms，每线程独立栈），是稳定的相减参照。
- **教训（实测验证）：**
  - 锁的开销 = 无竞争机制成本（12.2 ns/次）+ 竞争膨胀（最高 44.6 ns/次）——单线程测出的只是第一部分，评估真实系统要用目标并发度下的数字
  - 细粒度加锁（每计数一次）的量级惩罚已量化：1.2M 次计数在 4 线程竞争下要 53.9 ms（纯计数只需 0.32 ms）——真实业务要粗粒度（批量后锁一次）
  - 共享计数器竞争激烈时 `std::atomic` 明显快于 `std::mutex`（4 线程 2.8×）

---

## 7. 综合结论

### 7.1 核心发现排名

| 排名 | 发现 | 影响程度 |
|------|------|---------|
| 🔴 | **并行加速比上限 = 物理核数（2×）**：case3 实测 1.87×、并行效率 96%——旧「2.49× 之谜」真相是 Hyper-V 在 2P+8E 宿主上伪造了 6C12T 拓扑，真实物理核只有 2 个 | 高 |
| 🔴 | 锁开销：无竞争 12.2 ns/次，4 线程竞争膨胀 **3.7×** 到 44.6 ns/次（case5）——锁粒度必须粗，评估锁成本要用目标并发度下的数字 | 高 |
| 🟡 | 线程创建 ~23–29 μs/次（case1 实测，与 case4 per_task 差值 24 μs/对互证，WSL/Linux）——内核态栈分配 + TCB 初始化 + 首次调度，频繁创建应改用线程池 | 中 |
| 🟡 | SMT 有真实增益（case4：T=4 比 T=2 快 **1.29×**）——负载含内存停顿时 SMT 填充停顿槽；纯 ALU 负载另当别论 | 中 |
| 🟡 | 伪共享测量是环境敏感型：WSL2 宿主调度使惩罚呈双稳态（~10–18 ns/写），suite 运行序可把噪声变成确定性偏差——拓扑敏感基准必须带控制组（case2 诊断） | 中（方法论） |
| 🟢 | 超量线程惩罚温和：T=4–1000 全部落在 ±15% 平台内（case4）——旧「中等超量最差 +45%」是伪拓扑效应 | 低 |
| 🟢 | 竞争下 `std::atomic` 明显优于 `std::mutex`（4 线程 19.5 ms vs 53.9 ms，**2.8×**，case5 实测） | 低（已知结论） |

### 7.2 最佳实践

1. **永远使用线程池**，避免 per-task 创建线程（创建开销 ~25 μs/次实测，见 Case 1；任务 < 数 ms 时创建占比即不可忽略）
2. **对齐决策以写入频率为准**——纯写场景伪共享惩罚实测 ~13 ns/写（6.4×）；写间隔 ≫ 缓存行往返成本（本机实测 ~20–40 ns，非教科书 ~100 ns）时惩罚被摊薄（busy=1000 档比值降至 1.22×），可不对齐。惩罚绝对值按区间（~10–18 ns/写）理解，伪共享测量必带控制组
3. **CPU-bound 任务线程数 = 逻辑核数起步**（本机 4 vCPU：T=4 达 2.11×，SMT 增益 1.29×；再往上到 T=1000 仅 ±15% 波动）——超量惩罚温和，不值得为「最优线程数」过度调优
4. **锁的粒度要粗**，绝不在 tight loop 内反复加锁（无竞争 12.2 ns/次，4 线程竞争 44.6 ns/次，见 Case 5）
5. **共享计数器优先使用 `std::atomic`** 替代 `std::mutex`——无竞争快 ~1.9×，4 线程竞争快 2.8×（Case 5 实测）
6. **WSL2 上做基准要警惕拓扑谎言**：guest 的 CPU 拓扑（核数、缓存层级）是 Hyper-V 的虚拟呈现，与宿主真实拓扑（2P+8E）无简单对应；钉核在 guest 内无效——涉及物理拓扑的结论以宿主 `Win32_Processor` 信息为准

---

## 8. 原始数据 (Raw Data)

完整 benchmark 输出（Google Benchmark 原生格式，2026-08-27 实测，WSL2/Linux + clang Release，`--benchmark_repetitions=3`；case1/4/5 为手动计时版，`/0` = 硬件线程数 4）。以下为各基准的 mean 行，含 3 次 repetition 的完整输出见 `tmp/bench/basic_bench_4core_20260827.txt`：

```
2026-08-27T17:36:11+08:00
Running ./build/clang-ninja-release/1_benchmark/0_basic_test/basic_benchmark
Run on (4 X 2611.21 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x2)
  L1 Instruction 32 KiB (x2)
  L2 Unified 1280 KiB (x2)
  L3 Unified 12288 KiB (x1)
Load Average: 0.10, 0.08, 0.09
-------------------------------------------------------------------------------------------
Benchmark                                                        Time             CPU   Iterations
-------------------------------------------------------------------------------------------
case1_create_only/10/manual_time_mean                     0.271 ms        0.302 ms            3
case1_create_only/50/manual_time_mean                      1.44 ms         1.59 ms            3
case1_create_only/100/manual_time_mean                     2.94 ms         3.23 ms            3
case1_create_only/500/manual_time_mean                     13.1 ms         14.8 ms            3
case1_create_only/1000/manual_time_mean                    23.1 ms         26.9 ms            3
case2_pingPongAligned/10000000_mean                        46.8 ms        0.117 ms            3
case2_pingPongUnaligned/10000000_mean                       299 ms        0.097 ms            3
case2_pingPongAlignedWithBusyWork/1000000/0_mean           7.64 ms        0.068 ms            3
case2_pingPongAlignedWithBusyWork/1000000/10_mean          6.47 ms        0.095 ms            3
case2_pingPongAlignedWithBusyWork/1000000/100_mean         6.99 ms        0.098 ms            3
case2_pingPongAlignedWithBusyWork/1000000/1000_mean        86.9 ms        0.127 ms            3
case2_pingPongUnalignedWithBusyWork/1000000/0_mean         36.5 ms        0.081 ms            3
case2_pingPongUnalignedWithBusyWork/1000000/10_mean        37.0 ms        0.084 ms            3
case2_pingPongUnalignedWithBusyWork/1000000/100_mean       42.0 ms        0.091 ms            3
case2_pingPongUnalignedWithBusyWork/1000000/1000_mean       106 ms        0.102 ms            3
case3_task_cost_mean                                      91.2 ms         91.3 ms            3
case3_single_thread_cost_mean                              355 ms          373 ms            3
case3_multi_thread_cost_mean                               190 ms        0.215 ms            3
case4_thread_count/1/manual_time_mean                     2483 ms         2612 ms            3
case4_thread_count/2/manual_time_mean                     1526 ms         1546 ms            3
case4_thread_count/3/manual_time_mean                     1261 ms         1332 ms            3
case4_thread_count/4/manual_time_mean                     1179 ms          233 ms            3
case4_thread_count/6/manual_time_mean                     1102 ms          150 ms            3
case4_thread_count/8/manual_time_mean                     1148 ms         40.4 ms            3
case4_thread_count/12/manual_time_mean                    1091 ms         41.0 ms            3
case4_thread_count/16/manual_time_mean                    1052 ms         24.3 ms            3
case4_thread_count/24/manual_time_mean                    1106 ms         10.6 ms            3
case4_thread_count/48/manual_time_mean                    1022 ms         8.29 ms            3
case4_thread_count/100/manual_time_mean                   1125 ms         6.98 ms            3
case4_thread_count/1000/manual_time_mean                  1079 ms         31.8 ms            3
case4_per_task_thread_mean                                1103 ms         23.1 ms            3
case5_baseline/1/manual_time_mean                        0.514 ms        0.561 ms            3
case5_baseline/2/manual_time_mean                        0.326 ms        0.424 ms            3
case5_baseline/4/manual_time_mean                        0.321 ms        0.511 ms            3
case5_baseline/0/manual_time_mean                        0.332 ms        0.515 ms            3
case5_mutex/1/manual_time_mean                            15.1 ms         16.6 ms            3
case5_mutex/2/manual_time_mean                            52.1 ms         57.0 ms            3
case5_mutex/4/manual_time_mean                            53.9 ms         56.2 ms            3
case5_mutex/0/manual_time_mean                            52.1 ms         55.7 ms            3
case5_atomic/1/manual_time_mean                           8.27 ms         9.09 ms            3
case5_atomic/2/manual_time_mean                           18.1 ms         18.6 ms            3
case5_atomic/4/manual_time_mean                           19.5 ms         20.0 ms            3
case5_atomic/0/manual_time_mean                           18.6 ms         20.5 ms            3
```

---

*报告基于 `0_basic_test` 实际 benchmark 运行数据生成（2026-08-27，WSL2/Linux + clang Release 构建，4 vCPU 2611 MHz（2C4T），Load Average ≈ 0.1 低负载环境）。*

---

## 附：环境沿革与数据迁移说明

- **2026-08-25 之前**：数据在 Windows 11 原生（clang-cl）与 WSL2 12 vCPU 配置下采集。12 vCPU 下 guest 呈现「6C12T + L1/L2 x6」拓扑——该拓扑是 Hyper-V 在 2P+8E 宿主（i7-1255U）上的虚拟呈现，与真实物理核无对应关系。
- **2026-08-27 起**：WSL 呈现 4 vCPU（2C4T）。本报告全部数据迁移至此环境重测（Load Average ≈ 0.1）。迁移导致的结论变化：① case3 加速比 2.49× → 1.87×（上限 = 2 物理核，并行效率 96%）；② case4 甜点从「T=12」变为「T=4 起宽平台（±15%）」，「中等超量最差 +45%」被证伪；③ case5 竞争膨胀 6.2× → 3.7×（竞争线程数从 12 降到 4）；④ case2 惩罚修正为区间表述并附噪声诊断。
