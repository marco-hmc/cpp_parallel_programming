;           Copyright Oliver Kowalke 2009.
;  Distributed under the Boost Software License, Version 1.0.
;     (See accompanying file LICENSE_1_0.txt or copy at
;           http://www.boost.org/LICENSE_1_0.txt)

section .text
global make_fcontext

make_fcontext:
    ; prepare stack
    lea rsp, [rsp-0118h]

    ; 保存XMM寄存器相关操作
%ifdef BOOST_USE_TSX
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
%endif

    ; load NT_TIB
    mov  r10,  gs:[030h]
    ; save fiber local storage
    mov  rax, [r10+020h]
    mov  [rsp+0b0h], rax
    ; save current deallocation stack
    mov  rax, [r10+01478h]
    mov  [rsp+0b8h], rax
    ; save current stack limit
    mov  rax, [r10+010h]
    mov  [rsp+0c0h], rax
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
    mov  r9, rsp

    ; restore RSP (pointing to context-data) from RDX
    mov  rsp, rdx

    ; 从上次的MM寄存器相关操作恢复
%ifdef BOOST_USE_TSX
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
%endif

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
    mov rbx, [rsp+0100h]  ; restore RBX
    mov rbp, [rsp+0108h]  ; restore RBP

    ; 恢复110的数据
    mov rax, [rsp+0110h] ; restore hidden address of transport_t

    ; prepare stack
    lea rsp, [rsp+0118h]

    ; load return-address
    pop  r10

    ; transport_t returned in RAX
    ; return parent fcontext_t
    mov  [rax], r9
    ; return data
    mov  [rax+08h], r8

    ; transport_t as 1.arg of context-function
    mov  rcx,  rax

    ; indirect jump to context
    jmp  r10