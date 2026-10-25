#include "../r3/protocol.hpp"
#include "launch-diagnostics.hpp"
// Isolate CreateProcess from snapshots, debugger, hooks, GDI and business code.
int wmain(int argc,wchar_t** argv) {
    if(argc>1 && wcscmp(argv[1],L"--child")==0)return 0;
    try {
        wchar_t own[32768]{};r3::check(GetModuleFileNameW(nullptr,own,32768)!=0,"GetModuleFileNameW");
        const bool fixture=argc>1 && wcscmp(argv[1],L"--debug")!=0;
        const bool debug=argc>1 && wcscmp(argv[argc-1],L"--debug")==0;
        const std::filesystem::path image=fixture?argv[1]:own;
        const auto name=L"fv_gate_launch_probe_"+std::to_wstring(GetCurrentProcessId());
        HDESK desk=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_HOOKCONTROL|READ_CONTROL,nullptr);r3::check(desk!=nullptr,"CreateDesktopW");
        const auto desktop=r3::user_object_name(GetProcessWindowStation())+L"\\"+name;
        bool failed=false;unsigned completed=0;
        for(unsigned i=0;i<500;++i){
            std::wstring command=L"\""+image.wstring()+L"\" "+(fixture?L"--role bridge --state Local\\fv_gate_intentionally_missing":L"--child");
            STARTUPINFOW si{sizeof(si)};si.lpDesktop=const_cast<wchar_t*>(desktop.c_str());PROCESS_INFORMATION pi{};
            const auto flags=CREATE_NO_WINDOW|(debug?DEBUG_ONLY_THIS_PROCESS:0u);
            if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,flags,nullptr,image.parent_path().c_str(),&si,&pi)){
                const auto error=GetLastError();std::cout<<"{\"type\":\"launch_probe_failure\",\"sequence\":"<<i+1<<",\"error\":"<<error<<","<<gate001::launch_state(image,desktop,command,flags)<<"}\n";failed=true;break;
            }
            if(debug){bool done=false;while(!done){DEBUG_EVENT event{};r3::check(WaitForDebugEvent(&event,5000),"WaitForDebugEvent.launch_probe");
                if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT && event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
                if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
                const auto status=event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT && event.u.Exception.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE;
                done=event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;r3::check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status),"ContinueDebugEvent.launch_probe");
            }}
            const auto wait=WaitForSingleObject(pi.hProcess,5000);if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,ERROR_TIMEOUT);WaitForSingleObject(pi.hProcess,1000);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);throw r3::Error("launch_probe.wait",ERROR_TIMEOUT);}
            DWORD code=0;r3::check(GetExitCodeProcess(pi.hProcess,&code),"GetExitCodeProcess");
            std::cout<<"{\"type\":\"launch_probe_exit\",\"sequence\":"<<i+1<<",\"pid\":"<<pi.dwProcessId<<",\"exitCode\":"<<code<<"}\n";
            CloseHandle(pi.hProcess);CloseHandle(pi.hThread);++completed;
        }
        r3::check(CloseDesktop(desk),"CloseDesktop");std::cout<<"{\"type\":\"launch_probe_complete\",\"debugged\":"<<(debug?"true":"false")<<",\"completed\":"<<completed<<",\"creationFailed\":"<<(failed?"true":"false")<<",\"desktopClosed\":true}\n";return failed?2:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
