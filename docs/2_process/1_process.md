
## 进程那些事儿

### 0. concepts

什么是守护进程？ 如何查看守护进程？
什么是僵尸进程？ 如何查看僵尸进程？
- 请解释什么是 Daemon 进程，如何产生 Daemon 进程？
- 请解释僵尸进程的产生和消除方法。
创建进程的步骤？
进程切换发生的原因？
处理进程切换的步骤？
- **Daemon 进程**：后台运行的服务进程，无控制终端。
  - **产生方法**：通过 `fork` 创建子进程，父进程退出，子进程成为 Daemon 进程。

- **僵尸进程**：子进程退出后，父进程未调用 `wait` 系统调用回收子进程资源。
  - **消除方法**：父进程调用 `wait` 系统调用回收子进程资源。


* **什么是进程：**
进程是操作系统进行资源分配和调度的基本单位,是应用程序在操作系统中的一次运行过程.进程拥有自己独立的内存空间,每个进程中都有至少一个线程.进程之间的通信需要使用特殊的进程间通信机制.
  * 优点：
     * 进程是互相独立的，不涉及锁什么之类的，代码编写方便，调试方便
     * 进程是独立的，所以稳定性更高，一个进程挂了，其他也不会挂；
  * 缺点：
     * 进程创建慢，占用内存多，进程的上下文切换代价高昂
     * 进程之间通信麻烦且慢，常用且好的多进程通信机制一般为socket，所以通信内容一般为指令而非数据
* **进程的概念**：**进程是程序在某个数据集合上的一次运行活动，也是操作系统进行资源分配和保护的基本单位**。通俗来说，**「进程就是程序的一次执行过程」**，程序是静态的，它作为系统中的一种资源是永远存在的。而进程是动态的，它是动态的产生，变化和消亡的，拥有其自己的生命周期


* **进程的 ID**
  - **PID**：
    - 进程的唯一标识。对于多线程的进程而言，所有线程调用 `getpid` 函数会返回相同的值。
  - **PGID**：
    - 进程组 ID。每个进程都会有进程组 ID，表示该进程所属的进程组。默认情况下，新创建的进程会继承父进程的进程组 ID。
  - **SID**：
    - 会话 ID。每个进程也都有会话 ID。默认情况下，新创建的进程会继承父进程的会话 ID。

* 进程的组成

  * **进程控制块 PCB**。包含如下几个部分：
    - 进程描述信息，如pid，gid
    - 进程控制和管理信息，如进程状态，优先级，未决信号集，信号屏蔽字
    - 资源分配清单，如页表，打开文件列表
    - CPU 相关信息，如寄存器，状态寄存器，堆栈指针
  * **数据段**。即进程运行过程中各种数据（比如程序中定义的变量）
  * **程序段**。就是程序的代码（指令序列）

* 进程上下文切换

  * 首先，将进程 A 的运行环境信息存入 PCB，这个运行环境信息就是进程的上下文（Context）
  * 然后，将 PCB 移入相应的进程队列
  * 选择另一个进程 B 进行执行，并更新其 PCB 中的状态为运行态
  * 当进程 A 被恢复运行的时候，根据它的 PCB 恢复进程 A 所需的运行环境

#### 0.1 守护进程概念

* **守护进程**
  - **定义**：
    - 守护进程是后台运行的、不与任何终端关联的进程，无法通过终端进行输入输出。它们通常用于周期性地执行某种任务或等待处理特定的事件。
  - **实现思路**：
    - 将普通进程改造为守护进程的过程。不同版本的 Unix 系统其实现机制不同，BSD 和 Linux 下的实现细节有所不同。

#### 0.2 后台进程概念

后台进程是指在终端中启动后，不会阻塞终端，可以在终端中继续执行其他命令的进程。后台进程通常通过在命令末尾加上 & 符号来启动。后台进程在执行时，用户可以继续在终端中输入其他命令，而不需要等待后台进程完成。
后台进程其实和守护进程是非常类似的，只是严格意义下，后台进程一般还是和终端程序挂钩，而守护进程是与终端进程挂钩的。

#### 0.3 孤儿进程和僵尸进程
在操作系统中，进程的管理和状态是非常重要的。以下是对孤儿进程和僵尸进程的详细解释及其处理方法。

* **孤儿进程**
  - **定义**：
    - 孤儿进程是指一个父进程退出，而它的一个或多个子进程还在运行，那么这些子进程将成为孤儿进程。
    - 孤儿进程将被 `init` 进程（进程号为 1）所收养，并由 `init` 进程对它们完成状态收集工作。
  - **特点**：
    - 孤儿进程不会对系统造成危害，因为 `init` 进程会负责它们的状态收集和资源释放。

* **僵尸进程**
  - **定义**：
    - 僵尸进程是指一个进程使用 `fork` 创建子进程，如果子进程退出，而父进程没有调用 `wait` 或 `waitpid` 获取子进程的状态信息，那么子进程的进程描述符仍然保存在系统中。这种进程称之为僵尸进程。
  - **特点**：
    - 任何一个子进程（`init` 除外）在 `exit()` 之后，并非马上就消失，而是留下一个称为僵尸进程（Zombie）的数据结构，等待父进程处理。
    - 僵尸进程的进程号会一直被占用，系统所能使用的进程号是有限的，如果大量产生僵尸进程，将因为没有可用的进程号而导致系统不能产生新的进程。
  - **危害**：
    - 僵尸进程会占用系统的进程号资源，如果不及时处理，可能导致系统无法创建新的进程。
  - **解决办法**：
    - 子进程退出时向父进程发送 `SIGCHLD` 信号，父进程处理 `SIGCHLD` 信号，在处理函数中调用 `wait` 或 `waitpid`。

* **僵尸进程的危害场景**
  - **场景描述**：
    - 例如有个进程，它定期产生一个子进程，这个子进程需要做的事情很少，做完它该做的事情之后就退出了。因此这个子进程的生命周期很短。
    - 但是，父进程只管生成新的子进程，至于子进程退出之后的事情，则一概不闻不问。这样，系统运行一段时间之后，系统中就会存在很多的僵尸进程。如果用 `ps` 命令查看的话，就会看到很多状态为 `Z` 的进程。
  - **根本原因**：
    - 严格地来说，僵尸进程并不是问题的根源，罪魁祸首是产生出大量僵尸进程的那个父进程。因此，当我们寻求如何消灭系统中大量的僵尸进程时，答案就是把产生大量僵尸进程的那个元凶进程终止掉（通过 `kill` 发送 `SIGTERM` 或者 `SIGKILL` 信号）。
  - **处理方法**：
    - 终止元凶进程之后，它产生的僵尸进程就变成了孤儿进程，这些孤儿进程会被 `init` 进程接管，`init` 进程会 `wait()` 这些孤儿进程，释放它们占用的系统进程表中的资源，这样，这些已经僵死的孤儿进程就能被清理掉。

* **总结**
    简单来说孤儿进程是死了父进程，但是父进程还是会将孤儿进程委托给收养院。因此孤儿能健康长大；而僵尸进程则是被父进程遗弃了，父进程不处理子进程了。因此这个时候子进程就不能健康长大，就有危险了。

#### 1.4 说一下PCB/说一下进程地址空间/
https://blog.csdn.net/qq_38499859/article/details/80057427

PCB就是进程控制块,是操作系统中的一种数据结构,用于表示进程状态,操作系统通过PCB对进程进行管理.

PCB中包含有:进程标识符,处理器状态,进程调度信息,进程控制信息


​	PCB是操作系统中的一个数据结构描述,它是对系统的进程进行管理的重要依据,和进程管理相关的操作无一不用到PCB中的内容.一般情况下,PCB中包含以下内容:

(1)进程标识符(内部,外部)
(2)处理机的信息(通用寄存器,指令计数器,PSW,用户的栈指针).
(3)进程调度信息(进程状态,进程的优先级,进程调度所需的其它信息,事件)
(4)进程控制信息(程序的数据的地址,资源清单,进程同步和通信机制,链接指针)

​	不同操作系统PCB的具体实现可能会略有不同.

### 1. 进程创建

#### 1.1 fork，vfork，clone
创建进程的步骤？

### 2. [IPC](https://mp.weixin.qq.com/s/b6HLr348-v7ibntuWs1yRA)

* 进程间通信(IPC)，[详见](https://www.cnblogs.com/zgq0/p/8780893.html)


#### 2.1 套接字
与其它通信机制不同的是，它可用于不同机器间的进程通信。
* 套接字:套接口也是一种进程间通信机制,与其他通信机制不同的是,它可用于不同设备及其间的进程通信.


#### 2.1 管道

* 管道:管道是半双工的,双方需要通信的时候,需要建立两个管道.管道的实质是一个内核缓冲区,进程以先进先出的方式从缓冲区存取数据:管道一端的进程顺序地将进程数据写入缓冲区,另一端的进程则顺序地读取数据,该缓冲区可以看做一个循环队列,读和写的位置都是自动增加的,一个数据只能被读一次,读出以后再缓冲区都不复存在了.当缓冲区读空或者写满时,有一定的规则控制相应的读进程或写进程是否进入等待队列,当空的缓冲区有新数据写入或慢的缓冲区有数据读出时,就唤醒等待队列中的进程继续读写.管道是最容易实现的
  匿名管道pipe和命名管道除了建立,打开,删除的方式不同外,其余都是一样的.匿名管道只允许有亲缘关系的进程之间通信,也就是父子进程之间的通信,命名管道允许具有非亲缘关系的进程间通信.

  管道的底层实现 https://segmentfault.com/a/1190000009528245

* 通常指无名管道，是 UNIX 系统IPC最古老的形式

* 它是**半双工**的（即数据只能在一个方向上流动），具有固定的读端和写端
* 它只能用于**具有亲缘关系的进程之间的通信**（也是父子进程或者兄弟进程之间）
* 它可以看成是一种特殊的文件，对于它的读写也可以使用普通的read、write 等函数。但是它不是普通的文件，并**不属于其他任何文件系统**，并且**只存在于内存**中
* 原型

```c
1 #include <unistd.h>
2 int pipe(int fd[2]);    // 返回值：若成功返回0，失败返回-1
```

管道是通过调用 pipe 函数创建的，fd[0] 用于读，fd[1] 用于写。

```c
#include <unistd.h>
int pipe(int fd[2]);
```

它具有以下限制：

- 只支持半双工通信（单向交替传输）；
- 只能在父子进程或者兄弟进程中使用。

<div align="center"> <img src="https://cs-notes-1256109796.cos.ap-guangzhou.myqcloud.com/53cd9ade-b0a6-4399-b4de-7f1fbd06cdfb.png"/> </div><br>


#### 2.1 FIFO

* 也称为命名管道，它是一种文件类型

* FIFO可以在无关的进程之间交换数据，与无名管道不同
* FIFO有路径名与之相关联，它以一种**特殊设备文件形式存在于文件系统**中
* FIFO的通信方式类似于在进程中使用文件来传输数据，只不过FIFO类型文件同时具有管道的特性。**在数据读出时，FIFO管道中同时清除数据，并且“先进先出”**
* 原型

```c
1 #include <sys/stat.h>
2 // 返回值：成功返回0，出错返回-1
3 int mkfifo(const char *pathname, mode_t mode);
```

* 其中的` mode `参数与`open`函数中的 `mode `相同。一旦创建了一个 FIFO，就可以用一般的文件I/O函数操作它

  * 当 open 一个FIFO时，是否设置非阻塞标志（`O_NONBLOCK`）的区别：
    * 若没有指定`O_NONBLOCK`（默认），只读 open 要阻塞到某个其他进程为写而打开此 FIFO。类似的，只写 `open` 要阻塞到某个其他进程为读而打开它
    * 若指定了`O_NONBLOCK`，则只读` open `立即返回。而只写` open `将出错返回 -1 如果没有进程已经为读而打开该 FIFO，其`errno`置`ENXIO`。


也称为命名管道，去除了管道只能在父子进程中使用的限制。

```c
#include <sys/stat.h>
int mkfifo(const char *path, mode_t mode);
int mkfifoat(int fd, const char *path, mode_t mode);
```

FIFO 常用于客户-服务器应用程序中，FIFO 用作汇聚点，在客户进程和服务器进程之间传递数据。

<div align="center"> <img src="https://cs-notes-1256109796.cos.ap-guangzhou.myqcloud.com/2ac50b81-d92a-4401-b9ec-f2113ecc3076.png"/> </div><br>


#### 2.1 消息队列

消息队列就是一个消息的链表,是一系列保存在内核中消息的列表.用户进程可以向消息队列添加消息,也可以向消息队列读取消息.
消息队列与管道通信相比,其优势是对每个消息指定特定的消息类型,接收的时候不需要按照队列次序,而是可以根据自定义条件接收特定类型的消息.
可以把消息看做一个记录,具有特定的格式以及特定的优先级.对消息队列有写权限的进程可以向消息队列中按照一定的规则添加新消息,对消息队列有读权限的进程可以从消息队列中读取消息.



* **是消息的链接表，存放在内核中**。一个消息队列由一个标识符（即队列ID）来标识

* 消息队列是面向记录的，其中的消息具有特定的格式以及特定的优先级
  * 消息队列独立于发送与接收进程。**进程终止时，消息队列及其内容并不会被删除**
  * **消息队列可以实现消息的随机查询，消息不一定要以先进先出的次序读取,也可以按消息的类型读取**
  * 原型

```c
#include <sys/msg.h>
// 创建或打开消息队列：成功返回队列ID，失败返回-1
int msgget(key_t key, int fslag);
// 添加消息：成功返回0，失败返回-1
int msgsnd(int msqid, const void *ptr, size_t size, int flag);
// 读取消息：成功返回消息数据的长度，失败返回-1
int msgrcv(int msqid, void *ptr, size_t size, long type,int flag);
// 控制消息队列：成功返回0，失败返回-1
int msgctl(int msqid, int cmd, struct msqid_ds *buf);
```

* 在以下两种情况下，`msgget`将创建一个新的消息队列：

  - 如果没有与键值key相对应的消息队列，并且flag中包含了`IPC_CREAT`标志位。
  - key参数为`IPC_PRIVATE`。

  * 函数`msgrcv`在读取消息队列时，`type`参数有下面几种情况：
    * `type == 0`，返回队列中的第一个消息
    * `type > 0`，返回队列中消息类型为 `type` 的第一个消息
    * `type < 0`，返回队列中消息类型值小于或等于` type `绝对值的消息，如果有多个，则取类型值最小的消息
  * 可以看出，type值非 0 时用于以非先进先出次序读消息。也可以把` type` 看做优先级的权值

相比于 FIFO，消息队列具有以下优点：

- 消息队列可以独立于读写进程存在，从而避免了 FIFO 中同步管道的打开和关闭时可能产生的困难；
- 避免了 FIFO 的同步阻塞问题，不需要进程自己提供同步方法；
- 读进程可以根据消息类型有选择地接收消息，而不像 FIFO 那样只能默认地接收。
- 匿名管道是跟随进程的,消息队列是跟随内核的,也就是说进程结束之后,匿名管道就死了,但是消息队列还会存在(除非显示调用函数销毁).
- 管道是文件,存放在磁盘上,访问速度慢,消息队列是数据结构,存放在内存,访问速度快.
- 管道是数据流式存取,消息队列是数据块式存取.

```c
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
int msgget(key_t key, int msgflg);

```

#### 2.1 [信号(signal)](https://blog.csdn.net/h___q/article/details/84245317)

信号是一种比较复杂的通信方式,用于通知接收进程某个事件已经发生.

信号可以在任何时候发送给某一进程,而无须知道该进程的状态.如果该进程未处于执行状态,则该信号就由内核保存起来,直到该进程恢复执行并传递给它为止.如果一个信号被进程设置为阻塞,则该信号的传递被延迟,直到其阻塞被取消时才被传递给进程.

Linux提供了几十种信号,分别代表着不同的意义.信号之间依靠他们的值来区分,但是通常在程序中使用信号的名字来表示一个信号.通常程序中直接包含<signal.h>就好.

信号是在软件层次上对中断机制的一种模拟,是一种异步通信方式,信号可以在用户空间进程和内核之间直接交互.内核也可以利用信号来通知用户空间的进程.

信号的来源:

硬件来源,例如按下了cltr+C,通常产生中断信号sigint.
软件来源,例如使用系统调用或者命令发出信号.最常用的发送信号的系统函数是kill,raise,setitimer,sigation,sigqueue函数.软件来源还包括一些非法运算等操作.
一旦有信号产生,用户进程对信号产生的相应有三种方式:

执行默认操作,linux对每种信号都规定了默认操作.
捕捉信号,定义信号处理函数,当信号发生时,执行相应的处理函数.
忽略信号,当不希望接收到的信号对进程的执行产生影响,而让进程继续执行时,可以忽略该信号,即不对信号进程作任何处理.
但是有两个信号SIGKILL和SEGSTOP是应用进程无法捕捉和忽略的,这是为了使系统管理员能在任何时候中断或结束某一特定的进程.

信号是Linux系统中用于进程之间通信或操作的一种机制,信号可以在任何时候发送给某一进程,而无须知道该进程的状态.如果该进程并未处于执行状态,则该信号就由内核保存起来,知道该进程恢复执行并传递给他为止.如果一个信号被进程设置为阻塞,则该信号的传递被延迟,直到其阻塞被取消时才被传递给进程. 信号是开销最小的

它是一个计数器，用于为多个进程提供对共享数据对象的访问。
信号量多用于进程间的同步和互斥.

信号量的工作机制,它可以直接理解成计数器,信号量会有初值(>0),每当有进程申请使用信号量,通过一个P操作来对信号量进行-1操作,当计数器减到0的时候就说明没有资源了,其他进程要想访问就必须等待(比如忙等待或者睡眠),当该进程执行完这段工作(我们称之为临界区)之后,就会执行V操作来对信号量进行+1操作.

进程AB利用信号量通信,A创建信号量/初始化信号量;B用同样的key创建信号量;然后它们就可以利用信号量进行通信了.

```c
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
int semget(key_t key, int nsems, int semflg); // 信号量的创建:
int semop(int semid, struct sembuf *sops, unsigned nsops); // 信号量操作:P/V操作通过一个函数实现
```

* 信号是一种比较复杂的通信方式，**用于通知接收进程某个事件已经发生**

* 程序不可捕获、阻塞或忽略的信号有：**SIGKILL(9)，SIGSTOP(19)**

  - 它们向超级用户提供一种使进程终止或停止的可靠方法
  - 如果忽略某些由硬件异常产生的信号（例如非法存储访问或除以0），则进程的行为是未定义的

* 常见信号表

  ![signal_tab](imgs/os/signal_tab.png)

* 信号产生方式

  * [信号可以通过六个函数产生](https://www.jianshu.com/p/e4ce1f6488af):

    - **kill函数**
    - **raise函数**
    - **sigqueue函数**
    - **alarm函数**
    - **setitimer函数**
    - **abort函数**
    
  * 键盘产生
  
    * 如ctrl+c，ctrl+z，ctrl+/等
  
  * 程序异常
  
    * 除0错误。除0错误会导致硬件错误
    * core dumped（核心转储）：**当进程异常退出时，操作系统会将该进程发生异常退出之前在内存中的数据存储至硬盘上**
    * **2、9号信号不会产生core文件**
  
  * 使用kill命令
  
    * **kill 在无指定时默认发送2号信号，可将指定程序终止**。若仍无法终止该程序，可使用 SIGKILL(9) 信息尝试强制删除程序。程序或工作的编号可利用 ps 指令或 jobs 指令查看
  
    ```shell
    kill [-s <信息名称或编号>][程序]　或　
    kill [-l <信息编号>]
    
    -l <信息编号> 　若不加<信息编号>选项，则 -l 参数会列出全部的信息名称
    -s <信息名称或编号> 　指定要送出的信息
    [程序] 　[程序]可以是程序的PID或是PGID，也可以是工作编号
    
    最常用的信号是：
    1 (HUP)：重新加载进程
    9 (KILL)：杀死一个进程
    15 (TERM)：正常停止一个进程
    ```
  
  * 通过系统调用接口给特定进程发送信号
  
    ```c++
    #include<signal.h>
    
    int kill(pid_t pid, int signo);
    //向特定进程发送特定信号;成功返回0;失败返回-1
    
    int raise(int signo);
    //向当前进程发送特定信号;成功返回0;失败返回-1
    
    #include<stdlib.h>
    void abort(void);
    //使当前进程收到信号而异常终止；就像exit()函数一样，abort()函数总是会成功的，所以没有返回值
    ```
  
  * 由软件条件发送信号
  
    * SIGPIPE：SIGPIPE是一种由软件条件产生的信号，**当一个管道的读端被关闭时，这时候操作系统就会检测到该管道中写入的数据不会在有人来管道内读文件了，操作系统会认为该管道的存在会造成内存资源的极大浪费，则操作系统就会向写端对应的目标进程发送SIGPIPE信号**
  
    * 定时器
  
      ```c++
      #include<unistd.h>
      unsigned int alarm(unsigned int seconds);
      //调用alarm函数可以对当前进程设置一个闹钟，也就是告诉操作系统在seconds秒之后对当前进程发送SIGALRM信号，该信号的默认处理动作是终止当前进程
      ```
  
* **信号集操作函数**

  ```c++
  #include<signal.h>
  
  //注意：在使用sigset_t类型的变量前，一定要调用sigemptyset或sigfillset进行初始化，使信号集处于某种确定的状态，初始化之后就可以调用sigaddset或sigdelset在信号集中添加或删除某种有效信号
  
  int sigemptyset(sigset_t *set);
  //初始化set所指向的信号集，使其中所有信号对应的比特位清零，表示该信号集不包含任何信号
  
  int sigfillset(sigset_t *set);
  //初始化set所指向的信号集，将其中所有信号对应的比特位置1，表示该信号集的有效信号包括系统支持的所有信号
  
  int sigaddset(sigset_t *set, int signo);
  //表示将set所指向的信号集中的signo信号置1
  
  int sigdelset(sigset_t *set, int signo);
  //表示将set所指向的信号集中的signo信号清零
  
  int sigismember(const sigset_t *set, int signo);
  //用来判断set所指向的信号集的有效信号中是否包含signo信号，包含返回1，不包含返回0，出错返回-1
  
  int sigpending(sigset_t *set);
  // 获取进程的pending信号集
  // 成功返回0；失败返回-1
  ```

* **设置/修改进程的信号屏蔽字（block表）**

  ```c++
  #include<signal.h>
  
  int sigprocmask(int how, const sigset_t *set, sigset_t *oset);
  
  /*
    int how：
        SIG_BLOCK：set包含了用户希望添加到当前信号屏蔽字的信号，即就是在老的信号屏蔽字中添加上新的信号。相当于：mask=mask|set
        SIG_UNBLOCK：set包含了用户希望从当前信号屏蔽字中解除阻塞的信号，即就是在老的信号屏蔽字中取消set表中的信号。相当于：mask=mask&~set
        SIG_SETMASK：设置当前进程的信号屏蔽字为set所指向的信号集。相当于：mask=set
    const sigset_t *set：
        将要设置为进程block表的信号集
    sigset_t *oset：
        用来保存进程旧的block表
        若无需保存进程旧的block表，传递空指针即可
  */
  ```

* **自定义信号处理方式**

  ```c++
  #include<signal.h>
  
  struct sigaction
  {
      void (*sa_handler)(int);	//指向信号处理对应的函数
      void (*sa_sigaction)(int, siginfo_t *, void *);
      sigset_t sa_mask; //当在处理所收到信号时，想要附带屏蔽的其他普通信号，当不需要屏蔽其他信号时，需要使用sigemptyset初始化sa_mask
      int sa_flags;
      void (*sa_restorer)(void);
  };
  
  int sigaction(int signo, const struct sigaction *act, struct sigaction *oact);
  /*
  int signo：
    指定的信号编号
  const struct sigaction *act：
    若该act指针非空，则根据act指针来修改进程收到signo信号的处理动作
  struct sigaction *oact：
    若oact指针非空，则使用oact来保存信号旧的处理动作
  */
  ```

* **信号处理过程**

  <img src="imgs/os/sig_cap.png" alt="sig_cap" style="zoom:80%;" />

* 信号接收

  * **接收信号的任务是由内核代理的，但内核接收到信号后，会将其放到对应进程的PCB的未决信号集中，同时向进程发送一个中断，使其陷入内核态**
  
* **此时信号只是在未决信号集中，对进程来说是不知道信号到来的**
  
* 信号的检测

  * 进程陷入内核后，**有两种场景会对信号集进行检测**：
    * 进程**从内核态返回到用户态前进行信号检测**
    * 进程在内核态中，**从睡眠状态被唤醒的时候进行信号检测**
  * 当发现有新信号后，便会进入下一步，信号处理

* 信号的处理

  * 如果用户**未注册信号处理函数**，则内核按照信号的**默认处理方式**处理
  * **如果用户注册了信号处理函数，则信号处理函数是运行在用户态的**，调用处理函数前，**内核会将当前内核栈的内容备份拷贝到用户栈上，并且修改指令寄存器(eip)将其指向信号处理函数**
  * **接下来进程返回到用户态中，执行相应的信号处理函数**
  * **信号处理函数执行完成后，还需要返回内核态，检查是否还有其他信号未处理**
  * **如果所有信号都处理完成了，就会将内核栈回复(从用户栈的备份拷贝回来)，同时恢复指令寄存器(eip)将其指向中断前的运行位置，最后回到用户态继续执行进程**
  * 如果同时有多个信号到达，处理流程为上面1，2，3，4步骤间重复进行，直到所有信号处理完毕

* **处理信号的时机**

  * 进程收到一个信号时，**并不会立即就去处理这个信号，而是先将收到的信号保存下来，并在合适的时候对信号进行处理**，**操作系统会在进程进入了内核态并从内核态返回用户态时，检测进程中可以进行处理的信号，并进行处理**

* 用户写好的代码会在什么情况下进入内核态呢？

  - 调用系统调用接口
  - 异常
  - 中断

#### 2.1 信号量

* 信号量:信号量是一个计数器,可以用来控制多个进程对共享资源的访问.信号量只有等待和发送两种操作.等待(P(sv))就是将其值减一或者挂起进程,发送(V(sv))就是将其值加一或者将进程恢复运行.
信号是一种比较复杂的通信方式,用于通知接收进程某个事件已经发生.

信号可以在任何时候发送给某一进程,而无须知道该进程的状态.如果该进程未处于执行状态,则该信号就由内核保存起来,直到该进程恢复执行并传递给它为止.如果一个信号被进程设置为阻塞,则该信号的传递被延迟,直到其阻塞被取消时才被传递给进程.

Linux提供了几十种信号,分别代表着不同的意义.信号之间依靠他们的值来区分,但是通常在程序中使用信号的名字来表示一个信号.通常程序中直接包含<signal.h>就好.

信号是在软件层次上对中断机制的一种模拟,是一种异步通信方式,信号可以在用户空间进程和内核之间直接交互.内核也可以利用信号来通知用户空间的进程.

信号的来源:

硬件来源,例如按下了cltr+C,通常产生中断信号sigint.
软件来源,例如使用系统调用或者命令发出信号.最常用的发送信号的系统函数是kill,raise,setitimer,sigation,sigqueue函数.软件来源还包括一些非法运算等操作.
一旦有信号产生,用户进程对信号产生的相应有三种方式:

执行默认操作,linux对每种信号都规定了默认操作.
捕捉信号,定义信号处理函数,当信号发生时,执行相应的处理函数.
忽略信号,当不希望接收到的信号对进程的执行产生影响,而让进程继续执行时,可以忽略该信号,即不对信号进程作任何处理.
但是有两个信号SIGKILL和SEGSTOP是应用进程无法捕捉和忽略的,这是为了使系统管理员能在任何时候中断或结束某一特定的进程.


* 与已经介绍过的 IPC 结构不同，它是**一个计数器**。信号量**用于实现进程间的互斥与同步，而不是用于存储进程间通信数据**

* 信号量用于进程间同步，若要在进程间传递数据需要结合共享内存
  * 信号量基于操作系统的 PV 操作，程序对信号量的操作都是原子操作
  
  * 每次对信号量的 PV 操作不仅限于对信号量值加 1 或减 1，而且可以加减任意正整数
  
  * 支持信号量组
  
  * 原型
  
    ```c
    #include <sys/sem.h>
    // 创建或获取一个信号量组：若成功返回信号量集ID，失败返回-1
    int semget(key_t key, int num_sems, int sem_flags);
    // 对信号量组进行操作，改变信号量的值：成功返回0，失败返回-1
    int semop(int semid, struct sembuf semoparray[], size_t numops);  
    // 控制信号量的相关信息
    int semctl(int semid, int sem_num, int cmd, ...);
    ```
  
* 当`semget`创建新的信号量集合时，必须指定集合中信号量的个数（即`num_sems`），通常为1； 如果是引用一个现有的集合，则将`num_sems`指定为 0 。在`semop`函数中，`sembuf`结构的定义如下：

  ```
  struct sembuf 
  {
      short sem_num; // 信号量组中对应的序号，0～sem_nums-1
      short sem_op;  // 信号量值在一次操作中的改变量
      short sem_flg; // IPC_NOWAIT, SEM_UNDO
  }
  ```

* 其中` sem_op` 是一次操作中的信号量的改变量：

  - 若`sem_op > 0`，表示进程释放相应的资源数，将 sem_op 的值加到信号量的值上。如果有进程正在休眠等待此信号量，则换行它们
    - 若`sem_op < 0`，请求 `sem_op `的绝对值的资源
      - 如果相应的资源数可以满足请求，则将该信号量的值减去sem_op的绝对值，函数成功返回。
      - 当相应的资源数不能满足请求时，这个操作与`sem_flg`有关
        - `sem_flg `指定`IPC_NOWAIT`，则semop函数出错返回`EAGAIN`
        - `sem_flg` 没有指定`IPC_NOWAIT`，则将该信号量的`semncnt`值加1，然后进程挂起直到下述情况发生：
          1. 当相应的资源数可以满足请求，此信号量的`semncnt`值减1，该信号量的值减去sem_op的绝对值。成功返回；
          2. 此信号量被删除，函数`smeop`出错返回`EIDRM`；
          3. 进程捕捉到信号，并从信号处理函数返回，此情况下将此信号量的`semncnt`值减1，函数`semop`出错返回`EINTR`
    - 若`sem_op == 0`，进程阻塞直到信号量的相应值为0：
      - 当信号量已经为0，函数立即返回。
      - 如果信号量的值不为0，则依据`sem_flg`决定函数动作：
        - `sem_flg`指定`IPC_NOWAIT`，则出错返回`EAGAIN`。
        - `sem_flg`没有指定`IPC_NOWAIT`，则将该信号量的`semncnt`值加1，然后进程挂起直到下述情况发生：
          1. 信号量值为0，将信号量的`semzcnt`的值减1，函数`semop`成功返回；
          2. 此信号量被删除，函数`smeop`出错返回`EIDRM`；
          3. 进程捕捉到信号，并从信号处理函数返回，在此情况将此信号量的`semncnt`值减1，函数`semop`出错返回`EINTR`

  - 在`semctl`函数中的命令有多种，这里就说两个常用的：
    - `SETVAL`：用于初始化信号量为一个已知的值。所需要的值作为联合`semun`的`val`成员来传递。在信号量第一次使用之前需要设置信号量。
    - `IPC_RMID`：删除一个信号量集合。如果不删除信号量，它将继续在系统中存在，即使程序已经退出，它可能在你下次运行此程序时引发问题，而且信号量是一种有限的资源。

#### 2.1 共享内存

共享内存允许两个或多个进程共享一个给定的存储区,这一段存储区可以被两个或两个以上的进程映射至自身的地址空间中,就像由malloc()分配的内存一样使用.一个进程写入共享内存的信息,可以被其他使用这个共享内存的进程,通过一个简单的内存读取读出,从而实现了进程间的通信.共享内存的效率最高,缺点是没有提供同步机制,需要使用锁等其他机制进行同步.

允许多个进程共享一个给定的存储区。因为数据不需要在进程之间复制，所以这是最快的一种 IPC。

需要使用信号量用来同步对共享存储的访问。

多个进程可以将同一个文件映射到它们的地址空间从而实现共享内存。另外 XSI 共享内存不是使用文件，而是使用内存的匿名段。


* 指两个或多个进程共享一个给定的存储区

* **共享内存是最快的一种 IPC，因为进程是直接对内存进行存取**

* **因为多个进程可以同时操作，所以需要进行同步**

* **信号量+共享内存通常结合在一起使用，信号量用来同步对共享内存的访问**

* **共享内存实现原理**：共享内存是通过**把同一块内存分别映射到不同的进程空间**中实现进程间通信。而共享内存本身不带任何互斥与同步机制，但当多个进程同时对同一内存进行读写操作时会破坏该内存的内容，所以，在实际中，同步与互斥机制需要用户来完成

* 在**/proc/sys/kernel/**目录下，记录着共享内存的一些限制，如一个共享内存区的**最大字节数shmmax**，系统范围内最大共享内存区标识符数shmmni等，可以手工对其调整，但不推荐这样做

* 共享内存使用
  * 进程必须首先分配它
  * 随后需要访问这个共享内存块的每一个进程都必须将这个共享内存绑定到自己的地址空间中
  * 当完成通信之后，所有进程都将脱离共享内存，并且由一个进程释放该共享内存块

* 原型

  ```c
  #include <sys/shm.h>
  // 创建或获取一个共享内存：成功返回共享内存ID，失败返回-1
  int shmget(key_t key, size_t size, int flag);
  // 连接共享内存到当前进程的地址空间：成功返回指向共享内存的指针，失败返回-1
  void *shmat(int shm_id, const void *addr, int flag);
  // 断开与共享内存的连接：成功返回0，失败返回-1
  int shmdt(void *addr); 
  // 控制共享内存的相关信息：成功返回0，失败返回-1
  int shmctl(int shm_id, int cmd, struct shmid_ds *buf);
  ```

* 当用`shmget`函数创建一段共享内存时，必须指定其 size；而如果引用一个已存在的共享内存，则将 size 指定为0 
  * **当一段共享内存被创建以后，它并不能被任何进程访问。必须使用`shmat`函数连接该共享内存到当前进程的地址空间，连接成功后把共享内存区对象映射到调用进程的地址空间，随后可像本地空间一样访问**
  * `shmdt`函数是用来断开`shmat`建立的连接的。注意，**这并不是从系统中删除该共享内存，只是当前进程不能再访问该共享内存而已**
  * `shmctl`函数可以对共享内存执行多种操作，根据参数 cmd 执行相应的操作。常用的是`IPC_RMID`（从系统中删除该共享内存）

* **mmap实现共享内存**
  
  * mmap系统调用并不是完全为了用于共享内存而设计的。它本身提供了不同于一般对普通文件的访问方式，进程可以像读写内存一样对普通文件的操作。而Posix或系统V的共享内存IPC则纯粹用于共享目的，**当然mmap()实现共享内存也是其主要应用之一**
  * **mmap系统调用使得进程之间通过映射同一个普通文件实现共享内存**。普通文件被映射到进程地址空间后，进程可以像访问普通内存一样对文件进行访问，不必再调用read()，write（）等操作。
  * mmap并不分配空间，只是**将文件映射到调用进程的地址空间里**，然后你就可以用memcpy等操作写文件，而不用write()了。写完后用msync()同步一下，你所写的内容就保存到文件里了。 **不过这种方式没办法增加文件的长度**，**因为要映射的长度在调用mmap()的时候就决定了**
  * 简单说就是把一个文件的内容在内存里面做一个映像，内存比磁盘快些


### 3. 进程调度

https://blog.csdn.net/u011080472/article/details/51217754

https://blog.csdn.net/leex_brave/article/details/51638300

* 先来先服务 (FCFS first come first serve):按照作业到达任务队列的顺序调度  FCFS是非抢占式的,易于实现,效率不高,性能不好,有利于长作业(CPU繁忙性)而不利于短作业(I/O繁忙性).
* 短作业优先 (SHF short job first):每次从队列里选择预计时间最短的作业运行.SJF是非抢占式的,优先照顾短作业,具有很好的性能,降低平均等待时间,提高吞吐量.但是不利于长作业,长作业可能一直处于等待状态,出现饥饿现象;完全未考虑作业的优先紧迫程度,不能用于实时系统.
* 最短剩余时间优先 该算法首先按照作业的服务时间挑选最短的作业运行,在该作业运行期间,一旦有新作业到达系统,并且该新作业的服务时间比当前运行作业的剩余服务时间短,则发生抢占;否则,当前作业继续运行.该算法确保一旦新的短作业或短进程进入系统,能够很快得到处理.
* 高响应比优先调度算法(Highest Reponse Ratio First, HRRF)是非抢占式的,主要用于作业调度.基本思想:每次进行作业调度时,先计算后备作业队列中每个作业的响应比,挑选最高的作业投入系统运行.响应比 = (等待时间 + 服务时间) / 服务时间 = 等待时间 / 服务时间 + 1.因为每次都需要计算响应比,所以比较耗费系统资源.
* 时间片轮转 用于分时系统的进程调度.基本思想:系统将CPU处理时间划分为若干个时间片(q),进程按照到达先后顺序排列.每次调度选择队首的进程,执行完1个时间片q后,计时器发出时钟中断请求,该进程移至队尾.以后每次调度都是如此.该算法能在给定的时间内响应所有用户的而请求,达到分时系统的目的.
* 多级反馈队列(Multilevel Feedback Queue) 

进程切换发生的原因？
处理进程切换的步骤？

### 4. 进程控制块

PCB是操作系统中的一个数据结构描述,它是对系统的进程进行管理的重要依据,和进程管理相关的操作无一不用到PCB中的内容.一般情况下,PCB中包含以下内容:

(1)进程标识符(内部,外部)
(2)处理机的信息(通用寄存器,指令计数器,PSW,用户的栈指针).
(3)进程调度信息(进程状态,进程的优先级,进程调度所需的其它信息,事件)
(4)进程控制信息(程序的数据的地址,资源清单,进程同步和通信机制,链接指针)

​	不同操作系统PCB的具体实现可能会略有不同.

https://blog.csdn.net/qq_38499859/article/details/80057427

PCB就是进程控制块,是操作系统中的一种数据结构,用于表示进程状态,操作系统通过PCB对进程进行管理.

PCB中包含有:进程标识符,处理器状态,进程调度信息,进程控制信息
