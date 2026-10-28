#include "../r5-004/crash.hpp"
#include "probe-channel.hpp"
namespace {struct DesktopOwner {HDESK value=nullptr;~DesktopOwner(){if(value)CloseDesktop(value);}};}
int wmain(int argc,wchar_t** argv){using namespace r305;std::filesystem::path output;try{
    r3::Args args(argc,argv);output=args.get(L"--output");demand(!output.empty()&&!std::filesystem::exists(output),"new explicit probe output");std::filesystem::create_directories(output);Log log(output/"events.jsonl");
    const auto name=L"fv_cprobe_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());Mapping memory(L"Local\\"+name,sizeof(r5004c::Probe),true);auto& state=*new(memory.view)r5004c::Probe{};
    demand(args.get(L"--mode")==L"inline"||args.get(L"--mode")==L"deferred","explicit probe mode");demand(args.get(L"--worker")==L"plain"||args.get(L"--worker")==L"exception"||args.get(L"--worker")==L"registration","explicit worker mode");
    state.deferred=args.get(L"--mode")==L"deferred";state.exception_worker=args.get(L"--worker")==L"registration"?2u:args.get(L"--worker")==L"exception"?1u:0u;
    Handle job(CreateJobObjectW(nullptr,nullptr));check(job.value!=nullptr,"CreateJobObjectW.probe");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"SetInformationJobObject.probe");
    DesktopOwner desktop_owner;auto desktop=desktop_owner.value=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);check(desktop!=nullptr,"CreateDesktopW.probe");const auto full=user_object_name(GetProcessWindowStation())+L"\\"+name;
    auto child=launch(args.get(L"--target"),full,{L"--map",L"Local\\"+name},CREATE_SUSPENDED|DEBUG_ONLY_THIS_PROCESS,log,"probe");
    state.pid=child.pid;const auto created=r5004::creation(child.process.value);check(AssignProcessToJobObject(job.value,child.process.value),"AssignProcessToJobObject.probe");check(ResumeThread(child.thread.value)!=DWORD(-1),"ResumeThread.probe");
    bool forced=false,exited=false;DWORD exit=STILL_ACTIVE;unsigned cpp=0;auto end=GetTickCount64()+6000;DEBUG_EVENT event{};
    while(!exited){
        if(GetTickCount64()>=end){if(forced)throw Error("probe.exit_unconfirmed",ERROR_TIMEOUT);check(TerminateProcess(child.process.value,ERROR_TIMEOUT),"TerminateProcess.probe_timeout");forced=true;end=GetTickCount64()+2000;}
        if(!WaitForDebugEvent(&event,50)){if(GetLastError()==ERROR_SEM_TIMEOUT)continue;throw Error("WaitForDebugEvent.probe",GetLastError());}
        if(event.dwProcessId!=child.pid)throw Error("probe.event_owner",ERROR_INVALID_OWNER);
        DWORD status=DBG_CONTINUE;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT&&event.u.CreateProcessInfo.hFile)check(CloseHandle(event.u.CreateProcessInfo.hFile),"CloseHandle.probe_image_file");
        if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){wchar_t path[2048]{};const auto file=event.u.LoadDll.hFile;DWORD length=0,error=0;if(file){length=GetFinalPathNameByHandleW(file,path,2048,FILE_NAME_NORMALIZED);if(!length)error=GetLastError();check(CloseHandle(file),"CloseHandle.probe_DLL_file");}log.emit("module_loaded","\"base\":"+std::to_string(reinterpret_cast<std::uint64_t>(event.u.LoadDll.lpBaseOfDll))+",\"path\":"+json(length&&length<2048?path:L"")+",\"pathError\":"+std::to_string(error));}
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){const auto& e=event.u.Exception;const auto code=e.ExceptionRecord.ExceptionCode;const bool checkpoint=state.armed&&event.dwThreadId==state.gui&&reinterpret_cast<std::uint64_t>(e.ExceptionRecord.ExceptionAddress)==state.checkpoint;
            if(checkpoint){CONTEXT cpu{};cpu.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;check(GetThreadContext(child.thread.value,&cpu),"GetThreadContext.probe");log.emit("checkpoint_context","\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"ssp\":"+std::to_string(cpu.R11));}
            if(code==0xE06D7363)++cpp;
            status=checkpoint||code!=EXCEPTION_BREAKPOINT?DBG_EXCEPTION_NOT_HANDLED:DBG_CONTINUE;
            log.emit("exception","\"tid\":"+std::to_string(event.dwThreadId)+",\"code\":"+std::to_string(code)+",\"first\":"+std::to_string(e.dwFirstChance)+",\"checkpoint\":"+(checkpoint?"true":"false")+",\"continueStatus\":"+std::to_string(status));
        }
        if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){exited=true;exit=event.u.ExitProcess.dwExitCode;}
        check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status),"ContinueDebugEvent.probe");
    }
    check(WaitForSingleObject(child.process.value,1000)==WAIT_OBJECT_0,"WaitForSingleObject.probe_exit");child.exited=true;
    for(LONG i=0;i<std::min<LONG>(static_cast<LONG>(state.used),64);++i){const auto& r=state.marks[i];if(!r.committed)throw Error("probe.trace_uncommitted",ERROR_INVALID_DATA);log.emit("phase","\"sequence\":"+std::to_string(i)+",\"stage\":"+std::to_string(r.stage)+",\"tid\":"+std::to_string(r.tid)+",\"tick\":"+std::to_string(r.tick)+",\"rip\":"+std::to_string(r.rip)+",\"rsp\":"+std::to_string(r.rsp)+",\"ssp\":"+std::to_string(r.ssp));}
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};const auto job_end=GetTickCount64()+500;
    do{check(QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr),"QueryInformationJobObject.probe");if(!accounting.ActiveProcesses)break;Sleep(1);}while(GetTickCount64()<job_end);
    check(CloseHandle(child.thread.value),"CloseHandle.probe_thread");child.thread.value=nullptr;check(CloseHandle(child.process.value),"CloseHandle.probe_process");child.process.value=nullptr;check(CloseHandle(job.value),"CloseHandle.probe_job");job.value=nullptr;check(CloseDesktop(desktop),"CloseDesktop.probe");desktop_owner.value=nullptr;
    const auto absent_end=GetTickCount64()+500;while(r5004::desktop_exists(r3::utf8(full))&&GetTickCount64()<absent_end)Sleep(10);const auto absent=!r5004::desktop_exists(r3::utf8(full));
    const bool okay=!forced&&!state.failure&&state.destroyed&&state.wait==WAIT_OBJECT_0&&exit==0;
    std::ofstream summary(output/"summary.json");summary<<"{\"synthetic\":true,\"deferred\":"<<(state.deferred?"true":"false")<<",\"exceptionWorker\":"<<(state.exception_worker?"true":"false")<<",\"pid\":"<<state.pid<<",\"created\":"<<created<<",\"gui\":"<<state.gui<<",\"worker\":"<<state.worker<<",\"cleanupCompleted\":"<<(okay?"true":"false")<<",\"failure\":"<<state.failure<<",\"wait\":"<<state.wait<<",\"waitError\":"<<state.wait_error<<",\"exit\":"<<exit<<",\"forced\":"<<(forced?"true":"false")<<",\"cppEvents\":"<<cpp<<",\"jobEmpty\":"<<(accounting.ActiveProcesses==0?"true":"false")<<",\"desktopAbsent\":"<<(absent?"true":"false")<<"}\n";
    return absent&&accounting.ActiveProcesses==0?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(!output.empty()){std::ofstream file(output/"failure.json");const auto* failure=dynamic_cast<const Error*>(&e);file<<"{\"passed\":false,\"api\":"<<r305::json(failure?failure->api:"std.exception")<<",\"error\":"<<(failure?failure->code:0)<<",\"message\":"<<r305::json(e.what())<<"}\n";}return 2;}}
