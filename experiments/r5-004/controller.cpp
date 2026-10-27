#include "crash.hpp"
#include <map>
#include <memory>
namespace r5004 {
std::vector<DWORD> members(HANDLE job){std::vector<BYTE> bytes(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST)+128*sizeof(ULONG_PTR));auto* list=reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(bytes.data());check(QueryInformationJobObject(job,JobObjectBasicProcessIdList,list,static_cast<DWORD>(bytes.size()),nullptr),"QueryInformationJobObject.crash_members");if(list->NumberOfProcessIdsInList>128||list->NumberOfAssignedProcesses>128)throw Error("crash.job_bounds",ERROR_BUFFER_OVERFLOW);std::vector<DWORD> result;for(DWORD n=0;n<list->NumberOfProcessIdsInList;++n)result.push_back(static_cast<DWORD>(list->ProcessIdList[n]));return result;}
std::string field(const std::string& line,const std::string& key){const auto token="\""+key+"\":\"";const auto at=line.find(token);if(at==std::string::npos)throw Error("crash.log_field",ERROR_INVALID_DATA);std::string out;bool escape=false;for(size_t i=at+token.size();i<line.size();++i){const auto c=line[i];if(escape){if(c!='\\'&&c!='"')throw Error("crash.log_escape",ERROR_INVALID_DATA);out+=c;escape=false;}else if(c=='\\')escape=true;else if(c=='"')return out;else out+=c;}throw Error("crash.log_truncated",ERROR_HANDLE_EOF);}
std::vector<std::string> desktops(const std::filesystem::path& output,bool required){std::ifstream file(output/"controller.jsonl",std::ios::binary);if(!file.is_open()){if(required||!std::filesystem::exists(output/"recovery.jsonl"))throw Error("crash.desktop_evidence_missing",ERROR_FILE_NOT_FOUND);return {};}std::string line;std::set<std::string> names;while(std::getline(file,line))if(line.starts_with("{\"type\":\"desktop_created\""))names.insert(field(line,"name"));if(file.bad()||(required&&names.empty()))throw Error("crash.desktop_evidence_incomplete",ERROR_INVALID_DATA);return {names.begin(),names.end()};}
struct Owned {Identity identity;Handle handle;
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
    DWORD pending_image_error=0;
#endif
};
class Supervisor {
    std::filesystem::path out_,manager_output_,store_;Log log_;Handle job_,process_,thread_,ready_,release_,stdout_,stderr_,stdin_;std::unique_ptr<Mapping> gate_;std::map<std::pair<DWORD,std::uint64_t>,Owned> owned_;bool finished_=false;
    void close(Handle& h,const char* name){if(!h.value||h.value==INVALID_HANDLE_VALUE)return;check(CloseHandle(h.value),"CloseHandle.supervisor_owned");h.value=nullptr;log_.emit("owned_handle_closed","\"name\":"+json(name)+",\"closed\":true");}
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
    // No query retry and no extra wait. A failed optional image sample may
    // survive only until the next existing observation point. The exact
    // pinned process must then be signaled; a still-active instance refuses.
    void confirm_pending_images(){for(auto& [key,owned]:owned_){(void)key;if(!owned.pending_image_error)continue;
        const auto error=owned.pending_image_error;const auto waited=WaitForSingleObject(owned.handle.value,0);
        if(waited!=WAIT_OBJECT_0)throw Error("QueryFullProcessImageNameW.job_member",error);
        log_.emit("deferred_image_exit_confirmed","\"pid\":"+std::to_string(owned.identity.pid)+",\"created\":"+std::to_string(owned.identity.created)+",\"originalError\":"+std::to_string(error)+",\"samePinnedHandle\":true,\"nativeWaitSignaled\":true,\"queryRetried\":false");owned.pending_image_error=0;
    }}
#endif
    void sample(){
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        confirm_pending_images();
#endif
        for(auto pid:members(job_.value)){Handle h(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!h.value){const auto e=GetLastError();if(e==ERROR_INVALID_PARAMETER){log_.emit("job_member_exited_before_open","\"pid\":"+std::to_string(pid));continue;}throw Error("OpenProcess.job_member",e);}const auto created=creation(h.value);const auto key=std::make_pair(pid,created);if(owned_.contains(key))continue;BOOL in_job=FALSE;check(IsProcessInJob(h.value,job_.value,&in_job),"IsProcessInJob.crash_member");if(!in_job)throw Error("crash.job_identity",ERROR_INVALID_OWNER);wchar_t image[32768]{};DWORD count=32768;DWORD image_error=0;bool exited_during_query=false;
        if(!QueryFullProcessImageNameW(h.value,0,image,&count)){image_error=GetLastError();const auto stopped=WaitForSingleObject(h.value,0);
#ifndef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        if(stopped!=WAIT_OBJECT_0)throw Error("QueryFullProcessImageNameW.job_member",image_error);
#endif
        exited_during_query=stopped==WAIT_OBJECT_0;image[0]=0;}
        log_.emit("job_instance_pinned","\"pid\":"+std::to_string(pid)+",\"created\":"+std::to_string(created)+",\"inJob\":true,\"image\":"+json(image)+",\"imageQueryError\":"+std::to_string(image_error)+",\"exitConfirmedDuringImageQuery\":"+(exited_during_query?"true":"false"));Owned value;value.identity={pid,created};value.handle=std::move(h);
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        if(image_error&&!exited_during_query){value.pending_image_error=image_error;log_.emit("image_diagnostic_pending","\"pid\":"+std::to_string(pid)+",\"created\":"+std::to_string(created)+",\"error\":"+std::to_string(image_error)+",\"exitNotYetConfirmed\":true,\"queryRetried\":false");}
#endif
        owned_.emplace(key,std::move(value));}}
    void quarantine(const Identity& owner){if(!std::filesystem::exists(store_))return;const auto prefix=L".snapshot-"+std::to_wstring(owner.pid)+L"-"+std::to_wstring(owner.created)+L"-";const auto target=out_/"quarantine";for(const auto& entry:std::filesystem::directory_iterator(store_)){const auto name=entry.path().filename().wstring();if(!name.ends_with(L".partial"))continue;if(!name.starts_with(prefix)){log_.emit("foreign_partial_ignored","\"path\":"+json(entry.path().wstring().c_str())+",\"neverPromoted\":true");continue;}if(!entry.is_regular_file()||entry.is_symlink())throw Error("crash.partial_kind",ERROR_NOT_SUPPORTED);const auto before=native::digest_text(native::digest(native::read_file(entry.path())));std::filesystem::create_directories(target);const auto to=target/entry.path().filename();check(MoveFileExW(entry.path().c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH),"MoveFileExW.crash_quarantine");if(native::digest_text(native::digest(native::read_file(to)))!=before)throw Error("crash.quarantine_identity",ERROR_CRC);log_.emit("partial_quarantined","\"source\":"+json(entry.path().wstring().c_str())+",\"file\":"+json(to.wstring().c_str())+",\"sha256\":"+json(before)+",\"neverPromoted\":true");}}
    void failure_cleanup(){
        bool failed=false;auto attempt=[&](auto operation){try{operation();}catch(const Error& e){failed=true;log_.emit("failure_cleanup_step_error","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}catch(const std::exception& e){failed=true;log_.emit("failure_cleanup_step_error","\"api\":\"std.exception\",\"message\":"+json(e.what()));}};
        attempt([&]{if(process_.value&&WaitForSingleObject(process_.value,0)==WAIT_TIMEOUT){check(TerminateProcess(process_.value,ERROR_OPERATION_ABORTED),"TerminateProcess.failed_manager");const auto result=WaitForSingleObject(process_.value,5000);if(result!=WAIT_OBJECT_0)throw Error("WaitForSingleObject.failed_manager",result==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());}});
        if(job_.value){attempt([&]{sample();});attempt([&]{check(TerminateJobObject(job_.value,ERROR_OPERATION_ABORTED),"TerminateJobObject.failed_operation");const auto end=GetTickCount64()+5000;while(!members(job_.value).empty()&&GetTickCount64()<end)Sleep(10);if(!members(job_.value).empty())throw Error("crash.failed_job_not_empty",ERROR_BUSY);});}
        for(auto& [id,p]:owned_){(void)id;if(!p.handle.value)continue;attempt([&]{const auto result=WaitForSingleObject(p.handle.value,1000);if(result!=WAIT_OBJECT_0)throw Error("WaitForSingleObject.failed_member",result==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());});attempt([&]{close(p.handle,"failed-pinned-instance");});}
        for(auto* h:{&thread_,&process_,&job_,&ready_,&release_})attempt([&]{close(*h,"failure-owned-handle");});
        if(gate_){attempt([&]{if(gate_->view){check(UnmapViewOfFile(gate_->view),"UnmapViewOfFile.failed_gate");gate_->view=nullptr;}});attempt([&]{close(gate_->handle,"failed-mapping");});gate_.reset();}
        for(auto* h:{&stdout_,&stderr_,&stdin_})attempt([&]{close(*h,"failure-standard-handle");});
        finished_=true;if(failed)throw Error("crash.failure_cleanup_incomplete",ERROR_BUSY);log_.emit("failure_process_cleanup_confirmed","\"ownedProcessesExited\":true,\"ownedHandlesClosed\":true");
    }
public:
    Supervisor(const std::filesystem::path& out,const std::filesystem::path& target,const std::filesystem::path& store):out_(out),manager_output_(target),store_(store),log_(out/"supervisor.jsonl"){}
    ~Supervisor(){if(!finished_)try{failure_cleanup();}catch(const std::exception& e){std::cerr<<"SUPERVISOR_LAST_RESORT "<<e.what()<<'\n';}}
    int run(const std::filesystem::path& exe,const std::filesystem::path& client,const std::filesystem::path& app,const std::wstring& mode,const std::wstring& scenario,const std::filesystem::path& previous,const std::string& kill_at){
        Certificate certificate;bool hit=false,protocol=false,clean=false;DWORD code=0;
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        std::string primary_api;DWORD primary_error=0;
#endif
        try{
            job_.value=CreateJobObjectW(nullptr,nullptr);check(job_.value!=nullptr,"CreateJobObjectW.supervisor");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"SetInformationJobObject.supervisor");
            const auto name=L"Local\\fv_r5004_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(creation(GetCurrentProcess()));gate_=std::make_unique<Mapping>(name,sizeof(GateBlock),true);auto& block=*new(gate_->view)GateBlock{};block.supervisor=GetCurrentProcessId();block.supervisor_created=creation(GetCurrentProcess());if(kill_at.size()>=sizeof(block.wanted))throw Error("crash.point_length",ERROR_INVALID_PARAMETER);strcpy_s(block.wanted,kill_at.c_str());
            ready_.value=CreateEventW(nullptr,TRUE,FALSE,(name+L"_ready").c_str());check(ready_.value!=nullptr,"CreateEventW.crash_ready");release_.value=CreateEventW(nullptr,TRUE,FALSE,(name+L"_release").c_str());check(release_.value!=nullptr,"CreateEventW.crash_release");
            SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};stdout_.value=CreateFileW((out_/"manager-stdout.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(stdout_.value!=INVALID_HANDLE_VALUE,"CreateFileW.crash_stdout");stderr_.value=CreateFileW((out_/"manager-stderr.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(stderr_.value!=INVALID_HANDLE_VALUE,"CreateFileW.crash_stderr");stdin_.value=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(stdin_.value!=INVALID_HANDLE_VALUE,"CreateFileW.crash_stdin");
            SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size)throw Error("InitializeProcThreadAttributeList.size",GetLastError());std::vector<BYTE> storage(size);auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());check(InitializeProcThreadAttributeList(attributes,1,0,&size),"InitializeProcThreadAttributeList");struct Attributes{LPPROC_THREAD_ATTRIBUTE_LIST p;~Attributes(){DeleteProcThreadAttributeList(p);}} cleanup{attributes};HANDLE handles[]={stdin_.value,stdout_.value,stderr_.value};check(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr),"UpdateProcThreadAttribute.handle_list");
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=stdin_.value;startup.StartupInfo.hStdOutput=stdout_.value;startup.StartupInfo.hStdError=stderr_.value;startup.lpAttributeList=attributes;
            std::wstring command=quote(exe.wstring());const std::vector<std::wstring> args={L"--client",client.wstring(),L"--fixture",app.wstring(),L"--output",manager_output_.wstring(),L"--store",store_.wstring(),L"--mode",mode,L"--scenario",scenario,L"--gate",name,L"--previous",previous.wstring()};for(const auto& a:args)command+=L" "+quote(a);
            PROCESS_INFORMATION info{};const DWORD flags=CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT;auto launch_state=gate001::launch_state(exe,L"",command,flags);
            // The legacy diagnostics describe noninheriting launches. This
            // caller passes TRUE with the exact three-handle attribute list.
            const std::string assumed="\"inheritHandles\":false";const auto at=launch_state.find(assumed);if(at==std::string::npos)throw Error("crash.launch_diagnostic_schema",ERROR_INVALID_DATA);launch_state.replace(at,assumed.size(),"\"inheritHandles\":true");log_.emit("manager_launch_attempt",launch_state+",\"explicitInheritedHandleCount\":3");if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,flags,nullptr,exe.parent_path().c_str(),&startup.StartupInfo,&info)){const auto error=GetLastError();log_.emit("manager_launch_failed","\"api\":\"CreateProcessW\",\"error\":"+std::to_string(error));throw Error("CreateProcessW.crash_manager",error);}
            process_.value=info.hProcess;thread_.value=info.hThread;certificate.manager={info.dwProcessId,creation(process_.value)};block.manager=info.dwProcessId;check(AssignProcessToJobObject(job_.value,process_.value),"AssignProcessToJobObject.manager_before_resume");r5003::publish(owner_path(store_,certificate.manager),static_cast<r5003::Kind>(4),owner_bytes(certificate.manager));certificate.owner_hash=r5003::hash(r5003::read(owner_path(store_,certificate.manager)));
            if(scenario==L"foreign-generation"){const auto foreign=store_/(L".snapshot-"+std::to_wstring(certificate.manager.pid)+L"-"+std::to_wstring(certificate.manager.created-1)+L"-probe.partial");const BYTE bytes[]={1,2,3,4};Handle file(CreateFileW(foreign.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.foreign_generation_probe");DWORD count=0;check(WriteFile(file.value,bytes,4,&count,nullptr),"WriteFile.foreign_generation_probe");if(count!=4)throw Error("crash.probe_short_write",ERROR_WRITE_FAULT);log_.emit("foreign_generation_probe_created","\"file\":"+json(foreign.wstring().c_str())+",\"pid\":"+std::to_string(certificate.manager.pid)+",\"created\":"+std::to_string(certificate.manager.created-1)+",\"syntheticOwnershipCounterexample\":true");}
            sample();log_.emit("manager_contained_before_resume","\"pid\":"+std::to_string(info.dwProcessId)+",\"created\":"+std::to_string(certificate.manager.created)+",\"killPoint\":"+json(kill_at));check(ResumeThread(thread_.value)!=DWORD(-1),"ResumeThread.manager");close(thread_,"manager-thread");
            const HANDLE waits[]={ready_.value,process_.value};DWORD waited=WAIT_TIMEOUT;const auto wait_end=GetTickCount64()+120000;
            while(waited==WAIT_TIMEOUT&&GetTickCount64()<wait_end){waited=WaitForMultipleObjects(2,waits,FALSE,25);if(waited==WAIT_TIMEOUT)sample();}
            if(waited==WAIT_OBJECT_0){MemoryBarrier();if(block.ready!=1||block.manager!=info.dwProcessId||!memchr(block.reached,0,sizeof(block.reached))||kill_at!=block.reached)throw Error("crash.point_mismatch",ERROR_INVALID_DATA);
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            confirm_pending_images();
#endif
            hit=true;certificate.forced=true;certificate.point=block.reached;sample();certificate.desktops=desktops(manager_output_,true);DWORD count=0;check(GetProcessHandleCount(process_.value,&count),"GetProcessHandleCount.before_crash");log_.emit("terminate_manager","\"api\":\"TerminateProcess\",\"pid\":"+std::to_string(info.dwProcessId)+",\"created\":"+std::to_string(certificate.manager.created)+",\"point\":"+json(certificate.point)+",\"handleCount\":"+std::to_string(count));
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            confirm_pending_images();
#endif
            check(TerminateProcess(process_.value,crash_exit),"TerminateProcess.manager_injection");}
            else if(waited!=WAIT_OBJECT_0+1)throw Error("crash.manager_timeout",waited==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());
            const auto exited=WaitForSingleObject(process_.value,10000);if(exited!=WAIT_OBJECT_0)throw Error("WaitForSingleObject.crashed_manager",exited==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());check(GetExitCodeProcess(process_.value,&code),"GetExitCodeProcess.manager");certificate.manager_exit=code;protocol=kill_at.empty()?!hit:(hit&&code==crash_exit);if(certificate.desktops.empty())certificate.desktops=desktops(manager_output_,hit||code==0);
            DWORD manager_handles=0;const auto handle_query=GetProcessHandleCount(process_.value,&manager_handles);const auto handle_error=handle_query?0:GetLastError();log_.emit("manager_handles_after_exit","\"nativeResult\":"+std::to_string(handle_query)+",\"error\":"+std::to_string(handle_error)+",\"count\":"+std::to_string(manager_handles));
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            confirm_pending_images();
#endif
            const auto end=GetTickCount64()+5000;while(!members(job_.value).empty()&&GetTickCount64()<end){sample();Sleep(10);}auto left=members(job_.value);log_.emit("job_after_manager_exit","\"remaining\":"+std::to_string(left.size())+",\"automaticCleanup\":"+(left.empty()?"true":"false"));
            if(!left.empty()){certificate.rescue=true;sample();
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            confirm_pending_images();
#endif
            check(TerminateJobObject(job_.value,ERROR_PROCESS_ABORTED),"TerminateJobObject.owned_survivors");const auto deadline=GetTickCount64()+5000;while(!members(job_.value).empty()&&GetTickCount64()<deadline)Sleep(10);}
            if(!members(job_.value).empty())throw Error("crash.job_not_empty",ERROR_BUSY);
            for(const auto& [identity,p]:owned_){(void)identity;if(WaitForSingleObject(p.handle.value,1000)!=WAIT_OBJECT_0)throw Error("crash.instance_not_exited",ERROR_BUSY);DWORD exit=0;check(GetExitCodeProcess(p.handle.value,&exit),"GetExitCodeProcess.owned_member");certificate.processes.push_back(p.identity);log_.emit("instance_exit_confirmed","\"pid\":"+std::to_string(p.identity.pid)+",\"created\":"+std::to_string(p.identity.created)+",\"exit\":"+std::to_string(exit));}
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            // The last drain sample can add a pending image diagnostic just
            // before the Job becomes empty. All exact handles above are now
            // signaled; resolve its evidence before closing those handles.
            confirm_pending_images();
#endif
            // Confirm termination and release our process references before
            // independently checking desktop absence. This order alone does
            // not prove the desktop lifecycle; that assertion remains explicit.
            for(auto& [identity,p]:owned_){(void)identity;close(p.handle,"pinned-instance");}close(process_,"manager-process");close(job_,"supervisor-job");
            quarantine(certificate.manager);if(std::filesystem::exists(store_/"session.fvs"))certificate.session_hash=r5003::hash(r5003::read(store_/"session.fvs"));
            close(ready_,"ready-event");close(release_,"release-event");check(UnmapViewOfFile(gate_->view),"UnmapViewOfFile.crash_gate");gate_->view=nullptr;close(gate_->handle,"gate-mapping");gate_.reset();close(stdout_,"manager-stdout");close(stderr_,"manager-stderr");close(stdin_,"manager-stdin");
            for(const auto& d:certificate.desktops){const auto deadline=GetTickCount64()+5000;while(desktop_exists(d)&&GetTickCount64()<deadline)Sleep(10);if(desktop_exists(d))throw Error("crash.desktop_retained",ERROR_BUSY);log_.emit("desktop_absent_confirmed","\"name\":"+json(d));}
            const auto encoded=encode(certificate);r5003::publish(out_/"owner.fvc",certificate_kind,encoded);auto done=owner_path(store_,certificate.manager);done+=L".done";r5003::publish(done,certificate_kind,encoded);clean=true;finished_=true;
        }catch(const Error& e){
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            primary_api=e.api;primary_error=e.code;
#endif
            log_.emit("supervisor_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"message\":"+json(e.what()));}catch(const std::exception& e){
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
            primary_api="std.exception";
#endif
            log_.emit("supervisor_failure","\"api\":\"std.exception\",\"message\":"+json(e.what()));}
        if(!finished_)try{failure_cleanup();}catch(const Error& e){log_.emit("failure_cleanup_error","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}
        std::ofstream summary(out_/"summary.json",std::ios::binary);summary<<"{\"protocolPassed\":"<<(protocol?"true":"false")<<",\"cleanupVerified\":"<<(clean?"true":"false")<<",\"forcedTermination\":"<<(hit?"true":"false")<<",\"managerPid\":"<<certificate.manager.pid<<",\"managerCreated\":"<<certificate.manager.created<<",\"managerExit\":"<<code<<",\"point\":"<<json(certificate.point)<<",\"explicitSurvivorCleanup\":"<<(certificate.rescue?"true":"false")<<",\"instances\":"<<certificate.processes.size()<<",\"desktops\":"<<certificate.desktops.size()
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        <<",\"failureApi\":"<<json(primary_api)<<",\"failureError\":"<<primary_error
#endif
        <<"}\n";summary.flush();if(!summary.good())return 2;return protocol&&clean&&finished_?0:2;
    }
};
}
int wmain(int argc,wchar_t** argv){std::filesystem::path failure_output;try{const r3::Args args(argc,argv);const auto out=std::filesystem::absolute(args.get(L"--output")),target=std::filesystem::absolute(args.get(L"--manager-output")),store=std::filesystem::absolute(args.get(L"--store"));r3::demand(!args.get(L"--output").empty()&&!args.get(L"--manager-output").empty()&&!args.get(L"--store").empty()&&!std::filesystem::exists(out)&&!std::filesystem::exists(target),"new explicit supervisor/manager output directories");
    failure_output=out;std::filesystem::create_directories(out);
    std::filesystem::create_directories(r5004::owners(store));r305::Handle lock(CreateFileW((r5004::owners(store)/"lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));r305::check(lock.value!=INVALID_HANDLE_VALUE,"CreateFileW.session_owner_lock");
    r5004::Supervisor supervisor(out,target,store);return supervisor.run(std::filesystem::absolute(args.get(L"--manager")),std::filesystem::absolute(args.get(L"--client")),std::filesystem::absolute(args.get(L"--fixture")),args.get(L"--mode",L"restore"),args.get(L"--scenario",L"positive"),args.get(L"--previous"),r3::utf8(args.get(L"--kill-at")));
}catch(const std::exception& e){if(!failure_output.empty()&&!std::filesystem::exists(failure_output/"summary.json")){const auto native=dynamic_cast<const r305::Error*>(&e);std::ofstream summary(failure_output/"summary.json",std::ios::binary);summary<<"{\"protocolPassed\":false,\"cleanupVerified\":false,\"applicationsStarted\":false,\"managerPid\":0,\"managerCreated\":0,\"managerExit\":0,\"api\":"<<r305::json(native?native->api:"std.exception")<<",\"error\":"<<(native?native->code:0)<<",\"message\":"<<r305::json(e.what())<<"}\n";summary.flush();}std::cerr<<"SUPERVISOR_FAILURE "<<e.what()<<'\n';return 1;}}
