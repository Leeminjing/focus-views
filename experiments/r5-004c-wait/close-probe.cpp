#include "../r5-004/crash.hpp"
int wmain(int argc,wchar_t** argv){using namespace r305;try{
    r3::Args args(argc,argv);Log log(args.get(L"--log"));const auto pid=static_cast<DWORD>(std::stoul(args.get(L"--pid"))),tid=static_cast<DWORD>(std::stoul(args.get(L"--tid")));const auto created=std::stoull(args.get(L"--created"));const auto window=reinterpret_cast<HWND>(std::stoull(args.get(L"--window")));
    Handle process(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));check(process.value!=nullptr,"OpenProcess.normal_close");
    if(r5004::creation(process.value)!=created||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)throw Error("normal_close.process_identity",ERROR_INVALID_OWNER);
    DWORD owner=0;const auto owner_tid=GetWindowThreadProcessId(window,&owner);if(owner!=pid||owner_tid!=tid)throw Error("normal_close.window_identity",ERROR_INVALID_OWNER);
    log.emit("normal_close_post","\"pid\":"+std::to_string(pid)+",\"created\":"+std::to_string(created)+",\"tid\":"+std::to_string(tid)+",\"window\":"+std::to_string(reinterpret_cast<std::uint64_t>(window))+",\"desktop\":"+json(r3::thread_desktop().c_str()));
    check(PostMessageW(window,WM_CLOSE,0,0),"PostMessageW.normal_close");const auto end=GetTickCount64()+5000;
    while(IsWindow(window)&&GetTickCount64()<end){if(WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)break;Sleep(1);}
    const bool absent=!IsWindow(window);log.emit("normal_close_observed","\"windowAbsent\":"+std::string(absent?"true":"false")+",\"processStillAlive\":"+(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT?"true":"false")+",\"deadlineMilliseconds\":5000,\"destroyWindowReturnNotObservedDirectly\":true");return absent?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
