#ifndef COROUTINE_H_
#define COROUTINE_H_

#include <list>

#include "funenv.h"

class CoroutineMgr {
   public:
    void RegeisterFun(RegCallFun pfn);
    void UpdateFun();
    bool IsEmpty();

   protected:
    std::list<FunEnvInfoPtr> m_kFunList;
};

extern CoroutineMgr g_kFunMgr;

#endif