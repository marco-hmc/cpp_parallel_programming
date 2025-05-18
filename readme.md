## parallel bench and guideline

- 多线程怎么用？

  - 怎么创建多线程？
  - 多线程常见的性能开销是多少？

- 怎么使用多线程才是对的？

  - 如果说单核耗时是单位`1`，n 核使用多线程期望耗时应该是`1/n`，但实际缺差很远，是什么原因限制了？
  - 如果取加速比定义为：原来耗时/现在耗时，如何尽可能提升多核加速比，使之接近`n`？
  - 多线程有什么常见陷阱？

- 线程池和并行库的区别是什么？
  - 并行库的同步手段和标准库的会兼容吗？
  - 什么时候用并行库，什么时候用线程池？

### quiz

1. 线程的异常怎么处理？
2. 线程提前终止实现怎么做比较好？
3. 嵌套并行怎么处理？
4. **避免在持有锁时调用用户提供的代码**
5. 读写锁

# 8.2 影响并发代码性能的因素

你可能会想，这种情况不会发生在你身上；因为，你没有使用任何循环。你确定吗？那么互斥锁呢？如果你需要在循环中放置一个互斥量，那么你的代码就和之前从数据访问的差不多了。为了锁住互斥量，另一个线程必须将数据进行转移，就能弥补处理器的互斥性，并且对数据进行修改。当这个过程完成时，将会再次对互斥量进行修改，并对线程进行解锁，之后互斥数据将会传递到下一个需要互斥量的线程上去。转移时间，就是第二个线程等待第一个线程释放互斥量的时间：

```
std::mutex m;
my_data data;
void processing_loop_with_mutex()
{
  while(true)
  {
    std::lock_guard<std::mutex> lk(m);
    if(done_processing(data)) break;
  }
}
```

接下来看看最糟糕的部分：数据和互斥量已经准备好让多个线程进访问之后，当系统中的核心数和处理器数量增加时，很可能看到高竞争，以及一个处理器等待其他处理器的情况。如果在多线程情况下，能更快的对同样级别的数据进行处理，线程就会对数据和互斥量进行竞争。这里有很多这样的情况，很多线程会同时尝试对互斥量进行获取，或者同时访问变量，等等。

互斥量的竞争通常不同于原子操作的竞争，最简单的原因是，互斥量通常使用操作系统级别的序列化线程，而非处理器级别的。如果有足够的线程去执行任务，当有线程在等待互斥量时，操作系统会安排其他线程来执行任务，而处理器只会在其他线程运行在目标处理器上时，让该处理器停止工作。不过，对互斥量的竞争，将会影响这些线程的性能；毕竟，只能让一个线程在同一时间运行。

回顾第 3 章，一个很少更新的数据结构可以被一个“单作者，多读者”互斥量(详见 3.3.2)。乒乓缓存效应可以抵消互斥所带来的收益(工作量不利时)，因为所有线程访问数据(即使是读者线程)都会对互斥量进行修改。随着处理器对数据的访问次数增加，对于互斥量的竞争就会增加，并且持有互斥量的缓存行将会在核芯中进行转移，因此会增加不良的锁获取和释放次数。有一些方法可以改善这个问题，其本质就是让互斥量对多行缓存进行保护，不过这样的互斥量需要自己去实现。

如果乒乓缓存是一个糟糕的现象，那么该怎么避免它呢？在本章后面，答案会与提高并发潜能的指导意见相结合：减少两个线程对同一个内存位置的竞争。

虽然，要实现起来并不简单。即使给定内存位置被一个线程所访问，可能还是会有乒乓缓存的存在,是因为另一种叫做*伪共享*(false sharing)的效应。

## 8.2.3 伪共享

处理器缓存通常不会用来处理在单个存储位置，但其会用来处理称为*缓存行*(cache lines)的内存块。内存块通常大小为 32 或 64 字节，实际大小需要由正在使用着的处理器模型来决定。因为硬件缓存进处理缓存行大小的内存块，较小的数据项就在同一内存行的相邻内存位置上。有时，这样的设定还是挺不错：当线程访问的一组数据是在同一数据行中，对于应用的性能来说就要好于向多个缓存行进行传播。不过，当在同一缓存行存储的是无关数据，且需要被不同线程访问，这就会造成性能问题。

假设你有一个 int 类型的数组，并且有一组线程可以访问数组中的元素，且对数组的访问很频繁(包括更新)。通常 int 类型的大小要小于一个缓存行，同一个缓存行中可以存储多个数据项。因此，即使每个线程都能对数据中的成员进行访问，硬件缓存还是会产生乒乓缓存。每当线程访问 0 号数据项，并对其值进行更新时，缓存行的所有权就需要转移给执行该线程的处理器，这仅是为了让更新 1 号数据项的线程获取 1 号线程的所有权。缓存行是共享的(即使没有数据存在)，因此使用*伪共享*来称呼这种方式。这个问题的解决办法就是对数据进行构造，让同一线程访问的数据项存在临近的内存中(就像是放在同一缓存行中)，这样那些能被独立线程访问的数据将分布在相距很远的地方，并且可能是存储在不同的缓存行中。在本章接下来的内容中看到，这种思路对代码和数据设计的影响。

如果多线程访问同一内存行是一种糟糕的情况，那么在单线程下的内存布局将会如何带来哪些影响呢？

## 8.2.4 如何让数据紧凑？

伪共享发生的原因：某个线程所要访问的数据过于接近另一线程的数据，另一个是与数据布局相关的陷阱会直接影响单线程的性能。问题在于数据过于接近：当数据能被单线程访问时，那么数据就已经在内存中展开，就像是分布在不同的缓存行上。另一方面，当内存中有能被单线程访问紧凑的数据时，就如同数据分布在同一缓存行上。因此，当数据已传播，那么将会有更多的缓存行将会从处理器的缓存上加载数据，这会增加访问内存的延迟，以及降低数据的系能(与紧凑的数据存储地址相比较)。

同样的，如果数据已传播，在给定缓存行上就即包含于当前线程有关和无关的数据。在极端情况下，当有更多的数据存在于缓存中，你会对数据投以更多的关注，而非这些数据去做了什么。这就会浪费宝贵的缓存空间，增加处理器缓存缺失的情况，即使这个数据项曾经在缓存中存在过，还需要从主存中添加对应数据项到缓存中，因为在缓存中其位置已经被其他数据所占有。

现在，对于单线程代码来说就很关键了，何至于此呢？原因就是*任务切换*(task switching)。如果系统中的线程数量要比核芯多，每个核上都要运行多个线程。这就会增加缓存的压力，为了避免伪共享，努力让不同线程访问不同缓存行。因此，当处理器切换线程的时候，就要对不同内存行上的数据进行重新加载(当不同线程使用的数据跨越了多个缓存行时)，而非对缓存中的数据保持原样(当线程中的数据都在同一缓存行时)。

如果线程数量多于内核或处理器数量，操作系统可能也会选择将一个线程安排给这个核芯一段时间，之后再安排给另一个核芯一段时间。因此就需要将缓存行从一个内核上，转移到另一个内核上；这样的话，就需要转移很多缓存行，也就意味着要耗费很多时间。虽然，操作系统通常避免这样的情况发生，不过当其发生的时候，对性能就会有很大的影响。

当有超级多的线程准备运行时(非等待状态)，任务切换问题就会频繁发生。这个问题我们之前也接触过：超额认购。

## 8.2.5 超额认购和频繁的任务切换

多线程系统中，通常线程的数量要多于处理的数量。不过，线程经常会花费时间来等待外部 I/O 完成，或被互斥量阻塞，或等待条件变量，等等；所以等待不是问题。应用使用额外的线程来完成有用的工作，而非让线程在处理器处以闲置状态时继续等待。

这也并非长久之计，如果有很多额外线程，就会有很多线程准备执行，而且数量远远大于可用处理器的数量，不过操作系统就会忙于在任务间切换，以确保每个任务都有时间运行。如第 1 章所见，这将增加切换任务的时间开销，和缓存问题造成同一结果。当无限制的产生新线程，超额认购就会加剧，如第 4 章的递归快速排序那样；或者在通过任务类型对任务进行划分的时候，线程数量大于处理器数量，这里对性能影响的主要来源是 CPU 的能力，而非 I/O。

如果只是简单的通过数据划分生成多个线程，那可以限定工作线程的数量，如 8.1.2 节中那样。如果超额认购是对工作的天然划分而产生，那么不同的划分方式对这种问题就没有太多益处了。之前的情况是，需要选择一个合适的划分方案，可能需要对目标平台有着更加详细的了解，不过这也只限于性能已经无法接受，或是某种划分方式已经无法提高性能的时候。

其他因素也会影响多线程代码的性能。即使 CPU 类型和时钟周期相同，乒乓缓存的开销可以让程序在两个单核处理器和在一个双核处理器上，产生巨大的性能差，不过这只是那些对性能影响可见的因素。接下来，让我们看一下这些因素如何影响代码与数据结构的设计。

# 9.2 中断线程

很多情况下，使用信号来终止一个长时间运行的线程是合理的。这种线程的存在，可能是因为工作线程所在的线程池被销毁，或是用户显式的取消了这个任务，亦或其他各种原因。不管是什么原因，原理都一样：需要使用信号来让未结束线程停止运行。这里需要一种合适的方式让线程主动的停下来，而非让线程戛然而止。

你可能会给每种情况制定一个独立的机制，这样做的意义不大。不仅因为用统一的机制会更容易在之后的场景中实现，而且写出来的中断代码不用担心在哪里使用。C++11 标准没有提供这样的机制，不过实现这样的机制也并不困难。

在了解一下应该如何实现这种机制前，先来了解一下启动和中断线程的接口。

## 9.2.1 启动和中断线程

先看一下外部接口，需要从可中断线程上获取些什么？最起码需要和`std::thread`相同的接口，还要多加一个 interrupt()函数：

````c++
class interruptible_thread
{
public:
  template<typename FunctionType>
  interruptible_thread(FunctionType f);
  void join();
  void detach();
  bool joinable() const;
  void interrupt();
};
```c++

类内部可以使用`std::thread`来管理线程，并且使用一些自定义数据结构来处理中断。现在，从线程的角度能看到什么呢？“能用这个类来中断线程”——需要一个断点(*interruption point*)。在不添加多余的数据的前提下，为了使断点能够正常使用，就需要使用一个没有参数的函数：interruption_point()。这意味着中断数据结构可以访问thread_local变量，并在线程运行时，对变量进行设置，因此当线程调用interruption_point()函数时，就会去检查当前运行线程的数据结构。我们将在后面看到interruption_point()的具体实现。

thread_local标志是不能使用普通的`std::thread`管理线程的主要原因；需要使用一种方法分配出一个可访问的interruptible_thread实例，就像新启动一个线程一样。在使用已提供函数来做这件事情前，需要将interruptible_thread实例传递给`std::thread`的构造函数，创建一个能够执行的线程，就像下面的代码清单所实现。

清单9.9 interruptible_thread的基本实现

```c++
class interrupt_flag
{
public:
  void set();
  bool is_set() const;
};
thread_local interrupt_flag this_thread_interrupt_flag;  // 1

class interruptible_thread
{
  std::thread internal_thread;
  interrupt_flag* flag;
public:
  template<typename FunctionType>
  interruptible_thread(FunctionType f)
  {
    std::promise<interrupt_flag*> p;  // 2
    internal_thread=std::thread([f,&p]{  // 3
      p.set_value(&this_thread_interrupt_flag);
      f();  // 4
    });
    flag=p.get_future().get();  // 5
  }
  void interrupt()
  {
    if(flag)
    {
      flag->set();  // 6
    }
  }
};
````

提供函数 f 是包装了一个 lambda 函数 ③，线程将会持有 f 副本和本地 promise 变量(p)的引用 ②。在新线程中，lambda 函数设置 promise 变量的值到 this_thread_interrupt_flag(在 thread_local① 中声明)的地址中，为的是让线程能够调用提供函数的副本 ④。调用线程会等待与其 future 相关的 promise 就绪，并且将结果存入到 flag 成员变量中 ⑤。注意，即使 lambda 函数在新线程上执行，对本地变量 p 进行悬空引用，都没有问题，因为在新线程返回之前，interruptible_thread 构造函数会等待变量 p，直到变量 p 不被引用。实现没有考虑处理汇入线程，或分离线程。所以，需要 flag 变量在线程退出或分离前已经声明，这样就能避免悬空问题。

interrupt()函数相对简单：需要一个线程去做中断时，需要一个合法指针作为一个中断标志，所以可以仅对标志进行设置 ⑥。

## 9.2.2 检查线程是否中断

现在就可以设置中断标志了，不过不检查线程是否被中断，这样的意义就不大了。使用 interruption_point()函数最简单的情况；可以在一个安全的地方调用这个函数，如果标志已经设置，就可以抛出一个 thread_interrupted 异常：

````c++
void interruption_point()
{
  if(this_thread_interrupt_flag.is_set())
  {
    throw thread_interrupted();
  }
}
```c++

代码中可以在适当的地方使用这个函数：

```c++
void foo()
{
  while(!done)
  {
    interruption_point();
    process_next_item();
  }
}
````

虽然也能工作，但不理想。最好实在线程等待或阻塞的时候中断线程，因为这时的线程不能运行，也就不能调用 interruption_point()函数！在线程等待的时候，什么方式才能去中断线程呢？

## 9.2.3 中断等待——条件变量

OK，需要仔细选择中断的位置，并通过显式调用 interruption_point()进行中断，不过在线程阻塞等待的时候，这种办法就显得苍白无力了，例如：等待条件变量的通知。就需要一个新函数——interruptible_wait()——就可以运行各种需要等待的任务，并且可以知道如何中断等待。之前提到，可能会等待一个条件变量，所以就从它开始：如何做才能中断一个等待的条件变量呢？最简单的方式是，当设置中断标志时，需要提醒条件变量，并在等待后立即设置断点。为了让其工作，需要提醒所有等待对应条件变量的线程，就能确保感谢兴趣的线程能够苏醒。伪苏醒是无论如何都要处理的，所以其他线程(非感兴趣线程)将会被当作伪苏醒处理——两者之间没什么区别。interrupt_flag 结构需要存储一个指针指向一个条件变量，所以用 set()函数对其进行提醒。为条件变量实现的 interruptible_wait()可能会看起来像下面清单中所示。

清单 9.10 为`std::condition_variable`实现的 interruptible_wait 有问题版

````c++
void interruptible_wait(std::condition_variable& cv,
std::unique_lock<std::mutex>& lk)
{
  interruption_point();
  this_thread_interrupt_flag.set_condition_variable(cv);  // 1
  cv.wait(lk);  // 2
  this_thread_interrupt_flag.clear_condition_variable();  // 3
  interruption_point();
}
```c++

假设函数能够设置和清除相关条件变量上的中断标志，代码会检查中断，通过interrupt_flag为当前线程关联条件变量①，等待条件变量②，清理相关条件变量③，并且再次检查中断。如果线程在等待期间被条件变量所中断，中断线程将广播条件变量，并唤醒等待该条件变量的线程，所以这里就可以检查中断。不幸的是，代码有两个问题。第一个问题比较明显，如果想要线程安全：`std::condition_variable::wait()`可以抛出异常，所以这里会直接退出，而没有通过条件变量删除相关的中断标志。这个问题很容易修复，就是在析构函数中添加相关删除操作即可。

第二个问题就不大明显了，这段代码存在条件竞争。虽然，线程可以通过调用interruption_point()被中断，不过在调用wait()后，条件变量和相关中断标志就没有什么系了，因为线程不是等待状态，所以不能通过条件变量的方式唤醒。就需要确保线程不会在最后一次中断检查和调用wait()间被唤醒。这里，不对`std::condition_variable`的内部结构进行研究；不过，可通过一种方法来解决这个问题：使用lk上的互斥量对线程进行保护，这就需要将lk传递到set_condition_variable()函数中去。不幸的是，这将产生两个新问题：需要传递一个互斥量的引用到一个不知道生命周期的线程中去(这个线程做中断操作)为该线程上锁(调用interrupt()的时候)。这里可能会死锁，并且可能访问到一个已经销毁的互斥量，所以这种方法不可取。当不能完全确定能中断条件变量等待——没有interruptible_wait()情况下也可以时(可能有些严格)，那有没有其他选择呢？一个选择就是放置超时等待，使用wait_for()并带有一个简单的超时量(比如，1ms)。在线程被中断前，算是给了线程一个等待的上限(以时钟刻度为基准)。如果这样做了，等待线程将会看到更多因为超时而“伪”苏醒的线程，不过超时也不轻易的就帮助到我们。与interrupt_flag相关的实现的一个实现放在下面的清单中展示。

清单9.11 为`std::condition_variable`在interruptible_wait中使用超时

```c++
class interrupt_flag
{
  std::atomic<bool> flag;
  std::condition_variable* thread_cond;
  std::mutex set_clear_mutex;

public:
  interrupt_flag():
    thread_cond(0)
  {}

  void set()
  {
    flag.store(true,std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(set_clear_mutex);
    if(thread_cond)
    {
      thread_cond->notify_all();
    }
  }

  bool is_set() const
  {
    return flag.load(std::memory_order_relaxed);
  }

  void set_condition_variable(std::condition_variable& cv)
  {
    std::lock_guard<std::mutex> lk(set_clear_mutex);
    thread_cond=&cv;
  }

  void clear_condition_variable()
  {
    std::lock_guard<std::mutex> lk(set_clear_mutex);
    thread_cond=0;
  }

  struct clear_cv_on_destruct
  {
    ~clear_cv_on_destruct()
    {
      this_thread_interrupt_flag.clear_condition_variable();
    }
  };
};

void interruptible_wait(std::condition_variable& cv,
  std::unique_lock<std::mutex>& lk)
{
  interruption_point();
  this_thread_interrupt_flag.set_condition_variable(cv);
  interrupt_flag::clear_cv_on_destruct guard;
  interruption_point();
  cv.wait_for(lk,std::chrono::milliseconds(1));
  interruption_point();
}
```c++

如果有谓词(相关函数)进行等待，1ms的超时将会完全在谓词循环中完全隐藏：

```c++
template<typename Predicate>
void interruptible_wait(std::condition_variable& cv,
                        std::unique_lock<std::mutex>& lk,
                        Predicate pred)
{
  interruption_point();
  this_thread_interrupt_flag.set_condition_variable(cv);
  interrupt_flag::clear_cv_on_destruct guard;
  while(!this_thread_interrupt_flag.is_set() && !pred())
  {
    cv.wait_for(lk,std::chrono::milliseconds(1));
  }
  interruption_point();
}
````

这会让谓词被检查的次数增加许多，不过对于简单调用 wait()这套实现还是很好用的。超时变量很容易实现：通过制定时间，比如：1ms 或更短。OK，对于`std::condition_variable`的等待，就需要小心应对了；`std::condition_variable_any`呢？还是能做的更好吗？

## 9.2.4 使用`std::condition_variable_any`中断等待

`std::condition_variable_any`与`std::condition_variable`的不同在于，`std::condition_variable_any`可以使用任意类型的锁，而不仅有`std::unique_lock<std::mutex>`。可以让事情做起来更加简单，并且`std::condition_variable_any`可以比`std::condition_variable`做的更好。因为能与任意类型的锁一起工作，就可以设计自己的锁，上锁/解锁 interrupt_flag 的内部互斥量 set_clear_mutex，并且锁也支持等待调用，就像下面的代码。

清单 9.12 为`std::condition_variable_any`设计的 interruptible_wait

```c++
class interrupt_flag
{
  std::atomic<bool> flag;
  std::condition_variable* thread_cond;
  std::condition_variable_any* thread_cond_any;
  std::mutex set_clear_mutex;

public:
  interrupt_flag():
    thread_cond(0),thread_cond_any(0)
  {}

  void set()
  {
    flag.store(true,std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(set_clear_mutex);
    if(thread_cond)
    {
      thread_cond->notify_all();
    }
    else if(thread_cond_any)
    {
      thread_cond_any->notify_all();
    }
  }

  template<typename Lockable>
  void wait(std::condition_variable_any& cv,Lockable& lk)
  {
    struct custom_lock
    {
      interrupt_flag* self;
      Lockable& lk;

      custom_lock(interrupt_flag* self_,
                  std::condition_variable_any& cond,
                  Lockable& lk_):
        self(self_),lk(lk_)
      {
        self->set_clear_mutex.lock();  // 1
        self->thread_cond_any=&cond;  // 2
      }

      void unlock()  // 3
      {
        lk.unlock();
        self->set_clear_mutex.unlock();
      }

      void lock()
      {
        std::lock(self->set_clear_mutex,lk);  // 4
      }

      ~custom_lock()
      {
        self->thread_cond_any=0;  // 5
        self->set_clear_mutex.unlock();
      }
    };
    custom_lock cl(this,cv,lk);
    interruption_point();
    cv.wait(cl);
    interruption_point();
  }
  // rest as before
};

template<typename Lockable>
void interruptible_wait(std::condition_variable_any& cv,
                        Lockable& lk)
{
  this_thread_interrupt_flag.wait(cv,lk);
}
```

自定义的锁类型在构造的时候，需要所锁住内部 set_clear_mutex①，对 thread_cond_any 指针进行设置，并引用`std::condition_variable_any`传入锁的构造函数中 ②。Lockable 引用将会在之后进行存储，其变量必须被锁住。现在可以安心的检查中断，不用担心竞争了。如果这时中断标志已经设置，那么标志一定是在锁住 set_clear_mutex 时设置的。当条件变量调用自定义锁的 unlock()函数中的 wait()时，就会对 Lockable 对象和 set_clear_mutex 进行解锁 ③。这就允许线程可以尝试中断其他线程获取 set_clear_mutex 锁；以及在内部 wait()调用之后，检查 thread_cond_any 指针。这就是在替换`std::condition_variable`后，所拥有的功能(不包括管理)。当 wait()结束等待(因为等待，或因为伪苏醒)，因为线程将会调用 lock()函数，这里依旧要求锁住内部 set_clear_mutex，并且锁住 Lockable 对象 ④。现在，在 wait()调用时，custom_lock 的析构函数中 ⑤ 清理 thread_cond_any 指针(同样会解锁 set_clear_mutex)之前，可以再次对中断进行检查。

## 9.2.5 中断其他阻塞调用

这次轮到中断条件变量的等待了，不过其他阻塞情况，比如：互斥锁，等待 future 等等，该怎么办呢？通常情况下，可以使用`std::condition_variable`的超时选项，因为在实际运行中不可能很快的将条件变量的等待终止(不访问内部互斥量或 future 的话)。不过，在某些情况下，你知道知道你在等待什么，这样就可以让循环在 interruptible_wait()函数中运行。作为一个例子，这里为`std::future<>`重载了 interruptible_wait()的实现：

```c++
template<typename T>
void interruptible_wait(std::future<T>& uf)
{
  while(!this_thread_interrupt_flag.is_set())
  {
    if(uf.wait_for(lk,std::chrono::milliseconds(1)==
       std::future_status::ready)
      break;
  }
  interruption_point();
}
```

等待会在中断标志设置好的时候，或 future 准备就绪的时候停止，不过实现中每次等待 future 的时间只有 1ms。这就意味着，中断请求被确定前，平均等待的时间为 0.5ms(这里假设存在一个高精度的时钟)。通常 wait_for 至少会等待一个时钟周期，所以如果时钟周期为 15ms，那么结束等待的时间将会是 15ms，而不是 1ms。接受与不接受这种情况，都得视情况而定。如果这必要，且时钟支持的话，可以持续削减超时时间。这种方式将会让线程苏醒很多次，来检查标志，并且增加线程切换的开销。

OK，我们已经了解如何使用 interruption_point()和 interruptible_wait()函数检查中断。

当中断被检查出来了，要如何处理它呢？

## 9.2.6 处理中断

从中断线程的角度看，中断就是 thread_interrupted 异常，因此能像处理其他异常那样进行处理。

特别是使用标准 catch 块对其进行捕获：

````c++
try
{
  do_something();
}
catch(thread_interrupted&)
{
  handle_interruption();
}
```c++

捕获中断，进行处理。其他线程再次调用interrupt()时，线程将会再次被中断，这就被称为*断点*(interruption point)。如果线程执行的是一系列独立的任务，就会需要断点；中断一个任务，就意味着这个任务被丢弃，并且该线程就会执行任务列表中的其他任务。

因为thread_interrupted是一个异常，在能够被中断的代码中，之前线程安全的注意事项都是适用的，就是为了确保资源不会泄露，并在数据结构中留下对应的退出状态。通常，让线程中断是可行的，所以只需要让异常传播即可。不过，当异常传入`std::thread`的析构函数时，`std::terminate()`将会调用，并且整个程序将会终止。为了避免这种情况，需要在每个将interruptible_thread变量作为参数传入的函数中放置catch(thread_interrupted)处理块，可以将catch块包装进interrupt_flag的初始化过程中。因为异常将会终止独立进程，就能保证未处理的中断是异常安全的。interruptible_thread构造函数中对线程的初始化，实现如下：

```c++
internal_thread=std::thread([f,&p]{
        p.set_value(&this_thread_interrupt_flag);

        try
        {
          f();
        }
        catch(thread_interrupted const&)
        {}
      });
````

下面，我们来看个更加复杂的例子。

## 9.2.7 应用退出时中断后台任务

试想，在桌面上查找一个应用。这就需要与用户互动，应用的状态需要能在显示器上显示，就能看出应用有什么改变。为了避免影响 GUI 的响应时间，通常会将处理线程放在后台运行。后台进程需要一直执行，直到应用退出；后台线程会作为应用启动的一部分被启动，并且在应用终止的时候停止运行。通常这样的应用只有在机器关闭时，才会退出，因为应用需要更新应用最新的状态，就需要全时间运行。在某些情况下，当应用被关闭，需要使用有序的方式将后台线程关闭，其中一种方式就是中断。

下面清单中为一个系统实现了简单的线程管理部分。

清单 9.13 在后台监视文件系统

```c++
std::mutex config_mutex;
std::vector<interruptible_thread> background_threads;

void background_thread(int disk_id)
{
  while(true)
  {
    interruption_point();  // 1
    fs_change fsc=get_fs_changes(disk_id);  // 2
    if(fsc.has_changes())
    {
      update_index(fsc);  // 3
    }
  }
}

void start_background_processing()
{
  background_threads.push_back(
    interruptible_thread(background_thread,disk_1));
  background_threads.push_back(
    interruptible_thread(background_thread,disk_2));
}

int main()
{
  start_background_processing();  // 4
  process_gui_until_exit();  // 5
  std::unique_lock<std::mutex> lk(config_mutex);
  for(unsigned i=0;i<background_threads.size();++i)
  {
    background_threads[i].interrupt();  // 6
  }
  for(unsigned i=0;i<background_threads.size();++i)
  {
    background_threads[i].join(); // 7
  }
}
```

启动时，后台线程就已经启动 ④。之后，对应线程将会处理 GUI⑤。当用户要求进程退出时，后台进程将会被中断 ⑥，并且主线程会等待每一个后台线程结束后才退出 ⑦。后台线程运行在一个循环中，并时刻检查磁盘的变化 ②，对其序号进行更新 ③。调用 interruption_point()函数，可以在循环中对中断进行检查。

为什么中断线程前，对线程进行等待？为什么不中断每个线程，让它们执行下一个任务？答案就是“并发”。线程被中断后，不会马上结束，因为需要对下一个断点进行处理，并且在退出前执行析构函数和代码异常处理部分。因为需要汇聚每个线程，所以就会让中断线程等待，即使线程还在做着有用的工作——中断其他线程。只有当没有工作时(所有线程都被中断)，不需要等待。这就允许中断线程并行的处理自己的中断，并更快的完成中断。

中断机制很容易扩展到更深层次的中断调用，或在特定的代码块中禁用中断，这就当做留给读者的作业吧。
