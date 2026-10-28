#include "abort-channel.hpp"
#include <cstdio>
extern "C" std::uint64_t CleanupSsp();
extern "C" std::uint64_t CleanupRsp();
namespace {
thread_local r5004a::AbortChannel* channel=nullptr;
thread_local HANDLE mapping=nullptr;
thread_local HMODULE retained=nullptr;
thread_local bool attempted=false;
thread_local WNDPROC original_proc=nullptr;
thread_local HANDLE blocked_event=nullptr;
LRESULT CALLBACK block_proc(HWND w,UINT m,WPARAM a,LPARAM b){if(channel&&channel->synthetic_block&&channel->arm&&m==WM_DESTROY){r5004a::phase(channel,10);WaitForSingleObject(blocked_event,INFINITE);}return CallWindowProcW(original_proc,w,m,a,b);}
void failure(const char* api,DWORD error){char text[192]{};sprintf_s(text,"FV_ABORT_FAILURE|%s|%lu",api,error);OutputDebugStringA(text);}
bool owned(std::uint64_t value){DWORD pid=0;const auto tid=GetWindowThreadProcessId(reinterpret_cast<HWND>(value),&pid);return pid==GetCurrentProcessId()&&tid==GetCurrentThreadId();}
[[noreturn]] __declspec(noinline) void cleanup_entry(){
    auto* c=channel;
    if(!c||c->pid!=GetCurrentProcessId()||c->tid!=GetCurrentThreadId())ExitProcess(ERROR_INVALID_OWNER);
    r5004a::phase(c,5,reinterpret_cast<std::uint64_t>(&cleanup_entry),CleanupRsp(),CleanupSsp());
    if(!owned(c->top)||!owned(c->edit)||!owned(c->button)||GetParent(reinterpret_cast<HWND>(c->edit))!=reinterpret_cast<HWND>(c->top)||GetParent(reinterpret_cast<HWND>(c->button))!=reinterpret_cast<HWND>(c->top))c->error=ERROR_INVALID_OWNER;
    else{
        r5004a::phase(c,6);c->destroyed=DestroyWindow(reinterpret_cast<HWND>(c->top));const auto destroy_error=c->destroyed?0:GetLastError();r5004a::phase(c,7);if(!c->destroyed)c->error=destroy_error;
        c->gone=(!owned(c->top)?1u:0u)|(!owned(c->edit)?2u:0u)|(!owned(c->button)?4u:0u);
        if(c->gone!=7&&!c->error)c->error=ERROR_BUSY;
    }
    MemoryBarrier();r5004a::phase(c,8);InterlockedExchange(&c->done,1);
    // Disposal only: this entry never returns to the application. It neither
    // restores Snapshot state nor changes the protected return stack.
    ExitProcess(c->error?ERROR_PROCESS_ABORTED:r5004a::abort_exit);
}
LONG CALLBACK abort_handler(PEXCEPTION_POINTERS exception){
    auto* c=channel;
    if(!c||!InterlockedCompareExchange(&c->arm,0,0)||c->pid!=GetCurrentProcessId()||c->tid!=GetCurrentThreadId())return EXCEPTION_CONTINUE_SEARCH;
    const auto address=reinterpret_cast<std::uint64_t>(exception->ExceptionRecord->ExceptionAddress);
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT||(address!=c->checkpoint&&address!=c->alternate))return EXCEPTION_CONTINUE_SEARCH;
    const auto& cpu=*exception->ContextRecord;
    if(cpu.Rsp!=c->expected_rsp||cpu.R11!=c->expected_ssp||cpu.Rsp%16!=8||!c->expected_ssp){c->error=ERROR_INVALID_DATA;return EXCEPTION_CONTINUE_SEARCH;}
    c->observed_tid=GetCurrentThreadId();c->exception=exception->ExceptionRecord->ExceptionCode;r5004a::phase(c,2,cpu.Rip,cpu.Rsp,cpu.R11);
    if(!c->flow)cleanup_entry();
    // Supported exception continuation to an owned no-return disposal entry.
    // RSP, R11 and OS-managed CET state stay unchanged. Never a business RET.
    exception->ContextRecord->Rip=reinterpret_cast<DWORD64>(&cleanup_entry);
    r5004a::phase(c,3,exception->ContextRecord->Rip,exception->ContextRecord->Rsp,exception->ContextRecord->R11);
    return EXCEPTION_CONTINUE_EXECUTION;
}
LONG CALLBACK continue_handler(PEXCEPTION_POINTERS e){
    auto* c=channel;
    if(c&&c->flow&&c->arm&&c->observed_tid==GetCurrentThreadId()&&e->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT&&e->ContextRecord->Rip==reinterpret_cast<DWORD64>(&cleanup_entry))r5004a::phase(c,4,e->ContextRecord->Rip,e->ContextRecord->Rsp,e->ContextRecord->R11);
    return EXCEPTION_CONTINUE_SEARCH;
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
            else if(channel->magic!=r5004a::abort_magic||channel->version!=1||channel->bytes!=sizeof(*channel)||channel->flow>1||channel->pid!=GetCurrentProcessId()||channel->tid!=GetCurrentThreadId()||channel->created!=created||channel->thread_created!=r5004a::thread_created(GetCurrentThread())){failure("abort.channel_identity",ERROR_INVALID_OWNER);UnmapViewOfFile(channel);CloseHandle(mapping);channel=nullptr;mapping=nullptr;}
            else{
                if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&CleanupGetMessage),&retained)){channel->error=GetLastError();failure("GetModuleHandleExW.abort",channel->error);}
                else if(!AddVectoredExceptionHandler(1,abort_handler)){channel->error=ERROR_GEN_FAILURE;failure("AddVectoredExceptionHandler.null",ERROR_GEN_FAILURE);} // No documented specific Win32 error.
                if(!channel->error&&!AddVectoredContinueHandler(1,continue_handler)){channel->error=ERROR_GEN_FAILURE;failure("AddVectoredContinueHandler.null",ERROR_GEN_FAILURE);}
                if(!channel->error&&channel->synthetic_block){
                    blocked_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!blocked_event)channel->error=GetLastError();
                    if(blocked_event){SetLastError(0);original_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(reinterpret_cast<HWND>(channel->top),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&block_proc)));if(!original_proc)channel->error=GetLastError()?GetLastError():ERROR_INVALID_DATA;}
                }
                const wchar_t* names[]={L"ntdll.dll",L"kernel32.dll",L"user32.dll",L"gdi32.dll",L"imm32.dll",L"msctf.dll",L"combase.dll",L"abort-runtime.dll"};
                for(unsigned i=0;i<8;++i){auto& item=channel->modules[i];wcscpy_s(item.name,names[i]);const auto module=GetModuleHandleW(names[i]);item.base=reinterpret_cast<std::uint64_t>(module);if(!module){item.error=GetLastError();continue;}const auto size=GetModuleFileNameW(module,item.path,520);if(!size||size>=520){item.error=GetLastError();item.path[0]=0;}}
                r5004a::phase(channel,1);MemoryBarrier();InterlockedExchange(&channel->installed,channel->error?-1:1);
                // The VEH remains registered until process exit. Keep the DLL
                // reference; unloading it while registered would be unsafe.
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
