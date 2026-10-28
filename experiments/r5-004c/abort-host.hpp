#pragma once
#include "abort-channel.hpp"
#include "../r5-002/runtime/processes.hpp"
namespace r5004a {
inline bool inline_control=false,synthetic_block=false;
enum class AbortFault {none,wrong_thread,missing_mapping};
class AbortRegistry {
    struct Session {std::unique_ptr<r305::Mapping> mapping;r305::Process helper;r305::Log* log=nullptr;HANDLE job=nullptr;};
    std::map<DWORD,Session> sessions_;std::filesystem::path binaries_,output_;r305::Log& fallback_;AbortFault fault_;
public:
    AbortRegistry(const std::filesystem::path& binaries,const std::filesystem::path& output,r305::Log& fallback,bool enabled,AbortFault fault=AbortFault::none):binaries_(binaries),output_(output),fallback_(fallback),fault_(fault){
        if(!enabled)return;
        using namespace r305;experimental::cleanup_state=this;
        experimental::prepare_cleanup=[](void* p,Child& v,void* job){static_cast<AbortRegistry*>(p)->prepare(v,job);};
        experimental::stop_cleanup=[](void* p,Child& v){static_cast<AbortRegistry*>(p)->abort(v);};
        experimental::release_cleanup=[](void* p,Child& v){static_cast<AbortRegistry*>(p)->release(v);};
    }
    ~AbortRegistry(){using namespace r305;experimental::prepare_cleanup=nullptr;experimental::stop_cleanup=nullptr;experimental::release_cleanup=nullptr;experimental::cleanup_state=nullptr;
        if(!sessions_.empty())fallback_.emit("abort_registry_incomplete","\"sessions\":"+std::to_string(sessions_.size()));
    }
    void prepare(r305::Child& v,HANDLE job){using namespace r305;
        // The child is responsive here. No synchronous message is sent after
        // it subsequently enters the actual debug checkpoint.
        auto [it,inserted]=sessions_.try_emplace(v.p.pid);if(!inserted)throw Error("abort.duplicate_session",ERROR_INVALID_STATE);auto& s=it->second;
        s.job=job;s.log=&v.recorder();const auto created=process_created(v.p.process.value);if(!created)throw Error("GetProcessTimes.abort_target",GetLastError());
        const bool wrong_thread=fault_==AbortFault::wrong_thread;const auto tid=wrong_thread?GetCurrentThreadId():v.p.tid;
        const auto name=abort_name(v.p.pid,tid,created)+(fault_==AbortFault::missing_mapping?L"_missing":L"");s.mapping=std::make_unique<Mapping>(name,sizeof(AbortChannel),true);auto& c=*new(s.mapping->view)AbortChannel{};
        c.flow=inline_control?0u:1u;c.synthetic_block=synthetic_block?1u:0u;c.pid=v.p.pid;c.tid=tid;c.created=created;c.thread_created=thread_created(wrong_thread?GetCurrentThread():v.p.thread.value);if(!c.thread_created)throw Error("GetThreadTimes.abort_target",GetLastError());c.top=v.m.window;c.edit=v.m.edit;c.button=v.m.button;c.checkpoint=v.m.checkpoint;c.alternate=v.m.alt_checkpoint;
        if(wrong_thread)s.log->emit("abort_thread_owner_fault","\"declaredPid\":"+std::to_string(c.pid)+",\"actualThreadOwner\":"+std::to_string(GetCurrentProcessId())+",\"tid\":"+std::to_string(tid)+",\"managerOwnedThreadOnly\":true");
        const auto evidence=output_/v.name;
        s.helper=launch(binaries_/"abort-helper.exe",v.desktop->full,{L"--pid",std::to_wstring(c.pid),L"--tid",std::to_wstring(c.tid),L"--map",name,L"--dll",(binaries_/"abort-runtime.dll").wstring(),L"--log",(evidence/"abort-helper.jsonl").wstring()},CREATE_SUSPENDED,*s.log,v.name+"-abort-helper");
        check(AssignProcessToJobObject(job,s.helper.process.value),"AssignProcessToJobObject.abort_helper");
        check(ResumeThread(s.helper.thread.value)!=DWORD(-1),"ResumeThread.abort_helper");
        auto runtime_failure=[&]{const auto found=experimental::preparation_failures.find(v.p.pid);if(found!=experimental::preparation_failures.end())throw Error(found->second.api,found->second.error);};
        const auto end=GetTickCount64()+5000;while(!InterlockedCompareExchange(&c.installed,0,0)&&GetTickCount64()<end){if(WaitForSingleObject(s.helper.process.value,0)!=WAIT_TIMEOUT)throw Error("abort.helper_install_exit",ERROR_BROKEN_PIPE);v.poll();runtime_failure();}
        runtime_failure();
        if(c.installed!=1)throw Error("abort.handler_install",c.error?c.error:ERROR_TIMEOUT);
        for(const auto& module:c.modules)s.log->emit("cleanup_target_module","\"view\":"+json(v.name)+",\"name\":"+json(module.name)+",\"base\":"+std::to_string(module.base)+",\"path\":"+json(module.path)+",\"error\":"+std::to_string(module.error));
        s.log->emit("abort_handler_prepared","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(c.pid)+",\"tid\":"+std::to_string(c.tid)+",\"created\":"+std::to_string(created)+",\"top\":"+std::to_string(c.top)+",\"edit\":"+std::to_string(c.edit)+",\"button\":"+std::to_string(c.button));
    }
    void abort(r305::Child& v){using namespace r305;
        if(experimental::source_writes_started.contains(v.p.pid))throw Error("abort.source_state_already_written",ERROR_INVALID_STATE);
        const auto it=sessions_.find(v.p.pid);if(it==sessions_.end())throw Error("abort.session_missing",ERROR_INVALID_STATE);auto& s=it->second;auto& c=*static_cast<AbortChannel*>(s.mapping->view);
        if(c.installed!=1||WaitForSingleObject(s.helper.process.value,0)!=WAIT_TIMEOUT||process_created(v.p.process.value)!=c.created)throw Error("abort.session_unhealthy",ERROR_BROKEN_PIPE);
        const auto address=reinterpret_cast<std::uint64_t>(v.event.u.Exception.ExceptionRecord.ExceptionAddress);
        if(v.event.dwDebugEventCode!=EXCEPTION_DEBUG_EVENT||v.event.dwThreadId!=c.tid||(address!=c.checkpoint&&address!=c.alternate))throw Error("abort.event_identity",ERROR_INVALID_DATA);
        const auto stopped=v.context();c.expected_rsp=stopped.Rsp;c.expected_ssp=stopped.R11;MemoryBarrier();InterlockedExchange(&c.arm,1);s.log->emit("abort_exception_dispatched","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(c.pid)+",\"tid\":"+std::to_string(c.tid)+",\"checkpoint\":"+std::to_string(address)+",\"sourceCpuWritten\":false,\"status\":2147549185");
        v.continue_event(DBG_EXCEPTION_NOT_HANDLED);const auto end=GetTickCount64()+5000;while(!v.p.exited&&GetTickCount64()<end)v.poll();
        DWORD exit=0;check(GetExitCodeProcess(v.p.process.value,&exit),"GetExitCodeProcess.abort");
        const bool okay=v.p.exited&&InterlockedCompareExchange(&c.done,0,0)==1&&!c.error&&c.destroyed&&c.gone==7&&c.observed_tid==c.tid&&exit==abort_exit;
        s.log->emit("abort_native_window_cleanup","\"view\":"+json(v.name)+",\"observedTid\":"+std::to_string(c.observed_tid)+",\"destroyWindow\":"+(c.destroyed?"true":"false")+",\"goneMask\":"+std::to_string(c.gone)+",\"error\":"+std::to_string(c.error)+",\"processExited\":"+(v.p.exited?"true":"false")+",\"exit\":"+std::to_string(exit)+",\"verified\":"+(okay?"true":"false"));
        for(LONG i=0;i<std::min<LONG>(static_cast<LONG>(c.used),16);++i){const auto& r=c.phases[i];
            if(!r.committed)throw Error("abort.phase_uncommitted",ERROR_INVALID_DATA);
            s.log->emit("cleanup_phase","\"view\":"+json(v.name)+",\"flow\":"+std::to_string(c.flow)+",\"sequence\":"+std::to_string(i)+",\"stage\":"+std::to_string(r.stage)+",\"tid\":"+std::to_string(r.tid)+",\"targetTick\":"+std::to_string(r.tick)+",\"rip\":"+std::to_string(r.rip)+",\"rsp\":"+std::to_string(r.rsp)+",\"ssp\":"+std::to_string(r.ssp));
        }
        if(!okay){
            // Primary cleanup verdict is already fixed. Diagnostics cannot turn
            // a timeout into success and cannot prevent termination of this View.
            try{
                auto diagnostic=launch(binaries_/"diagnose.exe",v.desktop->full,{L"--pid",std::to_wstring(c.pid),L"--tid",std::to_wstring(c.tid),L"--created",std::to_wstring(c.created),L"--log",(output_/v.name/"cleanup-diagnostic.jsonl").wstring()},CREATE_SUSPENDED,*s.log,v.name+"-cleanup-diagnostic");
                struct OwnedDiagnostic {Process& process;Log& log;~OwnedDiagnostic(){if(!process.exited){const auto killed=TerminateProcess(process.process.value,ERROR_PROCESS_ABORTED);const auto error=killed?0:GetLastError();const auto waited=WaitForSingleObject(process.process.value,500);const auto wait_error=waited==WAIT_FAILED?GetLastError():0;try{log.emit("cleanup_diagnostic_emergency_exit","\"pid\":"+std::to_string(process.pid)+",\"terminateResult\":"+std::to_string(killed)+",\"error\":"+std::to_string(error)+",\"wait\":"+std::to_string(waited)+",\"waitError\":"+std::to_string(wait_error));}catch(...){std::cerr<<"cleanup diagnostic emergency logging failed\n";}}}} ownership{diagnostic,*s.log};
                check(AssignProcessToJobObject(s.job,diagnostic.process.value),"AssignProcessToJobObject.cleanup_diagnostic");check(ResumeThread(diagnostic.thread.value)!=DWORD(-1),"ResumeThread.cleanup_diagnostic");
                const auto waited=WaitForSingleObject(diagnostic.process.value,1500);bool killed=false;
                if(waited!=WAIT_OBJECT_0){check(TerminateProcess(diagnostic.process.value,ERROR_TIMEOUT),"TerminateProcess.cleanup_diagnostic");killed=true;check(WaitForSingleObject(diagnostic.process.value,500)==WAIT_OBJECT_0,"WaitForSingleObject.cleanup_diagnostic_exit");}
                DWORD diagnostic_exit=0;check(GetExitCodeProcess(diagnostic.process.value,&diagnostic_exit),"GetExitCodeProcess.cleanup_diagnostic");diagnostic.exited=true;
                s.log->emit("process_exited","\"name\":"+json(v.name+"-cleanup-diagnostic")+",\"pid\":"+std::to_string(diagnostic.pid)+",\"exitCode\":"+std::to_string(diagnostic_exit));
                s.log->emit("cleanup_diagnostic_finished","\"view\":"+json(v.name)+",\"boundedMilliseconds\":2000,\"forced\":"+(killed?"true":"false")+",\"error\":"+std::to_string(diagnostic_exit));
            }catch(const std::exception& e){s.log->emit("cleanup_diagnostic_failure","\"message\":"+json(e.what()));}
            throw Error("abort.native_cleanup_unconfirmed",c.error?c.error:ERROR_INVALID_STATE);
        }
    }
    void release(r305::Child& v){using namespace r305;const auto it=sessions_.find(v.p.pid);if(it==sessions_.end())return;auto& s=it->second;
        if(!v.p.exited)throw Error("abort.release_before_target_exit",ERROR_BUSY);
        const auto waited=WaitForSingleObject(s.helper.process.value,5000);if(waited!=WAIT_OBJECT_0)throw Error("WaitForSingleObject.abort_helper",waited==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError());
        DWORD exit=0;check(GetExitCodeProcess(s.helper.process.value,&exit),"GetExitCodeProcess.abort_helper");s.helper.exited=true;
        s.log->emit("process_exited","\"name\":"+json(v.name+"-abort-helper")+",\"pid\":"+std::to_string(s.helper.pid)+",\"exitCode\":"+std::to_string(exit));
        sessions_.erase(it);if(exit)throw Error("abort.helper_release",exit);
    }
};
}
