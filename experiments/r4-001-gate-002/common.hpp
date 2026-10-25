#pragma once
#include "../r3/protocol.hpp"
#include "../r4-001-gate-001/launch-diagnostics.hpp"
#include <evntprov.h>
#include <objbase.h>
#include <psapi.h>
#include <map>
#include <set>
namespace gate002 {
using namespace r3;
inline constexpr GUID marker_guid={0x2db2457a,0x3c46,0x4e81,{0x93,0xf7,0x85,0x49,0x62,0x64,0xe2,0x30}};
inline constexpr GUID session_guid={0xc82ced1e,0x7ab8,0x4cad,{0xa1,0x89,0xd1,0xc0,0xb2,0x1e,0x17,0x44}};
inline constexpr GUID process_guid={0x22fb2cd6,0x0e7b,0x422b,{0xa0,0xc7,0x2f,0xad,0x1f,0xd0,0xe7,0x16}};
inline constexpr GUID memory_guid={0xd1d93ef7,0xe1f2,0x4f45,{0x99,0x43,0x03,0xd2,0x45,0xfe,0x6c,0x00}};
inline constexpr wchar_t session_name[]=L"focus-views-GATE-002-process-diagnostic";
inline ULONGLONG utc(){FILETIME ft{};GetSystemTimePreciseAsFileTime(&ft);ULARGE_INTEGER n{};n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return n.QuadPart;}
inline std::string guid_text(const GUID& value){wchar_t text[40]{};StringFromGUID2(value,text,40);return utf8(text);}
inline std::string resources(){
    DWORD handles=0;const auto h=GetProcessHandleCount(GetCurrentProcess(),&handles);const auto he=h?0:GetLastError();
    PROCESS_MEMORY_COUNTERS_EX vm{};vm.cb=sizeof(vm);const auto v=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&vm),sizeof(vm));const auto ve=v?0:GetLastError();
    MEMORYSTATUSEX m{sizeof(m)};const auto okay=GlobalMemoryStatusEx(&m);const auto me=okay?0:GetLastError();
    return "{\"handleCount\":"+std::to_string(handles)+",\"handleQueryError\":"+std::to_string(he)+",\"privateBytes\":"+std::to_string(vm.PrivateUsage)+",\"workingSet\":"+std::to_string(vm.WorkingSetSize)+",\"vmQueryError\":"+std::to_string(ve)+",\"availablePhysical\":"+std::to_string(m.ullAvailPhys)+",\"availableCommit\":"+std::to_string(m.ullAvailPageFile)+",\"memoryQueryError\":"+std::to_string(me)+"}";
}
struct Log {
    std::ofstream file;
    explicit Log(const std::filesystem::path& path):file(path,std::ios::binary){demand(file.good(),"new diagnostic log");}
    void emit(const char* type,const std::string& fields){LARGE_INTEGER q{};QueryPerformanceCounter(&q);file<<"{\"type\":"<<json(type)<<",\"utc\":"<<utc()<<",\"qpc\":"<<q.QuadPart<<','<<fields<<"}\n";file.flush();demand(file.good(),"diagnostic log write");}
};
inline std::wstring quote(const std::wstring& text){std::wstring result=L"\"";size_t n=0;for(auto c:text){if(c==L'\\'){++n;continue;}result.append(c==L'"'?2*n+1:n,L'\\');n=0;result+=c;}result.append(n*2,L'\\');return result+L'"';}
struct Marker {
    REGHANDLE handle=0;ULONG registered=0;
    Marker(){registered=EventRegister(&marker_guid,nullptr,nullptr,&handle);}
    ~Marker(){if(handle)EventUnregister(handle);}
    void write(unsigned sequence,const wchar_t* phase,DWORD error=0){if(handle){const auto text=L"GATE002 seq="+std::to_wstring(sequence)+L" phase="+phase+L" error="+std::to_wstring(error);EventWriteString(handle,4,1,text.c_str());}}
};
}
