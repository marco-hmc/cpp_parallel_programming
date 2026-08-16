#include "fun.h"

#include <iostream>

#include "core.h"

void RunPrintfTest() {
    int i = 0;
    for (i = 0; i < 10; i++) {
        std::cout << "I am run,Times:" << i << '\n';
    }
}

void RunPrintfTestCanBreak(void* pParam) {
    int i = 0;
    int a = 100;
    for (i = 0; i < 10; i++) {
        a += 10;
        std::cout << "I am run,Times:" << i << ",the a is:" << a << '\n';
        COROUTINE_YIELD;
    }
    COROUTINE_END;
}