#include "coroutine.h"

#include "core.h"

CoroutineMgr g_kFunMgr;

void CoroutineMgr::RegeisterFun(RegCallFun pfn) {
    FunEnvInfoPtr EnvPtr = new FunEnvInfo;
    EnvPtr->pfCallFun = pfn;
    m_kFunList.push_back(EnvPtr);
}

void CoroutineMgr::UpdateFun() {
    auto iter = m_kFunList.begin();
    while (iter != m_kFunList.end()) {
        FunEnvInfoPtr env = *iter;
        COROUTINE_RUN(env);
        if (nullptr == env->pStackMem) {
            delete env;
            iter = m_kFunList.erase(iter);
        } else {
            iter++;
        }
    }
}

bool CoroutineMgr::IsEmpty() { return m_kFunList.empty(); }