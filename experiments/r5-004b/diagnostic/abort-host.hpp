#pragma once
#include "abort-channel.hpp"
#include "../../r5-002/runtime/processes.hpp"
#include <wct.h>
#ifdef FOCUS_VIEWS_ABORT_WAIT_DIAGNOSTIC
#include <dbghelp.h>
#endif
namespace r5004a {
enum class AbortFault {none,wrong_thread,missing_mapping};
class AbortRegistry {
    struct Session {std::unique_ptr<r305::Mapping> mapping;r305::Process helper;r305::Log* log=nullptr;};
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
        s.log=&v.recorder();const auto created=process_created(v.p.process.value);if(!created)throw Error("GetProcessTimes.abort_target",GetLastError());
        const bool wrong_thread=fault_==AbortFault::wrong_thread;const auto tid=wrong_thread?GetCurrentThreadId():v.p.tid;
        const auto name=abort_name(v.p.pid,tid,created)+(fault_==AbortFault::missing_mapping?L"_missing":L"");s.mapping=std::make_unique<Mapping>(name,sizeof(AbortChannel),true);auto& c=*new(s.mapping->view)AbortChannel{};
        c.pid=v.p.pid;c.tid=tid;c.created=created;c.thread_created=thread_created(wrong_thread?GetCurrentThread():v.p.thread.value);if(!c.thread_created)throw Error("GetThreadTimes.abort_target",GetLastError());c.top=v.m.window;c.edit=v.m.edit;c.button=v.m.button;c.checkpoint=v.m.checkpoint;c.alternate=v.m.alt_checkpoint;
        if(wrong_thread)s.log->emit("abort_thread_owner_fault","\"declaredPid\":"+std::to_string(c.pid)+",\"actualThreadOwner\":"+std::to_string(GetCurrentProcessId())+",\"tid\":"+std::to_string(tid)+",\"managerOwnedThreadOnly\":true");
        const auto evidence=output_/v.name;
        s.helper=launch(binaries_/"abort-helper.exe",v.desktop->full,{L"--pid",std::to_wstring(c.pid),L"--tid",std::to_wstring(c.tid),L"--map",name,L"--dll",(binaries_/"abort-runtime.dll").wstring(),L"--log",(evidence/"abort-helper.jsonl").wstring()},CREATE_SUSPENDED,*s.log,v.name+"-abort-helper");
        check(AssignProcessToJobObject(job,s.helper.process.value),"AssignProcessToJobObject.abort_helper");
        check(ResumeThread(s.helper.thread.value)!=DWORD(-1),"ResumeThread.abort_helper");
        auto runtime_failure=[&]{const auto found=experimental::preparation_failures.find(v.p.pid);if(found!=experimental::preparation_failures.end())throw Error(found->second.api,found->second.error);};
        const auto end=GetTickCount64()+5000;while(!InterlockedCompareExchange(&c.installed,0,0)&&GetTickCount64()<end){if(WaitForSingleObject(s.helper.process.value,0)!=WAIT_TIMEOUT)throw Error("abort.helper_install_exit",ERROR_BROKEN_PIPE);v.poll();runtime_failure();}
        runtime_failure();
        if(c.installed!=1)throw Error("abort.handler_install",c.error?c.error:ERROR_TIMEOUT);
        s.log->emit("abort_handler_prepared","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(c.pid)+",\"tid\":"+std::to_string(c.tid)+",\"created\":"+std::to_string(created)+",\"top\":"+std::to_string(c.top)+",\"edit\":"+std::to_string(c.edit)+",\"button\":"+std::to_string(c.button));
    }
    void abort(r305::Child& v){using namespace r305;
        if(experimental::source_writes_started.contains(v.p.pid))throw Error("abort.source_state_already_written",ERROR_INVALID_STATE);
        const auto it=sessions_.find(v.p.pid);if(it==sessions_.end())throw Error("abort.session_missing",ERROR_INVALID_STATE);auto& s=it->second;auto& c=*static_cast<AbortChannel*>(s.mapping->view);
        if(c.installed!=1||WaitForSingleObject(s.helper.process.value,0)!=WAIT_TIMEOUT||process_created(v.p.process.value)!=c.created)throw Error("abort.session_unhealthy",ERROR_BROKEN_PIPE);
        const auto address=reinterpret_cast<std::uint64_t>(v.event.u.Exception.ExceptionRecord.ExceptionAddress);
        if(v.event.dwDebugEventCode!=EXCEPTION_DEBUG_EVENT||v.event.dwThreadId!=c.tid||(address!=c.checkpoint&&address!=c.alternate))throw Error("abort.event_identity",ERROR_INVALID_DATA);
        InterlockedExchange(&c.arm,1);s.log->emit("abort_exception_dispatched","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(c.pid)+",\"tid\":"+std::to_string(c.tid)+",\"checkpoint\":"+std::to_string(address)+",\"sourceCpuWritten\":false,\"status\":2147549185");
        v.continue_event(DBG_EXCEPTION_NOT_HANDLED);const auto end=GetTickCount64()+5000;while(!v.p.exited&&GetTickCount64()<end)v.poll();
        DWORD exit=0;check(GetExitCodeProcess(v.p.process.value,&exit),"GetExitCodeProcess.abort");
        const bool okay=v.p.exited&&InterlockedCompareExchange(&c.done,0,0)==1&&!c.error&&c.destroyed&&c.gone==7&&c.observed_tid==c.tid&&exit==abort_exit;
        s.log->emit("abort_native_window_cleanup","\"view\":"+json(v.name)+",\"observedTid\":"+std::to_string(c.observed_tid)+",\"destroyWindow\":"+(c.destroyed?"true":"false")+",\"goneMask\":"+std::to_string(c.gone)+",\"error\":"+std::to_string(c.error)+",\"processExited\":"+(v.p.exited?"true":"false")+",\"exit\":"+std::to_string(exit)+",\"verified\":"+(okay?"true":"false"));
        if(!okay){
#ifdef FOCUS_VIEWS_ABORT_WAIT_DIAGNOSTIC
            s.log->emit("abort_native_phase","\"view\":"+json(v.name)+",\"phase\":"+std::to_string(c.phase));
            {Handle file(CreateFileW((output_/v.name/"abort-blocked.dmp").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.value!=INVALID_HANDLE_VALUE){const auto dumped=MiniDumpWriteDump(v.p.process.value,v.p.pid,file.value,static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo|MiniDumpWithFullMemoryInfo),nullptr,nullptr,nullptr);s.log->emit("abort_local_diagnostic_dump","\"nativeResult\":"+std::to_string(dumped)+",\"error\":"+std::to_string(dumped?0:GetLastError())+",\"publicArtifact\":false");}}
            const auto session=OpenThreadWaitChainSession(0,nullptr);
            if(session){WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT]{};DWORD count=WCT_MAX_NODE_COUNT;BOOL cycle=FALSE;const auto observed=GetThreadWaitChain(session,0,WCTP_GETINFO_ALL_FLAGS,v.p.tid,&count,nodes,&cycle);const auto error=observed?0:GetLastError();
                s.log->emit("abort_wait_chain","\"view\":"+json(v.name)+",\"nativeResult\":"+std::to_string(observed)+",\"error\":"+std::to_string(error)+",\"cycle\":"+std::to_string(cycle)+",\"nodes\":"+std::to_string(count));
                if(observed)for(DWORD i=0;i<count;++i){const auto& node=nodes[i];std::string fields="\"view\":"+json(v.name)+",\"index\":"+std::to_string(i)+",\"objectType\":"+std::to_string(node.ObjectType)+",\"status\":"+std::to_string(node.ObjectStatus);if(node.ObjectType==WctThreadType)fields+=",\"pid\":"+std::to_string(node.ThreadObject.ProcessId)+",\"tid\":"+std::to_string(node.ThreadObject.ThreadId);else fields+=",\"name\":"+json(node.LockObject.ObjectName);s.log->emit("abort_wait_node",fields);}CloseThreadWaitChainSession(session);
            }else s.log->emit("abort_wait_chain_unavailable","\"error\":"+std::to_string(GetLastError()));
#endif
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
