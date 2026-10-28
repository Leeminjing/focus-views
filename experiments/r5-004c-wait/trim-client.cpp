#include "../r5-002/clients/recorder_model.hpp"
#include "../r5-004c/run.hpp"
#include "profile.hpp"
#include "observe.hpp"
namespace {
class Trim : public r305::Experiment<RecorderClient> {
    void threads(r305::Child& v,const char* stage){using namespace r305;
        for(const auto tid:v.threads){Handle thread(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,tid));const auto opened_error=thread.value?0:GetLastError();DWORD error=opened_error;std::uint64_t created=0;
            if(thread.value){const auto owner=GetProcessIdOfThread(thread.value);if(owner!=v.p.pid)error=owner?ERROR_INVALID_OWNER:GetLastError();else{FILETIME c{},e{},k{},u{};if(GetThreadTimes(thread.value,&c,&e,&k,&u))created=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;else error=GetLastError();}}
            log_.emit("trim_thread_observation","\"stage\":"+json(stage)+",\"pid\":"+std::to_string(v.p.pid)+",\"tid\":"+std::to_string(tid)+",\"created\":"+std::to_string(created)+",\"error\":"+std::to_string(error)+",\"initializationCompletionKnown\":false");
        }
    }
public:
    using Base=r305::Experiment<RecorderClient>;
    Trim(const std::filesystem::path& manager,const std::filesystem::path& app,const std::filesystem::path& output):Base(manager,app,output){gui_=true;}
    int run(const std::filesystem::path& store,bool checkpoint,bool profile,bool cleanup=true,bool rebuild=true){using namespace r305;
        // Reduction step 1: only the existing native resource descriptor is
        // consumed. No memory/stack/CPU restore, SA/SB, artifacts or file work.
        Resources resources{};if(rebuild)resources=profile?wait_probe::resource_profile(store/"resource.bin",executable_):r5003::load(store/"S0.fvs",executable_).resources;
        std::unique_ptr<r5004a::AbortRegistry> abort;if(cleanup)abort=std::make_unique<r5004a::AbortRegistry>(exe_.parent_path(),out_,log_,true);
        else demand(!experimental::prepare_cleanup&&!experimental::stop_cleanup&&!experimental::release_cleanup,"fresh manager has no cleanup callbacks");
        auto v=start("trim",false);threads(*v,"ready");wait_probe::modules(*v,"ready",log_);bool native_cleanup=false;std::string first_api;DWORD first_error=0;
        try{
            v->resume();if(rebuild)resource(*v,2,"prepared",&resources);
            threads(*v,"resource-ready");wait_probe::modules(*v,"resource-ready",log_);
            log_.emit("trim_resource_preparation","\"performed\":"+std::string(rebuild?"true":"false"));
            if(cleanup)experimental::prepare_cleanup(experimental::cleanup_state,*v,job_.value);
            log_.emit("cleanup_runtime_installation","\"enabled\":"+std::string(cleanup?"true":"false")+",\"registryCreated\":"+(abort?"true":"false")+",\"prepareCalled\":"+(cleanup?"true":"false"));
            threads(*v,cleanup?"cleanup-runtime-ready":"cleanup-runtime-skipped");wait_probe::modules(*v,cleanup?"cleanup-runtime-ready":"cleanup-runtime-skipped",log_);
            threads(*v,"prepared");
            log_.emit("trim_prepared","\"pid\":"+std::to_string(v->p.pid)+",\"created\":"+std::to_string(r5003::creation(v->p.process.value))+",\"tid\":"+std::to_string(v->p.tid)+",\"window\":"+std::to_string(v->m.window)+",\"snapshotCpuApplied\":false,\"snapshotBusinessMemoryApplied\":false,\"checkpoint\":"+(checkpoint?"true":"false"));
            if(checkpoint){post(*v,prepare_stop_message);v->wait("checkpoint");threads(*v,"checkpoint-cleanup-start");v->stop();native_cleanup=true;}
            else{
                threads(*v,"normal-close-start");
                auto probe=launch(exe_.parent_path()/"close-probe.exe",v->desktop->full,{L"--pid",std::to_wstring(v->p.pid),L"--tid",std::to_wstring(v->p.tid),L"--created",std::to_wstring(r5003::creation(v->p.process.value)),L"--window",std::to_wstring(v->m.window),L"--log",(out_/"normal-close.jsonl").wstring()},CREATE_SUSPENDED,log_,"normal-close-probe");
                assign(probe);check(ResumeThread(probe.thread.value)!=DWORD(-1),"ResumeThread.normal_probe");const auto end=GetTickCount64()+5500;
                while(WaitForSingleObject(probe.process.value,0)==WAIT_TIMEOUT&&GetTickCount64()<end)v->poll(10);
                if(WaitForSingleObject(probe.process.value,0)!=WAIT_OBJECT_0)throw Error("trim.normal_probe_exit",ERROR_TIMEOUT);
                DWORD code=0;check(GetExitCodeProcess(probe.process.value,&code),"GetExitCodeProcess.normal_probe");probe.exited=true;log_.emit("process_exited","\"name\":\"normal-close-probe\",\"pid\":"+std::to_string(probe.pid)+",\"exitCode\":"+std::to_string(code));native_cleanup=code==0;
                if(!native_cleanup){
                    first_api="trim.normal_window_close_unconfirmed";first_error=ERROR_TIMEOUT;
                    try{
                    auto diagnostic=launch(exe_.parent_path()/"diagnose.exe",v->desktop->full,{L"--pid",std::to_wstring(v->p.pid),L"--tid",std::to_wstring(v->p.tid),L"--created",std::to_wstring(r5003::creation(v->p.process.value)),L"--log",(out_/"normal-diagnostic.jsonl").wstring()},CREATE_SUSPENDED,log_,"normal-close-diagnostic");assign(diagnostic);check(ResumeThread(diagnostic.thread.value)!=DWORD(-1),"ResumeThread.normal_diagnostic");
                    if(WaitForSingleObject(diagnostic.process.value,1500)!=WAIT_OBJECT_0){check(TerminateProcess(diagnostic.process.value,ERROR_TIMEOUT),"TerminateProcess.normal_diagnostic");check(WaitForSingleObject(diagnostic.process.value,500)==WAIT_OBJECT_0,"WaitForSingleObject.normal_diagnostic");}
                    DWORD exit=0;check(GetExitCodeProcess(diagnostic.process.value,&exit),"GetExitCodeProcess.normal_diagnostic");diagnostic.exited=true;log_.emit("process_exited","\"name\":\"normal-close-diagnostic\",\"pid\":"+std::to_string(diagnostic.pid)+",\"exitCode\":"+std::to_string(exit));
                    }catch(const Error& e){log_.emit("trim_diagnostic_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}
                }
                if(!native_cleanup)throw Error("trim.normal_window_close_unconfirmed",ERROR_TIMEOUT);
            }
        }catch(const Error& e){if(first_api.empty()){first_api=e.api;first_error=e.code;}log_.emit("trim_primary_failure","\"api\":"+json(first_api)+",\"error\":"+std::to_string(first_error));}
        try{v->stop();}catch(const Error& e){log_.emit("trim_secondary_cleanup_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));if(first_api.empty()){first_api=e.api;first_error=e.code;}}
        v.reset();
        log_.emit("trim_complete","\"nativeCleanupPassed\":"+std::string(native_cleanup?"true":"false")+",\"firstApi\":"+json(first_api)+",\"firstError\":"+std::to_string(first_error)+",\"restoredExecutionClaimed\":false");
        return native_cleanup&&first_api.empty()?0:2;
    }
};
}
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 4;}
extern "C" __declspec(dllexport) int RunCrashClient(const wchar_t* manager,const wchar_t* app,const wchar_t* output,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario,const wchar_t* gate,const wchar_t* previous){using namespace r305;try{
    const std::wstring name(scenario);const bool without_cleanup=name==L"normal-no-cleanup-runtime"||name==L"normal-new-window-direct";const bool rebuild=name!=L"normal-new-window-direct";const bool profile=name.starts_with(L"profile-")||without_cleanup;
    demand((profile&&!wcscmp(mode,L"save"))||(!profile&&!wcscmp(mode,L"restore")),"explicit preparation protocol, never execution restore");
    demand(name==L"trim-normal"||name==L"trim-checkpoint"||name==L"profile-normal"||name==L"profile-checkpoint"||without_cleanup,"explicit reduction close mode");
    r5004::Gate synchronization(gate,output);Log proof(std::filesystem::path(output)/"recovery.jsonl");if(!profile)r5004::verify_previous(previous,store,proof);
    proof.emit("reduction_scope","\"snapshotRead\":"+std::string(profile?"false":"true")+",\"sessionRead\":"+(profile?"false":"true")+",\"executionRestored\":false,\"profileOnly\":"+(profile?"true":"false"));
    r5004a::inline_control=true;Trim experiment(manager,app,output);return experiment.run(store,name.ends_with(L"checkpoint"),profile,!without_cleanup,rebuild);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
