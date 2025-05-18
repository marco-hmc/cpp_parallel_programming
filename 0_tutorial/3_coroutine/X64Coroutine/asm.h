#ifndef MYSIMPLECOROUTINE_ASM_H_
#define MYSIMPLECOROUTINE_ASM_H_

using corcontext = void*;
struct transfer {
    corcontext fcont;
    void* data;
};
using MakeFun = void (*)(transfer);

extern "C" transfer jump_fcontext(corcontext const to, void* vp);
extern "C" corcontext make_fcontext(void* sp, long long size, MakeFun fn);

#endif