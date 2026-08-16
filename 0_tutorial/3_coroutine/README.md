# C++ 协程教程

协程是一种"可以暂停并在之后恢复的函数"。与线程不同，协程的切换发生在用户态，由程序员或运行时库控制，不需要操作系统介入。这使得协程非常适合 I/O 密集型任务、生成器模式、状态机等场景——以顺序化的代码风格表达异步逻辑，同时避免线程的同步开销。

```
3_coroutine/
├── CMakeLists.txt
├── README.md
├── 1_cpp20/                     # C++20 协程（主教学路径，编译运行）
│   ├── 1_promiseType.cpp        #   协程生命周期：promise_type, 帧, eager/lazy, 异常, 销毁
│   ├── 2_awaiter.cpp            #   awaiter 三步流程，await_suspend 三种返回类型
│   ├── 3_customAwaiter.cpp      #   自定义 awaiter：计时器、回调适配、对称转移
│   ├── 4_generator.cpp          #   Generator<T>：惰性序列、斐波那契、管道
│   ├── 5_task.cpp               #   Task<T>：异步任务、异常传播、任务链
│   ├── 6_scheduler.cpp          #   协作式调度器：Round-Robin、定时挂起
│   └── 7_quiz.cpp               #   实战练习：状态机、生产者-消费者、管道
├── 2_boost/                     # Boost.Coroutine2 参考（默认不编译）
│   ├── 1_asymmetric.cpp         #   非对称协程：pull_type/push_type、生成器、异常
│   ├── 2_symmetric.cpp          #   对称协程：call_type/yield_type（概念参考）
│   └── 3_bidirectional.cpp      #   双向传递、管道、Boost.Asio 协程模式
└── 3_impl/                      # 从零实现协程（纯 C++，编译运行）
    ├── 1_manualStateMachine.cpp #   手写 switch 状态机——编译器协程转换的本质
    ├── 2_macroCoroutine.cpp     #   Duff's device 宏协程——用预处理器模拟 co_yield
    └── 3_simpleScheduler.cpp    #   协作式调度器：Round-Robin、优先级、事件驱动
```

## 学习路径建议

### 入门（理解协程是什么）
1. `1_promiseType.cpp` —— 协程的生命周期：帧、promise_type、eager vs lazy
2. `2_awaiter.cpp` —— co_await 背后发生了什么
3. `3_customAwaiter.cpp` —— 用协程做实际的事：计时器、回调适配

### 进阶（掌握协程的实战用法）
4. `4_generator.cpp` —— 生成器模式，惰性序列，管道的协程版
5. `5_task.cpp` —— 异步任务封装，异常安全，协程嵌套
6. `6_scheduler.cpp` —— 协程调度原理，事件循环
7. `7_quiz.cpp` —— 综合练习

### 深入原理（理解编译器做了什么）
8. `3_impl/1_manualStateMachine.cpp` —— 手写状态机，揭示协程的本质
9. `3_impl/2_macroCoroutine.cpp` —— Duff's device 技巧，编译器优化背后的思想
10. `3_impl/3_simpleScheduler.cpp` —— 用最原始的机制搭建调度器

### 对照参考
- `2_boost/` —— Boost.Coroutine2（有栈协程）的用法参考。需要安装 Boost，本机未安装，仅供阅读。

## 编译要求

| 要求 | 说明 |
| --- | --- |
| **C++ 标准** | C++20（协程是 C++20 特性） |
| **编译器** | MSVC 2022 / GCC 11+ / Clang 14+ |
| **CMake** | 3.16+ |
| **Boost** | 可选（仅 2_boost/ 需要，需安装 Boost.Coroutine2） |

## 快速编译

### CMake 构建（推荐）

```bash
# 配置
cmake -S 3_coroutine -B 3_coroutine/build

# 编译
cmake --build 3_coroutine/build --config Release

# 运行所有示例
3_coroutine/build/Release/01_promiseType.exe
3_coroutine/build/Release/02_awaiter.exe
# ... 等等
```

### 单文件编译（MSVC）

```bash
cl /std:c++20 /utf-8 /EHsc 1_cpp20/1_promiseType.cpp
```

### 单文件编译（GCC/Clang）

```bash
g++ -std=c++20 1_cpp20/1_promiseType.cpp -o promiseType
```

### Boost 文件手动编译（需安装 Boost）

```bash
g++ -std=c++17 2_boost/1_asymmetric.cpp -lboost_coroutine -pthread
```

## 代码风格

- 每个 `.cpp` 文件是独立的可执行程序
- 使用 `namespace` 隔离不同主题
- 每个 namespace 有统一的入口函数 `task()`
- 注释使用中文教学风格
- namespace 使用 `snake_case`，类型使用 `PascalCase`

## 与 1_multiThread 的关系

| 维度 | 线程（1_multiThread） | 协程（3_coroutine） |
| --- | --- | --- |
| **调度方式** | 抢占式（操作系统控制） | 协作式（程序员控制 yield 点） |
| **运行态** | 内核态线程 | 用户态（同一个线程内） |
| **切换开销** | ~1-10 µs（上下文切换） | ~ns 级（函数调用级别） |
| **并发模型** | 真并行（多核同时执行） | 真并发（单核交替执行） |
| **同步手段** | mutex, atomic, condition_variable | co_yield / co_await（天然互斥） |
| **适用场景** | CPU 密集型、真并行计算 | I/O 密集型、异步流程、生成器 |

协程不是线程的替代品，而是互补品。高频 I/O、大量并发连接用协程；CPU 密集型计算用线程。
