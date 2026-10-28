#pragma once
#include <windows.h>
#include <cstdint>
namespace r5004c {
struct Mark {LONG committed=0;unsigned stage=0;DWORD tid=0;std::uint64_t tick=0,rip=0,rsp=0,ssp=0;};
struct Probe {
    std::uint64_t magic=0x3150424f52504346ULL,checkpoint=0;
    DWORD pid=0,gui=0,worker=0;unsigned deferred=0,exception_worker=0;
    volatile LONG armed=0,used=0,failure=0;
    DWORD wait=WAIT_FAILED,wait_error=0;BOOL destroyed=FALSE;
    Mark marks[64]{};
};
inline void mark(Probe* c,unsigned stage,std::uint64_t rip=0,std::uint64_t rsp=0,std::uint64_t ssp=0){
    const auto n=InterlockedIncrement(&c->used)-1;
    if(n<0||n>=64){InterlockedExchange(&c->failure,ERROR_BUFFER_OVERFLOW);return;}
    auto& r=c->marks[n];r.stage=stage;r.tid=GetCurrentThreadId();r.tick=GetTickCount64();r.rip=rip;r.rsp=rsp;r.ssp=ssp;
    MemoryBarrier();InterlockedExchange(&r.committed,1);
}
}
