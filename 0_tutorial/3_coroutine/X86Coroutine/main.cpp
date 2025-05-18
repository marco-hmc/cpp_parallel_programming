#include <iostream>

#include "coroutine.h"
#include "fun.h"

int main() {
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetDebugFillThreshold(0);
#endif

    std::cout << "Hello World!" << '\n';
    RunPrintfTest();

    int iMainFrame = 0;
    g_kFunMgr.RegeisterFun(RunPrintfTestCanBreak);
    while (!g_kFunMgr.IsEmpty()) {
        std::cout << "FrameCout:" << iMainFrame++ << '\n';
        g_kFunMgr.UpdateFun();
    }
    std::cout << "update FunMgr over!" << '\n';

    return 1;
}