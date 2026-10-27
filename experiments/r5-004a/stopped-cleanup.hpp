#pragma once
#include <tlhelp32.h>
#include <set>
#include <vector>
#include <map>
// Experiment-only native cleanup support. The debug event must still be held
// when this is called. No thread is resumed after acquiring these suspensions.
namespace r305::experimental {
struct DebugHandles {ULONG_PTR process=0;std::map<DWORD,ULONG_PTR> threads;};
inline thread_local std::map<DWORD,DebugHandles> debug_handles;
template<class Recorder>
void released_debug_handle(ULONG_PTR value,const char* kind,Recorder& log){
    DWORD flags=0;const auto okay=GetHandleInformation(reinterpret_cast<HANDLE>(value),&flags);const auto error=okay?0:GetLastError();
    log.emit("cleanup_system_handle_probe","\"handle\":"+std::to_string(value)+",\"kind\":"+json(kind)+",\"valid\":"+(okay?"true":"false")+",\"error\":"+std::to_string(error)+",\"manuallyClosed\":false");
}
template<class Recorder>
std::vector<Handle> suspend_owned_threads(DWORD pid,const std::set<DWORD>& registered,Recorder& log){
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));
    check(snapshot.value!=INVALID_HANDLE_VALUE,"CreateToolhelp32Snapshot.cleanup_threads");
    THREADENTRY32 entry{sizeof(entry)};std::set<DWORD> actual;
    if(!Thread32First(snapshot.value,&entry))throw Error("Thread32First.cleanup",GetLastError());
    do{if(entry.th32OwnerProcessID==pid)actual.insert(entry.th32ThreadID);entry.dwSize=sizeof(entry);}while(Thread32Next(snapshot.value,&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES)throw Error("Thread32Next.cleanup",GetLastError());
    if(actual!=registered||actual.empty())throw Error("cleanup.thread_manifest",ERROR_INVALID_DATA);
    log.emit("cleanup_thread_manifest","\"pid\":"+std::to_string(pid)+",\"nativeCount\":"+std::to_string(actual.size())+",\"registeredCount\":"+std::to_string(registered.size())+",\"matched\":true");
    std::vector<Handle> result;
    for(auto tid:actual){
        Handle thread(OpenThread(THREAD_SUSPEND_RESUME|THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,tid));
        check(thread.value!=nullptr,"OpenThread.cleanup_suspend");
        if(GetProcessIdOfThread(thread.value)!=pid)throw Error("cleanup.thread_owner",ERROR_INVALID_OWNER);
        const auto count=SuspendThread(thread.value);if(count==DWORD(-1))throw Error("SuspendThread.cleanup",GetLastError());
        log.emit("cleanup_thread_suspended","\"pid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(tid)+",\"handle\":"+std::to_string(reinterpret_cast<ULONG_PTR>(thread.value))+",\"previousCount\":"+std::to_string(count));
        result.push_back(std::move(thread));
    }
    return result;
}
template<class Recorder>
void close_exited_threads(std::vector<Handle>& threads,Recorder& log){
    for(auto& thread:threads){
        if(WaitForSingleObject(thread.value,0)!=WAIT_OBJECT_0)throw Error("cleanup.thread_not_exited",ERROR_BUSY);
        const auto value=reinterpret_cast<ULONG_PTR>(thread.value);check(CloseHandle(thread.value),"CloseHandle.cleanup_thread");thread.value=nullptr;
        log.emit("cleanup_thread_handle_closed","\"handle\":"+std::to_string(value)+",\"exitConfirmed\":true");
    }
}
}
