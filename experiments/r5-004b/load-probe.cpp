#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

// A loader-only observation. No target instruction, Snapshot or CPU write is
// needed to learn the main image base from CREATE_PROCESS_DEBUG_EVENT.
static unsigned long long observe(const wchar_t* exe) {
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    std::wstring command=L"\""+std::wstring(exe)+L"\"";
    if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,
        DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) {
        std::printf("{\"api\":\"CreateProcessW\",\"error\":%lu}\n",GetLastError());
        throw std::runtime_error("create failed");
    }
    unsigned long long base=0; bool exited=false; bool killed=false;
    const auto deadline=GetTickCount64()+10000;
    while(GetTickCount64()<deadline) {
        DEBUG_EVENT ev{};
        if(!WaitForDebugEvent(&ev,100))continue;
        if(ev.dwProcessId!=pi.dwProcessId)throw std::runtime_error("foreign event");
        if(ev.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
            base=reinterpret_cast<unsigned long long>(ev.u.CreateProcessInfo.lpBaseOfImage);
            if(ev.u.CreateProcessInfo.hFile)CloseHandle(ev.u.CreateProcessInfo.hFile);
            // Never let the tested executable run business initialization.
            killed=TerminateProcess(pi.hProcess,0xE5004B01)!=FALSE;
        }
        if(ev.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && ev.u.LoadDll.hFile)CloseHandle(ev.u.LoadDll.hFile);
        const bool end=ev.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;
        if(!ContinueDebugEvent(ev.dwProcessId,ev.dwThreadId,DBG_CONTINUE))break;
        if(end){exited=true;break;}
    }
    if(!exited){TerminateProcess(pi.hProcess,0xE5004B02);WaitForSingleObject(pi.hProcess,3000);}
    const auto signed_out=WaitForSingleObject(pi.hProcess,3000)==WAIT_OBJECT_0;
    std::printf("{\"pid\":%lu,\"imageBase\":%llu,\"terminatedBeforeEntry\":%s,\"exitEventContinued\":%s,\"signedOut\":%s}\n",
        pi.dwProcessId,base,killed?"true":"false",exited?"true":"false",signed_out?"true":"false");
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    if(!base||!killed||!exited||!signed_out)throw std::runtime_error("loader cleanup incomplete");
    return base;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3)throw std::runtime_error("load-probe source-exe byte-identical-copy-exe");
        const auto a=observe(argv[1]);const auto b=observe(argv[2]);
        std::printf("{\"differentActualBases\":%s,\"sourceBase\":%llu,\"targetBase\":%llu}\n",a!=b?"true":"false",a,b);
        return a==b?2:0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
