[有栈与无栈协程DEMO](https://github.com/lishaojiang/talkcoroutine)

一个是的提取boost的fiber汇编切换代码有独立栈协程。

## X64平台下基于boost库的fiber独立栈协程

### 原理与C++代码的设计
有了前面的知识的铺垫，我们进行X64平台下有独立栈协程就容易多了，这个协程实际是基于boost的Fiber协程的汇编切换，但我又不想用全部。本想全面介绍boost的协程，发现本文篇副已经够长了。boost封装的比较好，毕况别人是经典，代码中用了很多模板，一下理解起来有点绕，下次深入文章再介绍吧，我们只取精华，完成我们独立栈协程即可。本例是在windows平台X64下，使用VS2019编译，注意VS2019的x64不支持内嵌汇编，可以支持纯汇编，但要做一点点设定，设定过程可以参考我前面的设文章。

[Visual Studio 2019 x64 C++ 编译与调用纯汇编](https://zhuanlan.zhihu.com/p/346726444)

有了C++的那个协程，我们先写个类似的函数testfun，函数遇到COROUTINE_YIELD能够返回并保存现场，再次调用时testfun时，能够恢复现场，接着执行，对于本例，也就是局部变量actemp[512]的值要能正确的恢复，正确的打印。

```C++
void testfun(transfer t)
{
    int actemp[512] = { 0 };
    actemp[100] = 100;
    std::cout << "testfun:run point 1->a100:" << actemp[100] << std::endl;
    actemp[100] = 22;
    COROUTINE_YIELD;
    std::cout << "testfun:run point 2->a100:" << actemp[100] << std::endl;
    COROUTINE_YIELD;
    actemp[100] = 2111;
    std::cout << "testfun:run point 3->a100:" << actemp[100] << std::endl;
    actemp[100] = 27222;
    COROUTINE_YIELD;
    std::cout << "testfun:run end->a100:" << actemp[100] << std::endl;
    COROUTINE_END;
}
```
同理我们也要对协程的执行环境进行设计，但这次就有点抽像了，或者说毕竞是封装过一层了。
```C++
typedef void (*coroutinefunc)(transfer);

struct FuncRecord
{
    FuncRecord() :
        pkFunc(0),
        iFuncIndex(0),
        pkStackMem(nullptr),
        iStatckSize(0),
        iUseStatckSize(0),
        pkUseStack(nullptr),
        pkCorContext(nullptr) {}
    FuncRecord(coroutinefunc _pf,size_t _iFuncIndex,
            void* _pkMem,size_t _iSize) :
        pkFunc(_pf),
        iFuncIndex(_iFuncIndex),
        pkStackMem(_pkMem),
        iStatckSize(_iSize),
        iUseStatckSize(0),
        pkUseStack(nullptr),
        pkCorContext(nullptr) {}

    void* pkStackMem; // sp memory
    size_t iStatckSize; // sp size
    coroutinefunc pkFunc;
    size_t iFuncIndex;
    void* pkUseStack;
    size_t iUseStatckSize; // use size
    corcontext pkCorContext;
};
```
我来解释一下成员的意义，pkStackMem表示栈内存，但这个栈内存是独立栈，也就协程的执行环境会切到这个栈中，而不像非独立栈协程，只是保存副本，这里没有副本这个概念了。iStatckSize表示这个栈内存的大小。pkFunc表示协程函数，用来注册的协程函数。pkUseStack表示使用栈内存地址，iUseStatckSize表示使用栈内存大小，pkCorContext表示协程的上下文地址，基于boost库的，这个需要后面读汇编。

我们的原理：**每当注册一个协程函数时，创建一块堆内存，将这块堆内存做为协程的独立内存，目前而言也就是要浪费点内存换来独立，稍后字节对齐后，我们记录一下FuncRecord信信息，将在这块内存上创立协程执行的环境，这块内存保存协程运行时上下文和RIP，和线程的主环境进行动态切换，满足协程的切换与交互，这一切封装的更彻底。**

我们看一下和boost的汇编函数交互声明，我并没有取boost全部汇编,他们为了不同的平台设计太多了，实际我们只是windows x64平台，只用jump_fcontext与make_fcontext两个纯汇编函数就够了，它们分别位于JumpContext.asm和MakeContext.asm中。我们用extern "C"声明出来，只是为了方便其它地方调用，准备函数的调用方式方法，同时告编译器我们存在这个签名的函数，编译时请放心，链接时按照这个格式链接就可以了。
```C++
// asm.h
// just for asm code function declare
// create by rayhunterli
// 2021/4/5

#ifndef MYSIMPLECOROUTINE_ASM_H_
#define MYSIMPLECOROUTINE_ASM_H_


typedef void* corcontext;
struct transfer
{
    corcontext fcont;
    void* data;
};
typedef void (*MakeFun)(transfer);

extern "C" transfer jump_fcontext(corcontext const to ,void* vp);
extern "C" corcontext make_fcontext(void* sp,long long size,MakeFun fn);


#endif
```
我们再看一下协程调度的声明，基本等于win32的设定，也是有个容器来保存注册的函数，有了这些我们下面可以仔细讲一下汇编层面的设计，毕况C++层面的都比较简单，没有什么难度。
```C++
class coroutine
{
public:
    coroutine();
    ~coroutine();
    void UpdateFun();

    void RegeisterFun(coroutinefunc pf);
    void EndFuncInstance(transfer t);
    void BreakFuncInstance(transfer t);

    const bool IsEmpty() const { return m_kFunRecordMap.size() == 0; }

private:
    char* AllocMemory(size_t iSize);
private:
    const int m_iDefaultStackMemorySize;
    size_t m_iCurrentFuncIndex;
    size_t m_iCreateFuncIndex;
    std::map<size_t,FuncRecord> m_kFunRecordMap;
public:
    
};

extern coroutine g_kFunMgr;

#define COROUTINE_YIELD g_kFunMgr.BreakFuncInstance(t)
#define COROUTINE_END g_kFunMgr.EndFuncInstance(t)
```
不过我们先看一下怎么注册协程函数的
```C++
void coroutine::RegeisterFun(coroutinefunc pf)
{
    size_t iStackSize = m_iDefaultStackMemorySize;
    char* pkMem = AllocMemory(iStackSize);
    if (pkMem == nullptr)
    {
        return;
    }

    FuncRecord rec(pf, ++m_iCreateFuncIndex, (void*)pkMem, iStackSize);

    char* pkMemTop = pkMem + iStackSize;
    void* pkStackTop = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pkMemTop) \
        & ~static_cast<uintptr_t>(0xff));
    pkStackTop = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(pkStackTop) \
        - static_cast<uintptr_t>(64));
    size_t size = (reinterpret_cast<uintptr_t>(pkStackTop) - reinterpret_cast<uintptr_t>(pkMem));

    rec.pkUseStack = pkStackTop;
    rec.iUseStatckSize = size;
    rec.pkCorContext = make_fcontext(pkStackTop, size, pf);

    m_kFunRecordMap[m_iCreateFuncIndex] = rec;
}
```
从上面代码可以看出，先是直接分配内存，m_iDefaultStackMemorySize是128kb，那么本例每个协程先分配128kb，至于不够用需要动态规划，暂时不在我们第一期讨论范围之内。然后进行FuncRecord记录，再后面进行字节对齐操作，在调用make_fcontext这个汇编函数后，放入map容器中，也没有太多复杂的。

至于运行主要就靠jump_fcontext进行上下文切换，我们看一下C++代码，只不过是BreakFuncInstance保存返回值，EndFuncInstance没有保存返回值，并将协程函数的上下文设置为空，表示结束。
```C++
void coroutine::BreakFuncInstance(transfer t)
{	
    // change from  virtual stack context to main context
    auto iter = m_kFunRecordMap.find(m_iCurrentFuncIndex);
    if (iter != m_kFunRecordMap.end())
    {
        // t.fcont is main context,
        // but the iter->second.pkCorContext is coroutine context
        iter->second.pkCorContext = jump_fcontext(t.fcont, nullptr).fcont;
    }
    
}

void coroutine::EndFuncInstance(transfer t)
{
    // it needs exit the coroutine function context
    auto iter = m_kFunRecordMap.find(m_iCurrentFuncIndex);
    if (iter != m_kFunRecordMap.end())
    {
        iter->second.pkCorContext = nullptr;
        jump_fcontext(t.fcont, nullptr);
    }
}
```

接下来就是到了硬核的汇编层面分析了，一定要静下心，看boost的巧妙设计。

### 拆解make_fcontext创建协程上下文的设计

先看一下boost对x64下寄存器的规划，可以看出看出，用了0x150h字节来保存运行的上下文，其中包括了XMM寄存器，通用寄存器，以及函数的参数，返回地址，以及windows平台规定的fiber相关操作。

![4](https://github.com/lishaojiang/talkcoroutine/blob/main/raw/4.png)


继续说make_fcontext的内容，我加了详细的中文注释。

```nasm
; standard C library function
EXTERN  _exit:PROC
.code

;函数原型是
;extern "C" BOOST_CONTEXT_DECL
;fcontext_t BOOST_CONTEXT_CALLDECL make_fcontext( \
; void * sp, std::size_t size, void (* fn)( transfer_t) );
;有三个参数，第一个栈顶指针，栈的大小，函数指针，分别对应rcx，rdx，r8
;其中fcontext_t就是void*,transfer_t是两个void*的结构体，void* context，void* data;

; generate function table entry in .pdata and unwind information in
make_fcontext PROC EXPORT FRAME
    ; .xdata for a function's structured exception handling unwind behavior
    .endprolog

    ; first arg of make_fcontext() == top of context-stack
    ; 将栈顶保存到rax中，栈顶这里是高内存
    mov  rax, rcx

    ; shift address in RAX to lower 16 byte boundary
    ; == pointer to fcontext_t and address of context stack
    ; 将栈顶进行16对齐，位置减小一点，实际栈空间向下指一点
    and  rax, -16

    ; reserve space for context-data on context-stack
    ; on context-function entry: (RSP -0x8) % 16 == 0
    ;向下偏150H的内存，实际就原来的栈空间，往下指150H，这个150H的映射关系
    ;上面表里面都有，这个地址是本次函数栈空间的结束，后面都用+操作
    sub  rax, 0150h

    ; third arg of make_fcontext() == address of context-function
    ; stored in RBX
    ; 将函数指针r8传入栈内存+0x100H中，这里保留给rbx了
    mov  [rax+0100h], r8

    ; first arg of make_fcontext() == top of context-stack
    ; save top address of context stack as 'base'
    ; 将第一个参数*sp rcx，表示栈顶的内存，传入栈内存+0xc8H中，这里保存base
    mov  [rax+0c8h], rcx
    
    ; second arg of make_fcontext() == size of context-stack
    ; negate stack size for LEA instruction (== substraction)
    ; 将第二个参数size rdx，表示栈可以用的大小，进行取反
    neg  rdx
    
    ; compute bottom address of context stack (limit)
    ; size进行取反后，减计算，可以得出栈底的位置，也就最低地址的大小，
    ; 地址是小的，不能越界
    lea  rcx, [rcx+rdx]
    
    ; save bottom address of context stack as 'limit'
    ; 将最低地址的大小，放入到栈内存+0c0H中
    mov  [rax+0c0h], rcx
    
    ; save address of context stack limit as 'dealloction stack'
    ; 由上面计算可以知，再次备份一下，这个地址也是将重新分配的地址大小，放到0xb8中
    mov  [rax+0b8h], rcx
    
    ; set fiber-storage to zero
    ; 清空rcx，放到0xb0中
    xor  rcx, rcx
    mov  [rax+0b0h], rcx

    ; save MMX control- and status-word
    ;保存mmx的标识与控制状态
    stmxcsr  [rax+0a0h]
    ; save x87 control-word
    fnstcw  [rax+0a4h]

    ; compute address of transport_t
    ; 计算出回调函数参数transport_t的地址，也就是本函数栈底内存加上140H
    lea rcx, [rax+0140h]
    ; store address of transport_t in hidden field
    ; 将其存放到本函数栈底内存加上110H
    mov [rax+0110h], rcx

    ; compute abs address of label trampoline
    ; 计算编译后trampoline的地址，放到rcx中
    lea  rcx, trampoline
    ; save address of trampoline as return-address for context-function
    ; will be entered after calling jump_fcontext() first time
    ; 将这个地址放到本函数栈底内存加上118h中，作eip，后面会用这个来跳转
    mov  [rax+0118h], rcx

    ; compute abs address of label finish
    ; 计算编译后finish的地址，放到rcx中
    lea  rcx, finish
    ; save address of finish as return-address for context-function in RBP
    ; will be entered after context-function returns 
    ; 将这个地址放到本函数栈底内存加上108h中，作eip，这里是rbp中，这里会退出
    ; 如果函数执行完，没有走清理，根据调用规则trampoline的push rbp，ret地址时
    ; 会到finish地址，也就是强制退出了
    mov  [rax+0108h], rcx

    ; 退中本函数调用
    ret ; return pointer to context-data

trampoline:
    ; store return address on stack
    ; fix stack alignment
    ; 保护rpb，并跳到rbx中，rbp由上面分析暂时完全退中标识
    ; rbx由上面分析可以知就是r8地址
    push rbp
    ; jump to context-function
    jmp rbx

finish:
    ; exit code is zero
    xor  rcx, rcx
    ; exit application
    call  _exit
    hlt
make_fcontext ENDP
END
```
本函数主要作用，rcx传新的堆内存作协程的栈内存，rdx传堆内存大小，r8协程函数，规定要的 **void (* fn)( transfer_t)**格式协程函数。将传入的堆内存进行模拟压栈操作，向下分配0x150H字节进行和表中关系映射。最重要是下面两句

```nasm
    lea  rcx, trampoline
    ; save address of trampoline as return-address for context-function
    ; will be entered after calling jump_fcontext() first time
    ; 将这个地址放到本函数栈底内存加上118h中，作eip，后面会用这个来跳转
    mov  [rax+0118h], rcx
```
取出标签trampoline的地址，并放入0x118h偏移的EIP中，下次再调用这个栈时，会巧秒的用到这个118h的EIP。接着看一下trampoline的内容。
```nasm
trampoline:
    ; store return address on stack
    ; fix stack alignment
    ; 保护rpb，并跳到rbx中，rbp由上面分析暂时完全退中标识
    ; rbx由上面分析可以知就是r8地址
    push rbp
    ; jump to context-function
    jmp rbx

finish:
    ; exit code is zero
    xor  rcx, rcx
    ; exit application
    call  _exit
    hlt
```
可以看出就是先是pop出rbp，然后强行跳转到rbx对应的值中，所以关键是什么存放到rbx中，对于第一次来说，就是r8的值，也就函数的地址，设计的非常巧妙，如果不是第一次，我们就要看jump_fcontext函数，将什么存放rbx中。

随便说一下，这里rbp存放提finish的地址，如果不合法，程序ret返回时，会直接到finish标签下，直接调用系统调用函数exit，退出程序。

我作了一副图

![5](https://github.com/lishaojiang/talkcoroutine/blob/main/raw/5.png)

### 拆解jump_fcontext切换协程上下文的设计

在分析之前，我们先看一下，这个汇编函数的具体内容，我已经加了详细的中文注释。
```nasm
;保存当前的栈和寄存器与调用时rip（rsp+118h）（隐藏的）
;将jump的返回值transform地址写入到栈+110h地址中
;将当前栈转到transform。context，传入的r8转到transform。data

;将rdx的对应的栈切入，rsp
;将rdx栈的对应rip换入rsp，并到r10中，那么栈内存更大一级
;将上述的transform当参数，转给新函数地址
;无条件跳转过去执行
;将rdx栈对应110h地址，作为rax返回地址，返回的结果是传参用的transform

;所以jump函数，切到新context并跳转过去执行，保存老的栈并将老的栈地址-118h返回


.code

jump_fcontext PROC EXPORT FRAME
    .endprolog

    ; prepare stack
    ; 分配118H字节栈内存
    ;太重要了，就是118h完成rip交换
    lea rsp, [rsp-0118h]

    ;保存XMM寄存器相关操作
IFNDEF BOOST_USE_TSX
    ; save XMM storage
    movaps  [rsp], xmm6
    movaps  [rsp+010h], xmm7
    movaps  [rsp+020h], xmm8
    movaps  [rsp+030h], xmm9
    movaps  [rsp+040h], xmm10
    movaps  [rsp+050h], xmm11
    movaps  [rsp+060h], xmm12
    movaps  [rsp+070h], xmm13
    movaps  [rsp+080h], xmm14
    movaps  [rsp+090h], xmm15
    ; save MMX control- and status-word
    stmxcsr  [rsp+0a0h]
    ; save x87 control-word
    fnstcw  [rsp+0a4h]
ENDIF

    ; load NT_TIB
    ; 获取线程的TEB数据，MS x64约定的
    mov  r10,  gs:[030h]
    ; save fiber local storage
    ; 保存纤程大小数据，并放到rsp+b0H中，MS x64约定
    mov  rax, [r10+020h]
    mov  [rsp+0b0h], rax
    ; save current deallocation stack
    ; 保存当前线程分配栈的地址，并放到rsp+b8H中，MS x64约定
    mov  rax, [r10+01478h]
    mov  [rsp+0b8h], rax
    ; save current stack limit
    ; 保存当前线程分配栈的低地址，并放到rsp+c0H中，MS x64约定
    mov  rax, [r10+010h]
    mov  [rsp+0c0h], rax
    ; 保存当前线程分配栈的高地址，并放到rsp+c8H中，MS x64约定
    ; save current stack base
    mov  rax,  [r10+08h]
    mov  [rsp+0c8h], rax

    ; 保存常规寄存器
    mov [rsp+0d0h], r12  ; save R12
    mov [rsp+0d8h], r13  ; save R13
    mov [rsp+0e0h], r14  ; save R14
    mov [rsp+0e8h], r15  ; save R15
    mov [rsp+0f0h], rdi  ; save RDI
    mov [rsp+0f8h], rsi  ; save RSI
    mov [rsp+0100h], rbx  ; save RBX
    mov [rsp+0108h], rbp  ; save RBP

    ; rcx是特殊的，参传进来的，表示返回值
    mov [rsp+0110h], rcx  ; save hidden address of transport_t

    ; preserve RSP (pointing to context-data) in R9
    ; 昨时保存rsp到r9中
    mov  r9, rsp

    ; restore RSP (pointing to context-data) from RDX
    ; rdx是特殊的，参传进来的，这里是上次用到栈内存
    mov  rsp, rdx

    ;从上次的MM寄存器相关操作恢复
IFNDEF BOOST_USE_TSX
    ; restore XMM storage
    movaps  xmm6, [rsp]
    movaps  xmm7, [rsp+010h]
    movaps  xmm8, [rsp+020h]
    movaps  xmm9, [rsp+030h]
    movaps  xmm10, [rsp+040h]
    movaps  xmm11, [rsp+050h]
    movaps  xmm12, [rsp+060h]
    movaps  xmm13, [rsp+070h]
    movaps  xmm14, [rsp+080h]
    movaps  xmm15, [rsp+090h]
    ; restore MMX control- and status-word
    ldmxcsr  [rsp+0a0h]
    ; save x87 control-word
    fldcw   [rsp+0a4h]
ENDIF

    ; 同样从上次栈中恢复前程数据
    ; load NT_TIB
    mov  r10,  gs:[030h]
    ; restore fiber local storage
    mov  rax, [rsp+0b0h]
    mov  [r10+020h], rax
    ; restore current deallocation stack
    mov  rax, [rsp+0b8h]
    mov  [r10+01478h], rax
    ; restore current stack limit
    mov  rax, [rsp+0c0h]
    mov  [r10+010h], rax
    ; restore current stack base
    mov  rax, [rsp+0c8h]
    mov  [r10+08h], rax

    ; 恢复常规寄存器
    mov r12, [rsp+0d0h]  ; restore R12
    mov r13, [rsp+0d8h]  ; restore R13
    mov r14, [rsp+0e0h]  ; restore R14
    mov r15, [rsp+0e8h]  ; restore R15
    mov rdi, [rsp+0f0h]  ; restore RDI
    mov rsi, [rsp+0f8h]  ; restore RSI
    mov rbx, [rsp+0100h]  ; restore RBX --------------very important,will jump here
    mov rbp, [rsp+0108h]  ; restore RBP

    ; 恢复110的数据
    mov rax, [rsp+0110h] ; restore hidden address of transport_t

    ; prepare stack
    ; 将上次的EIP内存地址放到rsp寄存器中，very nice
    lea rsp, [rsp+0118h]

    ; load return-address
    ;这里将rip间接的传到到r10中，并rsp+8
    pop  r10

    ; transport_t returned in RAX
    ; return parent fcontext_t
    ;将r9，r8保存到【rax】对应的内存中
    ;r9是切换前的栈内存，r8是传入第二个参数所指内存
    mov  [rax], r9
    ; return data
    mov  [rax+08h], r8

    ; transport_t as 1.arg of context-function
    ; 将rax转为参数rcx
    mov  rcx,  rax

    ; indirect jump to context
    ;跳转到r10执行代码，这时间r10实际是指向make_fcontext汇编的trampoline指令地址
    ;也就是push rbp，jmp rbx
    jmp  r10
jump_fcontext ENDP
END
```
这段汇编非常巧秒的完成上下文的切换，其实主要作用功能如果下。

![6](https://github.com/lishaojiang/talkcoroutine/blob/main/raw/6.png)

可以看出RIP的交换主要通过前面说的0x118h,并不像我们自己设计的那么直白，带有一定的隐藏性。
```nasm
    ; prepare stack
    ; 将上次的EIP内存地址放到rsp寄存器中，very nice
    lea rsp, [rsp+0118h]

    ; load return-address
    ;这里将rip间接的传到到r10中，并rsp+8
    pop  r10
```
实际汇编的第一句，根据call的调用原则，rsp实际就是指向函数返回的EIP，但他并不是直接保存，来了减118h，然后下次调用是加上118h，然后pop r10，多么巧的设计。
```nasm
    ; prepare stack
    ; 分配118H字节栈内存
    ;太重要了，就是118h完成rip交换
    lea rsp, [rsp-0118h]
```
****

总结：
相信认真读到这里的，也理解了，根据GitHub的示例代码，再调试调试，一定有能彻底理解透。因为代码是demo代码，可能有一些我暂未发现的问题，可以提出一起讨论。本文就到这结束吧，下次可以讲一下高级点的用法。