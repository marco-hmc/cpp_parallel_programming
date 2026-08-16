# C++ 多线程编程教程

本教程系统地介绍 C++ 多线程编程，从基础概念到高级技术，适合有 C++ 基础的学习者。

## 目录结构

```
1_multiThread/
├── 1_threadCreate/          # 线程创建与管理
│   ├── 1_threadCreate.cpp     std::thread 创建、重载函数、成员函数、引用参数、移动语义
│   ├── 2_threadManage.cpp     线程ID、thread_local、yield、join/joinable
│   ├── 3_async.cpp            std::async、launch::async vs launch::deferred
│   ├── 4_packaged_task.cpp    std::packaged_task、与 thread/async 的对比
│   ├── 5_jthread.cpp          C++20 jthread、stop_token、stop_source
│   └── 6_parallel_algorithms.cpp  C++17 并行算法（sort、transform、reduce）
│
├── 2_threadsManage/         # 线程同步原语
│   ├── 0_mutex.cpp            std::mutex、try_lock、recursive_mutex、shared_mutex
│   ├── 1_lockManager.cpp      lock_guard、unique_lock、scoped_lock
│   ├── 2_wait.cpp             condition_variable、wait、wait_for、notify_all_at_thread_exit
│   ├── 3_promise.cpp          promise/future、shared_future、wait_for/wait_until
│   ├── 4_atomic_operator.cpp  原子操作、lock-free、atomic_flag、call_once、fence
│   ├── 5_memoryOrder.cpp      memory_order_relaxed、release-consume、release-acquire、seq_cst
│   ├── 6_cpp20_sync.cpp       C++20 latch、barrier、counting_semaphore
│   └── 7_deadlock.cpp         死锁场景、scoped_lock 避免死锁、层级锁
│
├── 3_openmp/                # OpenMP 并行编程
│   ├── 01_parallelControl.cpp   parallel、parallel for、sections、task、simd、target
│   ├── 02_default.cpp           private、firstprivate、lastprivate、reduction
│   ├── 03_parallelSync.cpp      critical、atomic、barrier、ordered、lock
│   ├── 04_func_and_env.cpp      omp API 函数和环境变量
│   └── quiz/                    练习：生产者-消费者、链表处理、π 计算、数值积分
│
├── 4_tbb/                   # Intel TBB 并行库
│   ├── 0_parallel_for.cpp      tbb::parallel_for、parallel_reduce、parallel_scan
│   ├── 1_exception.cpp         TBB 异常处理
│   ├── 2_concurrent_containers.cpp  concurrent_vector、concurrent_queue、concurrent_hash_map
│   ├── 3_task_arena.cpp        task_arena、task_scheduler_observer
│   └── 4_flow_graph.cpp        TBB Flow Graph（数据流图）
│
├── 5_threadsQuiz/           # 经典并发问题实战
│   ├── 01_producerConsumer.cpp  生产者-消费者（有界队列 + 单槽位）
│   ├── 02_printOrder.cpp        顺序打印 ABC（cv/atomic/future 三种解法）
│   ├── 03_threads_separate.cpp  并行 accumulate 分块计算
│   ├── 04_countDownLatch.cpp    CountDownLatch 实现
│   ├── 05_pingPongCache.cpp     伪共享（False Sharing）演示
│   ├── 06_AtomicVsMutex.cpp     atomic vs mutex 性能对比
│   ├── 07_readersWriters.cpp    读写锁（C++11 手动实现 + C++17 shared_mutex）
│   └── 08_threadPool.cpp        简单线程池实现
│
└── 6_lockFree/              # 无锁编程
    ├── CAS.cpp                   CAS 基础
    ├── lockfree_queue.cpp        SPSC/MPSC 无锁队列
    └── aba_problem.cpp           ABA 问题与 Tagged Pointer 解决方案
```

## 学习路径建议

### 入门级（理解多线程基础）
1. `1_threadCreate/1_threadCreate.cpp` — 创建线程的各种方式
2. `1_threadCreate/2_threadManage.cpp` — 线程生命周期管理
3. `2_threadsManage/0_mutex.cpp` — 互斥锁基础
4. `2_threadsManage/1_lockManager.cpp` — RAII 锁管理器

### 进阶级（掌握同步机制）
5. `2_threadsManage/3_promise.cpp` — promise/future 异步结果
6. `1_threadCreate/3_async.cpp` — std::async 异步任务
7. `1_threadCreate/4_packaged_task.cpp` — packaged_task 任务包装
8. `2_threadsManage/2_wait.cpp` — 条件变量
9. `2_threadsManage/4_atomic_operator.cpp` — 原子操作
10. `2_threadsManage/5_memoryOrder.cpp` — 内存顺序

### 高级（实践与深入）
11. `5_threadsQuiz/` 各文件 — 经典并发问题
12. `2_threadsManage/7_deadlock.cpp` — 死锁与避免
13. `6_lockFree/` — 无锁编程
14. `5_threadsQuiz/08_threadPool.cpp` — 线程池实现

### 框架与现代化
15. `3_openmp/` — OpenMP 快速上手
16. `4_tbb/` — Intel TBB 高级并行
17. `1_threadCreate/5_jthread.cpp` — C++20 新特性
18. `2_threadsManage/6_cpp20_sync.cpp` — C++20 同步原语
19. `1_threadCreate/6_parallel_algorithms.cpp` — C++17 并行算法

## 编译要求

- **编译器**: 支持 C++17 或更高（MSVC 2019+、GCC 9+、Clang 10+）
- **可选依赖**:
  - OpenMP: 编译时添加 `-fopenmp`（GCC/Clang）或 `/openmp`（MSVC）
  - TBB: 安装 Intel TBB 库，编译时链接 `-ltbb`

### 快速编译示例

```bash
# 基础 C++ 文件
g++ -std=c++17 -pthread 1_threadCreate/1_threadCreate.cpp -o threadCreate

# OpenMP 文件
g++ -std=c++17 -fopenmp 3_openmp/01_parallelControl.cpp -o openmp_demo

# TBB 文件
g++ -std=c++17 -fopenmp 4_tbb/0_parallel_for.cpp -ltbb -o tbb_demo
```

也可以使用 CMake 构建（推荐），见各子目录下的 CMakeLists.txt。

## 代码风格

- 每个 `.cpp` 文件是独立的可执行程序
- 使用 `namespace` 隔离不同主题
- 每个 namespace 有统一的入口函数 `task()`
- 注释使用中文教学风格
- 命名规范: namespace 使用 `snake_case`，类型使用 `PascalCase`
