[有栈与无栈协程DEMO](https://github.com/lishaojiang/talkcoroutine)

一个是linux下X86无独立栈协程
gcc

## X86平台下的无独立栈协程

### 原理与C++代码的设计

有了C#的那个例子，我们先写个类似的函数RunPrintfTestCanBreak，函数遇到COROUTINE_YIELD能够返回并保存现场，再次调用时RunPrintfTestCanBreak时，能够恢复现场，接着执行，对于本例，也就是局部变量i,a的值要能正确的恢复，正确的打印。
```C++
void RunPrintfTestCanBreak(void* pParam)
{
    int i = 0;
    int a = 100;
    for(i = 0;i < 10;i++)
    {
        a += 10;
        std::cout << "I am run,Times:" << i << ",the a is:"<< a << std::endl;
        COROUTINE_YIELD;
    }
    COROUTINE_END;
}
```
有了上面C++必备知识，我们可以写个结构体来描述函数协程环境，然后函数传入这个环境就可以了，嗯，是个不错主意，先给出代码。

```C++
//rayhutnerli
//2021/4/17


#ifndef FUNINFO_H_
#define FUNINFO_H_

#include <stdint.h>
#include <string.h>

#pragma pack(1)

typedef void (*RegCallFun)(void*);

// now we just deal x86 version
// struct FunEnvInfo
typedef struct _FunEnvInfo
{
    _FunEnvInfo()
    {
        memset(this,0,sizeof(_FunEnvInfo));
    }
    int reEIP; // 0
    int reESP; // 4
    int reEBP; // 8
    int reEAX; // 12
    int reEBX; // 16
    int reECX; // 20
    int reEDX; // 24
    int reESI; // 28
    int reEDI; // 32
    int reEFLAG; // 36
    int iStackSize; // 40
    void* pStackMem; // 44
    RegCallFun pfCallFun; // 48
}FunEnvInfo,*FunEnvInfoPtr;

#pragma pack()

#endif
```

我们这里FunEnvInfo保存了x86的常见寄存器，并给出了偏移地址，同时可以看出，暂时我们不处理XMM相关的寄存器。实际根据Intel i386 ABI调用约定，eax作为返回值，也基本不用处理，除非父函数有使用eax，要保护一下，我这里演示虽然保留了，但实际没有处理。iStackSize是表示函数栈内存的大小，pStackMem表示栈内存要拷贝的副本内存，是一个手动malloc分配的堆内存，pfCallFun表示协程函数，我们会注册用，每次注册会生成一个实例，函数可以相同。

我们的原理：**当协程函数退出时，FunEnvInfo里面寄存器保存协程函数当时退出时的寄存器值，用malloc分配的堆内存pStackMem来保存协程函数退出的栈内存，记录退出时EIP的值；当退出的协程函数再次调用时，我们并不切换栈，只是将手动的保存的栈内存拷贝回来，寄存器恢复，并跳转到协程函数退出时EIP的指令内存地址。**

我们说一下与协程函数交互的核心代码，先贴出函数定义，这三个函数都是我用汇编实现的，每个都会详细讲解。其中CoroutineExec用来**调用和恢复协程函数**，也可以理解为简单的调度器，CoroutineBreak用来从**协程函数退出，做保存协程函数上下文**，CoroutineEnd退出当前协程，做一些**内存清理与标记**，这三个函数重新宏定义了一下，COROUTINE_YIELD 和COROUTINE_END插入到协程函数，使其看起来更像C#的yield写法。

```C++
//rayhutnerli
//2021/4/17


#ifndef CORE_H_
#define CORE_H_

#include "funenv.h"


void __stdcall CoroutineExec(FunEnvInfoPtr infoPtr);
void __stdcall CoroutineBreak(FunEnvInfoPtr infoPtr);
void __stdcall CoroutineEnd(FunEnvInfoPtr infoPtr);


#define COROUTINE_RUN(env) CoroutineExec(env);
#define COROUTINE_YIELD CoroutineBreak((FunEnvInfoPtr)pParam)
#define COROUTINE_END CoroutineEnd((FunEnvInfoPtr)pParam)


#endif
```
### 谈__declspec(naked) ，__stdcall，__cdecl关键字

看一下CoroutineExec的定义，里面引入了关键字
```C++
__declspec(naked) void __stdcall CoroutineExec(FunEnvInfoPtr infoPtr)
{
    // don't believe MSVC compile,it always assumes you use ebp
    // I've been cheated many times,rayhunterli
    // ex: mov eax, infoPtr
    // It may be translated incorrectly
    __asm
    {
        //...
    }
}
```
**__declspec(naked)** 表示告诉MSVC编译器，这个函数是我完全自定义，我有能力有信心处理好汇编级函数实现，编译生成obj时不要自动加任何汇编指令代码。包括push ebp;mov ebp,esp;这样保存调用栈,ret函数返回值这样的约定，都不要给我加。如果我漏写，可能是我故意的，也可能我真的书写bug，充分信任我，出问题自己负责。

小提示：我多次遇到__declspec(naked)如果不写最原生的汇编，结合带有C++语言结合汇编，它经常会出现错误的释译机器指令，一定要反汇编看看，并去掉符号。它默认你是保存栈帧的，实际上有时我不保存，我觉得我的需求不用。

**__stdcall** 表示函数调用规则，用栈传递参数，从右向左传递参数，由**被调用者自己清理栈内存**， 我这里主要用到这点，因为有了这点，我们可以保证函数返回EIP不是平衡栈内存的指令，看起来干净些，不过这也无所谓，只要维护好栈，一样的。

**__cdecl**和__stdcall基本相同，只是栈的清理是刚好相反，__cdecl是调用者自己清理，这也是普通代码默认的。

### 拆解协程函数的调用与重入设计
下面正式进入关键函数的分析，一点一点，详细分析,主要完成对协程函数的调用，分为第一次和非第一次调用，第一次调用比较简单，非第一次涉及前面说的模拟函数调用过程，我拆成了6个小步。

><u><small>（关于汇编书写先做一点歉意：昨天我写这个汇编时间，对于函数参数FunEnvInfo的成员访问，我本来写成的更规范的方法，假设参数infoPtr我已经放到eax寄存器，比如访问[eax+48]，就是访问infoPtr->pfCallFun,我直接用的+48偏移，理论应该用更标准易读的形式[eax]FunEnvInfo.pfCallFun; 比如下面指令cmp [eax], 0，我应该写成cmp [eax]FunEnvInfo.reEIP, 0有更好易读性，大家下载源码后，如果研究，可以要对照FunEnvInfo结构体偏移处理了，非常抱歉。主要当时我nake函数，没有处理ebp，有时微软翻译会不符合我的想法，当时直接用数字了。现在版本已经修正）</u></small>~~

```nasm
// compare last save eip empty
mov eax, [esp + 4]
cmp [eax]FunEnvInfo.reEIP, 0

// protect some use regs
push ebp
push esi
push edi
push ecx
push ebx

// compare
je FIRSTEXCFUNCTION
```

根据前面提到内容汇编**call __stdcall**函数原理，esp保存的是函数返回的EIP值，esp+4保存的就是函数参数，本文就FunEnvInfoPtr infoPtr，是我们协程的工作环境。下面一条就是比较infoPtr->reEIP是否为0，因为reEIP偏移值为零，所以直接[eax]就可以。根据我们的设计如果为0，说明这个协程是第一次执行，跳转到FIRSTEXCFUNCTION标签地址，不为0，说明这个协程并不是第一次执行。

下面的push 几个寄存器，实际就是为了保护父函数这些寄存器的值，因为我们CoroutineExec和里面子函数调用，会修改这几个寄存器值，如果不保护，退出时，就可能直接修改掉了父函数的，这是不正确的设计。

#### 首调协程函数

```nasm
FIRSTEXCFUNCTION:
// prepare function param and function addresss
push eax
mov eax, [eax]FunEnvInfo.pfCallFun

// call the function
call eax
```

这里代码比较简单，先用push传参infoPtr，然后根据结构体偏移，取出协程函数infoPtr->pfCallFun，然后直接call,完成第一次对协程函数的调用，这段汇编代码相当于C++中
```C++
(*infoPtr->pfCallFun)(infoPtr);
```
#### 重入协程函数6步走

下面也就是我们进入非第一次进入协程函数的设计，前面我们是直接call的infoPtr->pfCallFun，现在不同了，我们有函数的执行环境，我们有它的栈内存，它的寄存器，我们要模拟这个过程。我拆解为6步，一步一步的分解。

1. 第一步我们准备函数的返回值地址EIP，也就是说这个协程函数执行结束后我们让这个函数退到那里，当然是退到同正常call结束的地方，那么我们需要push一个eip值到栈里面，好解决，汇编中设记一个标签EXEC_RET就可以了。整个过程就模拟传参数，然给返回地址，返回地址就用标签EXEC_RET取一下放到ecx中，ok我们已经做好了call的过程。
```nasm
// here,we jump the fuction again
// 1. prepare function param and return address
push eax
mov ecx, dword ptr[EXEC_RET]
push ecx
```

2. 正常函数调用都要保存调用栈，也就是保存ebp数据，这个简单，毕竞我们知道调用约定的流程，按照标准的流程处理即可
```nasm
// 2.setup ebp
push ebp
mov ebp, esp
```
3. 准备完调用栈，我们就要准备调用栈了，毕竞一个函数需要多少栈内存，编译器是知道的，我们这里怎么办，没关系，我们第一次的时间，已经保存了这个大小，直接取infoPtr->iStackSize的值就可以了，然后将栈内存向后移动就这个数值就可以了。
```nasm
// 3.calculate stack size
sub esp, [eax]FunEnvInfo.iStackSize
``` 
4. 准备好栈内存，我们需要将上次的栈内存拷贝回来就可了，拷贝比较简单，就是数据移动，对于汇编指令来说，设置好esi，edi，ecx就可以了，我们在前面已经备好了这三个寄存器，不用担心我们修改它了。
```nasm
// 4.copy memory
mov edi, esp
mov esi, [eax]FunEnvInfo.pStackMem
mov ecx, [eax]FunEnvInfo.iStackSize
rep movsb
```
5. 准备好栈内存，我们需要恢复原来协程的寄存器，这个也比较简单，就是数据移动回来，轻松完成。根据前面说的，我们做教程，暂时处理ebx,ecx,edx,esi,edi就可以了
```nasm
// 5.setup common regs
mov ebx, [eax]FunEnvInfo.reEBX
mov ecx, [eax]FunEnvInfo.reECX
mov edx, [eax]FunEnvInfo.reEDX
mov esi, [eax]FunEnvInfo.reESI
mov edi, [eax]FunEnvInfo.reEDI
```
6. 做好前面5步，我们万事具备，只欠东风，准备跳转到上次协程函数退出时EIP地址就可以了,经过这6步，基于我们对调用过程的理解，我们没有借助call指令，完全完成了模拟函数调用，且完成函数的现场的恢复。
```nasm
// 6.jum EIP
mov eax, [eax]FunEnvInfo.reEIP
jmp eax
```

做完第一次和非第一次的恢复，我们唯一没有处理的就是从协程函数返回的处理，这个也是前面提到EXEC_RET:标签的地方，看一下怎么处理。
```nasm
EXEC_RET :
// We have to balance the stack

// fucntion praam 
add esp, 4

// save change
pop ebx
pop ecx
pop edi
pop esi
pop ebp
// call address and param
ret 4;
```
我们解析一下，这几行汇编，第一步esp加4，是因为我们协程函数都规定有一个指针参数，是协程函数的执行环境，我们是push传入，那么我们要自然要平衡，加4就可以了。下面pop指令就平衡原来说的父函数的几个我们可能修改的寄存器，保证和开始的push是成对调用就可了。到于最后的ret 4;是因为我们是stdcall，除了返回地址外，我们还要平衡压入的参数，所以一定ret 4才能平衡。

到此我们就完成CoroutineExec的汇编解析，主要就是要能对函数调用的深入理解，理解透后，一切迎刃而解。

### 拆解协程函数保存上下文设计
当执行CoroutineBreak时，协程函数要中途退出，我们就要保存好函数的现场与下次要执行的EIP值，就算OK了。
```nasm
// save some parent function regs
// we must save ebx,edx,esi,edi
mov eax, [esp + 4]
mov [eax]FunEnvInfo.reESP, esp
mov [eax]FunEnvInfo.reEBP, ebp
mov [eax]FunEnvInfo.reEBX, ebx
mov [eax]FunEnvInfo.reECX, ecx
mov [eax]FunEnvInfo.reEDX, edx
mov [eax]FunEnvInfo.reESI, esi
mov [eax]FunEnvInfo.reEDI, edi

// use esi instead of eax,eax maybe use as function return value
mov esi, eax
```
我们将父函数的寄存器值保存到协程函数的寄存器对应的环境中，这些都是根据FunEnvInfo的偏移值，一一对应即可。因为后面我们要进行函数调用，占用eax，所以我们不能一直用eax来存infoPtr的指针，交换给esi即可。

```nasm
// get the eip and save
// it will be jump to eip when the function is called next time
mov ecx, [esp]
mov [esi]FunEnvInfo.reEIP, ecx
```
我们要获取EIP的值，根据call调用原则，这里我们也没有改写ESP的值，直接将esp所保存内容到到infoPtr->reEIP中即可，这一步完成EIP的保存。

完成了EIP和寄存器的保存，我们就要检测是不要保存栈内存，如果从来没有保存过栈内存，我们需要申请一块堆内存作为保存栈内存的副本，如果已经保存过栈内存，我们只需直接拷贝就可以了。
```nasm
// skip the function ret address and param temp and save esp
mov ecx, [esi]FunEnvInfo.reESP
// we must add 8,4 ret,4 param
add ecx, 8
mov [esi]FunEnvInfo.reESP, ecx

// calculate stack size of parent function and iStackSize
mov edx, [esi]FunEnvInfo.reEBP
sub edx,ecx
mov [esi]FunEnvInfo.iStackSize, edx
```
这里我解释一下，我们先是对esp进行加8操作，是因为对于__stdcall调用，我们的协程函数先是push参数，然后push了函数的返回值，所以我们加8表示完全退到父函数的栈内存中，紧接着就是ebp-esp，可以准计算出父函数栈内存的大小，并保存到infoPtr->iStackSize中。
```nasm
// if the parent function stack is greater than 0, 
// allocate memory to save
test edx,edx
jle ERROR_PARENT_STACK_SIZE

// If memory has been allocated, 
// it will not be allocated again
mov eax, [esi]FunEnvInfo.pStackMem
test eax, eax
jne HAS_ALLOCATED_MEMROY
```
接着我们就来判断infoPtr->iStackSize是否小于等于0，如果是，证明不需要栈内存，我们可以直接进行相关处理返回，如果不是，那么我们要对栈内存进行检测，查一下是否已经申请过堆内存，如果没有申请过，证明是第一次来到保存，不然就是非第一次，只需要保存栈内存就可以，而不需要申请堆内存来备份。
```nasm
// allocate memory
push edx
push ecx
push edx
call dword ptr[malloc]
add esp, 4
mov [esi]FunEnvInfo.pStackMem, eax
pop ecx
pop edx
```
这里是malloc申请堆内存来保存栈内存，内存的大小放入edx，申请后在eax，将eax放入到infoPtr->pStackMem即可。

到这里我们保存寄存器的值，保存了EIP，对于没有申请堆内存，我们也进行了堆内存的申请，那么就要内存的拷贝了，将协程函数的栈内存保存到申请的堆内存中。
```nasm
// memcpy the parent stack memory to new allocated memory
HAS_ALLOCATED_MEMROY:
push esi
mov edi, eax
mov esi, ecx
mov ecx, edx
rep movsb
pop esi
```

完成了这些我们也该进行协程函数的退出了
```nasm
// we need to jump back to the coroutineexec function
// not the parent function
ERROR_PARENT_STACK_SIZE:
// calculate parent stack size,
//skip the self function return address 4 bytes
mov eax, [esi]FunEnvInfo.iStackSize
add eax, 8
// balance the stack
add esp, eax

// get the caller's parent caller
pop ebp
ret
```

我们不需只从CoroutineBreak退出，我们直接跳到父函数的父数，就是coroutineexec中。那么也就将协程函数栈都跳过，再加上CoroutineBreak函数的参数与返回值，我们还要再加上8，那么我们再进行__cdecl返回，pop ebp与ret就可以回到了coroutineexec中，深入理解了这点，我们就充分完成对函数执行过程的控制。到此CoroutineBreak的拆解完成。

### 拆解协程函数退出设计
这个函数比较简单，协程函数退出时，如果申请过堆内存，将其释放就可以了，看一下完整个汇编代码，判断infoPtr->pStackMem是否为空，不为空，就free掉其内存，然后将FunEnvInfo的iStackSize，pStackMem，reEIP置为0，这里一定要用dword ptr修鉓，不然可能只是处理一个字节。
```C++
__declspec(naked) void __stdcall CoroutineEnd(FunEnvInfoPtr infoPtr)
{
    // don't believe MSVC compile,it always assumes you use ebp
    // I've been cheated many times,rayhunterli
    // ex: mov eax, infoPtr
    // It may be translated incorrectly
    __asm
    {
        // compare whether we need to delete the stack memory
        push esi
        mov esi, [esp + 8]
        mov eax, [esi]FunEnvInfo.pStackMem
        test eax, eax
        je DO_NOT_DELETE

        // delete the memory
        push edx
        push ecx
        push eax
        call dword ptr[free]
        add esp, 4
        pop ecx
        pop edx



        // clean up some function's FunEnvInfoPtr params
        // we must use dword ptr for number
        mov dword ptr[esi]FunEnvInfo.pStackMem, 0
        mov dword ptr[esi]FunEnvInfo.iStackSize, 0
        mov dword ptr[esi]FunEnvInfo.reEIP, 0

        DO_NOT_DELETE:
        pop esi
        ret 4;
    }
}
```

到此我们完成了win32下无独立栈的协程设计，相信看到这里的朋友肯定对此比较有兴趣，全部代码工程，前面已经提过，放到github中了。下面我们会介绍X64下独立栈内存的协程设计。

****

