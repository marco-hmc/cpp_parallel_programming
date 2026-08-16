#ifndef CORE_H_
#define CORE_H_

#include "funenv.h"

void __attribute__((stdcall, naked)) CoroutineExec(FunEnvInfoPtr infoPtr);
void __attribute__((stdcall, naked)) CoroutineBreak(FunEnvInfoPtr infoPtr);
void __attribute__((stdcall, naked)) CoroutineEnd(FunEnvInfoPtr infoPtr);

#define COROUTINE_RUN(env) CoroutineExec(env)
#define COROUTINE_YIELD CoroutineBreak((FunEnvInfoPtr)pParam)
#define COROUTINE_END CoroutineEnd((FunEnvInfoPtr)pParam)

#endif