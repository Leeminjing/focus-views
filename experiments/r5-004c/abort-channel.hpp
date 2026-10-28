#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
namespace r5004a {
constexpr DWORD abort_exit=0xE504A001;
constexpr std::uint64_t abort_magic=0x6334303554524f42;
struct Phase {volatile LONG committed=0;DWORD stage=0,tid=0;std::uint64_t tick=0,rip=0,rsp=0,ssp=0;};
struct Module {std::uint64_t base=0;DWORD error=0;wchar_t name[32]{},path[520]{};};
struct AbortChannel {
    std::uint64_t magic=abort_magic,created=0,thread_created=0,top=0,edit=0,button=0,checkpoint=0,alternate=0;
    DWORD pid=0,tid=0;
    volatile LONG installed=0,arm=0,done=0;
    DWORD error=0,observed_tid=0,exception=0;
    BOOL destroyed=FALSE;unsigned gone=0;
    std::uint64_t expected_rsp=0,expected_ssp=0;unsigned version=1,bytes=sizeof(AbortChannel),flow=1,synthetic_block=0;volatile LONG used=0;Phase phases[16]{};Module modules[8]{};
};
inline void phase(AbortChannel* c,DWORD stage,std::uint64_t rip=0,std::uint64_t rsp=0,std::uint64_t ssp=0){
    const auto slot=InterlockedIncrement(&c->used)-1;
    if(slot<0||slot>=16){c->error=ERROR_BUFFER_OVERFLOW;return;}
    auto& r=c->phases[slot];r.stage=stage;r.tid=GetCurrentThreadId();r.tick=GetTickCount64();r.rip=rip;r.rsp=rsp;r.ssp=ssp;
    MemoryBarrier();InterlockedExchange(&r.committed,1);
}
inline std::uint64_t process_created(HANDLE p){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(p,&c,&e,&k,&u))return 0;return(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
inline std::uint64_t thread_created(HANDLE t){FILETIME c{},e{},k{},u{};if(!GetThreadTimes(t,&c,&e,&k,&u))return 0;return(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
inline std::wstring abort_name(DWORD pid,DWORD tid,std::uint64_t created){return L"Local\\fv_r5004c_abort_"+std::to_wstring(pid)+L"_"+std::to_wstring(tid)+L"_"+std::to_wstring(created);}
}
