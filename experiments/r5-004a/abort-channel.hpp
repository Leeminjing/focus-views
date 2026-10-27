#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
namespace r5004a {
constexpr DWORD abort_exit=0xE504A001;
constexpr std::uint64_t abort_magic=0x6134303554524f42;
struct AbortChannel {
    std::uint64_t magic=abort_magic,created=0,top=0,edit=0,button=0,checkpoint=0,alternate=0;
    DWORD pid=0,tid=0;
    volatile LONG installed=0,arm=0,done=0;
    DWORD error=0,observed_tid=0,exception=0;
    BOOL destroyed=FALSE;unsigned gone=0;
};
inline std::uint64_t process_created(HANDLE p){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(p,&c,&e,&k,&u))return 0;return(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
inline std::wstring abort_name(DWORD pid,DWORD tid,std::uint64_t created){return L"Local\\fv_r5004a_abort_"+std::to_wstring(pid)+L"_"+std::to_wstring(tid)+L"_"+std::to_wstring(created);}
}
