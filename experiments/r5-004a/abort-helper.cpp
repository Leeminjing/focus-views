#include "../r5-004/crash.hpp"
#include "abort-channel.hpp"
int wmain(int argc,wchar_t** argv){using namespace r305;std::unique_ptr<Log> evidence;try{
    r3::Args args(argc,argv);evidence=std::make_unique<Log>(args.get(L"--log"));auto& log=*evidence;const auto pid=static_cast<DWORD>(std::stoul(args.get(L"--pid"))),tid=static_cast<DWORD>(std::stoul(args.get(L"--tid")));
    Mapping mapping(args.get(L"--map"),sizeof(r5004a::AbortChannel),false);auto& c=*static_cast<r5004a::AbortChannel*>(mapping.view);
    Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));check(process.value!=nullptr,"OpenProcess.abort_target");
#ifdef FOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT
    log.emit("abort_identity_observed","\"expectedMagic\":"+std::to_string(r5004a::abort_magic)+",\"magic\":"+std::to_string(c.magic)+",\"pid\":"+std::to_string(c.pid)+",\"argumentPid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(c.tid)+",\"argumentTid\":"+std::to_string(tid)+",\"created\":"+std::to_string(c.created)+",\"actualCreated\":"+std::to_string(r5004a::process_created(process.value))+",\"channelBytes\":"+std::to_string(sizeof(c)));
#endif
    if(c.magic!=r5004a::abort_magic||c.pid!=pid||c.tid!=tid||c.created!=r5004a::process_created(process.value))throw Error("abort.helper_identity",ERROR_INVALID_OWNER);
    Handle thread(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,tid));check(thread.value!=nullptr,"OpenThread.abort_target");
    const auto actual_pid=GetProcessIdOfThread(thread.value);check(actual_pid!=0,"GetProcessIdOfThread.abort_target");
    const auto actual_tid=GetThreadId(thread.value);check(actual_tid!=0,"GetThreadId.abort_target");
    const auto actual_created=r5004a::thread_created(thread.value);if(!actual_created)throw Error("GetThreadTimes.abort_target",GetLastError());
    if(actual_pid!=pid||actual_tid!=tid||actual_created!=c.thread_created)throw Error("abort.thread_owner",ERROR_INVALID_OWNER);
    log.emit("abort_thread_pinned","\"pid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(tid)+",\"created\":"+std::to_string(c.thread_created));
    HMODULE module=LoadLibraryW(args.get(L"--dll").c_str());check(module!=nullptr,"LoadLibraryW.abort_runtime");
    const auto entry=reinterpret_cast<HOOKPROC>(GetProcAddress(module,"CleanupGetMessage"));check(entry!=nullptr,"GetProcAddress.CleanupGetMessage");
    if(WaitForSingleObject(thread.value,0)!=WAIT_TIMEOUT||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)throw Error("abort.target_exited_before_hook",ERROR_PROCESS_ABORTED);
    HHOOK hook=SetWindowsHookExW(WH_GETMESSAGE,entry,module,tid);check(hook!=nullptr,"SetWindowsHookExW.abort_thread");
    log.emit("abort_hook_installed","\"pid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(tid)+",\"global\":false");
    check(PostThreadMessageW(tid,WM_NULL,0,0),"PostThreadMessageW.abort_prepare");
    const auto waited=WaitForSingleObject(process.value,120000);const auto wait_error=waited==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
    const auto unhooked=UnhookWindowsHookEx(hook);const auto unhook_error=unhooked?0:GetLastError();
    log.emit("abort_hook_removed","\"targetExited\":"+(waited==WAIT_OBJECT_0?std::string("true"):"false")+",\"unhooked\":"+(unhooked?std::string("true"):"false")+",\"error\":"+std::to_string(unhook_error)+",\"releaseBasis\":\"pinned-target-process-exit\"");
    check(FreeLibrary(module),"FreeLibrary.abort_helper");
    if(waited!=WAIT_OBJECT_0)throw Error("WaitForSingleObject.abort_target",wait_error);
    return 0;
}catch(const Error& e){if(evidence)evidence->emit("abort_helper_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));std::cerr<<e.what()<<'\n';return 2;}catch(const std::exception& e){if(evidence)evidence->emit("abort_helper_failure","\"api\":\"std.exception\",\"message\":"+json(e.what()));std::cerr<<e.what()<<'\n';return 2;}}
