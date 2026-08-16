---
layout: post
title: （二）协程那些事儿：C++20 协程
categories: C++
related_posts: True
tags: coroutine
toc:
  sidebar: right
---

[toc]

## （二）协程那些事儿：C++20 协程

### 1. concepts

#### 1.1 什么是协程？

协程是**可以暂停并在之后恢复的函数**。为什么需要这种能力？许多函数在执行过程中会遇到需要等待的操作（如网络 I/O、磁盘 I/O 等），如果线程被阻塞等待这些操作完成，就会浪费宝贵的处理资源。解决这种问题的手段就是异步编程，协程就是一种达成异步目的的方式。

那什么是异步编程呢？异步编程的核心思想是**非阻塞等待**，即在等待操作完成时，线程可以继续处理其他任务，等到结果准备好再回来处理，从而避免阻塞。换句话说，可以从"通知"和"执行"这两个角度来理解异步编程：当一个操作需要等待时，程序不会停下来等它完成，而是继续执行其他代码。当等待的操作完成后，会通过某种方式（如回调函数、事件等）**通知**程序结果已经准备好，然后继续**运行**适当的操作。

而实现异步的本质方式就有两种：多线程（多一个人处理）和调度机制（停下来，之后再处理）。多线程：为等待操作另启线程；通知的实现依靠线程同步手段。问题就在于这会带来同步与竞态的复杂性。调度机制：依赖背后一个机制将需要异步（长时间耗时的事情）搁置起来，延后处理。比如说事件循环，就是这种调度机制的一种最常见方式。利用 `sendEvent` 和 `postEvent` 等发出事件将一个事情插入事件列表，延后处理；只要顺序处理事件列表，就会恢复过来处理这种事情了。这是这种方式会使代码结构变得分散和难以维护。

p.s.: 比如说烧水-劈材-做饭。在烧水的时候，我去劈材，这个就是异步的，即可以在烧水的时候停下来，回过头来再处理，什么时候处理依赖具体机制调度实现，可以有很多方式；而如果我让我朋友去烧水，我继续劈材，那就是多线程。如果我再让一个朋友去做饭，做饭要柴火，我一边劈材，朋友一边用我劈好的柴火做饭，就是生产者-消费者模型，多线程同步。

协程也可以认为也是调度机制的一种形式。怎么理解呢？协程在等待时会暂停执行并让出控制权，那让给谁，什么时候恢复，都需要背后一个的调度机制。只是一般而言，协程的调度机制更轻量级一些，调度到哪里去得在程序当中就声明出来。协程根据调度形式也可以分为两种。

| 类型           | 特点                       | 控制流          | 适用场景         |
| -------------- | -------------------------- | --------------- | ---------------- |
| **非对称协程** | 使用 yield 返回调用方      | 调用-被调用关系 | 生成器、异步函数 |
| **对称协程**   | 使用 resume 切换到任意协程 | 平等的相互切换  | 复杂的协程网络   |

其中一种最常见的是非对称协程。什么是非对称协程呢？从开发角度理解则是，使用 `yield` 指定返回到调用点，也只能从调用点返回就是非对称协程。因为协程关系之间是不平等，所以有一个学术一点的说法就是非对称协程。这种调度形式，其实就是类似于一个 `goto` 跳转，只是 `goto` 跳转只能在同一个函数内跳转，而协程可以跨函数调用边界跳转。因此需要 `goto` 之前保存当前状态，`goto` 到的地方，恢复之前保存的状态继续执行。

另外一种则是对称协程，对称协程允许协程之间相互切换，而不需要通过调用点返回。即可以通过 `resume()` 这样，协程之间可以平等地切换执行权。对称协程的实现通常更复杂一些，因为需要管理多个协程的状态和切换逻辑。这个时候调度就更麻烦一些。这种对称协程使用相对少一些。补充了解概念即可。

总结来说，协程提供了一种更轻量的机制：在协程内部可以显式地暂停并让出控制权，待适当时机恢复，从而以更直观的顺序化代码风格实现异步逻辑。与操作系统调度的线程不同，协程的调度通常由程序员或运行时库控制，因此被视为"轻量级"的并发手段（注意：协程和提升单线程性能无关，只是提高并发能力，以及代码写法的调整）。其优点如下：

1. 减少线程阻塞，提高并发量；
2. 以顺序化的写法表达异步流程，代码更清晰可维护。

#### 1.2 协程实现原理

协程实现的本质是**上下文切换**，包含三个关键步骤：

需要保存的上下文信息：

- **程序计数器（PC/EIP/RIP）**：当前执行指令地址
- **栈指针（SP）**：当前栈顶位置
- **通用寄存器**：处理器寄存器状态
- **局部变量**：函数调用栈中的数据

```cpp
// 伪代码示例
void coroutine_switch(Coroutine* from, Coroutine* to) {
    // 1. 保存当前协程状态
    save_context(&from->context);

    // 2. 切换到目标协程
    current_coroutine = to;

    // 3. 恢复目标协程状态
    restore_context(&to->context);
}
```

---

### 2. 协程的使用

协程函数是在其函数体内使用了 `co_await`、`co_yield` 或 `co_return` 的函数。

- `co_await`：通常用于等待一个 awaitable，对当前协程进行挂起并将控制权移交给等待者或调度器，待被恢复后继续执行。
- `co_yield`：常用于生成器，挂起并把一个值返回给调用方，下一次恢复时继续执行。
- `co_return`：结束协程并返回值（或对 void 协程表示终止）。

注意：具体行为并非由关键字本身完全决定，而是受协程类型及其 `promise_type`/调度器的实现影响。

#### 2.1 co_yield 的示例

```c++
    struct Generator {...}; // 省略 Generator 实现细节

    Generator fibonacci(int count) {
        int a = 0, b = 1;
        for (int i = 0; i < count; ++i) {
            co_yield a;
            auto next = a + b;
            a = b;
            b = next;
        }
    }

    void demonstrateBasicGenerator() {
        std::cout << "\n=== 基础生成器演示 ===" << std::endl;

        std::cout << "斐波那契数列 (前10个): ";
        for (auto value : fibonacci(10)) {
            std::cout << value << " ";
        }
        std::cout << std::endl;
    }
```

`Generator` 是一个协程函数，这个函数执行到 `co_yield a;` 的时候会暂停执行，并将 `a` 的值返回给调用方。下一次调用时会从暂停的地方继续执行。

#### 2.2 co_await 与 co_return 的示例

```c++
    template<typename T>
    struct Task {...}; // 省略 Task 实现细节

    // 简单的异步操作模拟
    Task<int> async_compute(int value) {
        std::cout << "[AsyncCompute] 开始计算: " << value << std::endl;
        // 模拟异步工作
        std::this_thread::sleep_for(100ms);
        int result = value * value;
        std::cout << "[AsyncCompute] 计算完成: " << result << std::endl;
        co_return result;
    }

    // 链式异步操作
    Task<std::string> async_process_chain(int input) {
        std::cout << "[ProcessChain] 开始处理: " << input << std::endl;
        auto squared = co_await async_compute(input);
        std::this_thread::sleep_for(50ms);
        auto result = "结果: " + std::to_string(squared);
        std::cout << "[ProcessChain] 处理完成: " << result << std::endl;
        co_return result;
    }

    void demonstrateAsyncTask() {
        std::cout << "\n=== 异步任务演示 ===" << std::endl;
        auto task1 = async_compute(5);
        auto task2 = async_process_chain(7);
        std::cout << "任务1结果: " << task1.get() << std::endl;
        std::cout << "任务2结果: " << task2.get() << std::endl;
    }
```

以上示例省略了 Task 与 Generator 的实现细节（它们涉及协程帧、promise、awaiter 等底层机制），后文会详细介绍这些实现要点。

---

### 3. 协程类型

协程的行为由其返回类型（例如 Generator、Task）及其关联的 `promise_type` 决定。与普通函数的返回类型类似，协程返回类型负责管理协程帧、调度与结果传递。为了方便，将协程函数返回类型称为"协程类型"。

实现协程类型时常从以下三方面考虑：

- `promise_type`（生命周期与行为定义）
- `std::coroutine_handle`（协程句柄，用于控制协程执行）
- awaiter/awaitable（实现 `co_await` 的可等待对象接口）

重要提示：作为内置原生语法的一部分，协程接口不需要通过虚函数的方式去实现，而是由编译器在编译期通过约定的方法名静态调用（即隐式契约），以减少运行时开销。有点像特殊成员函数（构造函数、析构函数等）的隐式调用。

#### 3.1 协程句柄

```c++
#include <coroutine>
#include <iostream>
struct SimpleCoroutine {
    // 编译器根据这个类型生成协程帧，确定生命周期管理和行为
    struct promise_type {
        ... // 省略 promise_type 实现细节
    };

    using handle_type = std::coroutine_handle<promise_type>;
    explicit SimpleCoroutine(handle_type h) : coro(h) {}
    ~SimpleCoroutine() { if (coro) coro.destroy(); }
    void resume() { coro.resume(); }

private:
    handle_type coro;
}
```

`std::coroutine_handle<promise_type>` 这个类型是协程的句柄，允许我们控制协程的执行（如恢复、销毁等）。通过这个句柄，我们可以在外部管理协程的生命周期。句柄的背后是协程帧（coroutine frame），它包含了当前函数的局部变量信息、运行状态信息，以便恢复和暂停。协程句柄一般提供以下常用操作：

- `resume()`: 恢复协程的执行。
- `destroy()`: 销毁协程帧，释放资源。
- `done()`: 检查协程是否已经完成执行。
- `promise()`: 获取与协程关联的 promise 对象。

`promise_type` 定义了协程的行为和生命周期管理，下面详细介绍 promise_type 的常见成员。

#### 3.2 协程 promise 协议——行为和生命周期定义

```c++
template<typename T>
struct Task {
    // 协程生命周期管理
    struct promise_type {
        T value_;
        // 协程初始挂起状态
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T value) { value_ = value; }
        void unhandled_exception() { std::exit(1); }
        Task get_return_object() {
            return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;
    explicit Task(handle_type h) : coro(h) {}
    ~Task() { if (coro) coro.destroy(); }
    T get() {
        coro.resume();
        return coro.promise().value_;
    }
private:
    handle_type coro;
};
```

常见的 `promise_type` 成员及其含义：

- `initial_suspend()`: 决定协程创建时是否立即运行（返回 awaiter，如 `std::suspend_always` 或 `std::suspend_never`）。
- `final_suspend()`: 在协程结束时调用，返回一个 noexcept 的 awaiter，用于决定结束后的行为（例如唤醒等待者或让调度器接管）。
- `get_return_object()`: 在协程帧创建后、`initial_suspend` 之前调用，用于构造并返回外壳对象（如 Task、Generator）。
- `return_value(T)`/`return_void()`: 处理 `co_return` 的结果存放。
- `yield_value(U)`: 处理 `co_yield` 的值（常用于生成器）。
- `unhandled_exception()`: 处理协程内部未捕获的异常（通常保存异常以便在 `get`/`await_resume` 时重新抛出）。
- `await_transform(expr)`: 可选，允许 promise 拦截 `co_await` 的表达式以自定义等待语义。
- 自定义 `operator new`/`delete`：可选，用于控制协程帧的分配策略。

其他注意事项：

- `final_suspend` 的 awaiter 需谨慎设计，避免在唤醒等待者后产生悬空访问。
- 若 promise 中管理等待队列或原子状态，要小心并发场景与资源释放时机。
- `unhandled_exception` 应保存异常（如 `std::current_exception()`），不要在该函数再抛出异常。

编译器通过 `coroutine_traits<R, Args...>::promise_type` 找到相应的 `promise_type`，允许基于返回类型或参数组合自定义协程行为。

#### 3.3 awaitable/awaiter 示例

前面提到，`co_await` 后面可以跟任意类型的表达式，编译器会根据该类型查找相应的 awaiter 接口。一个类型要成为 awaitable，需要实现以下三个方法：

```c++
struct SimpleAwaitable {
    bool await_ready() const noexcept { return false; } // 是否立即就绪
    void await_suspend(std::coroutine_handle<> h) noexcept {
        // 挂起协程，保存句柄以便稍后恢复
        handle_ = h;
    }
    int await_resume() noexcept {
        // 恢复时返回结果
        return 42;
    }
private:
    std::coroutine_handle<> handle_;
};
```

而 `std::suspend_always` 和 `std::suspend_never` 是标准库提供的两个简单 awaiter 类型，分别表示总是挂起和从不挂起：

```c++
struct suspend_always {
    bool await_ready() const noexcept { return false; } // 总是挂起
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};
struct suspend_never {
    bool await_ready() const noexcept { return true; } // 从不挂起
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};
```

`co_await` 一个表达式时，编译器会检查该表达式的类型是否满足 "Awaitable" 协议。这个协议由以下三个核心函数定义，它们共同决定了 `co_await` 的行为，我们可以将其理解为一个"三步走"的流程：

**第 1 步: `await_ready()` —— 需要挂起吗？**

这是 `co_await` 调用的第一个函数，用于进行快速路径检查。

- **返回 `true`**: 表示操作已同步完成，结果立即可用。协程**不会**挂起。流程将跳过 `await_suspend`，直接调用 `await_resume` 获取结果。这是一种性能优化，避免了不必要的协程挂起和恢复开销。
- **返回 `false`**: 表示操作未就绪，需要等待。协程**准备挂起**，流程将继续到第 2 步 `await_suspend`。

```cpp
bool await_ready() const noexcept;
```

**第 2 步: `await_suspend()` —— 挂起后做什么？**

仅当 `await_ready` 返回 `false` 时，此函数才会被调用。它的核心职责是处理挂起逻辑，例如将协程的句柄（`h`）交给某个执行器或注册一个回调。

- **参数**: `std::coroutine_handle<> h`，这是指向当前协程的句柄，你可以通过它在未来恢复（`h.resume()`）或销毁（`h.destroy()`）协程。
- **返回值**:
  - `void`: 协程被挂起。恢复协程的责任完全交给了 `await_suspend` 的实现者（例如，在另一个线程中或当 I/O 完成时调用 `h.resume()`）。
  - `bool`:
    - 返回 `true`：确认挂起。协程将保持暂停状态。
    - 返回 `false`：**取消挂起**。协程会立即恢复执行，这相当于一次同步操作，但经历了完整的挂起检查流程。
  - `std::coroutine_handle<>`: 将执行权转移给另一个协程。运行时会立即恢复返回的那个协程。这是一种高级用法，用于实现协程间的调度。

```cpp
// 常见签名
void await_suspend(std::coroutine_handle<> h) noexcept;
```

**第 3 步: `await_resume()` —— 恢复后做什么？**

当协程恢复执行时（无论是从 `await_ready` 直接过来，还是在 `await_suspend` 挂起后被 `resume()`），`await_resume` 会被调用。

- **职责**:
  1. **返回结果**: `co_await` 表达式的最终值就是此函数的返回值。如果协程函数写的是 `auto result = co_await some_awaitable;`，那么 `result` 的值就来自 `await_resume`。
  2. **传递异常**: 如果异步操作失败，可以在 `await_resume` 中 `throw` 一个异常，该异常会被正在执行的协程捕获。

```cpp
// 返回类型 T 就是 co_await 表达式的结果类型
T await_resume();
```

**总结**：`co_await` 的行为可以看作是协程与 Awaiter 之间的一次交互：协程询问"你准备好了吗？"(`await_ready`)，如果没好，协程就说"那你去准备吧，好了叫我"并交出自己的联系方式(`await_suspend`)，最后当被叫醒时，协程会问"结果是什么？"(`await_resume`)。

---

### 99. quiz

#### 1. 什么是纤程？纤程和协程的区别是什么？

| 特性         | 协程（Coroutine） | 纤程（Fiber） |
| ------------ | ----------------- | ------------- |
| **平台支持** | 跨平台通用        | Windows 特定  |
| **实现方式** | 语言特性或库实现  | 操作系统 API  |
| **内存模型** | 共享进程内存空间  | 共享线程栈    |
| **调度方式** | 程序自主调度      | 依赖系统 API  |

#### 2. 按栈管理方式分类

| 类型           | 内存模型       | 优势                   | 劣势           |
| -------------- | -------------- | ---------------------- | -------------- |
| **有栈协程**   | 独立栈空间     | 实现简单，支持深层调用 | 内存开销大     |
| **共享栈协程** | 运行时栈共享   | 内存使用优化           | 切换时需要复制 |
| **无栈协程**   | 状态保存在堆中 | 内存效率最高           | 需要编译器支持 |

#### 3. 什么是有栈协程？什么是无栈协程？

C++20 协程是**无栈协程**的典型代表。它们不为每个协程分配独立的运行栈，而是将局部变量和状态保存在协程帧中，通常分配在堆上。这样可以节省内存，并允许协程在 suspension 后由任意线程恢复。

boost.coroutine2 则是**有栈协程**的例子。每个协程都有自己的栈空间，允许更自然的函数调用和递归，但会带来较大的内存开销。

有栈协程的实现通常涉及切换栈指针和保存/恢复寄存器状态，而无栈协程则依赖编译器生成的状态机来管理控制流和局部状态。

因此 C++ 无栈协程这一套需要编译器的支持，而有栈协程则可以通过库实现。

#### 4. 有栈协程的实现基础

- 函数怎么提前返回？
- 怎么手动模拟函数调用？
- 函数参数到底怎么传？
- 函数如何能跨调用层次返回？
- 怎么能获取 EIP/RIP？
- 函数怎么跳转到特定地址执行？
- 函数怎么保存上下文？
- 汇编中为什么要保存寄存器？
- 汇编中怎么恢复栈？
- 汇编怎么平衡栈？
- 什么是 `__stdcall` 和 `__cdecl`？
