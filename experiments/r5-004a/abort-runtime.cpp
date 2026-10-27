#include "abort-channel.hpp"
#include <cstdio>
namespace {
thread_local r5004a::AbortChannel* channel=nullptr;
thread_local HANDLE mapping=nullptr;
thread_local HMODULE retained=nullptr;
thread_local bool attempted=false;
void failure(const char* api,DWORD error){char text[192]{};sprintf_s(text,"FV_ABORT_FAILURE|%s|%lu",api,error);OutputDebugStringA(text);}
bool owned(std::uint64_t value){DWORD pid=0;const auto tid=GetWindowThreadProcessId(reinterpret_cast<HWND>(value),&pid);return pid==GetCurrentProcessId()&&tid==GetCurrentThreadId();}
LONG CALLBACK abort_handler(PEXCEPTION_POINTERS exception){
    auto* c=channel;
    if(!c||!InterlockedCompareExchange(&c->arm,0,0)||c->pid!=GetCurrentProcessId()||c->tid!=GetCurrentThreadId())return EXCEPTION_CONTINUE_SEARCH;
    const auto address=reinterpret_cast<std::uint64_t>(exception->ExceptionRecord->ExceptionAddress);
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT||(address!=c->checkpoint&&address!=c->alternate))return EXCEPTION_CONTINUE_SEARCH;
    c->observed_tid=GetCurrentThreadId();c->exception=exception->ExceptionRecord->ExceptionCode;
    if(!owned(c->top)||!owned(c->edit)||!owned(c->button)||GetParent(reinterpret_cast<HWND>(c->edit))!=reinterpret_cast<HWND>(c->top)||GetParent(reinterpret_cast<HWND>(c->button))!=reinterpret_cast<HWND>(c->top))c->error=ERROR_INVALID_OWNER;
    else{
        c->destroyed=DestroyWindow(reinterpret_cast<HWND>(c->top));if(!c->destroyed)c->error=GetLastError();
        c->gone=(!owned(c->top)?1u:0u)|(!owned(c->edit)?2u:0u)|(!owned(c->button)?4u:0u);
        if(c->gone!=7&&!c->error)c->error=ERROR_BUSY;
    }
    MemoryBarrier();InterlockedExchange(&c->done,1);
    // No exception continuation, restored CONTEXT, or business RET. Windows
    // entered this handler on the target thread's own current exception path.
    ExitProcess(c->error?ERROR_PROCESS_ABORTED:r5004a::abort_exit);
}
}
extern "C" __declspec(dllexport) LRESULT CALLBACK CleanupGetMessage(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&wp==PM_REMOVE&&!attempted){
        attempted=true;
        const auto created=r5004a::process_created(GetCurrentProcess());if(!created){failure("GetProcessTimes.abort",GetLastError());return CallNextHookEx(nullptr,code,wp,lp);}const auto name=r5004a::abort_name(GetCurrentProcessId(),GetCurrentThreadId(),created);
        mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name.c_str());
        if(!mapping)failure("OpenFileMappingW.abort",GetLastError());
        if(mapping){
            channel=static_cast<r5004a::AbortChannel*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(r5004a::AbortChannel)));
            if(!channel){const auto error=GetLastError();failure("MapViewOfFile.abort",error);CloseHandle(mapping);mapping=nullptr;}
            else if(channel->magic!=r5004a::abort_magic||channel->pid!=GetCurrentProcessId()||channel->tid!=GetCurrentThreadId()||channel->created!=created||channel->thread_created!=r5004a::thread_created(GetCurrentThread())){failure("abort.channel_identity",ERROR_INVALID_OWNER);UnmapViewOfFile(channel);CloseHandle(mapping);channel=nullptr;mapping=nullptr;}
            else{
                if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&CleanupGetMessage),&retained)){channel->error=GetLastError();failure("GetModuleHandleExW.abort",channel->error);}
                else if(!AddVectoredExceptionHandler(1,abort_handler)){channel->error=ERROR_GEN_FAILURE;failure("AddVectoredExceptionHandler.null",ERROR_GEN_FAILURE);} // No documented specific Win32 error.
                MemoryBarrier();InterlockedExchange(&channel->installed,channel->error?-1:1);
                // The VEH remains registered until process exit. Keep the DLL
                // reference; unloading it while registered would be unsafe.
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
