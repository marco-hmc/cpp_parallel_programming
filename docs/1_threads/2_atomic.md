---
layout: post
title: （二）多线程那些事儿：原子类型数据和内存序
categories: C++
related_posts: True
tags: threads
toc:
  sidebar: right
---

[toc]

## （二）多线程那些事儿：原子类型数据和内存序

### 1. concepts

#### 1.1 怎么理解原子性

在并发编程中，**原子性 (Atomicity)** 指一个或一系列操作在执行过程中是不可分割的，要么全部执行成功，要么完全不执行，不会被任何其他线程中断。

这个概念最经典的例子是 `i++` 操作。在高级语言中它只是一行代码，但其底层执行通常包含三个独立的步骤：

1.  **读取 (Read)**：从内存中读取变量 `i` 的当前值到 CPU 寄存器。
2.  **修改 (Modify)**：在寄存器中将该值加 1。
3.  **写回 (Write)**：将寄存器中的新值写回内存中的变量 `i`。 p.s.: cpu 是不会直接操作内存的，都是先读到寄存器里操作，然后再写回内存。

在多线程环境下，如果两个线程同时对 `i` 执行 `i++`，它们可能都读取到相同的初始值，各自加 1 后写回，最终结果只增加了 1，而不是预期的 2。这就是因为 `i++` 这个“读取-修改-写回”的过程不是原子的，它在执行中途可能被其他线程的操作打断。

更进一步，即使是单纯的“读取”或“写入”操作，也未必是原子的。这通常发生在以下情况：

- **超大结构体**：对于非常大的数据结构，CPU 无法通过单条指令完成读写，其访问必然是分步的，因此不是原子的。

不过，现代处理器架构通常能保证对**原生对齐的基础类型**（如 `int`, `double`, 指针等）的单次读写是原子的。但我们编写可移植代码时不应依赖此项保证，而应使用 C++ 提供的原子工具。

总之，原子性确保一个操作在其他线程看来是“瞬间完成”的，不存在中间状态。

#### 1.2 `atomic`原子性实现？是否有锁？

简单来说，`std::atomic` 是一个高级抽象，它承诺提供原子性。它会尽力以最高效的无锁方式实现这一承诺，如果做不到，则用有锁方式作为保底方案，确保正确性始终得到满足。

1. **原子性如何实现：** 现代处理器大都提供特殊的原子指令集。例如，在 x86/x64 架构上，许多指令可以通过添加 `LOCK` 前缀来变为原子操作。这个前缀的作用并非总是锁住整个系统总线，更现代的做法是**锁住相关的缓存行 (Cache Locking)**。当一个核心执行带 `LOCK` 前缀的指令时，它会通过缓存一致性协议（如 MESI）确保其他核心不能同时访问该缓存行，直到操作完成。这种方式远比锁总线高效。其他架构（如 ARM）也有类似的机制（如 `LDREX`/`STREX` 指令对）。即可以认为原子性是通过硬件不给其他处理器核心访问相关内存区域来实现的。

2. **是否有锁？** `std::atomic` 的一个核心优势是它能够根据目标类型和平台能力，智能地选择实现方式：
   - **无锁实现**：如果 `std::atomic<T>` 的目标类型 `T` 是一个简单类型（如 `int`, `bool`, 指针等），且平台支持对该类型的原子指令，那么 `std::atomic` 的操作就会被编译为高效的、无锁的机器指令。你可以通过 `is_lock_free()` 方法在运行时检查一个原子对象是否为无锁的。
   - **有锁实现**：如果类型 `T` 过于复杂（例如，一个大的结构体），或者平台缺少相应的硬件支持，`std::atomic` 的实现会**自动回退 (fallback)** 到使用内部的互斥锁（Mutex）来保证原子性。在这种情况下，每次操作 `std::atomic` 对象就等同于加锁和解锁，虽然保证了线程安全，但性能开销会大得多。

通常，原子指令大多较为简单，本身就是单一的不可打断操作；而有锁操作往往需要通过锁住总线来实现。

#### 1.3 为何`atomic`禁止拷贝与复制

`std::atomic` 类型被设计为**禁止拷贝和移动**，这意味着你不能这样做：

```cpp
std::atomic<int> x(5);
std::atomic<int> y = x; // 编译错误！
void foo(std::atomic<int> z);
foo(x); // 编译错误！
```

这背后的原因至关重要，它体现了原子类型的核心语义：

1.  **唯一性与身份**：一个原子变量代表内存中一个**特定的、唯一的同步点**。如果允许拷贝，就会产生两个独立的 C++ 对象（例如 `x` 和 `y`）指向同一块内存。此时，对 `x` 的原子操作和对 `y` 的原子操作实际上是在竞争同一个地址，这会破坏 `std::atomic` 封装的抽象，让代码变得极难推理和维护。

2.  **避免非原子读写**：拷贝操作本身（`memcpy`）通常不是原子的。如果允许 `y = x;`，这个赋值过程可能会被撕裂，即在拷贝过程中，另一个线程修改了 `x` 的值，导致 `y` 得到一个“新旧混合”的垃圾值。这违背了原子性的基本原则。

3.  **明确所有权**：禁止拷贝和移动强制要求原子变量的生命周期和所有权是明确的。它要么是全局/静态变量，要么是某个类的成员，你只能通过引用或指针来共享它，而不能创建它的副本。

简单来说，可以把 `std::atomic<T>` 看作是一个**同步工具**，而不是一个普通的值。就像你不能随意拷贝一个 `std::mutex` 一样，你也不能拷贝一个 `std::atomic`。

#### 1.4 atomic 类型能有什么成员函数？怎么用？

| 操作               | 对应函数（接口）                                                                                             | 对应操作符                                                  |
| ------------------ | ------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------- |
| **读取**           | `T load(memory_order order = memory_order_seq_cst) const noexcept;`                                          | 转换操作符：`operator T() noexcept;`                        |
| **存储**           | `void store(T desired, memory_order order = memory_order_seq_cst) noexcept;`                                 | 赋值操作符：`operator=(T desired) noexcept;`                |
| **加法/自增**      | `T fetch_add(T arg, memory_order order = memory_order_seq_cst) noexcept;`                                    | `operator+=(T arg) noexcept;`<br>后置自增 `operator++(int)` |
| **减法/自减**      | `T fetch_sub(T arg, memory_order order = memory_order_seq_cst) noexcept;`                                    | `operator-=(T arg) noexcept;`<br>后置自减 `operator--(int)` |
| **按位与**         | `T fetch_and(T arg, memory_order order = memory_order_seq_cst) noexcept;`                                    | `operator&=(T arg) noexcept;`                               |
| **按位或**         | `T fetch_or(T arg, memory_order order = memory_order_seq_cst) noexcept;`                                     | `operator\|=(T arg) noexcept;`                              |
| **按位异或**       | `T fetch_xor(T arg, memory_order order = memory_order_seq_cst) noexcept;`                                    | `operator^=(T arg) noexcept;`                               |
| **交换**           | `T exchange(T desired, memory_order order = memory_order_seq_cst) noexcept;`                                 | ——（无对应操作符）                                          |
| **比较交换（弱）** | `bool compare_exchange_weak(T& expected, T desired, memory_order success, memory_order failure) noexcept;`   | ——（无对应操作符）                                          |
| **比较交换（强）** | `bool compare_exchange_strong(T& expected, T desired, memory_order success, memory_order failure) noexcept;` | ——（无对应操作符）                                          |

- **为什么同时提供函数接口，和操作符接口？** 操作符（如赋值和类型转换）默认使用严格的内存序`memory_order_seq_cst`），而函数版本允许开发者根据具体场景指定其他内存序（例如 relaxed、acquire/release），以便进行更细粒度的性能调优。

#### 1.5 自定义的 atomic 有什么限制？

- **基础要求** 要让自定义类型`T`能使用`std::atomic<T>`，该类型必须满足**Trivially Copyable**条件，具体包含以下几点：

  1. **拷贝语义方面**：得有平凡的拷贝构造函数与赋值运算符。这意味着不能有用户自定义的拷贝或移动操作。
  2. **析构函数方面**：析构函数必须是平凡的，不能包含任何自定义操作。
  3. **基类与成员方面**：所有基类和非静态数据成员也都要是 Trivially Copyable 类型。

- **尺寸与对齐限制**
  - **是否无锁**：`std::atomic<T>::is_always_lock_free` 是一个编译期常量布尔值，表示在该实现下此类型的原子操作是否“始终无锁”。也可通过对象上的 `is_lock_free()` 在运行期查询。标准并不要求一定无锁；当无法无锁时，库可退化为内部加锁实现。
  - **常见情况**：主流平台对 1/2/4/8 字节整型与指针往往是无锁的，16 字节在部分架构也支持；超过平台能力时一般不是无锁实现。
  - **对齐**：若 T 的自然对齐不足以支持无锁原子，可使用 `alignas` 强化对齐；即便对齐不足，`atomic<T>` 仍可用，但实现可能退化为加锁。

### 2. 内存序

#### 2.1 什么是指令乱序？

内存序的本质是对编译器和 CPU 在优化或执行时可能出现的指令重排施加约束，从而在多线程环境中建立可推理的可见性与顺序关系。重排是为了性能（如指令并行、流水线和缓存优化），在单线程语境下通常无害，但在多线程场景下会改变不同线程之间观察到的事件顺序，导致同步错误。

在单线程场景下，C++作为编译型语言，能够充分利用程序上下文信息生成正确代码。例如`int a = 0; a = a + 1;`，编译器基于完整代码，必定能保证先定义后操作的顺序。而对于`a = a + 1; b = true;`这类情况，编译器无法确定`a`与`b`的计算先后顺序，但在单线程环境中，这种不确定性并无影响。

然而，在多线程场景下，指令顺序变得至关重要。例如，一个线程执行`a = a + 1; b = true;`，另一个线程执行`while(!b){}; print(a);`，即等待`b`为真时输出`a`的值。此时，第一个线程`a`和`b`的计算顺序将直接影响第二个线程的执行结果。为解决多线程环境下的指令乱序问题，主要有以下两种思路：

- **上锁机制**：对执行`a = a + 1; b = true;`的代码块以及`while(!b){}; print(a);`的代码块分别上锁，确保这两个操作不能同时进行，然后通过`std::condition_variable`来协调执行先后顺序，从而避免问题。
- **限制乱序行为**：允许两个操作同时进行，但保证特定的执行顺序，如确保先执行`a = a + 1;`，再执行`b = true`，以此保证程序正确性。

对于第一种思路，此处不再赘述。而限制乱序行为主要通过两种方式实现：

- **内存屏障**：以`AB|CD`为例，内存屏障可保证`AB`操作在`CD`之前执行，但不保证`AB`内部以及`CD`内部的执行顺序。
- **内存序**：通过指定某个指令必须在特定操作之前或之后执行来保证顺序。例如在上述例子中，通过指定内存序，使得`b = true`这一写操作，在`a = a + 1`之后执行，具体来说是保证`b`的赋值操作是作用域内（`{}`或者函数内）最后一条写操作，这样就能保证多线程环境下的执行顺序正确。

一句话，指令乱序是 CPU 层面、编译器层面的，是开发不可控的，可假定CPU和编译器一定会积极使用指令乱序，只是单线程乱序时，编译器能保证安全的；多线程乱序就不一定安全了，需要开发者通过内存序来保证。

#### 2.2 谁会对指令重排？

1. **编译器重排**：

编译器为优化程序性能，在生成机器代码时可能调整指令顺序。例如，对于如下 C++代码：

```cpp
int a = 5;
int b = 3;
int c = a + b;
```

如果编译器分析发现`a = 5`和`b = 3`这两条指令不存在数据依赖关系，为提高执行效率，可能会交换它们的顺序。或者，对于条件语句中的指令，若符合优化条件，编译器可能将某条指令移至条件分支外部。编译器重排通常是安全的，因为它会保证在单线程环境中，重排后的程序行为与原程序一致。这是编译器基于对代码整体逻辑和性能优化的考量，在不改变单线程语义的前提下，对指令顺序进行的调整，以期望提升程序在目标机器上的执行效率。

2. **CPU 重排**：

现代 CPU 为进一步提高执行效率，在执行指令时也可能改变指令顺序。例如，假设存在一条指令需要从内存中读取数据，但由于内存访问延迟，该指令暂时无法执行。此时，CPU 可能会先执行后续不依赖该数据的指令。从汇编层面看，若有指令序列`LOAD A, [mem]`（从内存地址`mem`加载数据到寄存器`A`）、`ADD B, A`（将寄存器`A`与`B`相加）、`SUB C, D`（寄存器`C`减去`D`），当`LOAD A, [mem]`因内存延迟等待时，CPU 可能会先执行`SUB C, D`。CPU 还可能通过并行执行多个指令来提升整体处理速度。与编译器重排类似，这种在 CPU 层面的重排一般也是安全的，因为 CPU 会确保在单核环境下，重排后的程序执行结果与原程序保持一致。CPU 重排是利用硬件特性，在不影响单核执行逻辑正确性的基础上，对指令执行顺序进行动态调整，从而充分发挥硬件资源的效能。

需要注意的是，在多线程环境下，无论是编译器重排还是 CPU 重排，都可能引发数据竞争和同步问题，导致程序出现非预期的行为。这是因为不同线程对共享数据的访问顺序可能因重排而改变，从而破坏程序的正确性。

#### 2.3 乱序行为例子

```c++
#include <atomic>
#include <iostream>
#include <thread>

bool ready = false;
int data = 0;

void producer() {
    data++;
    ready = true
}

void consumer() {
    while (!ready) {
    }
    std::cout << "Data: " << data << std::endl;
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

上述代码对应的伪汇编代码：

```
wait(ready): 如果 ready 条件满足，则继续执行；否则，让出 CPU。

| producer A        | producer B        |
| ---------------   | ---------------   |
| move reg data     | ready             |
| add reg 1         | ...               |
| move data reg     | move reg data     |
| ...               | add reg 1         |
| ready             | move data reg     |
| ...               | ...               |
| ...               | ...               |

| consumer          |
| ---------------   |
| wait(ready)       |
| print data        |
| ...               |
| ...               |
```

正如前面提到，指令乱序行为可以假定是会发生，且只能再单线程的上下文中保证正确性。因此`void producer()`的函数，是可能被译成`producer A`和`producer B`这两种情况的。这个时候哪怕保证`data++`是原子的，即`move reg data; add reg 1; move data reg;`这三条指令不会被打断，也是没有意义的。因为`ready = true;`这条指令可能会被重排到`data++`之前执行（即`producer B`的情况）。这样的话，`consumer`线程在执行`while(!ready){}`时，可能会立刻跳出循环，然后执行`print data;`，而此时`data`还没有被自增，输出的结果就是 0。

这个时候最常用的写法就是（最常用，简洁且高效）：

```c++
std::atomic<bool> ready{false};
int data = 0;

// Producer
void producer() {
    data++;
    ready.store(true, std::memory_order_release); // 发布：确保 前面 写对其他线程可见
}

// Consumer
void consumer() {
    while (!ready.load(std::memory_order_acquire)) { /* 等待 */ } // 获取：看到 release 之前的写
    std::cout << "Data: " << data << '\n'; // 保证能看到 data++ 的结果
}
```

除此之外，还有多种内存序的组合方式，来限制重排，但是更复杂一些。

#### 2.4 内存序有哪些？

1. **`memory_order_relaxed`（宽松内存序）**：

   - 此内存序仅确保原子操作自身具备原子性，不提供任何关于同步或顺序方面的保证。这意味着在多线程环境下，该原子操作与其他线程中的操作之间不存在特定的顺序关系。
   - 这种内存序适用于类似计数器这样的场景，在这些场景中，并不需要进行跨线程的同步操作，仅关注对计数器值的原子更新。例如，在单线程环境下对某个计数器进行频繁自增操作，使用 `memory_order_relaxed` 既能保证自增操作的原子性，又能获得较好的性能，因为无需额外的同步开销。

2. **`memory_order_consume`（消费内存序）**：

   - 该内存序保证当前线程中，依赖于该原子变量的后续操作不会被重排到该原子操作之前。与 `memory_order_acquire` 相比，`memory_order_consume` 的限制更窄，它仅对那些实际依赖于该原子变量值的操作起作用，而 `memory_order_acquire` 则保证所有后续操作不会被重排到该原子操作之前。
   - 例如，当一个线程读取一个原子变量，并且后续操作是基于这个读取的值进行的（如根据读取的标志位决定是否执行某个函数），使用 `memory_order_consume` 可以确保这些依赖操作不会被提前到读取操作之前执行。不过，使用 `memory_order_consume` 需要更谨慎，因为它要求对依赖关系有清晰的界定，否则可能导致难以察觉的错误。

3. **`memory_order_acquire`（获取内存序）与 `memory_order_release`（释放内存序）**：

   - **`memory_order_acquire`**：在当前线程中，此内存序保证所有后续的内存操作不会被重排到该获取操作之前。也就是说，一旦执行了带有 `memory_order_acquire` 的原子操作，后续对内存的访问（读或写）在执行顺序上会遵循该原子操作之后的顺序，不会提前到该原子操作之前执行。
   - **`memory_order_release`**：与之相对，在当前线程内，`memory_order_release` 确保所有之前的内存操作不会被重排到该释放操作之后。即一旦执行了带有 `memory_order_release` 的原子操作，之前对内存的访问（读或写）在执行顺序上会保持在该原子操作之前，不会推迟到该原子操作之后执行。
   - 这两种内存序常用于锁的获取与释放场景。例如，当一个线程获取锁（类似 `memory_order_acquire` 的语义）后，能确保后续对共享资源的访问是在获取锁之后进行的；而当一个线程释放锁（类似 `memory_order_release` 的语义）前，能保证对共享资源的修改都已完成，不会被重排到释放锁之后，从而确保其他获取锁的线程能看到正确的共享资源状态。

4. **`memory_order_acq_rel`（获取 - 释放内存序）**：

   - 这种内存序同时具备 `memory_order_acquire` 和 `memory_order_release` 的语义。也就是说，对于执行此内存序的原子操作，在当前线程中，既保证后续的内存操作不会被重排到此操作之前，又保证之前的内存操作不会被重排到此操作之后。
   - 它适用于读 - 修改 - 写这类操作场景。例如，在多线程环境下对共享变量进行先读取值，然后根据读取的值进行修改，最后再写回的操作，使用 `memory_order_acq_rel` 能确保该操作在内存访问顺序上的正确性，同时兼顾获取和释放内存序的语义要求。

5. **`memory_order_seq_cst`（顺序一致性内存序）**：
   - 该内存序提供了全局的顺序一致性保证，即所有线程看到的内存操作顺序都是一致的。这是一种非常严格的内存序，它确保了所有线程对内存的访问都按照一个全局统一的顺序进行，就好像所有线程的操作都是顺序执行的一样。
   - 然而，由于这种严格的顺序保证需要额外的同步开销，所以 `memory_order_seq_cst` 通常是最强的内存序，但也可能对程序性能产生一定影响。在对性能要求极高且对操作顺序一致性要求不那么严格的场景下，可能需要权衡是否使用该内存序。

- `memory_order_consume` 实际实现上长期存在分歧，主流编译器一般将其等同于 `acquire` 处理，标准也将其处于“暂不推荐使用”的状态。工程中建议直接使用 `acquire`/`release`。

- **不同操作支持什么内存序？**

  1. **Store 操作（存储操作，即写操作）**：

     - `memory_order_relaxed`
     - `memory_order_release`
     - `memory_order_seq_cst`

  2. **Load 操作（加载操作，即读操作）**：

     - `memory_order_relaxed`
     - `memory_order_consume`
     - `memory_order_acquire`
     - `memory_order_seq_cst`

  3. **Read - modify - write（读 - 改 - 写）操作**：

     - `memory_order_relaxed`
     - `memory_order_consume`
     - `memory_order_acquire`
     - `memory_order_release`
     - `memory_order_acq_rel`
     - `memory_order_seq_cst`

  所有操作的默认内存序为 `memory_order_seq_cst`。这意味着在未显式指定内存序时，编译器会按照最严格的顺序一致性内存序来处理原子操作，以确保程序在多线程环境下的正确性，但可能会牺牲一定的性能。在实际编程中，开发者可根据具体需求，在保证程序正确性的前提下，选择更合适的内存序来优化性能。

#### 2.5 六种内存序怎么用？

以下给出常见范式，按需取用：

1. 发布-订阅（Publish/Subscribe）

- 需求：线程 A 写入一批数据后，设置标志；线程 B 看到标志后读取数据。
- 做法：A 在写完数据后 `flag.store(true, release)`；B 使用 `while(!flag.load(acquire)) {}`，随后读取数据。

2. 计数器/统计量（无需跨线程顺序）

- 需求：只需原子性，不关心与其他操作的先后关系。
- 做法：`counter.fetch_add(1, relaxed)`；读取用 `relaxed` 或默认均可。

3. 读-改-写（RMW）依赖值的后续逻辑

- 需求：RMW 的结果影响后续读取/写入。
- 做法：对 `fetch_add/exchange` 等使用 `memory_order_acq_rel`（或成功 `acq_rel`）。

4. CAS 循环

- 典型写法：

```c++
T expected = old;
while (!atom.compare_exchange_weak(expected, desired,
                                   std::memory_order_acq_rel,
                                   std::memory_order_acquire)) {
    // expected 已被更新为当前值，按需调整 desired 并重试
}
```

- 说明：成功使用 `acq_rel`，失败侧通常用 `acquire`（或 `relaxed` 若失败路径不依赖读取到的值）。

5. 全局统一顺序

- 需求：便于推理，所有原子操作看到相同的全序。
- 做法：`seq_cst`。简单但可能性能最弱。

6. 栅栏（fence）桥接

- 需求：将一批普通写与某个原子事件建立顺序。
- 做法：写完非原子字段 →`atomic_thread_fence(release)`→ 发布原子标志（可 `relaxed`）；消费侧先获取标志（`acquire`）→`atomic_thread_fence(acquire)`→ 读取非原子字段。

### 3. 原子幻觉

#### 3.1 什么是原子幻觉？

```c++
#include <atomic>
#include <climits>
#include <iostream>
#include <thread>
#include <vector>

class MinMaxTracker {
  private:
    std::atomic<int> min_{INT_MAX};

  public:
    void updateMinValueIfSmaller(int value) {
        if (value < min_.load()) {
            min_.store(value);
        }
    }

    int get_min() const { return min_.load(); }
};

void worker(MinMaxTracker& tracker, int id) {
    for (int i = 1000; i > 0; --i) {
        tracker.updateMinValueIfSmaller(id * 1000 + i);
    }
}

int main() {
    MinMaxTracker tracker;
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(worker, std::ref(tracker), i);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << "Min value: " << tracker.get_min() << std::endl;
    return 0;
}
```

这段代码旨在找出最小值，然而，它存在问题。那么，问题出在哪里呢？其实，这属于典型的原子幻觉问题。

```c++
    if (value < min_.load()) {
        min_.store(value);
    }
```

在多线程环境下，当`value < min_.load()`这个条件为真时，由于其他线程也可能同时在执行`updateMinValueIfSmaller`函数，竞争对`min_`的操作，在执行到下一行`min_.store(value)`之前，`min_`的值极有可能已被其他线程改变。此时，再用`value`去覆盖`min_`的值就可能导致错误结果。原子操作保证的是单个操作不可被打断，但多个原子操作组合起来的时候，就不再具有原子性。这种因疏忽而误以为组合的原子操作仍具备原子特性的错觉，就被称为原子幻觉。

为避免这种原子幻觉带来的问题，可以使用互斥锁来保证`updateMinValueIfSmaller`函数中操作的原子性。例如，在`MinMaxTracker`类中添加一个`std::mutex`成员变量，在`updateMinValueIfSmaller`函数中先锁定互斥锁，完成比较和存储操作后再解锁。或者，也可以使用`std::atomic`提供的`compare_exchange_weak`或`compare_exchange_strong`方法，通过比较并交换操作，以原子方式完成整个更新过程，确保结果的正确性。

#### 3.2 CAS 技术

通过前面的示例，我们已了解到具备原子性的变量可用于同步，发挥类似 `lock()/unlock()` 对的功能。而在实现无锁编程时，一种常见的借助原子变量达成此目的的技巧便是 CAS。

CAS，即 Compare-And-Swap（比较并交换），是一项广泛应用的原子操作，常用于实现无锁编程。它依赖硬件支持的原子指令，以此保障多线程环境下的操作安全性。

以下为一个简单的 CAS 操作示例代码：

```c++
bool CAS(int* addr, int expected, int desired) {
    if (*addr == expected) {
        *addr = desired;
        return true;
    }
    return false;
}
```

在标准库中，上述 CAS 操作被封装成了以下两种形式：

| 操作               | 对应函数（接口）                                                                                             | 对应操作符         |
| ------------------ | ------------------------------------------------------------------------------------------------------------ | ------------------ |
| **比较交换（弱）** | `bool compare_exchange_weak(T& expected, T desired, memory_order success, memory_order failure) noexcept;`   | ——（无对应操作符） |
| **比较交换（强）** | `bool compare_exchange_strong(T& expected, T desired, memory_order success, memory_order failure) noexcept;` | ——（无对应操作符） |

**参数类型分析**： 在 `compare_exchange_weak()` 函数中，第一个参数 `expected` 采用引用类型，它实际上等价于前面自定义 `CAS(int* addr, int expect, int desired)` 函数中的 `expected` 参数，用于表示我们期望目标地址处的值。而 `compare_exchange_weak()` 函数中的 `desired` 参数同样对应自定义 `CAS` 函数中的 `desired` 参数，即我们想要替换成的新值。此外，`compare_exchange_weak()` 函数后面的 `success` 和 `failure` 参数均为内存序相关参数，分别用于指定当比较交换操作成功和失败时所采用的内存序。

**compare_exchange_weak()（弱版本）**：

- **允许伪失败**：此版本的一个重要特点是即使当前值与预期值相等，操作也有可能失败。这种失败并非由于数据本身的问题，而是源于硬件实现的一些限制。例如，在某些特定平台上，底层的 CAS 指令可能会偶尔出现失败情况。
- **适合循环使用**：鉴于可能出现伪失败，在实际应用中，`compare_exchange_weak()` 通常需要放在循环结构中反复尝试，直至操作成功。这样可以确保在遇到伪失败时，程序仍能继续尝试达成预期的比较交换操作。
- **性能更高**：在某些硬件环境下，`compare_exchange_weak` 的实现相比 `compare_exchange_strong` 更为高效。这是因为其实现可能利用了硬件的特定特性，以牺牲一定的操作确定性来换取更好的性能表现。
- 例如，对于 `compare_exchange_weak()` 函数，当目标变量的原始值与预期值一致时，存储操作仍有可能不成功。在这种情况下，变量的值不会发生改变，并且 `compare_exchange_weak()` 函数的返回值为 `false`。这种情况可能出现在缺少单条 CAS 操作（“比较 - 交换”指令）的机器上。当处理器无法保证该操作能够自动完成时，比如线程操作导致指令队列从中间关闭，且另一个线程安排的指令被操作系统替换（尤其在线程数多于处理器数量的情况下），就会出现这种所谓的“伪失败”（_spurious failure_）。其根本原因在于时间上的竞争条件，而非变量值本身的问题。

**compare_exchange_strong()（强版本）**： 与弱版本不同，`compare_exchange_strong()` 函数具有更强的确定性。只要实际值与期望值不相符，该函数就能保证返回 `false`，即不会出现伪失败的情况。这意味着在当前值等于预期值时，操作一定会成功完成比较交换。

**使用建议**： 综合来看，由于 `compare_exchange_weak()` 虽然可能出现伪失败，但在循环使用时能保证最终达成目标，且在某些硬件上具有性能优势，所以在很多场景下，使用 `compare_exchange_weak` 搭配循环是一种较为通用的做法。然而，具体的选择还需根据实际应用场景的需求和硬件环境等因素来综合考量。例如，在对操作确定性要求极高，性能要求相对较低的场景中，`compare_exchange_strong` 可能更为合适。

`compare_exchange_weak` (和 `strong`) 的接口设计非常精妙：

```cpp
bool compare_exchange_weak(T& expected, T desired, ...);
```

- **`expected` (输入/输出参数)**：这是一个**引用**参数，其行为是 CAS 的关键。

  - **调用前 (输入)**：你将你**期望**原子变量的当前值存入 `expected`。
  - **调用后 (输出)**：
    - 如果 CAS **成功**（原子变量的值确实等于 `expected`），函数返回 `true`，原子变量被更新为 `desired`。`expected` 的值保持不变。
    - 如果 CAS **失败**（原子变量的值不等于 `expected`），函数返回 `false`，原子变量的值**不会**改变。但最重要的是，**`expected` 参数会被自动更新为原子变量的当前实际值**。

- **`desired` (输入参数)**：你希望在 CAS 成功时将原子变量设置成的新值。

**为什么 `expected` 的自动更新如此重要？** 这个设计使得在循环中使用 CAS 变得极其方便。当一次尝试失败时，你无需再次手动 `load` 原子变量的最新值，`expected` 已经为你准备好了下一次循环所需要的新“期望值”。

**示例：无锁更新最小值**

```cpp
void update(int value) {
    int old_min = min_.load(); // 1. 读取当前最小值
    // 2. 循环尝试更新
    while (value < old_min && !min_.compare_exchange_weak(old_min, value)) {
        // 如果 CAS 失败：
        // - 意味着 min_ 的值在步骤1之后被其他线程改变了。
        // - old_min 会被自动更新为 min_ 的新当前值。
        // - 循环条件 `value < old_min` 会用这个新值重新判断是否需要继续尝试。
    }
}
```

#### 3.3 CAS 的 ABA 问题

一些场景中，即使出现 ABA 问题（值从 A 变为 B 再变回 A）也不会影响正确性，因为我们只关心值本身，而不关心它的变化过程。但在基于指针的无锁结构中，ABA 常会引发严重错误。

- 问题本质：CAS 仅比较“当前值是否等于预期值”，无法分辨期间是否经历过其他变更。
- 常见对策：
  - 版本标记/标记指针（tagged pointer）：将指针与版本计数打包到一个原子字中，每次成功更新版本递增。
  - Epoch/Hazard Pointers 等内存回收方案：避免节点被回收重用导致“回到旧值”的假象。
  - 时间戳/序列号：与版本号思路类似。

### 4. 内存屏障

内存屏障（也称为内存栅栏）是一种 CPU 指令，它确保屏障之前的所有操作完成后才执行屏障之后的操作。内存屏障可以防止编译器和处理器对指令重排序。

- Load Barrier：加载屏障，确保屏障前的读先于屏障后的读。
- Store Barrier：存储屏障，确保屏障前的写先于屏障后的写。
- Full Barrier：全屏障，同时约束读与写。

在 C++ 中，可用 `std::atomic_thread_fence(order)` 发出栅栏。但需注意：fence 与原子操作共同建立顺序，单独对非原子对象无效。通常优先用 `release/acquire` 的原子读写；仅在需要桥接非原子批量写入与一个原子事件时使用 fence 模式。

### 5. 总结

内存序的行为和硬件高度相关，我看过很多用无锁实现后面都被发现有安全问题，这些安全问题可能是某些特定平台，或者特定编译器编译才会产生的。特别是当任务耗时较长时，采用锁机制和无锁机制保障线程安全，二者在任务耗时上的差异或许仅有百分之一。

所以，我不是特别赞同在未进行性能测试、未有足够单元测试保证安全的情况下，就贸然使用`std::atomic`开展无锁编程。要是使用`mutex`（互斥锁）时出了错，多调试几次往往就能找出问题所在。然而无锁编程一旦出错，很可能是内存序设置不当所致，这类错误通常具有偶发性。而且，CPU 对内存序的排序行为影响显著，不同厂家生产的 CPU，其乱序行为很可能各不相同。我尝试采用宽松内存序，期待通过故意写错来获得一个失败结果，但根本无法复现。应该是编译器和平台侧做了优化，帮我自动改正了行为。不是说这种优化不好，只是这种优化可能会不利于安全性的保证，因为我不知道其他平台和其他编译器是否也对。

对于不可感知且被屏蔽的编译器层面和硬件层面，还是需要心存敬畏。

因此不要未经测试就盲目使用无锁编程。除非你对内存序和 CPU 重排序有深入理解，并且能确保代码在目标平台上稳定运行，否则还是老实用锁机制吧。

### 99. quiz

#### 1. 无锁编程的时候，无脑使用严格内存序可以吗？

使用严格内存序（`memory_order_seq_cst`）在无锁编程中确实可以保证正确性，因为它是默认且最强的内存序，提供了全局顺序一致性。但这并非没有代价：严格内存序会引入额外的同步开销，在某些平台上其性能开销可能与直接使用互斥锁相当甚至更大。因此，如果选择无锁编程，就应当根据场景选用合适的内存序（如 `acquire`/`release`），以充分发挥无锁的性能优势。否则，直接使用互斥锁是更简单、更安全的选择。

#### 2. 多核 CPU 的缓存及缓存一致性维护

在多核 CPU 环境下，每个核心都配备高速缓存以加速数据访问。但多个核心同时操作同一份数据时，易引发数据不一致问题。例如，CPU 核心 A 和 B 都缓存了变量`x`，若 A 修改`x`值，B 仍使用旧缓存值，就会出现数据不一致情况。为此，多核 CPU 采用特定机制来保障缓存一致性。而保障缓存一致性常见的协议就是`MESI`协议。

1. **MESI 协议概述**：MESI 协议是常用的缓存一致性协议，其名称源于四个状态：Modified（已修改）、Exclusive（独占）、Shared（共享）和 Invalid（无效），每个缓存行都具备其中一种状态来表明当前状况。其核心原理就一句话，**写入时，持有修改权的核心通知其他核心对应缓存行无效，确保各核心缓存与主内存数据一致。**
2. **MESI 协议原理**：
   - `Modified（已修改）`：此状态下缓存行数据已被修改，与主内存数据不一致，且仅有一个 CPU 核心可持有。
   - `Exclusive（独占）`：缓存行数据与主内存一致，且仅一个核心持有。
   - `Shared（共享）`：缓存行数据与主内存一致，多个核心可同时持有。
   - `Invalid（无效）`：该缓存行数据无效，不可使用。
3. **缓存一致性操作**：
   - `读取操作`：若 CPU 核心读取的缓存行状态为 Invalid，需从主内存或其他核心缓存获取最新数据。
   - `写入操作`：CPU 核心写入缓存行时，要通知其他核心将该缓存行状态设为 Invalid。

#### 3. 不同作用域下调用`notify` 的区别？

```c++
void producer() {
    {
        std::unique_lock<std::mutex> lock(mtx);
        data++;
        ready = true;
    }
    cv.notify_one();
}

void producer2() {
    {
        std::unique_lock<std::mutex> lock(mtx);
        data++;
        ready = true;
        cv.notify_one();
    }
}
```

如果生产者`notify`了，但是还没有让出锁；消费者被唤醒，但是却拿不到锁，这会不会有问题？会不会随后让出`cpu`，然后这个数据就一直没消费了？

一句话回答，表现都正确，即都是线程安全。但是前者效率更好一些。

因为在 C++ 的条件变量机制中，线程被唤醒后（无论是虚假唤醒，还是调用`notify`）必须先获取锁，才能检查条件。这一顺序由 `std::wait()` 的底层实现强制保证，确保线程安全。如果被唤醒，锁被占用了，则进入 `Ready` 队列等待锁，持续竞争 CPU，不主动让出资源；拿到锁之后，如果条件不满足，就会让出锁和 cpu 资源，重新回到`Block`状态。即这次 notify 不会被消费。
