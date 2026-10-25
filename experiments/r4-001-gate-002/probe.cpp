#include "common.hpp"
namespace gate002 {
struct OwnedDesktop {
    HDESK value=nullptr;Log& log;std::wstring full;
    explicit OwnedDesktop(Log& l):log(l){const auto name=L"fv_gate002_"+std::to_wstring(GetCurrentProcessId());value=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_HOOKCONTROL|READ_CONTROL,nullptr);check(value!=nullptr,"CreateDesktopW");full=user_object_name(GetProcessWindowStation())+L"\\"+name;log.emit("desktop_created","\"name\":"+json(full.c_str()));}
    ~OwnedDesktop(){if(value){const auto result=CloseDesktop(value);const auto error=result?0:GetLastError();log.emit("desktop_closed","\"name\":"+json(full.c_str())+",\"closed\":"+(result?"true":"false")+",\"error\":"+std::to_string(error));}}
};
struct Child {
    PROCESS_INFORMATION info{};Log& log;unsigned sequence;bool debug=false,exited=false;
    Child(Log& l,unsigned n):log(l),sequence(n){}
    void close(){
        for(auto slot:{&info.hThread,&info.hProcess})if(*slot){const auto number=reinterpret_cast<uintptr_t>(*slot);const auto okay=CloseHandle(*slot);const auto error=okay?0:GetLastError();*slot=nullptr;log.emit("owned_handle_closed","\"sequence\":"+std::to_string(sequence)+",\"handle\":"+std::to_string(number)+",\"closed\":"+(okay?"true":"false")+",\"error\":"+std::to_string(error));demand(okay!=FALSE,"owned process-info handle close");}
    }
    ~Child(){if(info.hProcess){if(!exited){TerminateProcess(info.hProcess,ERROR_TIMEOUT);if(debug){const auto end=GetTickCount64()+3000;bool done=false;while(!done&&GetTickCount64()<end){DEBUG_EVENT event{};if(!WaitForDebugEvent(&event,100))continue;if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT&&event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT&&event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);done=event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE);}}const auto wait=WaitForSingleObject(info.hProcess,1000);log.emit("emergency_cleanup","\"sequence\":"+std::to_string(sequence)+",\"pid\":"+std::to_string(info.dwProcessId)+",\"confirmedExit\":"+(wait==WAIT_OBJECT_0?"true":"false"));}if(info.hThread)CloseHandle(info.hThread);CloseHandle(info.hProcess);}}
};
void debug_events(Child& child,ULONGLONG deadline){
    std::map<DWORD,HANDLE> threads;HANDLE process=nullptr;bool exited=false;unsigned serial=0;
    while(!exited){
        if(GetTickCount64()>=deadline)throw Error("debug.deadline",ERROR_TIMEOUT);
        DEBUG_EVENT event{};if(!WaitForDebugEvent(&event,100)){const auto error=GetLastError();if(error==ERROR_SEM_TIMEOUT)continue;throw Error("WaitForDebugEvent",error);}
        demand(event.dwProcessId==child.info.dwProcessId,"single owned debug PID");++serial;
        auto fields="\"sequence\":"+std::to_string(child.sequence)+",\"eventSequence\":"+std::to_string(serial)+",\"pid\":"+std::to_string(event.dwProcessId)+",\"tid\":"+std::to_string(event.dwThreadId)+",\"code\":"+std::to_string(event.dwDebugEventCode);
        HANDLE file=nullptr;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){process=event.u.CreateProcessInfo.hProcess;threads[event.dwThreadId]=event.u.CreateProcessInfo.hThread;file=event.u.CreateProcessInfo.hFile;fields+=",\"processHandle\":"+std::to_string(reinterpret_cast<uintptr_t>(process));demand(GetProcessId(process)==child.info.dwProcessId,"debug process handle identity");}
        if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT){demand(!threads.contains(event.dwThreadId),"no overlapping debug TID");threads[event.dwThreadId]=event.u.CreateThread.hThread;}
        if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT)file=event.u.LoadDll.hFile;
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT)fields+=",\"exception\":"+std::to_string(event.u.Exception.ExceptionRecord.ExceptionCode)+",\"firstChance\":"+std::to_string(event.u.Exception.dwFirstChance);
        if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)fields+=",\"exitCode\":"+std::to_string(event.u.ExitProcess.dwExitCode);
        if(threads.contains(event.dwThreadId)){fields+=",\"threadHandle\":"+std::to_string(reinterpret_cast<uintptr_t>(threads.at(event.dwThreadId)));demand(GetThreadId(threads.at(event.dwThreadId))==event.dwThreadId,"debug thread handle identity");}
        child.log.emit("debug_event",fields);
        if(file){const auto okay=CloseHandle(file);const auto error=okay?0:GetLastError();child.log.emit("debug_file_closed",fields+",\"handle\":"+std::to_string(reinterpret_cast<uintptr_t>(file))+",\"closed\":"+(okay?"true":"false")+",\"error\":"+std::to_string(error));demand(okay!=FALSE,"debug image-file handle close");}
        const auto status=event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT&&event.u.Exception.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE;
        check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status),"ContinueDebugEvent");
        child.log.emit("debug_continued",fields+",\"continueStatus\":"+std::to_string(status));
        if(event.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT || event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){
            const bool all=event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;
            for(auto it=threads.begin();it!=threads.end();){if(!all && it->first!=event.dwThreadId){++it;continue;}DWORD flags=0;const auto valid=GetHandleInformation(it->second,&flags);const auto error=valid?0:GetLastError();child.log.emit("debug_handle_auto_closed",fields+",\"handle\":"+std::to_string(reinterpret_cast<uintptr_t>(it->second))+",\"stillValid\":"+(valid?"true":"false")+",\"error\":"+std::to_string(error));demand(!valid&&error==ERROR_INVALID_HANDLE,"Windows owns debug-thread handle closure");it=threads.erase(it);}
            if(all){DWORD flags=0;const auto valid=GetHandleInformation(process,&flags);const auto error=valid?0:GetLastError();child.log.emit("debug_process_auto_closed",fields+",\"stillValid\":"+(valid?"true":"false")+",\"error\":"+std::to_string(error));demand(!valid&&error==ERROR_INVALID_HANDLE,"Windows owns debug-process handle closure");}
        }
        exited=event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT;
    }
}
int run(const Args& args){
    const auto path=std::filesystem::absolute(args.get(L"--log"));demand(!std::filesystem::exists(path),"new log required");Log log(path);Marker marker;
    const auto mode=args.get(L"--mode",L"own");const bool fixture=mode.find(L"fixture")!=std::wstring::npos,debug=mode.find(L"debug")!=std::wstring::npos;
    const auto count=args.number(L"--count",256);demand(count>=1&&count<=500,"bounded iteration count");
    wchar_t self[32768]{};check(GetModuleFileNameW(nullptr,self,32768)!=0,"GetModuleFileNameW");const auto image=std::filesystem::absolute(fixture?args.get(L"--fixture"):self);
    std::wstring command=quote(image.wstring())+(fixture?L" --role bridge --state Local\\fv_gate002_intentionally_missing":L" --child");const auto flags=CREATE_NO_WINDOW|(debug?DEBUG_ONLY_THIS_PROCESS:0u);
    unsigned completed=0;bool denied=false;const auto deadline=GetTickCount64()+60000;
    log.emit("case_start","\"pid\":"+std::to_string(GetCurrentProcessId())+",\"tid\":"+std::to_string(GetCurrentThreadId())+",\"mode\":"+json(mode.c_str())+",\"planned\":"+std::to_string(count)+",\"markerRegistration\":"+std::to_string(marker.registered)+",\"resources\":"+resources());
    try{
        {OwnedDesktop desktop(log);log.emit("launch_configuration",gate001::launch_state(image,desktop.full,command,flags));
        for(unsigned i=1;i<=count;++i){
            if(GetTickCount64()>=deadline)throw Error("case.deadline",ERROR_TIMEOUT);
            Child child(log,i);child.debug=debug;STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=desktop.full.data();auto mutable_command=command;
            log.emit("before_create","\"sequence\":"+std::to_string(i)+",\"resources\":"+resources());marker.write(i,L"before_create");
            const auto begin=utc();const auto okay=CreateProcessW(image.c_str(),mutable_command.data(),nullptr,nullptr,FALSE,flags,nullptr,image.parent_path().c_str(),&startup,&child.info);const auto error=okay?0:GetLastError();const auto end=utc();
            marker.write(i,okay?L"create_succeeded":L"create_failed",error);
            if(!okay){
                denied=true;log.emit("create_failed","\"sequence\":"+std::to_string(i)+",\"api\":\"CreateProcessW\",\"error\":"+std::to_string(error)+",\"beginUtc\":"+std::to_string(begin)+",\"endUtc\":"+std::to_string(end)+",\"reportedPid\":"+std::to_string(child.info.dwProcessId)+",\"reportedThread\":"+std::to_string(child.info.dwThreadId)+",\"outputProcessHandle\":"+std::to_string(reinterpret_cast<uintptr_t>(child.info.hProcess))+",\"outputThreadHandle\":"+std::to_string(reinterpret_cast<uintptr_t>(child.info.hThread))+",\"resources\":"+resources()+","+gate001::launch_state(image,desktop.full,command,flags));
                // Failed API outputs are not ownership grants. Never close or
                // terminate arbitrary values from an unsuccessful creation.
                child.info.hProcess=nullptr;child.info.hThread=nullptr;break;
            }
            log.emit("process_created","\"sequence\":"+std::to_string(i)+",\"pid\":"+std::to_string(child.info.dwProcessId)+",\"tid\":"+std::to_string(child.info.dwThreadId)+",\"beginUtc\":"+std::to_string(begin)+",\"endUtc\":"+std::to_string(end)+",\"processHandle\":"+std::to_string(reinterpret_cast<uintptr_t>(child.info.hProcess))+",\"threadHandle\":"+std::to_string(reinterpret_cast<uintptr_t>(child.info.hThread)));
            demand(GetProcessId(child.info.hProcess)==child.info.dwProcessId && GetThreadId(child.info.hThread)==child.info.dwThreadId,"owned process-info identity");
            if(debug)debug_events(child,std::min(deadline,GetTickCount64()+10000));
            check(WaitForSingleObject(child.info.hProcess,5000)==WAIT_OBJECT_0,"WaitForSingleObject.child");child.exited=true;
            DWORD code=0;check(GetExitCodeProcess(child.info.hProcess,&code),"GetExitCodeProcess.child");demand(GetProcessId(child.info.hProcess)==child.info.dwProcessId,"process-info handle survives debugger closure");
            log.emit("process_exited","\"sequence\":"+std::to_string(i)+",\"pid\":"+std::to_string(child.info.dwProcessId)+",\"exitCode\":"+std::to_string(code));
            child.close();demand(code==(fixture?1u:0u),"expected minimal child exit");++completed;
            log.emit("after_close","\"sequence\":"+std::to_string(i)+",\"resources\":"+resources());marker.write(i,L"closed");
        }}
        log.emit("case_complete","\"mode\":"+json(mode.c_str())+",\"completed\":"+std::to_string(completed)+",\"creationRefused\":"+(denied?"true":"false")+",\"resources\":"+resources());return denied?2:0;
    }catch(const Error& error){log.emit("case_error","\"api\":"+json(error.api)+",\"error\":"+std::to_string(error.code)+",\"completed\":"+std::to_string(completed));return 3;}
}
}
int wmain(int argc,wchar_t** argv){if(argc==2&&!wcscmp(argv[1],L"--child"))return 0;try{return gate002::run(r3::Args(argc,argv));}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
