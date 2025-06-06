#include <iostream>
#include "coroutine.h"

void testfun(transfer t) {
    int actemp[512] = {0};
    actemp[100] = 100;
    std::cout << "testfun:run point 1->a100:" << actemp[100] << '\n';
    actemp[100] = 22;
    COROUTINE_YIELD;
    std::cout << "testfun:run point 2->a100:" << actemp[100] << '\n';
    COROUTINE_YIELD;
    actemp[100] = 2111;
    std::cout << "testfun:run point 3->a100:" << actemp[100] << '\n';
    actemp[100] = 27222;
    COROUTINE_YIELD;
    std::cout << "testfun:run end->a100:" << actemp[100] << '\n';
    COROUTINE_END;
}

int main(int argc, const char* argv[]) {
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetDebugFillThreshold(0);
#endif

    std::cout << "begin" << '\n';
    g_kFunMgr.RegeisterFun(testfun);

    int iMainFrame = 0;
    while (!g_kFunMgr.IsEmpty()) {
        std::cout << "FrameCout:" << ++iMainFrame << '\n';
        g_kFunMgr.UpdateFun();
    }

    return 0;
}