#include "core.h"

#include <stdlib.h>

void __attribute__((stdcall, naked)) CoroutineExec(FunEnvInfoPtr infoPtr) {
    asm volatile(
        // compare last save eip empty
        "movl 4(%%esp), %%eax\n\t"
        "cmpl $0, 0(%%eax)\n\t"

        // protect some use regs
        "pushl %%ebp\n\t"
        "pushl %%esi\n\t"
        "pushl %%edi\n\t"
        "pushl %%ecx\n\t"
        "pushl %%ebx\n\t"

        // compare
        "je FIRSTEXCFUNCTION\n\t"

        // here,we jump the function again

        // 1. prepare function param and return address
        "pushl %%eax\n\t"
        "movl $EXEC_RET, %%ecx\n\t"
        "pushl %%ecx\n\t"

        // 2.setup ebp
        "pushl %%ebp\n\t"
        "movl %%esp, %%ebp\n\t"

        // 3.calculate stack size
        "subl 0(%%eax), %%esp\n\t"

        // 4.copy memory
        "movl %%esp, %%edi\n\t"
        "movl 4(%%eax), %%esi\n\t"
        "movl 8(%%eax), %%ecx\n\t"
        "rep movsb\n\t"

        // 5.setup common regs
        "movl 12(%%eax), %%ebx\n\t"
        "movl 16(%%eax), %%ecx\n\t"
        "movl 20(%%eax), %%edx\n\t"
        "movl 24(%%eax), %%esi\n\t"
        "movl 28(%%eax), %%edi\n\t"

        // 6.jump EIP
        "movl 32(%%eax), %%eax\n\t"
        "jmp *%%eax\n\t"

        "FIRSTEXCFUNCTION:\n\t"
        // prepare function param and function address
        "pushl %%eax\n\t"
        "movl 36(%%eax), %%eax\n\t"

        // call the function
        "call *%%eax\n\t"

        "EXEC_RET:\n\t"
        // We have to balance the stack

        // 1.function param
        "addl $4, %%esp\n\t"

        // save change
        "popl %%ebx\n\t"
        "popl %%ecx\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebp\n\t"
        // call address and param
        "ret $4\n\t"
        :
        :
        : "memory");
}

void __attribute__((stdcall, naked)) CoroutineBreak(FunEnvInfoPtr infoPtr) {
    asm volatile(
        // save some parent function regs
        // we must save ebx,edx,esi,edi
        "movl 4(%%esp), %%eax\n\t"
        "movl %%esp, 0(%%eax)\n\t"
        "movl %%ebp, 4(%%eax)\n\t"
        "movl %%ebx, 8(%%eax)\n\t"
        "movl %%ecx, 12(%%eax)\n\t"
        "movl %%edx, 16(%%eax)\n\t"
        "movl %%esi, 20(%%eax)\n\t"
        "movl %%edi, 24(%%eax)\n\t"

        // use esi instead of eax,eax maybe use as function return value
        "movl %%eax, %%esi\n\t"

        // get the eip and save
        // it will be jump to eip when the function is called next time
        "movl (%%esp), %%ecx\n\t"
        "movl %%ecx, 28(%%esi)\n\t"

        // skip the function ret address and param temp and save esp
        "movl 0(%%esi), %%ecx\n\t"
        // we must add 8,4 ret,4 param
        "addl $8, %%ecx\n\t"
        "movl %%ecx, 0(%%esi)\n\t"

        // calculate stack size of parent function and iStackSize
        "movl 4(%%esi), %%edx\n\t"
        "subl %%ecx, %%edx\n\t"
        "movl %%edx, 8(%%esi)\n\t"

        // if the parent function stack is greater than 0, allocate memory to save
        "testl %%edx, %%edx\n\t"
        "jle ERROR_PARENT_STACK_SIZE\n\t"

        // If memory has been allocated, it will not be allocated again
        "movl 12(%%esi), %%eax\n\t"
        "testl %%eax, %%eax\n\t"
        "jne HAS_ALLOCATED_MEMORY\n\t"

        // allocate memory
        "pushl %%edx\n\t"
        "pushl %%ecx\n\t"
        "pushl %%edx\n\t"
        "call malloc\n\t"
        "addl $4, %%esp\n\t"
        "movl %%eax, 12(%%esi)\n\t"
        "popl %%ecx\n\t"
        "popl %%edx\n\t"

        // memcpy the parent stack memory to new allocated memory
        "HAS_ALLOCATED_MEMORY:\n\t"
        "pushl %%esi\n\t"
        "movl %%eax, %%edi\n\t"
        "movl %%ecx, %%esi\n\t"
        "movl %%edx, %%ecx\n\t"
        "rep movsb\n\t"
        "popl %%esi\n\t"

        // we need to jump back to the coroutineexec function
        // not the parent function
        "ERROR_PARENT_STACK_SIZE:\n\t"
        // calculate parent stack size,skip the self function return address 4 bytes
        "movl 8(%%esi), %%eax\n\t"
        "addl $8, %%eax\n\t"
        // balance the stack
        "addl %%eax, %%esp\n\t"

        // get the caller's parent caller
        "popl %%ebp\n\t"
        "ret\n\t"
        :
        :
        : "memory");
}

void __attribute__((stdcall, naked)) CoroutineEnd(FunEnvInfoPtr infoPtr) {
    asm volatile(
        // compare whether we need to delete the stack memory
        "pushl %%esi\n\t"
        "movl 8(%%esp), %%esi\n\t"
        "movl 0(%%esi), %%eax\n\t"
        "testl %%eax, %%eax\n\t"
        "je DO_NOT_DELETE\n\t"

        // delete the memory
        "pushl %%edx\n\t"
        "pushl %%ecx\n\t"
        "pushl %%eax\n\t"
        "call free\n\t"
        "addl $4, %%esp\n\t"
        "popl %%ecx\n\t"
        "popl %%edx\n\t"

        // clean up some function's FunEnvInfoPtr params
        // we must use dword ptr for number
        "movl $0, 0(%%esi)\n\t"
        "movl $0, 8(%%esi)\n\t"
        "movl $0, 28(%%esi)\n\t"

        "DO_NOT_DELETE:\n\t"
        "popl %%esi\n\t"
        "ret $4\n\t"
        :
        :
        : "memory");
}