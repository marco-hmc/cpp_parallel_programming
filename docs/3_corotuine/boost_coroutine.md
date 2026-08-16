---
layout: post
title: （一）协程那些事儿：Boost.Coroutine2
categories: C++
related_posts: True
tags: coroutine
toc:
  sidebar: right
---

[toc]

## （一）协程那些事儿：Boost.Coroutine2

### 0. concepts

#### 0.1 基本概念

- **`pull_type`**：消费端，从协程中"拉取"值。构造时传入一个接收 `push_type&` 的函数或 lambda，协程体在第一次 `sink(...)` 之前会运行。
- **`push_type`**：生产端，在协程体中通过 `sink(value)` 将值传回 `pull_type`，随后协程挂起直到被继续。
- **交互语义**：`sink(x)` 将值发给 `pull`，`pull.get()` 或 `operator()` 用于取值并驱动协程继续。

---

### 1. 基础用法

#### 1.1 最小生成器示例

```cpp
#include <boost/coroutine2/all.hpp>
#include <iostream>

void simple_generator_demo() {
    using coro_t = boost::coroutines2::coroutine<int>;
    coro_t::pull_type consumer([](coro_t::push_type& sink){
        for (int i = 0; i < 5; ++i) {
            sink(i); // 把 i 发给 consumer，然后协程暂停
        }
    });

    while (consumer) {
        int v = consumer.get();
        std::cout << "Got: " << v << '\n';
    }
}
```

说明：构造 `pull_type` 会启动协程直到第一个 `sink`，之后每次获取值都会在生产端和消费端之间切换栈并暂停/恢复。

---

### 2. 异步回调适配

#### 2.1 将异步回调适配为协程

Boost.Coroutine2 本身不负责异步 I/O，但可以把异步完成事件通过 `push_type::operator()` (即 `sink`) 发回到 `pull_type`，从而用同步风格的代码处理异步结果。

关键点：

- 启动协程时把 `push_type&` 的引用传入异步发起器；异步完成时在回调中调用 `sink(result)`。
- 必须保证 `pull_type` 对象在异步完成前仍然存活，否则调用 `sink` 是未定义行为。
- 常见做法：使用共享状态（例如 `std::shared_ptr` + 互斥/原子）或用同步原语（`condition_variable`、`future`）来稳健地管理生命周期。

#### 2.2 AsyncResult 封装（示例代码要点）

下面是一个常用的封装思路，用于把异步完成结果发送回协程：

```cpp
template<typename T>
class AsyncResult {
  private:
    using coro_t = boost::coroutines2::coroutine<T>;
    using push_t = typename coro_t::push_type;
    push_t* sink_ = nullptr;          // 异步完成时用来发送结果
    bool completed_ = false;
    T result_;
  public:
    void set_sink(push_t* s) { sink_ = s; }
    void complete(T r) {
        result_ = std::move(r);
        completed_ = true;
        if (sink_) (*sink_)(result_);
    }
    bool is_completed() const { return completed_; }
    const T& get_result() const { return result_; }
};
```

用法：在 `pull_type` 的构造 lambda 中，把 `push_type&` 注册给 `AsyncResult`；异步操作完成后调用 `complete`，这将触发 `sink(result)`，使 `pull` 收到结果。

#### 2.3 异步文件/HTTP 示例（基于线程模拟）

示例结构参考仓库中的 `boost_coroutineUsage.cpp`：

- `AsyncFileReader::async_read_file(push_type& sink, const std::string& filename)`：在后台线程休眠模拟 I/O，完成时 `sink(content)`。
- `AsyncHttpClient::async_get(push_type& sink, const std::string& url)`：在后台线程模拟网络延迟并 `sink(response)`。
- `async_pipeline_demo()`：通过构造 `pull_type` 调用异步 API，随后 `pull.get()` 获得结果并继续处理。

简化示例（伪代码）：

```cpp
// pull 侧
boost::coroutines2::coroutine<std::string>::pull_type file_reader(
    [](auto& sink){
        AsyncFileReader::async_read_file(sink, "example.txt");
    }
);

if (file_reader) {
    std::string content = file_reader.get();
    // 使用 content
}
```

注意：后台线程在调用 `sink(...)` 时，`pull_type` 必须仍然有效；否则会触发未定义行为。

---

### 3. 生命周期与线程安全

#### 3.1 核心注意事项

- 切勿在异步回调中调用已销毁的 `push_type`。解决办法：
  - 使用共享状态（`std::shared_ptr`）来延长相关数据生命周期；
  - 在 `pull_type` 析构时注销回调或设置标志，避免在回调中访问已释放资源。
- 如果 `sink` 可能跨线程调用，请确保你的同步策略是安全的（互斥、原子或设计为单线程调用）。
- 对于复杂的并发场景，考虑用 `std::promise`/`std::future` 或更高级的事件循环来管理同步，而不是直接跨线程调用 `sink`。

---

### 4. 常见陷阱与建议

- 不要在异步回调中直接访问局部对象或栈上资源；使用共享或拷贝的值传递到回调中。
- 对于需要跨线程或跨组件的异步协调，优先设计明确的生命周期管理（注册/注销/引用计数）。
- 在新项目中，如无特殊栈语义需求，优先考虑使用 C++20 协程以获得更好语言级支持与生态整合。

---

### 99. quiz

#### 1. Boost.Coroutine2 与 C++20 协程的定位差异？

Boost.Coroutine2 是**有栈协程**，每个协程拥有独立栈空间，允许自然的深层调用和递归，但内存开销较大。C++20 协程是**无栈协程**，状态保存在堆上的协程帧中，内存效率更高且可由任意线程恢复，但需要编译器支持。选型时：需要深层递归或已有 boost 依赖的项目用 Boost.Coroutine2；新项目优先考虑 C++20 协程。
