#include "../r5-004/crash.hpp"
#include "probe-channel.hpp"
extern "C" void ProbeTrap();
extern "C" BYTE ProbeSite;
extern "C" std::uint64_t ProbeSsp();
namespace {
r5004c::Probe* state=nullptr;
HANDLE start=nullptr,finished=nullptr;
HWND window=nullptr;
LONG CALLBACK ignore_exception(PEXCEPTION_POINTERS){return EXCEPTION_CONTINUE_SEARCH;}
DWORD WINAPI worker(void*){
    state->worker=GetCurrentThreadId();r5004c::mark(state,10);
    if(WaitForSingleObject(start,5000)!=WAIT_OBJECT_0)return ERROR_TIMEOUT;
    r5004c::mark(state,11);
    if(state->exception_worker==1){try{throw 73;}catch(int value){if(value!=73)return ERROR_INVALID_DATA;r5004c::mark(state,12);}}
    if(state->exception_worker==2){
        r5004c::mark(state,14);auto entry=AddVectoredExceptionHandler(0,ignore_exception);
        if(!entry){InterlockedExchange(&state->failure,ERROR_GEN_FAILURE);return ERROR_GEN_FAILURE;}
        r5004c::mark(state,15);if(!RemoveVectoredExceptionHandler(entry)){InterlockedExchange(&state->failure,ERROR_GEN_FAILURE);return ERROR_GEN_FAILURE;}
        r5004c::mark(state,16);
    }
    r5004c::mark(state,13);SetEvent(finished);return 0;
}
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){
    if(m==WM_DESTROY){
        r5004c::mark(state,6);SetEvent(start);
        state->wait=WaitForSingleObject(finished,1000);state->wait_error=state->wait==WAIT_FAILED?GetLastError():0;
        r5004c::mark(state,7);
        if(state->wait!=WAIT_OBJECT_0)InterlockedExchange(&state->failure,state->wait==WAIT_TIMEOUT?ERROR_TIMEOUT:state->wait_error);
    }
    return DefWindowProcW(w,m,a,b);
}
[[noreturn]] void cleanup(){
    r5004c::mark(state,4,0,0,ProbeSsp());
    state->destroyed=DestroyWindow(window);if(!state->destroyed)InterlockedExchange(&state->failure,GetLastError());
    r5004c::mark(state,8);ExitProcess(state->failure?ERROR_PROCESS_ABORTED:0);
}
LONG CALLBACK handler(PEXCEPTION_POINTERS e){
    if(!state||!state->armed||GetCurrentThreadId()!=state->gui||e->ExceptionRecord->ExceptionCode!=EXCEPTION_BREAKPOINT||reinterpret_cast<std::uint64_t>(e->ExceptionRecord->ExceptionAddress)!=state->checkpoint)return EXCEPTION_CONTINUE_SEARCH;
    const auto& cpu=*e->ContextRecord;r5004c::mark(state,2,cpu.Rip,cpu.Rsp,cpu.R11);
    if(!state->deferred)cleanup();
    e->ContextRecord->Rip=reinterpret_cast<DWORD64>(&cleanup);
    r5004c::mark(state,3,e->ContextRecord->Rip,e->ContextRecord->Rsp,e->ContextRecord->R11);
    return EXCEPTION_CONTINUE_EXECUTION;
}
}
int wmain(int argc,wchar_t** argv){using namespace r305;try{
    r3::Args args(argc,argv);Mapping mapping(args.get(L"--map"),sizeof(r5004c::Probe),false);state=static_cast<r5004c::Probe*>(mapping.view);
    if(state->magic!=r5004c::Probe{}.magic||state->pid!=GetCurrentProcessId())return ERROR_INVALID_OWNER;
    state->gui=GetCurrentThreadId();state->checkpoint=reinterpret_cast<std::uint64_t>(&ProbeSite);
    Handle go(CreateEventW(nullptr,TRUE,FALSE,nullptr)),done(CreateEventW(nullptr,TRUE,FALSE,nullptr));check(go.value&&done.value,"CreateEventW.probe");start=go.value;finished=done.value;
    Handle thread(CreateThread(nullptr,0,worker,nullptr,0,nullptr));check(thread.value!=nullptr,"CreateThread.probe");
    WNDCLASSW klass{};klass.hInstance=GetModuleHandleW(nullptr);klass.lpfnWndProc=proc;klass.lpszClassName=L"FocusViewsCleanupDependencyProbe";check(RegisterClassW(&klass)!=0,"RegisterClassW.probe");
    window=CreateWindowExW(0,klass.lpszClassName,L"owned synthetic cleanup dependency",WS_OVERLAPPEDWINDOW,0,0,240,100,nullptr,nullptr,klass.hInstance,nullptr);check(window!=nullptr,"CreateWindowExW.probe");
    check(AddVectoredExceptionHandler(1,handler)!=nullptr,"AddVectoredExceptionHandler.probe");r5004c::mark(state,1);InterlockedExchange(&state->armed,1);ProbeTrap();return ERROR_INVALID_STATE;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
