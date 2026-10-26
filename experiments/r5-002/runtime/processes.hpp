#pragma once
#include "abi.hpp"
#include "native.hpp"
#include "../../r4-001-gate-001/courier.hpp"
#include "../../r4-001-gate-001/launch-diagnostics.hpp"
#include <map>
#include <set>
#include <functional>
namespace r305 {
struct Log {
    std::ofstream file;
    explicit Log(const std::filesystem::path& p):file(p,std::ios::binary) { demand(file.good(),"controller log"); }
    void emit(const char* type,const std::string& fields) { file << "{\"type\":" << json(type) << ",\"tickMs\":" << GetTickCount64() << ',' << fields << "}\n"; file.flush(); }
};
inline std::wstring quote(const std::wstring& a) {
    std::wstring r=L"\""; size_t n=0;
    for(auto c:a) { if(c==L'\\') {++n;continue;} r.append(c==L'"'?2*n+1:n,L'\\'); n=0;r+=c;} r.append(n*2,L'\\'); return r+L'"';
}
struct Desktop {
    HDESK value=nullptr; std::wstring full;
    Log& log;
    Desktop(const std::wstring& name,Log& recorder):log(recorder) { value=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_ENUMERATE|DESKTOP_HOOKCONTROL,nullptr); check(value!=nullptr,"CreateDesktopW"); full=user_object_name(GetProcessWindowStation())+L"\\"+name;log.emit("desktop_created","\"name\":"+json(full.c_str())); }
    ~Desktop(){if(value){const auto okay=CloseDesktop(value);const auto error=okay?0:GetLastError();log.emit("desktop_closed","\"name\":"+json(full.c_str())+",\"closed\":"+(okay?"true":"false")+",\"error\":"+std::to_string(error));}}
};
struct Process {
    Handle process,thread; DWORD pid=0,tid=0; bool exited=false;
    Process()=default;Process(Process&&)=default;Process& operator=(Process&&)=default;
    ~Process(){if(process.value && !exited && WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){TerminateProcess(process.value,0xE3050003);WaitForSingleObject(process.value,3000);}}
};
inline Process launch(const std::filesystem::path& exe,const std::wstring& desktop,const std::vector<std::wstring>& args,DWORD flags,Log& log,const std::string& name) {
    std::wstring command=quote(exe.wstring());for(const auto& a:args)command+=L" "+quote(a);
    STARTUPINFOW startup{sizeof(startup)}; auto desk=desktop;startup.lpDesktop=desk.data(); PROCESS_INFORMATION info{};
    log.emit("process_launch_attempt","\"name\":"+json(name)+","+gate001::launch_state(exe,desktop,command,flags|CREATE_NO_WINDOW));
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,flags|CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&startup,&info)){
        const auto error=GetLastError();
        log.emit("process_launch_failed","\"name\":"+json(name)+",\"api\":\"CreateProcessW\",\"error\":"+std::to_string(error)+","+gate001::launch_state(exe,desktop,command,flags|CREATE_NO_WINDOW));
        throw Error("CreateProcessW",error);
    }
    Process p;p.process.value=info.hProcess;p.thread.value=info.hThread;p.pid=info.dwProcessId;p.tid=info.dwThreadId;
    log.emit("process_created","\"name\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"tid\":"+std::to_string(p.tid)+",\"debugged\":"+((flags&DEBUG_ONLY_THIS_PROCESS)?"true":"false"));return p;
}
class Child;
class DebugPump {
    Log& log_; DWORD owner_=GetCurrentThreadId();
    std::map<DWORD,Child*> branches_;
public:
    explicit DebugPump(Log& log):log_(log){}
    void owner(){demand(GetCurrentThreadId()==owner_,"all create/debug operations belong to one thread");}
    void add(Child& v);
    void remove(Child& v);
    void poll(DWORD timeout,const std::string& requester);
    void validate_recipient(const Child& v,const DEBUG_EVENT& event);
};
class Child {
    Log& log_; DebugPump& pump_; DWORD metadata_rva_; bool stopping_=false,startup_=false;
    std::uint64_t ntdll_=0,ntdll_end_=0;
public:
    Process p,courier;
    std::unique_ptr<Mapping> control;LONG control_sequence=0;
    std::unique_ptr<Desktop> desktop;
    std::unique_ptr<Mapping> shared;
    std::wstring shared_name;
    std::string name,halt;
    Metadata m{};ExecutionLayout layout{};
    std::uint64_t image=0;
    DEBUG_EVENT event{};bool pending=false;
    unsigned initialized=0,checkpoints=0,input_epoch=0;
    std::set<DWORD> threads; ULONGLONG deadline=0; std::string stage; bool registered=false,published=false;
    unsigned business[execution_capacity]{};
    unsigned prefix_count()const{unsigned n=0;for(auto x:business)n+=x;return n;}
    bool prefixes_once()const{for(unsigned i=0;i<layout.frames;++i)if(business[i]!=1)return false;return true;}
    Child(Log& log,DebugPump& pump,DWORD rva):log_(log),pump_(pump),metadata_rva_(rva){}
    ~Child(){try{stop();}catch(const std::exception& e){log_.emit("cleanup_error","\"error\":"+json(e.what()));}if(registered)pump_.remove(*this);}
    Metadata metadata(){
        m=native::read_value<Metadata>(p.process.value,image+metadata_rva_);
        if(m.magic!=signature||(m.abi_version!=1&&m.abi_version!=2)||m.machine!=IMAGE_FILE_MACHINE_AMD64||
            m.abi_bytes!=(m.abi_version==1?sizeof(Metadata):sizeof(MetadataV2)))throw Error("abi.header_mismatch",ERROR_REVISION_MISMATCH);
        if(m.resource_kinds!=15)throw Error("abi.unknown_required_resource",ERROR_NOT_SUPPORTED);
        if(m.pid&&m.image!=image)throw Error("abi.loaded_image_base",ERROR_INVALID_DATA);
        if(m.pid){
            demand(m.data_bytes>0&&m.data_bytes<=16*1024*1024&&m.stack_bytes>=65536&&m.stack_bytes<=32*1024*1024&&m.root%65536==0&&m.stack%65536==0,"declared region bounds");
            demand(m.root<=0x7fffffffffffULL-m.data_bytes&&m.stack<=0x7fffffffffffULL-m.stack_bytes&&(m.root+m.data_bytes<=m.stack||m.stack+m.stack_bytes<=m.root),"declared regions are separate user ranges");
        }
        layout={};
        if(m.abi_version==1){layout.frames=3;layout.returns=5;for(unsigned i=0;i<3;++i)layout.frame[i]={m.frame_entry[i],m.local[i],m.business_sites[i]};}
        else layout=native::read_value<ExecutionLayout>(p.process.value,image+metadata_rva_+sizeof(Metadata));
        if(layout.version!=1||layout.bytes!=sizeof(ExecutionLayout)||(layout.frames!=3&&layout.frames!=4)||layout.returns!=layout.frames+2)throw Error("abi.execution_depth",ERROR_NOT_SUPPORTED);
        if(m.pid){
            if(m.abi_version==2&&!layout.caller_entry)throw Error("abi.execution_caller_missing",ERROR_INVALID_DATA);
            for(unsigned i=0;i<layout.frames;++i){const auto& f=layout.frame[i];
                if(!f.entry||!f.business)throw Error("abi.execution_frame_missing",ERROR_INVALID_DATA);
                for(unsigned j=0;j<i;++j)if(layout.frame[j].entry==f.entry||layout.frame[j].business==f.business)throw Error("abi.execution_frame_duplicate",ERROR_INVALID_DATA);
                if(f.local&&(f.local<m.stack+4096||f.local>m.stack+m.stack_bytes-8||f.local%8))throw Error("abi.execution_local_bounds",ERROR_INVALID_DATA);
            }
        }
        return m;
    }
    CONTEXT context(){demand(pending,"context requires stopped debuggee");CONTEXT cpu{};cpu.ContextFlags=cpu_flags;check(GetThreadContext(p.thread.value,&cpu),"GetThreadContext");return cpu;}
    void continue_event(DWORD status=DBG_CONTINUE){pump_.owner();demand(pending && event.dwProcessId==p.pid && threads.contains(event.dwThreadId),"continue only matching PID/TID event");check(ContinueDebugEvent(p.pid,event.dwThreadId,status),"ContinueDebugEvent");pending=false;halt.clear();}
    void resume(bool trap=true){auto cpu=context();if(trap){cpu.Rip=reinterpret_cast<std::uint64_t>(event.u.Exception.ExceptionRecord.ExceptionAddress)+1;check(SetThreadContext(p.thread.value,&cpu),"SetThreadContext.advance_trap");}continue_event();}
    void poll(DWORD timeout=10){pump_.poll(timeout,name);}
    void receive(const DEBUG_EVENT& incoming){
        pump_.validate_recipient(*this,incoming);demand(!pending,"second uncontinued event for this PID");event=incoming;pending=true;
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT || event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT)threads.insert(event.dwThreadId);
        demand(threads.contains(event.dwThreadId),"event thread registered to routed PID");
        log_.emit("debug_event","\"view\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"tid\":"+std::to_string(event.dwThreadId)+",\"code\":"+std::to_string(event.dwDebugEventCode));
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){image=reinterpret_cast<std::uint64_t>(event.u.CreateProcessInfo.lpBaseOfImage);if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);}
        // Debug-event process/thread handles are closed by Windows when their
        // EXIT events are continued. Keep them alive until then; closing early
        // can reuse their handle-table slots for unrelated live IPC objects.
        else if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT){}
        else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){
            Handle file(event.u.LoadDll.hFile);if(file.value){wchar_t path[32768]{};if(GetFinalPathNameByHandleW(file.value,path,32768,FILE_NAME_NORMALIZED)){
                auto base=reinterpret_cast<std::uint64_t>(event.u.LoadDll.lpBaseOfDll);
                if(_wcsicmp(std::filesystem::path(path).filename().c_str(),L"ntdll.dll")==0){ntdll_=base;const auto dos=native::read_value<IMAGE_DOS_HEADER>(p.process.value,base);ntdll_end_=base+native::read_value<IMAGE_NT_HEADERS64>(p.process.value,base+dos.e_lfanew).OptionalHeader.SizeOfImage;}
            }}
        } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){
            const auto code=event.u.ExitProcess.dwExitCode;continue_event();check(WaitForSingleObject(p.process.value,1000)==WAIT_OBJECT_0,"WaitForSingleObject.debug_exit");p.exited=true;
            log_.emit("process_exited","\"name\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"exitCode\":"+std::to_string(code));if(!stopping_)throw Error("unexpected_debug_exit",code);return;
        } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT && !stopping_){
            auto& e=event.u.Exception;auto address=reinterpret_cast<std::uint64_t>(e.ExceptionRecord.ExceptionAddress);
            log_.emit("exception","\"view\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"code\":"+std::to_string(e.ExceptionRecord.ExceptionCode)+",\"address\":"+std::to_string(address)+",\"firstChance\":"+std::to_string(e.dwFirstChance));
            if(e.ExceptionRecord.ExceptionCode==0xE06D7363 && e.dwFirstChance){continue_event(DBG_EXCEPTION_NOT_HANDLED);return;}
            if(e.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT){
                std::ostringstream parameters;parameters<<'[';for(DWORD i=0;i<e.ExceptionRecord.NumberParameters;++i){if(i)parameters<<',';parameters<<e.ExceptionRecord.ExceptionInformation[i];}parameters<<']';
                auto cpu=context();std::uint64_t normal_return=0;SIZE_T count=0;
                const auto readable=ReadProcessMemory(p.process.value,reinterpret_cast<void*>(cpu.Rsp),&normal_return,8,&count);
                BYTE instruction[8]{};SIZE_T instruction_count=0;
                ReadProcessMemory(p.process.value,reinterpret_cast<void*>(cpu.Rip),instruction,8,&instruction_count);
                std::ostringstream bytes;for(SIZE_T i=0;i<instruction_count;++i)bytes<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(instruction[i]);
                log_.emit("continuation_fault","\"view\":"+json(name)+",\"exceptionCode\":"+std::to_string(e.ExceptionRecord.ExceptionCode)+",\"parameters\":"+parameters.str()+",\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"normalReturnReadable\":"+std::to_string(readable)+",\"normalReturn\":"+std::to_string(normal_return)+",\"instructionHex\":"+json(bytes.str()));
                throw Error("debug.exception",e.ExceptionRecord.ExceptionCode);
            }
            if(!startup_ && address>=ntdll_ && address<ntdll_end_)startup_=true;
            else{
                metadata();
                demand(event.dwThreadId==p.tid,"owned checkpoint GUI thread");
                if(address==m.ready)halt="ready";else if(address==m.init){halt="init";++initialized;}else if(address==m.checkpoint || address==m.alt_checkpoint){halt="checkpoint";++checkpoints;}else if(address==m.continued)halt="continued";else if(address==m.rejected)halt="rejected";else if(address==m.changed)halt="changed";
                else {bool found=false;for(unsigned i=0;i<layout.frames;++i)if(address==layout.frame[i].business){halt="business";++business[i];found=true;break;}if(!found)throw Error("unknown_experiment_breakpoint",ERROR_INVALID_DATA);}
                if(halt=="init" || halt=="business")log_.emit("business_effect","\"view\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"address\":"+std::to_string(address)+",\"initializations\":"+std::to_string(initialized)+",\"outer\":"+std::to_string(business[0])+",\"middle\":"+std::to_string(business[1])+",\"leaf\":"+std::to_string(business[2])+",\"prefixTotal\":"+std::to_string(prefix_count())+",\"declaredFrames\":"+std::to_string(layout.frames));
                log_.emit("halt","\"view\":"+json(name)+",\"phase\":"+json(halt)+",\"pid\":"+std::to_string(p.pid)+",\"initializations\":"+std::to_string(initialized)+",\"checkpoints\":"+std::to_string(checkpoints)+",\"published\":"+std::to_string(m.published)+",\"shadowPolicy\":"+std::to_string(m.shadow_policy)+",\"error\":"+std::to_string(m.rejected_error));return;
            }
        }
        const bool thread_exit=event.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT;const auto tid=event.dwThreadId;continue_event();if(thread_exit)threads.erase(tid);
    }
    void wait(const char* expected){stage=expected;deadline=GetTickCount64()+8000;try{while(!pending && GetTickCount64()<deadline)poll();demand(pending && halt==expected,(std::string("expected debug halt ")+expected+", got "+halt).c_str());deadline=0;}catch(...){deadline=0;throw;}}
    void stop(){
        if(courier.process.value && !courier.exited){
            InterlockedExchange(&static_cast<gate001::Courier*>(control->view)->stop,1);
            const auto end=GetTickCount64()+5000;
            while(WaitForSingleObject(courier.process.value,0)==WAIT_TIMEOUT && GetTickCount64()<end)poll();
            if(WaitForSingleObject(courier.process.value,0)!=WAIT_OBJECT_0){TerminateProcess(courier.process.value,ERROR_TIMEOUT);WaitForSingleObject(courier.process.value,1000);throw Error("courier.stop_timeout",ERROR_TIMEOUT);}
            DWORD code=0;check(GetExitCodeProcess(courier.process.value,&code),"GetExitCodeProcess.courier");courier.exited=true;
            log_.emit("process_exited","\"name\":"+json(name+"-courier")+",\"pid\":"+std::to_string(courier.pid)+",\"exitCode\":"+std::to_string(code));
        }
if(!p.process.value||p.exited)return;stopping_=true;deadline=0;published=false;check(TerminateProcess(p.process.value,0xE3050001),"TerminateProcess.owned_GUI");if(pending)continue_event();const auto stop_end=GetTickCount64()+5000;while(!p.exited&&GetTickCount64()<stop_end)poll();demand(p.exited,"GUI exit confirmed");}
};
inline void DebugPump::add(Child& v){owner();demand(!branches_.contains(v.p.pid),"duplicate live PID");branches_.emplace(v.p.pid,&v);v.registered=true;}
inline void DebugPump::remove(Child& v){owner();demand(v.p.exited,"unregister only confirmed exited PID");branches_.erase(v.p.pid);v.registered=false;}
inline void DebugPump::validate_recipient(const Child& v,const DEBUG_EVENT& e){
    owner();const auto found=branches_.find(e.dwProcessId);
    if(found==branches_.end() || found->second!=&v || e.dwProcessId!=v.p.pid)throw Error("debug.route.PID",ERROR_INVALID_OWNER);
    if(e.dwDebugEventCode!=CREATE_PROCESS_DEBUG_EVENT && e.dwDebugEventCode!=CREATE_THREAD_DEBUG_EVENT && !v.threads.contains(e.dwThreadId))throw Error("debug.route.TID",ERROR_INVALID_THREAD_ID);
}
inline void DebugPump::poll(DWORD timeout,const std::string& requester){
    owner();for(const auto& [pid,v]:branches_){(void)pid;if(v->deadline && !v->p.exited && GetTickCount64()>=v->deadline)throw Error("debug.branch_deadline",ERROR_TIMEOUT,v->name+"/"+v->stage);}
    DEBUG_EVENT event{};if(!WaitForDebugEvent(&event,timeout)){const auto error=GetLastError();if(error==ERROR_SEM_TIMEOUT)return;throw Error("WaitForDebugEvent",error);}
    const auto found=branches_.find(event.dwProcessId);if(found==branches_.end())throw Error("debug.route.unknown_PID",ERROR_INVALID_OWNER);
    auto& target=*found->second;validate_recipient(target,event);
    log_.emit("debug_route","\"ownerThread\":"+std::to_string(owner_)+",\"waiter\":"+json(requester)+",\"recipient\":"+json(target.name)+",\"eventPid\":"+std::to_string(event.dwProcessId)+",\"recipientPid\":"+std::to_string(target.p.pid)+",\"eventTid\":"+std::to_string(event.dwThreadId)+",\"code\":"+std::to_string(event.dwDebugEventCode));
    target.receive(event);
}

}
