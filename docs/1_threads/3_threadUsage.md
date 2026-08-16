---
layout: post
title: （三）多线程那些事儿：实践与性能
categories: C++
related_posts: True
tags: threads
toc:
  sidebar: right
---

[toc]

## （三）多线程那些事儿：实践与性能

多线程的意义首先在于正确性，然后才是性能。本文将围绕线程安全和多线程性能展开。

### 1. 多线程概念

#### 1.1 什么是多线程加速比？

- **如果说单核耗时是单位`1`，n 核使用多线程期望耗时应该是`1/n`，但实际却差很远，是什么原因限制了？**

  - `理论速比`: `1/n` 是理想化模型，现实受多种系统与软件因素限制。
  - `线程切换开销`: 频繁调度、上下文切换和同步会吞噬并行收益。
  - `同步与资源竞争`: 锁、原子与等待导致阻塞；共享数据引发缓存一致性开销。
  - `缓存与内存瓶颈`: 伪共享、缓存未命中和内存带宽限制会拖慢并行性能。
  - `硬件影响`: NUMA、大小核（big.LITTLE）、频率/能耗限制会使多核不能线性扩展。
  - `任务粒度问题`: 任务太小时，调度/同步开销占比大，导致实际加速远低于 `n`。


- **如果取加速比定义为：原来耗时/现在耗时，如何尽可能提升多核加速比，使之接近`n`？**

  - `增大任务粒度`: 合并小任务或批量处理，减少调度与同步频次。
  - `减少同步点`: 局部聚合（per-thread buffers）、批量提交、减少共享写热点。
  - `优化数据局部性`: 按线程划分数据，避免伪共享（`alignas(64)`、填充）。
  - `负载均衡`: 动态划分任务或采用 work-stealing，避免部分核空闲。
  - `控制线程数`: 以物理核数或容量为基准，必要时做线程亲和性绑定（pinning）。
  - `用对工具与算法`: 优先使用高质量线程池、合适的并行范式（流水线、分片、map-reduce）。
  - `性能剖析驱动优化`: 用分析工具定位瓶颈（CPU、缓存、内存带宽、锁竞争），有的放矢调整。

  在我个人 12 核的电脑中，我测试过无竞争数据，数据错开缓存行的任务，测试加速比也不过是 6 左右。原因可能就是，任务太小，线程本身调度开销是能够和任务耗时比较的；也许在某个不清楚硬件特性，如访存问题、cache miss 等情况影响了加速比；也许 12 核不是同一性能，存在大小核等等；也许我电脑是笔记本，电源比较渣，即单核能跑满功率，多核情况下跑不满功率。

- **为什么加速比距离理论上限差这么远？** 
    达到理想加速比的前提是独立的计算资源，和独立的存储资源（内存硬盘），以及独立的数据（数据之间不依赖）。实际上多线程在跑的时候可以认为只有计算资源是独立的，而存储资源不是独立的，只有每一个核上的 cache 是独立的，L3 和内存都是共享的；而数据可能也不是共享的，多个线程可能需要操作同一份数据。共享的存储资源在硬件层面上会有影响，而共享的数据一般会带来数据的同步开销。因此，n 核机器的性能是很难做到单核机器的 n 倍，但如何让性能接近 n 倍却是一件有挑战且有意义的事情。

#### 1.2 上锁开销有多少？

实测数据显示，单线程下连续对锁进行一百万次的上锁和解锁操作，总耗时也仅为 2 毫秒。单次耗时为 2 纳秒。这意味着在业务场景中，锁的基础性能开销几乎可以忽略不计。

然而，实际应用中锁的性能瓶颈往往并非源自锁本身的执行时间，而是由于线程在等待锁时产生的阻塞。特别是在高并发环境下，多个线程争夺同一把锁时，线程可能会频繁进入等待状态，导致上下文切换和缓存失效等额外开销。这些等待时间往往远远超过锁操作本身的时间成本。

#### 1.3 如何减小锁的粒度？

锁的粒度尽量细化。如下通过“局部聚合 + 批量提交”减少锁竞争。

```c++
ThreadSafeVector<int> vec{}; // vec.size() == 32
ThreadSafeVector<int> ret{};

// 使用parallel操作vector，每4个element是1个task
// function是对vec做valid操作，没问题则拷贝到ret
tbb::parallel_for(policy:_4_element_per_task,
  function:[](size_t taskIdx){
      for(i=taskIdx * 4; i<(taskIdx+1) * 4); i++){
          if (valid(vec[i]))
              ret.push_back(ret);
      }
  }
)
```

ret 是线程安全的，内部通过锁实现。上面这个实现看上去没问题，但一般不如下面这种好。

```c++
ThreadSafeVector<int> vec{}; // vec.size() == 32
ThreadSafeVector<int> ret{};

tbb::parallel_for(policy:_4_element_per_task,
  function:[](){
      std::vector<int> local_vec;
      for(i=0; i<4; i++){
          if (valid(vec[i]))
              local_vec.push_back(ret);
      }
      ret.insert(ret.end(), local_vec.begin(), local_vec.end());
  }
)
```

简单来说，优化的地方在于批量提交，减少上锁次数。

#### 1.4 线程数开多少合适？

线程数量的设置对程序性能有着重要影响。一般而言，当线程数超过 CPU 核心数时，并不会带来性能提升。这是因为 CPU 核心数量决定了同一时刻能够真正并行执行的线程上限。过多的线程会导致操作系统线程调度变得更为复杂，增加了上下文切换的开销。每次上下文切换时，操作系统需要保存当前线程的运行状态（如寄存器值、程序计数器等），并恢复即将执行线程的状态，这一过程会消耗 CPU 时间和资源，从而降低了整体性能。

#### 1.5 如何理解多线程的缓存？

多线程的缓存失效，是线程上下文切换开销的主要组成之一。而理解多线程的缓存利用率，可以从指令缓存和数据缓存入手。

- **流水线和指令缓存** 我们可以从 CPU 单核的简单模型切入，探究指令执行的具体过程。通常情况下，一条汇编代码可能对应一条或多条机器指令，而一条指令的完整执行流程，涵盖取指令（IF）、指令译码（ID）、指令执行（EX）、访存取数（MEM）和结果写回（WB）这 5 个子过程（过程段） 。每个子过程至少需要一个时钟周期，实际指令执行耗时往往在几个到十几个周期不等。

那么，执行一条指令是否至少需要 5 个时钟周期呢？答案是否定的。现代 CPU 采用流水线技术，当第一条指令完成取指令（IF）阶段，进入指令译码（ID）阶段时，第二条指令便可以立即进入取指令（IF）阶段。这意味着，如果仅执行单条指令，确实至少需要 5 个时钟周期；但当执行多条指令时，执行效率会大幅提升。例如，执行十条指令，可能仅需 15 个时钟周期。

这种流水线技术极大地提升了 CPU 的指令执行效率，但也带来了新的挑战，其中**分支预测**的重要性尤为凸显。由于 CPU 会在当前指令尚未执行完毕时，提前预取下一条指令并执行，当遇到条件判断（如`if`语句）等具有分支逻辑的指令时，若分支预测错误，提前执行的指令结果将无效，需要被丢弃并重新执行正确分支的指令。

- **数据缓存**
  - **避免虚假共享**：当多个线程访问不同的变量，但这些变量恰好位于同一个缓存行中时，就会发生虚假共享问题。这会导致一个线程对缓存行的修改会使其他线程的缓存行失效，从而降低缓存命中率。可以通过将不同线程访问的变量分配到不同的缓存行中来避免虚假共享。例如，在 C 语言中，可以使用`alignas`关键字来指定变量的对齐方式，确保其独占一个缓存行。
  - **优化线程调度**：尽量让访问相同数据的线程在同一 CPU 核心上执行，这样可以利用 CPU 核心的本地缓存，减少跨核心的数据传输和缓存同步开销。可以通过设置线程的亲和性来实现，即将线程绑定到特定的 CPU 核心上。
  - **同步开销**：当多个线程同时拥有一份会被修改的数据的时候，如果这个数据会被频繁修改，多核就需要频繁同步这些数据。这个保证不同缓存一致性的协议，叫`mesi`协议。

* 铺垫完流水线、指令缓存、数据缓存的概念后，举一个例子。方法 A:

  ```c++
    void FuncA() {
      size_t n = 1<<11;
      std::vector<Data> dats(n);

      TICK(process);
      tbb::parallel_for_each(dats.begin(), dats.end(), [&] (Data &dat) {
          dat.step1();
          dat.step2();
          dat.step3();
          dat.step4();
      });
      TOCK(process);
    }

    void FuncB() {
      size_t n = 1<<11;
      std::vector<Data> dats(n);

      TICK(process);
      tbb::parallel_for_each(dats.begin(), dats.end(), [&] (Data &dat) {
          dat.step1();
      });
      tbb::parallel_for_each(dats.begin(), dats.end(), [&] (Data &dat) {
          dat.step2();
      });
      tbb::parallel_for_each(dats.begin(), dats.end(), [&] (Data &dat) {
          dat.step3();
      });
      tbb::parallel_for_each(dats.begin(), dats.end(), [&] (Data &dat) {
          dat.step4();
      });
      TOCK(process);
    }

    void FuncC() {
      size_t n = 1<<11;

      std::vector<Data> dats(n);

      TICK(process);
      auto it = dats.begin();
      tbb::parallel_pipeline(8,
        tbb::make_filter<void, Data *>(tbb::filter_mode::serial_in_order,
        [&] (tbb::flow_control &fc) -> Data * {
            if (it == dats.end()) {
                fc.stop();
                return nullptr;
            }
            return &*it++;
        })
        , tbb::make_filter<Data *, Data *>(tbb::filter_mode::parallel,
        [&] (Data *dat) -> Data * {
            dat->step1();
            return dat;
        })
        , tbb::make_filter<Data *, Data *>(tbb::filter_mode::parallel,
        [&] (Data *dat) -> Data * {
            dat->step2();
            return dat;
        })
        , tbb::make_filter<Data *, Data *>(tbb::filter_mode::parallel,
        [&] (Data *dat) -> Data * {
            dat->step3();
            return dat;
        })
        , tbb::make_filter<Data *, void>(tbb::filter_mode::parallel,
        [&] (Data *dat) -> void {
            dat->step4();
        })
      );
      TOCK(process);
    }
    int main() {
        return 0;
    }
  ```

  在性能对比实验中，方法 B 较方法 A 实现了约 5%的性能提升，而方法 C 的处理效率更是达到前两者的两倍之多。其核心优化逻辑在于采用类流水线作业模式：将多核并行中每个核心独立执行全流程任务（如原料接收、加工处理、成品转运）的方式，调整为多核分工协作——核 A 专职原料接收，核 B 专注加工处理，核 C 负责成品转运。这种设计显著提升了指令缓存的利用率，减少了重复计算开销。

  不过，需要明确的是，这种流水线优化与分支预测属于时钟周期级别的优化手段，其优化效果以纳秒为单位计量。然而需要辩证看待这类优化方案的普适性。在实际业务场景中，若任务存在强同步性要求，或任务本身耗时大，上述缓存优化带来的效率增益可被完全忽略。

  因此，该案例的价值在于给出多线程极致优化的思考方向之一，更在于警示开发者：在多线程编程实践中，需始终保持审慎态度，充分评估技术方案与业务场景的适配性，避免陷入理论性能陷阱。


### 2. 线程安全陷阱问题

#### 2.1 线程让出后的虚假检查

```c++
// 错误示例：检查未受保护，且按值遍历造成复制
void Foo(std::vector<Foo> arr, int size) {
    for (auto foo : arr) {
        if (!foo.is_valid())
            continue;
        {
            std::lock_guard<std::mutex> lock(mtx);
            // Do something with foo.
        }
    }
}
```

当通过`valid()`判断，但因为占不到锁让出 CPU，再回来的时候`foo`难保就失效了。因此`foo`的有效性检查也得受`mutex`的保护；同时要按引用遍历避免复制。

```c++
// 正确示例：在锁内做有效性检查与使用，按引用遍历
std::mutex mtx;
struct Item { bool is_valid() const; /*...*/ };

void process(std::vector<Item>& arr) {
    for (auto& it : arr) {
        std::lock_guard<std::mutex> lk(mtx);
        if (!it.is_valid()) continue;
        // 安全使用 it
    }
}
```

#### 2.2 线程饥饿问题
一个典型的线程饥饿示例代码如下：
```c++
int parent_task = 20;
int child_task = 10;
std::vector<std::future<void>> futures;
// 添加若干个 parentTask，每个 parentTask 创建若干个 subTask。
for (int i = 0; i < parent_task; ++i) {
    // add parentTask
    futures.emplace_back(gPool.submitTask([child_task]() {
        std::vector<std::future<void>> childFutures;
        childFutures.reserve(child_task);
        for (int j = 0; j < child_task; ++j) {
            childFutures.emplace_back(
                gPool.submitTask([]() { return taskNear100ms(); }));
        }
        for (auto& future : childFutures) {
            future.get();
        }
    }));
}

for (auto& future : futures) {
    future.get();
}
```

当`parent_task`取值为 5 时，程序运行顺畅，全程耗时约 5 秒；然而，一旦`parent_task`的值超过`coreNum`，程序便陷入卡死状态，实际测试发现，等待 5 分钟后仍未结束运行。为何任务量仅增加一点，就会导致程序卡死呢？

这一现象的根源在于线程池采用的是固定大小设计，无法动态扩展。当`parent_task`数量过多，占用了线程池的全部线程资源后，`subTask`便无法再获取到可用线程。这些`subTask`只能滞留在任务队列中，无法执行。而`parent_task`又必须等待`subTask`执行完毕才能释放其所占用的线程，如此一来，`subTask`因缺乏线程资源无法运行，`parent_task`也因等待`subTask`而无法释放线程，最终形成死锁，导致程序卡死。

这种因线程资源分配不均，导致部分任务无法获取线程的问题，在业界被统称为**线程饥饿**。

解决这种线程饥饿问题的一个有效方法是，允许线程池动态创建线程。当线程池中的所有线程都被占用时，若有新的任务需要执行，线程池可以根据当前负载情况，动态增加线程数量，以满足任务的执行需求。这样，即使`parent_task`占用了所有初始线程资源，`subTask`仍然能够通过新增的线程获得执行机会，从而避免了死锁现象的发生。

另外一个有效方法是，为`subTask`单独创建线程池。这样，即使`parent_task`占用了主线程池的所有线程资源，`subTask`仍然可以在其独立的线程池中获取线程资源，顺利执行。但这个方法依赖开发者意识到父任务和子任务同时在一个固定线程池中运行可能会引发线程饥饿问题，并主动为子任务创建独立线程池。

但不管怎样，尽管理论上说线程池数量大于系统核心数是无意义的的，但实际使用中，线程数量一定要大于核心数才行。否则任务一旦存在依赖，就导致这种类死锁问题了。因此，线程数量不能一刀切定死，也不能无限制放开。而任务管理器，其实可以看当前线程数量的。正常家用机使用的时候往往有`3-6000`的线程。所以不要随意滥用，其实不需要考虑线程数量太多的问题。


#### 2.3 多线程的乒乓缓存/ 伪共享问题

```c++
namespace {

    NO_OPTIMIZE void countNumber(int counter) {
        int value = 0;
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void countNumberWithRef(int& value, int counter = 240'000'000) {
        for (int i = 0; i < counter; ++i) {
            ++value;
        }
        assert(value == counter);
    }

    NO_OPTIMIZE void taskNear100ms() { countNumber(240'000'000); }

}  // namespace

void A_single_thread_cost(benchmark::State& state) {
    for (auto _ : state) {
        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            taskNear100ms();
        }
    }
}

void B_multi_thread_falseSharing_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<int> nums(std::thread::hardware_concurrency());

        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([&nums, i]() { countNumberWithRef(nums[i]); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}

void C_multi_thread_no_falseSharing_cost(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(std::thread::hardware_concurrency());

        for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
            threads.emplace_back([]() { taskNear100ms(); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
}
```

- 方法 A（单线程执行）

  - **耗时**：1000ms
  - **原因**：  
    单线程顺序执行多个计算任务，每个任务耗时约 100ms。由于硬件通常支持 6-16 核并发，单线程无法利用多核优势，因此总耗时为任务数 × 单任务耗时（约 100ms × 10 核 ≈ 1000ms）。

- 方法 B（多线程+伪共享）

  - **耗时**：4905ms（显著高于方法 A 和 C）
  - **原因**：  
    多个线程共享一个连续内存数组`nums`，每个线程修改其中一个元素。由于 CPU 缓存以缓存行为单位（通常 64 字节）加载数据，相邻的数组元素可能被加载到同一缓存行。当不同线程修改同一缓存行中的不同变量时，会触发**缓存乒乓（Cache Ping-Pong）**：
    1. 线程 T1 修改元素`nums[0]`，导致缓存行被标记为脏（Dirty）并写回内存。
    2. 线程 T2 修改相邻元素`nums[1]`，发现缓存行已失效，必须从内存重新加载。
    3. 频繁的缓存行失效和同步操作（MESI 协议）导致大量额外开销，称为**伪共享（False Sharing）**。

- 方法 C（多线程+无伪共享）
  - **耗时**：323ms（性能最佳）
  - **原因**：  
    每个线程独立执行`taskNear100ms()`，不共享任何变量。由于无需访问共享内存，线程间无缓存一致性开销，各线程可充分利用本地缓存，实现真正的并行计算。多核 CPU 并行处理多个任务，总耗时接近单任务耗时（100ms），仅受线程创建和同步开销影响。

1. **乒乓缓存（Cache Ping-Pong）**  
   多个线程交替修改同一缓存行中的不同变量时，缓存行在 CPU 核心间频繁传输（类似乒乓球），导致性能下降。
2. **伪共享（False Sharing）**  
   当多个变量被放入同一缓存行，但不同线程分别修改这些变量时，尽管变量逻辑上独立，仍会因缓存行共享而相互影响，触发缓存失效。

实战规避建议：

- 使用填充/对齐隔离热写字段：

```c++
struct alignas(64) PaddedInt { int v; char pad[64 - sizeof(int)]; };
```

- 将不同线程写入的数据拆分到不同缓存行；批量合并结果。
- 若以数组承载，每步 stride 设为缓存行大小的倍数，或改成 AoS→SoA 的数据布局。



### 3. 怎么用好多线程？

#### 3.1 双重检查

```c++
class Singleton {
public:
    static Singleton* getInstance() {
        // 第一次检查（无需加锁，快速路径）
        if (instance == nullptr) {
            std::lock_guard<std::mutex> lock(mutex_);
            // 第二次检查（在锁内，再次确认）
            if (instance == nullptr) {
                instance = new Singleton();
            }
        }
        return instance;
    }

    void doSomething() {
        std::cout << "Doing something..." << std::endl;
    }
// ...
}
```

#### 3.2 局部聚合 + 批量合并

- 目标：减少全局锁竞争，避免每条记录都加锁。
- 做法：每个线程使用局部缓冲（vector/local bucket），任务结束后一次性合并到全局容器。

示例骨架

```c++
std::mutex m; std::vector<Item> global;
parallel_for(tasks, [&](Task t){
  std::vector<Item> local;
  // 线程内自由 push 到 local
  // ...
  if (!local.empty()) {
    std::lock_guard<std::mutex> lk(m);
    global.insert(global.end(), local.begin(), local.end());
  }
});
```

### 99. quiz

#### 1. 线程抛出异常会怎么样？并行库抛出异常会怎么样？

- `std::thread`：线程函数内未捕获异常将调用 `std::terminate`。需在线程函数内捕获，并通过 `std::promise`/回调上报。
- `std::async`：异常被捕获并存入共享状态，在 `future.get()` 处重新抛出。
- 线程池/并行库（如 TBB/OpenMP）：通常聚合并在调用点重新抛出或转换为库自定义异常；务必在并行边界做 try/catch 并审阅库文档的传播策略。

#### 2. 如果是单核环境，多线程编程是否就不会出现数据竞争的情况呢？

即使只有一个 CPU 核心，多线程程序中如果 多个线程访问同一共享变量，且至少一个是写操作，且没有同步机制，依然会发生数据竞争。

虽然单核 CPU **同一时刻只能执行一个线程**，但：

1. **线程调度是抢占式的**：

   - 操作系统会在多个线程之间频繁切换（即“上下文切换”），这可能发生在一条语句执行了一半时；
   - 导致线程 A 修改某个变量到一半，被挂起，线程 B 也访问同一个变量，就可能出问题。

2. **读-改-写不是原子操作**：

   - 举个例子：`x++` 实际执行为 `load → add → store` 三步；
   - 如果线程 A 做到一半，线程 B 抢占了，就会覆盖结果。

3. **编译器/CPU 有指令重排行为（store/load reordering）**：

   - 即使在单核上，编译器为了优化，可能重排执行顺序；
   - 如果没有使用 `std::atomic` 或内存屏障，结果也可能错误。

#### 3. 多线程程序中持有锁，然后时间片耗光，让出 CPU，锁会释放吗？

不会。 这个时候线程状态会切换到`Running`状态。一直持有锁，哪怕没有持有时间片。因此，锁的粒度需要精细控制，不要有多重的开销。
