#include "protocol.hpp"
#include "../common/native.hpp"
#include <functional>
#include "files.hpp"
#include "../r4-001-gate-001/courier.hpp"
#include <map>
#include <set>
#include "../r4-001-gate-001/launch-diagnostics.hpp"
namespace r305 {
namespace {
struct Log {
    std::ofstream file;
    explicit Log(const std::filesystem::path& p):file(p,std::ios::binary) { demand(file.good(),"controller log"); }
    void emit(const char* type,const std::string& fields) { file << "{\"type\":" << json(type) << ",\"tickMs\":" << GetTickCount64() << ',' << fields << "}\n"; file.flush(); }
};
std::wstring quote(const std::wstring& a) {
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
Process launch(const std::filesystem::path& exe,const std::wstring& desktop,const std::vector<std::wstring>& args,DWORD flags,Log& log,const std::string& name) {
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
    Metadata m{};
    std::uint64_t image=0;
    DEBUG_EVENT event{};bool pending=false;
    unsigned initialized=0,checkpoints=0,input_epoch=0;
    std::set<DWORD> threads; ULONGLONG deadline=0; std::string stage; bool registered=false,published=false;
    unsigned business[3]{};
    Child(Log& log,DebugPump& pump,DWORD rva):log_(log),pump_(pump),metadata_rva_(rva){}
    ~Child(){try{stop();}catch(const std::exception& e){log_.emit("cleanup_error","\"error\":"+json(e.what()));}if(registered)pump_.remove(*this);}
    Metadata metadata(){m=native::read_value<Metadata>(p.process.value,image+metadata_rva_);demand(m.magic==signature,"metadata identity");return m;}
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
                else if(address==m.business_sites[0]){halt="business";++business[0];}else if(address==m.business_sites[1]){halt="business";++business[1];}else if(address==m.business_sites[2]){halt="business";++business[2];}
                else throw Error("unknown_experiment_breakpoint",ERROR_INVALID_DATA);
                if(halt=="init" || halt=="business")log_.emit("business_effect","\"view\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"address\":"+std::to_string(address)+",\"initializations\":"+std::to_string(initialized)+",\"outer\":"+std::to_string(business[0])+",\"middle\":"+std::to_string(business[1])+",\"leaf\":"+std::to_string(business[2]));
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
    Work state(){return native::read_value<Work>(p.process.value,data_address);}
};
void DebugPump::add(Child& v){owner();demand(!branches_.contains(v.p.pid),"duplicate live PID");branches_.emplace(v.p.pid,&v);v.registered=true;}
void DebugPump::remove(Child& v){owner();demand(v.p.exited,"unregister only confirmed exited PID");branches_.erase(v.p.pid);v.registered=false;}
void DebugPump::validate_recipient(const Child& v,const DEBUG_EVENT& e){
    owner();const auto found=branches_.find(e.dwProcessId);
    if(found==branches_.end() || found->second!=&v || e.dwProcessId!=v.p.pid)throw Error("debug.route.PID",ERROR_INVALID_OWNER);
    if(e.dwDebugEventCode!=CREATE_PROCESS_DEBUG_EVENT && e.dwDebugEventCode!=CREATE_THREAD_DEBUG_EVENT && !v.threads.contains(e.dwThreadId))throw Error("debug.route.TID",ERROR_INVALID_THREAD_ID);
}
void DebugPump::poll(DWORD timeout,const std::string& requester){
    owner();for(const auto& [pid,v]:branches_){(void)pid;if(v->deadline && !v->p.exited && GetTickCount64()>=v->deadline)throw Error("debug.branch_deadline",ERROR_TIMEOUT,v->name+"/"+v->stage);}
    DEBUG_EVENT event{};if(!WaitForDebugEvent(&event,timeout)){const auto error=GetLastError();if(error==ERROR_SEM_TIMEOUT)return;throw Error("WaitForDebugEvent",error);}
    const auto found=branches_.find(event.dwProcessId);if(found==branches_.end())throw Error("debug.route.unknown_PID",ERROR_INVALID_OWNER);
    auto& target=*found->second;validate_recipient(target,event);
    log_.emit("debug_route","\"ownerThread\":"+std::to_string(owner_)+",\"waiter\":"+json(requester)+",\"recipient\":"+json(target.name)+",\"eventPid\":"+std::to_string(event.dwProcessId)+",\"recipientPid\":"+std::to_string(target.p.pid)+",\"eventTid\":"+std::to_string(event.dwThreadId)+",\"code\":"+std::to_string(event.dwDebugEventCode));
    target.receive(event);
}

struct Snapshot {
    Metadata meta; Work work; CONTEXT cpu{};
    std::vector<std::uint8_t> memory,stack;
    std::array<std::uint8_t,32> executable{};
    std::uint64_t cookie=0,complement=0,expected=0;
    std::array<std::uint64_t,5> returns{};
    Resources resources{};
    FileVersion file;
};
void write_bytes(const std::filesystem::path& path,const void* data,size_t size){std::ofstream f(path,std::ios::binary);demand(f.good(),"raw evidence file");f.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));demand(f.good(),"raw evidence write");}
struct Input {
    std::unique_ptr<Mapping> mapping;Process bridge;std::uint64_t sequence=0;LONG request=0;
    r304::Channel& c(){return *static_cast<r304::Channel*>(mapping->view);}
};
class Experiment {
    std::filesystem::path exe_,out_;Log log_;Actions actions_;DebugPump pump_;Handle job_;DWORD metadata_rva_;std::wstring prefix_;
    unsigned failed_=0,serial_=0; bool primary_passed_=false; std::array<std::uint8_t,32> executable_;

    bool gui_=false;
    std::wstring scenario_;
    void verify(const std::string& name,bool okay,const std::string& detail){if(!okay)++failed_;log_.emit("assertion","\"name\":"+json(name)+",\"passed\":"+(okay?"true":"false")+",\"detail\":"+json(detail));std::cout<<name<<": "<<(okay?"pass":"FAIL")<<" ("<<detail<<")\n";}
    void assign(Process& p){check(AssignProcessToJobObject(job_.value,p.process.value),"AssignProcessToJobObject");}
    void confirm(Process& p,Child& v,DWORD timeout=5000){const auto end=GetTickCount64()+timeout;while(WaitForSingleObject(p.process.value,0)==WAIT_TIMEOUT && GetTickCount64()<end)v.poll();if(WaitForSingleObject(p.process.value,0)!=WAIT_OBJECT_0){TerminateProcess(p.process.value,ERROR_TIMEOUT);throw Error("helper_timeout",ERROR_TIMEOUT);}if(!p.exited){DWORD code=0;check(GetExitCodeProcess(p.process.value,&code),"GetExitCodeProcess.helper");p.exited=true;log_.emit("process_exited","\"name\":\"helper\",\"pid\":"+std::to_string(p.pid)+",\"exitCode\":"+std::to_string(code));}}
    void ensure_courier(Child& v){
        if(v.courier.process.value)return;
        const auto name=v.shared_name+L"_courier";
        v.control=std::make_unique<Mapping>(name,sizeof(gate001::Courier),true);new(v.control->view)gate001::Courier{};
        v.courier=launch(exe_,v.desktop->full,{L"--role",L"courier",L"--courier",name,L"--state",v.shared_name,L"--pid",std::to_wstring(v.p.pid)},0,log_,v.name+"-courier");assign(v.courier);
        const auto deadline=GetTickCount64()+5000;auto& c=*static_cast<gate001::Courier*>(v.control->view);
        while(!c.ready&&GetTickCount64()<deadline){if(WaitForSingleObject(v.courier.process.value,0)!=WAIT_TIMEOUT)throw Error("courier.start_exit",ERROR_BROKEN_PIPE);v.poll();}
        if(!c.ready)throw Error("courier.start_timeout",ERROR_TIMEOUT);
        log_.emit("courier_ready","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.courier.pid));
    }
    void courier_command(Child& v,const std::wstring& reply,bool capture,UINT message=0,WPARAM wp=0,const std::filesystem::path& path={}){
        ensure_courier(v);auto& c=*static_cast<gate001::Courier*>(v.control->view);
        demand(c.done==v.control_sequence&&!c.stop,"one outstanding courier command");
        c.capture=capture?1:0;c.message=message;c.wp=wp;c.error=0;
        wcscpy_s(c.reply,reply.c_str());wcscpy_s(c.path,path.c_str());
        const auto sequence=++v.control_sequence;MemoryBarrier();InterlockedExchange(&c.request,sequence);
        const auto deadline=GetTickCount64()+5000;
        while(c.done!=sequence&&GetTickCount64()<deadline){
            if(WaitForSingleObject(v.courier.process.value,0)!=WAIT_TIMEOUT)throw Error("courier.command_exit",ERROR_BROKEN_PIPE);
            v.poll(); // another View's events, or this post's checkpoint, remain owned
        }
        if(c.done!=sequence)throw Error("courier.command_timeout",ERROR_TIMEOUT);
        MemoryBarrier();log_.emit("courier_command","\"view\":"+json(v.name)+",\"sequence\":"+std::to_string(sequence)+",\"capture\":"+(capture?"true":"false")+",\"workerPid\":"+std::to_string(v.courier.pid)+",\"result\":"+std::to_string(c.error));
    }

    void post(Child& v,UINT message,WPARAM wp=0){
        Actions::Ticket ticket;const bool application_input=message==WM_CHAR;
        const auto before=application_input?v.state():Work{};
        if(application_input){ticket=actions_.begin(v.name,"window.character");actions_.state(ticket,1);}
        try{
        const auto name=L"Local\\"+prefix_+L"_reply_"+std::to_wstring(++serial_);Mapping reply(name,sizeof(Reply),true);new(reply.view)Reply{};
        courier_command(v,name,false,message,wp);
        auto& r=*static_cast<Reply*>(reply.view);
        log_.emit("post_reply","\"view\":"+json(v.name)+",\"message\":"+std::to_string(message)+",\"done\":"+std::to_string(r.done)+",\"api\":"+json(r.api)+",\"error\":"+std::to_string(r.error));
        if(!r.done)throw Error("bridge.post.no_ack",ERROR_INVALID_STATE);
        if(r.error)throw Error("bridge.post/"+std::string(r.api),r.error);
        if(application_input){
            actions_.state(ticket,2,"{\"api\":\"PostMessageW\",\"applicationComplete\":false}");
            run_until(v,[&]{return v.state().length==before.length+1;},"window.character_effect");
            const auto now=v.state();demand(now.text[before.length]==wp,"actual root character handler");
            actions_.state(ticket,3,"{\"nativeEvidence\":"+json(v.name+"/events.jsonl")+",\"work\":"+work_json(now)+"}");
        }
        }catch(const Error& e){if(application_input)actions_.fail(ticket,e);throw;}
    }
    void run_until(Child& v,const std::function<bool()>& done,const char* api){
        struct Deadline {Child& v;~Deadline(){v.deadline=0;}} cleanup{v};
        v.stage=api;v.deadline=GetTickCount64()+7000;
        while(!done()&&GetTickCount64()<v.deadline){v.poll();if(v.pending)throw Error(api,ERROR_INVALID_STATE,"unexpected debug halt "+v.halt);}
        if(!done())throw Error(api,ERROR_TIMEOUT);
    }
public:
    std::uint64_t xstate(Child& v,const char* phase){
        // Read-only CET_U probe: mask setters configure the read buffer only.
        // No SetThreadContext write of CET state or shadow-page permission change.
        try{
            using Init=BOOL(WINAPI*)(PVOID,DWORD,PCONTEXT*,PDWORD,ULONG64);
            const auto init=reinterpret_cast<Init>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"InitializeContext2"));check(init!=nullptr,"GetProcAddress.InitializeContext2");
            const auto enabled=GetEnabledXStateFeatures();DWORD size=0;PCONTEXT ctx=nullptr;
            SetLastError(0);const auto sized=init(nullptr,cpu_flags|CONTEXT_XSTATE,&ctx,&size,enabled|XSTATE_MASK_CET_U);
            if(sized || GetLastError()!=ERROR_INSUFFICIENT_BUFFER)throw Error("InitializeContext2.size",GetLastError());
            std::vector<BYTE> bytes(size);check(init(bytes.data(),cpu_flags|CONTEXT_XSTATE,&ctx,&size,enabled|XSTATE_MASK_CET_U),"InitializeContext2.buffer");
            check(SetXStateFeaturesMask(ctx,XSTATE_MASK_CET_U),"SetXStateFeaturesMask.read_request");check(GetThreadContext(v.p.thread.value,ctx),"GetThreadContext.CET_U");
            DWORD64 mask=0;check(GetXStateFeaturesMask(ctx,&mask),"GetXStateFeaturesMask");DWORD length=0;const auto cet=static_cast<PXSAVE_CET_U_FORMAT>(LocateXStateFeature(ctx,XSTATE_CET_U,&length));
            const bool present=cet && length>=sizeof(XSAVE_CET_U_FORMAT) && (mask&XSTATE_MASK_CET_U);
            log_.emit("xstate_read","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"enabledMask\":"+std::to_string(enabled)+",\"returnedMask\":"+std::to_string(mask)+",\"cetPresent\":"+(present?"true":"false")+",\"cetControl\":"+std::to_string(present?cet->Ia32CetUMsr:0)+",\"ssp\":"+std::to_string(present?cet->Ia32Pl3SspMsr:0));
            demand(present && cet->Ia32CetUMsr==1,"CET_U must actually be enabled");return cet->Ia32Pl3SspMsr;
        }catch(const Error& e){log_.emit("xstate_read_unavailable","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));throw;}
    }
    std::array<std::uint64_t,5> chain(Child& v,const char* phase){
        demand(v.image==reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)),"same EXE layout for native unwind");
        auto cpu=v.context();const auto ssp=xstate(v,phase);demand(ssp==cpu.R11,"RDSSPQ and kernel CET_U agree at checkpoint");
        auto copy=VirtualAlloc(reinterpret_cast<void*>(stack_address),stack_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(copy!=nullptr,"VirtualAlloc.chain_copy");
        struct Release{void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release{copy};
        native::read_memory(v.p.process.value,stack_address+4096,static_cast<BYTE*>(copy)+4096,stack_size-4096);
        std::array<std::uint64_t,5> returns{};
        for(unsigned i=0;i<returns.size();++i){
            const auto rsp=cpu.Rsp;DWORD64 base=0;const auto entry=RtlLookupFunctionEntry(cpu.Rip,&base,nullptr);
            demand(rsp>=stack_address+4096 && rsp+8<=stack_address+stack_size,"chain read in managed stack");
            if(!entry){cpu.Rip=*reinterpret_cast<std::uint64_t*>(cpu.Rsp);cpu.Rsp+=8;}
            else{demand(base==v.image,"chain frame belongs to fixed EXE");PVOID handler=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,cpu.Rip,entry,&cpu,&handler,&frame,nullptr);}
            const auto shadow=native::read_value<std::uint64_t>(v.p.process.value,ssp+8*i);returns[i]=cpu.Rip;
            log_.emit("call_chain","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"index\":"+std::to_string(i)+",\"frameRsp\":"+std::to_string(rsp)+",\"returnSlot\":"+std::to_string(cpu.Rsp-8)+",\"normalReturn\":"+std::to_string(cpu.Rip)+",\"shadowAddress\":"+std::to_string(ssp+8*i)+",\"shadowReturn\":"+std::to_string(shadow));
            demand(cpu.Rip==shadow,"normal stack return matches target-owned shadow return");
        }
        return returns;
    }
    void until_checkpoint(Child& v){
        for(unsigned i=0;i<5;++i){v.wait("business");v.resume();
            const auto end=GetTickCount64()+8000;while(!v.pending && GetTickCount64()<end)v.poll();
            if(v.pending && v.halt=="checkpoint")return;
        }
        throw Error("checkpoint.business_prefix",ERROR_INVALID_STATE);
    }
    void unwind(const Snapshot& s){
        demand(s.meta.image==reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)),"diagnostic unwind requires identical loaded EXE base");
        auto buffer=VirtualAlloc(reinterpret_cast<void*>(stack_address),stack_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(buffer!=nullptr,"VirtualAlloc.local_unwind_copy");
        struct Release{void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release_buffer{buffer};
        memcpy(static_cast<BYTE*>(buffer)+4096,s.stack.data(),s.stack.size());auto cpu=s.cpu;unsigned found=0;
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(s.meta.image);
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(s.meta.image+dos->e_lfanew);
        const auto image_size=nt->OptionalHeader.SizeOfImage;
        auto primary=[&](DWORD64 base,const RUNTIME_FUNCTION* entry){
            for(unsigned depth=0;depth<16;++depth){
                demand(base==s.meta.image && entry->UnwindData+4<image_size,"unwind image bounds");
                const auto info=reinterpret_cast<const BYTE*>(base+entry->UnwindData);
                if(!((info[0]>>3)&UNW_FLAG_CHAININFO))return entry->BeginAddress;
                const auto offset=4+2*((static_cast<unsigned>(info[2])+1)&~1u);
                demand(entry->UnwindData+offset+sizeof(RUNTIME_FUNCTION)<=image_size,"chained unwind bounds");
                entry=reinterpret_cast<const RUNTIME_FUNCTION*>(info+offset);
            }throw Error("unwind.chain_depth",ERROR_INVALID_DATA);
        };
        for(unsigned i=0;i<8 && cpu.Rsp>=stack_address+4096 && cpu.Rsp<stack_address+stack_size && found!=7;++i){
            DWORD64 base=0;const auto entry=RtlLookupFunctionEntry(cpu.Rip,&base,nullptr);
            log_.emit("native_unwind_frame","\"index\":"+std::to_string(i)+",\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"functionRva\":"+std::to_string(entry?entry->BeginAddress:0));
            if(!entry){cpu.Rip=*reinterpret_cast<std::uint64_t*>(cpu.Rsp);cpu.Rsp+=8;continue;}
            for(unsigned n=0;n<3;++n){DWORD64 expected_base=0;const auto expected=RtlLookupFunctionEntry(s.meta.frame_entry[n],&expected_base,nullptr);if(expected && base==expected_base && primary(base,entry)==primary(expected_base,expected))found|=1u<<n;}
            PVOID handler=nullptr;DWORD64 frame=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,cpu.Rip,entry,&cpu,&handler,&frame,nullptr);
        }
        verify("source.native-unwind-sees-three-CPP-frames",found==7,"Windows unwind metadata independently walks leaf/middle/outer from captured raw stack; no guessed pointer scan");
    }
    Experiment(const std::filesystem::path& exe,const std::filesystem::path& out):exe_(exe),out_(out),log_(out/"controller.jsonl"),actions_(out/"actions.jsonl"),pump_(log_),metadata_rva_(native::metadata_rva(native::read_file(exe),"R305Metadata")),prefix_(L"fv_r305_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64())),executable_(native::digest(native::read_file(exe))){
        job_.value=CreateJobObjectW(nullptr,nullptr);check(job_.value!=nullptr,"CreateJobObjectW");JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&info,sizeof(info)),"SetInformationJobObject");
    }
    std::unique_ptr<Child> start(const std::string& name,bool source=false,bool wrong_chain=false){
        pump_.owner();std::filesystem::create_directories(out_/name);auto v=std::make_unique<Child>(log_,pump_,metadata_rva_);v->name=name;auto wide=std::wstring(name.begin(),name.end());v->desktop=std::make_unique<Desktop>(prefix_+L"_"+wide,log_);
        v->shared_name=L"Local\\"+prefix_+L"_"+wide;v->shared=std::make_unique<Mapping>(v->shared_name,sizeof(Shared),true);new(v->shared->view)Shared{};
        v->p=launch(exe_,v->desktop->full,{L"--role",L"fixture",L"--mode",source?L"source":L"restore",L"--state",v->shared_name,L"--log",(out_/name/"events.jsonl").wstring(),L"--gui",gui_?L"1":L"0",L"--wrong-chain",wrong_chain?L"1":L"0",L"--omit-ref",!source&&scenario_==L"unregistered-ref"?L"1":L"0",L"--unknown-child",source&&scenario_==L"unknown-child"?L"1":L"0",L"--bad-parent",!source&&scenario_==L"fail-B"&&name=="B"?L"1":L"0"},DEBUG_ONLY_THIS_PROCESS,log_,name);pump_.add(*v);assign(v->p);
        v->wait("ready");
        verify(name+".CET-and-CFG-enabled",(v->m.shadow_policy&1) && (v->m.cfg_policy&1),"read actual process policies; no mitigation policy is modified");
        log_.emit("security_policy","\"view\":"+json(name)+",\"shadowPolicy\":"+std::to_string(v->m.shadow_policy)+",\"cfgPolicy\":"+std::to_string(v->m.cfg_policy));
        log_.emit("runtime_prepared","\"view\":"+json(name)+",\"pid\":"+std::to_string(v->p.pid)+",\"image\":"+std::to_string(v->image)+",\"window\":"+std::to_string(v->m.window)+",\"shadowPolicy\":"+std::to_string(v->m.shadow_policy)+",\"originalStackLow\":"+std::to_string(v->m.original_stack_low)+",\"originalStackHigh\":"+std::to_string(v->m.original_stack_high)+",\"managedStackLow\":"+std::to_string(v->m.active_stack_low)+",\"managedStackHigh\":"+std::to_string(v->m.active_stack_high)+",\"reportedStackLow\":"+std::to_string(v->m.reported_stack_low)+",\"reportedStackHigh\":"+std::to_string(v->m.reported_stack_high));
        return v;
    }
    std::unique_ptr<Input> input(Child& v){
        const unsigned no=0;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,input_closed),&no,sizeof(no));
        auto in=std::make_unique<Input>();in->mapping=std::make_unique<Mapping>(r304::channel_name(v.p.pid,v.p.tid),sizeof(r304::Channel),true);auto& c=*new(in->mapping->view)r304::Channel{};
        c.pid=v.p.pid;c.tid=v.p.tid;c.top=v.m.window;c.edit=v.m.edit;c.enabled=1;c.virtualize=1;c.token=(GetTickCount64()<<16)^v.p.pid;
        in->bridge=launch(exe_,v.desktop->full,{L"--role",L"input",L"--channel",r304::channel_name(v.p.pid,v.p.tid),L"--dll",(exe_.parent_path()/L"runtime304.dll").wstring(),L"--log",(out_/v.name/("input-"+std::to_string(++v.input_epoch)+".jsonl")).wstring()},0,log_,v.name+"-input");assign(in->bridge);
        run_until(v,[&]{return c.ready!=0;},"input.install");demand(c.health==1 && c.ready==1,"existing R304 runtime installed");return in;
    }
    void submit(Child& v,Input& in,std::vector<r304::Packet> packets,bool close=false){auto& c=in.c();demand(c.health==1,"input healthy");r304::Packet end{};end.kind=r304::barrier;packets.push_back(end);c.count=static_cast<unsigned>(packets.size());c.command=close?2:1;
        for(unsigned i=0;i<c.count;++i){packets[i].sequence=++in.sequence;c.packets[i]=packets[i];}MemoryBarrier();InterlockedExchange(&c.request,++in.request);
        run_until(v,[&]{return c.command_done==in.request||c.health==-1;},"input.processed");if(c.health==-1)throw Error(c.error_api,static_cast<DWORD>(c.error));
        log_.emit("input_processed","\"view\":"+json(v.name)+",\"sequence\":"+std::to_string(in.sequence)+",\"consumed\":"+std::to_string(c.consumed)+",\"closed\":"+(close?"true":"false"));
        if(close)confirm(in.bridge,v);
    }
    void character(Child& v,Input& in,wchar_t ch){r304::Packet p{};p.kind=r304::character;p.wp=ch;submit(v,in,{p});}
    void keys(Child& v,Input& in,const std::vector<std::pair<unsigned,bool>>& events){std::vector<r304::Packet> packets;for(const auto& e:events){r304::Packet p{};p.kind=r304::key;p.vk=e.first;p.down=e.second?1:0;packets.push_back(p);}submit(v,in,packets);}
    void select_prefix(Child& v,Input& in){keys(v,in,{{VK_HOME,true},{VK_HOME,false},{VK_LSHIFT,true},{VK_END,true},{VK_END,false},{VK_LSHIFT,false}});}
    void mouse_click(Child& v,Input& in){r304::Packet down{};down.kind=r304::mouse;down.action=r304::left_down;down.x=90;down.y=170;auto up=down;up.action=r304::left_up;submit(v,in,{down,up});}
    void close_input(Child& v,Input& in){r304::Packet p{};p.kind=r304::release;submit(v,in,{p},true);verify(v.name+".input-clean-close",in.c().health==0 && in.c().restored && !in.c().pinned && !in.c().iat_installed,"unmodified R304 lifecycle releases state and restores both imports before checkpoint/exit");const unsigned yes=1;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,input_closed),&yes,sizeof(yes));}
    Resources resource(Child& v,unsigned operation,const char* phase,const Resources* payload=nullptr,unsigned fault=0){
        demand(!v.pending,"resource messages require responsive GUI thread, not a debug stop");
        ResourceService request{};request.operation=operation;request.sequence=++serial_;request.fault=fault;if(payload)request.payload=*payload;
        native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,service),&request,sizeof(request));post(v,resource_message);
        run_until(v,[&]{return v.metadata().service.done==request.sequence;},"resource.service_ack");const auto reply=v.metadata().service;
        log_.emit("resource_result","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"operation\":"+std::to_string(operation)+",\"sequence\":"+std::to_string(reply.done)+",\"error\":"+std::to_string(reply.error)+",\"api\":"+json(reply.api)+",\"diagnosticError\":"+std::to_string(reply.diagnostic_error)+",\"nativeResult\":"+std::to_string(reply.native_result)+",\"resources\":"+resources_json(reply.observed));
        {std::ofstream f(out_/v.name/(std::string(phase)+"-resources.json"),std::ios::binary);f<<resources_json(reply.observed)<<'\n';}
        write_bytes(out_/v.name/(std::string(phase)+"-resources.bin"),&reply.observed,sizeof(reply.observed));
        if(reply.error)throw Error(reply.api,reply.error);return reply.observed;
    }
    void freeze(Child& v){post(v,freeze_message);run_until(v,[&]{return v.metadata().frozen==1;},"runtime.freeze");}
    void capture(Child& v,const std::string& label){const auto reply_name=L"Local\\"+prefix_+L"_png_"+std::to_wstring(++serial_);Mapping reply(reply_name,sizeof(Reply),true);auto* r=new(reply.view)Reply{};auto wide=std::wstring(label.begin(),label.end());
        courier_command(v,reply_name,true,0,0,out_/v.name/(label+".png"));
        log_.emit("capture","\"name\":"+json(label)+",\"file\":"+json(v.name+"/"+label+".png")+",\"view\":"+json(v.name)+",\"api\":"+json(r->api)+",\"error\":"+std::to_string(r->error)+",\"counter\":"+std::to_string(r->print_update)+",\"beforeDraws\":"+std::to_string(r->before_draws)+",\"afterDraws\":"+std::to_string(r->after_draws)+",\"apiBeginMs\":"+std::to_string(r->api_begin_ms)+",\"apiEndMs\":"+std::to_string(r->api_end_ms)+",\"clientDx\":"+std::to_string(r->client_dx)+",\"clientDy\":"+std::to_string(r->client_dy));
        if(!r->done)throw Error("capture.no_ack",ERROR_INVALID_STATE);
        if(r->error)throw Error("capture/"+std::string(r->api),r->error);
        verify(label+".actual-GDI",r->after_draws>r->before_draws,"private native PrintWindow/WM_PRINT helper records actual target paint; script checks counter/control pixels");
    }
    Snapshot save(Child& v,const std::string& label="S0"){v.wait("checkpoint");Snapshot s;s.meta=v.metadata();s.work=v.state();s.cpu=v.context();s.cpu.Rip=s.meta.checkpoint+1;s.executable=executable_;
        demand(s.meta.frozen && !s.meta.in_dispatch && !s.meta.paint_depth && s.cpu.Rsp>=stack_address+4096 && s.cpu.Rsp<stack_address+stack_size,"snapshot safe point bounds");
        s.memory.resize(data_size);native::read_memory(v.p.process.value,data_address,s.memory.data(),s.memory.size());
        s.stack.resize(stack_size-4096);native::read_memory(v.p.process.value,stack_address+4096,s.stack.data(),s.stack.size());
        s.cookie=native::read_value<std::uint64_t>(v.p.process.value,s.meta.cookie_address);s.complement=native::read_value<std::uint64_t>(v.p.process.value,s.meta.complement_address);
        const auto l=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[0]);const auto m=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[1]);const auto o=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[2]);s.expected=receipt(s.cpu.R10,l,m,o)^s.work.seed;
        log_.emit("snapshot","\"label\":"+json(label)+",\"view\":"+json(v.name)+",\"work\":"+work_json(s.work)+",\"rip\":"+std::to_string(s.cpu.Rip)+",\"rsp\":"+std::to_string(s.cpu.Rsp)+",\"registerR10\":"+std::to_string(s.cpu.R10)+",\"leaf\":"+std::to_string(l)+",\"middle\":"+std::to_string(m)+",\"outer\":"+std::to_string(o)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"refs\":"+std::to_string(s.meta.ref_count)+",\"localAddresses\":["+std::to_string(s.meta.local[0])+","+std::to_string(s.meta.local[1])+","+std::to_string(s.meta.local[2])+"]");
        log_.emit("raw_layout","\"dataAddress\":"+std::to_string(data_address)+",\"windowOffset\":"+std::to_string(offsetof(Work,window))+",\"ripOffset\":"+std::to_string(offsetof(CONTEXT,Rip))+",\"rspOffset\":"+std::to_string(offsetof(CONTEXT,Rsp))+",\"r10Offset\":"+std::to_string(offsetof(CONTEXT,R10))+",\"xmmOffset\":"+std::to_string(offsetof(CONTEXT,Xmm0)));
        write_bytes(out_/(label+"-data.bin"),s.memory.data(),s.memory.size());write_bytes(out_/(label+"-context.bin"),&s.cpu,sizeof(s.cpu));
        write_bytes(out_/(label+"-metadata.bin"),&s.meta,sizeof(s.meta));
        const auto active_offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/(label+"-active-stack.bin"),s.stack.data()+active_offset,s.stack.size()-active_offset);
        s.returns=chain(v,"source-checkpoint");unwind(s);return s;
    }
    void restore(Child& v,const Snapshot& s,const std::string& fault){
        demand(v.pending && v.halt=="checkpoint" && !v.m.published,"restore private structurally rebuilt View only");
        if(strcmp(v.m.build,s.meta.build) || s.executable!=executable_ || v.image!=s.meta.image)throw Error("restore.build_or_image_identity",ERROR_REVISION_MISMATCH);
        const auto boot_cpu=v.context();const auto rebuilt=chain(v,"target-before-restore");
        const bool matched=rebuilt==s.returns && boot_cpu.Rsp==s.cpu.Rsp && reinterpret_cast<std::uint64_t>(v.event.u.Exception.ExceptionRecord.ExceptionAddress)==s.meta.checkpoint && !memcmp(v.m.local,s.meta.local,sizeof(s.meta.local));
        log_.emit("chain_match","\"view\":"+json(v.name)+",\"matched\":"+std::string(matched?"true":"false")+",\"sourceSsp\":"+std::to_string(s.cpu.R11)+",\"targetSsp\":"+std::to_string(boot_cpu.R11)+",\"sourceRsp\":"+std::to_string(s.cpu.Rsp)+",\"targetRsp\":"+std::to_string(boot_cpu.Rsp)+",\"initializations\":"+std::to_string(v.initialized)+",\"businessPrefixes\":"+std::to_string(v.business[0]+v.business[1]+v.business[2]));
        if(!matched)throw Error("restore.call_chain_mismatch",ERROR_INVALID_DATA);
        demand(v.initialized==0 && v.business[0]+v.business[1]+v.business[2]==0,"no source business replay");
        std::vector<BYTE> before_data(data_size);native::read_memory(v.p.process.value,data_address,before_data.data(),data_size);
        std::vector<BYTE> before_stack(s.stack.size());native::read_memory(v.p.process.value,stack_address+4096,before_stack.data(),before_stack.size());
        write_bytes(out_/v.name/"target-before-restore-data.bin",before_data.data(),before_data.size());
        const auto active=static_cast<size_t>(boot_cpu.Rsp-stack_address-4096);write_bytes(out_/v.name/"target-before-restore-stack.bin",before_stack.data()+active,before_stack.size()-active);write_bytes(out_/v.name/"target-before-restore-context.bin",&boot_cpu,sizeof(boot_cpu));
        if(gui_){
        demand(v.m.window_pid==v.p.pid && v.m.window_tid==v.p.tid && v.m.window_user_data==data_address &&
            !wcscmp(v.m.window_class,s.meta.window_class) && v.m.window_style==s.meta.window_style && v.m.window_ex_style==s.meta.window_ex_style &&
            !memcmp(&v.m.window_rect,&s.meta.window_rect,sizeof(RECT)),"native window descriptor/ownership matches supported fixed resource");
        log_.emit("window_resource_verified","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.m.window_pid)+",\"tid\":"+std::to_string(v.m.window_tid)+",\"class\":"+json(v.m.window_class)+",\"style\":"+std::to_string(v.m.window_style)+",\"exStyle\":"+std::to_string(v.m.window_ex_style)+",\"userData\":"+std::to_string(v.m.window_user_data)+",\"left\":"+std::to_string(v.m.window_rect.left)+",\"top\":"+std::to_string(v.m.window_rect.top)+",\"right\":"+std::to_string(v.m.window_rect.right)+",\"bottom\":"+std::to_string(v.m.window_rect.bottom));
        demand(v.m.active_stack_low==s.meta.active_stack_low && v.m.active_stack_high==s.meta.active_stack_high,"stack bounds match");
        if((v.m.shadow_policy&1) && boot_cpu.R11 && s.cpu.R11){
            const auto target_return=native::read_value<std::uint64_t>(v.p.process.value,boot_cpu.R11);
            const auto expected_return=*reinterpret_cast<const std::uint64_t*>(s.stack.data()+static_cast<size_t>(s.cpu.Rsp-stack_address-4096));
            log_.emit("shadow_stack_diagnostic","\"view\":"+json(v.name)+",\"sourceSsp\":"+std::to_string(s.cpu.R11)+",\"targetBootSsp\":"+std::to_string(boot_cpu.R11)+",\"targetShadowReturn\":"+std::to_string(target_return)+",\"savedNormalReturn\":"+std::to_string(expected_return)+",\"policy\":"+std::to_string(v.m.shadow_policy));
        }
        }
        log_.emit("reference_registry","\"view\":"+json(v.name)+",\"sourceTracked\":"+std::to_string(s.meta.ref_count)+",\"sourceRequired\":"+std::to_string(s.meta.required_count)+",\"targetTracked\":"+std::to_string(v.m.ref_count)+",\"targetRequired\":"+std::to_string(v.m.required_count));
        write_bytes(out_/v.name/"target-metadata-before-restore.bin",&v.m,sizeof(v.m));
        if(s.meta.ref_count!=7 || s.meta.required_count!=7 || v.m.ref_count!=7 || v.m.required_count!=7 || memcmp(s.meta.refs,s.meta.required_refs,7*sizeof(Reference)) || memcmp(v.m.refs,v.m.required_refs,7*sizeof(Reference)) || memcmp(s.meta.refs,v.m.refs,7*sizeof(Reference)))throw Error("restore.unregistered_live_HWND",ERROR_INVALID_DATA);
        auto old_handle=[&](std::uint64_t logical){return logical==1?s.meta.window:logical==2?s.meta.edit:logical==3?s.meta.button:0;};
        auto new_handle=[&](std::uint64_t logical){return logical==1?v.m.window:logical==2?v.m.edit:logical==3?v.m.button:0;};
        auto refs=std::vector<Reference>(s.meta.refs,s.meta.refs+s.meta.ref_count);
        for(auto ref:refs){const bool data=ref.address>=data_address && ref.address+sizeof(HWND)<=data_address+data_size;const bool stack=ref.address>=s.cpu.Rsp && ref.address+sizeof(HWND)<=stack_address+stack_size;
            if(!old_handle(ref.logical) || !(data||stack))throw Error("restore.unresolved_reference",ERROR_INVALID_DATA);
            const auto offset=static_cast<size_t>(ref.address-(data?data_address:stack_address+4096));std::uint64_t old=0;memcpy(&old,(data?s.memory:s.stack).data()+offset,8);if(old!=old_handle(ref.logical))throw Error("restore.reference_preimage",ERROR_INVALID_DATA);
        }
        if(gui_)demand(v.m.window!=s.meta.window && v.m.window!=0,"new native window identity");
        native::write_memory(v.p.process.value,data_address,s.memory.data(),s.memory.size());native::write_memory(v.p.process.value,stack_address+4096,s.stack.data(),s.stack.size());
        for(auto ref:refs){const auto handle=new_handle(ref.logical);native::write_memory(v.p.process.value,ref.address,&handle,8);log_.emit("resource_rebind","\"view\":"+json(v.name)+",\"logicalWindow\":"+std::to_string(ref.logical)+",\"address\":"+std::to_string(ref.address)+",\"oldHwnd\":"+std::to_string(old_handle(ref.logical))+",\"newHwnd\":"+std::to_string(handle));}
        if(fault!="cookie"){native::write_memory(v.p.process.value,v.m.cookie_address,&s.cookie,8);native::write_memory(v.p.process.value,v.m.complement_address,&s.complement,8);}
        auto memory=s.memory;for(const auto& ref:refs)if(ref.address>=data_address&&ref.address<data_address+data_size){const auto handle=new_handle(ref.logical);memcpy(memory.data()+static_cast<size_t>(ref.address-data_address),&handle,8);}std::vector<std::uint8_t> readback(data_size);native::read_memory(v.p.process.value,data_address,readback.data(),data_size);
        verify(v.name+".raw-memory-restored",readback==memory,"entire data page read back equals S0 except declared HWND relocation");
        auto expected_stack=s.stack;for(auto ref:refs)if(ref.address>=stack_address+4096){const auto handle=new_handle(ref.logical);memcpy(expected_stack.data()+static_cast<size_t>(ref.address-stack_address-4096),&handle,8);}
        std::vector<std::uint8_t> stack_readback(s.stack.size());native::read_memory(v.p.process.value,stack_address+4096,stack_readback.data(),stack_readback.size());
        verify(v.name+".raw-stack-restored",stack_readback==expected_stack,"entire managed stack read back equals S0 except explicitly registered live HWND references");
        if(fault.empty()){write_bytes(out_/v.name/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/v.name/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);}
        if(fault=="stack"){const auto offset=static_cast<size_t>(s.meta.local[0]-stack_address-4096);native::write_memory(v.p.process.value,s.meta.local[0],before_stack.data()+offset,8);}
        if(fault=="memory")native::write_memory(v.p.process.value,data_address,before_data.data(),8);
        auto cpu=s.cpu;cpu.R11=boot_cpu.R11;if(fault=="register")cpu.R10=boot_cpu.R10;
        if(fault=="context"){Handle denied(OpenThread(THREAD_GET_CONTEXT,FALSE,v.p.tid));check(denied.value!=nullptr,"OpenThread.without_SET_CONTEXT");if(!SetThreadContext(denied.value,&cpu))throw Error("SetThreadContext.restore.denied",GetLastError());throw Error("context_fault_not_rejected",ERROR_INVALID_STATE);}
        check(SetThreadContext(v.p.thread.value,&cpu),"SetThreadContext.restore");
        const LONG rebuilt_done=0;native::write_memory(v.p.process.value,v.m.rebuild_address,&rebuilt_done,sizeof(rebuilt_done));
        chain(v,"after-ordinary-context-restore");
        const auto got=v.context();verify(v.name+".CPU-context-readback",got.Rip==cpu.Rip && got.Rsp==cpu.Rsp && got.R10==cpu.R10 && got.MxCsr==cpu.MxCsr,"stopped target RIP/RSP/register and floating point control read back after SetThreadContext");
        write_bytes(out_/v.name/"target-context-before-continue.bin",&got,sizeof(got));
        native::read_memory(v.p.process.value,data_address,readback.data(),readback.size());native::read_memory(v.p.process.value,stack_address+4096,stack_readback.data(),stack_readback.size());
        write_bytes(out_/v.name/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/v.name/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);
        log_.emit("restored_context","\"view\":"+json(v.name)+",\"rip\":"+std::to_string(got.Rip)+",\"rsp\":"+std::to_string(got.Rsp)+",\"r10\":"+std::to_string(got.R10)+",\"published\":false");
        v.continue_event();v.wait("continued");const auto actual=v.state();
        xstate(v,"after-CPP-returns");
        log_.emit("execution_result","\"view\":"+json(v.name)+",\"work\":"+work_json(actual)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"restoredR10\":"+std::to_string(cpu.R10)+",\"fault\":"+json(fault));
        if(actual.receipt!=s.expected || actual.returned!=7)throw Error("restore.execution_receipt",ERROR_CRC);
        verify(v.name+".ordinary-CPP-return-chain",actual.receipt==s.expected && actual.returned==7 && v.initialized==0 && v.checkpoints==1 && v.business[0]+v.business[1]+v.business[2]==0,"actual leaf/middle/outer CALL/RET chain completed with restored register and volatile stack locals; structural checkpoint only, no business prefixes");
        verify(v.name+".original-S0-state",actual.seed==s.work.seed && actual.counter==s.work.counter && actual.clicks==s.work.clicks && !wcscmp(actual.text,s.work.text),"counter and unsaved text match S0 at continuation before next GUI iteration");
        log_.emit("continuation","\"view\":"+json(v.name)+",\"work\":"+work_json(actual)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"initializations\":"+std::to_string(v.initialized));
        if(gui_){unsigned yes=1;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,published),&yes,sizeof(yes));
        log_.emit("application_activated","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.p.pid)+",\"usableViewPublished\":false");v.resume();}
    }
    std::string fingerprint(const Snapshot& s,const std::string& phase){
        std::vector<BYTE> material;
        std::string fields="\"phase\":"+json(phase);
        auto part=[&](const char* name,const void* data,size_t size){
            const auto first=static_cast<const BYTE*>(data);std::vector<BYTE> bytes;if(size)bytes.assign(first,first+size);
            const auto hash=native::digest_text(native::digest(bytes));fields+=",\""+std::string(name)+"\":"+json(hash);
            material.insert(material.end(),bytes.begin(),bytes.end());
        };
        const auto file_manifest=s.file.describe();part("fileManifest",file_manifest.data(),file_manifest.size());part("fileContent",s.file.content.data(),s.file.content.size());
        part("memory",s.memory.data(),s.memory.size());part("stack",s.stack.data(),s.stack.size());
        part("context",&s.cpu,sizeof(s.cpu));part("resources",&s.resources,sizeof(s.resources));
        part("metadata",&s.meta,sizeof(s.meta));part("returns",s.returns.data(),sizeof(s.returns));
        const std::array<std::uint64_t,3> abi{s.cookie,s.complement,s.expected};
        part("abi",abi.data(),sizeof(abi));part("executable",s.executable.data(),sizeof(s.executable));
        const auto hash=native::digest_text(native::digest(material));log_.emit("snapshot_fingerprint",fields+",\"sha256\":"+json(hash));return hash;
    }
    struct View {
        std::unique_ptr<Child> child;
        std::unique_ptr<Input> input;
    };
    void running(Child& v){
        const unsigned no=0;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,frozen),&no,sizeof(no));
    }
    void alive(const std::vector<Child*>& views,const char* phase){
        std::string fields="\"phase\":"+json(phase)+",\"views\":[";
        std::set<DWORD> pids,tids;std::set<std::wstring> desks;
        for(size_t i=0;i<views.size();++i){auto& v=*views[i];
            const bool okay=v.published&&!v.p.exited&&!v.pending&&WaitForSingleObject(v.p.process.value,0)==WAIT_TIMEOUT&&!v.metadata().frozen;
            verify(v.name+".running-"+phase,okay,"published process is alive, not debug-stopped or runtime-frozen");
            pids.insert(v.p.pid);tids.insert(v.p.tid);desks.insert(v.desktop->full);
            if(i)fields+=',';fields+="{\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.p.pid)+",\"tid\":"+std::to_string(v.p.tid)+",\"desktop\":"+json(v.desktop->full.c_str())+"}";
        }
        verify(std::string(phase)+".distinct-native-environments",pids.size()==views.size()&&tids.size()==views.size()&&desks.size()==views.size(),"distinct native PID/TID/Desktop for simultaneous Views");
        log_.emit("simultaneously_running",fields+"]");
    }
    void routing_guard(Child& a,Child& b){
        demand(b.pending&&b.event.dwProcessId==b.p.pid,"use a real Windows B debug event as negative routing input");
        const auto event=b.event;const auto a_event=a.event;
        const auto a_count=a.checkpoints,b_count=b.checkpoints;const auto a_pending=a.pending;
        DWORD error=0;
        try{pump_.validate_recipient(a,event);}catch(const Error& e){error=e.code;}
        verify("debug.foreign-PID-not-consumed",error==ERROR_INVALID_OWNER&&a.pending==a_pending&&a.checkpoints==a_count&&b.checkpoints==b_count&&b.pending&&!memcmp(&event,&b.event,sizeof(event))&&!memcmp(&a_event,&a.event,sizeof(a_event)),"actual B ready event rejected for A before any mutation or ContinueDebugEvent");
        log_.emit("foreign_event_rejected","\"intendedView\":"+json(a.name)+",\"actualView\":"+json(b.name)+",\"eventPid\":"+std::to_string(event.dwProcessId)+",\"intendedPid\":"+std::to_string(a.p.pid)+",\"error\":"+std::to_string(error)+",\"eventPreserved\":true");
        auto bad=event;bad.dwThreadId=0;error=0;try{pump_.validate_recipient(b,bad);}catch(const Error& e){error=e.code;}
        verify("debug.foreign-TID-not-consumed",error==ERROR_INVALID_THREAD_ID&&b.pending&&!memcmp(&event,&b.event,sizeof(event)),"invalid TID rejected without consuming the genuine pending B event");
    }
    void build_view(View& result,const std::string& name,const Snapshot& s,Child* other=nullptr){
        bool workspace_owned=false;const auto workspace=out_/"workspace"/name;
        const auto ticket=actions_.begin(name,"snapshot.restore");actions_.state(ticket,1);
        try{
            result.child=start(name,false,name=="RB"&&scenario_==L"branch-failure");auto& v=*result.child;
            if(other)routing_guard(*other,v);
            v.resume();resource(v,2,"prepared",&s.resources);
            Files::restore(s.file,workspace,name=="RB"&&scenario_==L"file-version"?"version":name=="RB"&&scenario_==L"file-write"?"write":"");workspace_owned=true;
            const auto id=s.file.present?Files::identity(workspace/"document.txt"):Files::Identity{};
            log_.emit("workspace_restored","\"view\":"+json(name)+",\"file\":"+s.file.describe()+",\"targetVolume\":"+std::to_string(id.volume)+",\"targetObject\":"+std::to_string(id.object)+",\"directory\":"+json(workspace.wstring().c_str()));
            post(v,prepare_stop_message);v.wait("checkpoint");actions_.state(ticket,2);restore(v,s,"");
            const auto r=resource(v,3,"initial");
            verify(name+".native-initial-snapshot",!wcscmp(r.nodes[1].caption,s.resources.nodes[1].caption)&&r.selection_start==s.resources.selection_start&&r.selection_end==s.resources.selection_end&&r.modified==s.resources.modified&&r.focus==s.resources.focus&&r.can_undo==s.resources.can_undo,"native control and original Undo match selected Snapshot after actual C++ returns");
            if(s.file.present)verify(name+".private-file-version",Artifacts::digest(workspace/"document.txt")==s.file.sha256,"private workspace bytes match captured completed file version");
            else verify(name+".empty-original-workspace",std::filesystem::is_empty(workspace),"S0 carrier receives no later branch file");
            capture(v,"initial");running(v);result.input=input(v);
            r304::Packet move{};move.kind=r304::mouse;move.action=r304::move;move.x=40;move.y=85;submit(v,*result.input,{move});
            const auto ready=resource(v,3,"input-ready");
            verify(name+".input-processed-before-publication",static_cast<std::uint64_t>(result.input->c().consumed)==result.input->sequence&&!wcscmp(ready.nodes[1].caption,s.resources.nodes[1].caption)&&ready.selection_start==s.resources.selection_start&&ready.selection_end==s.resources.selection_end&&ready.modified==s.resources.modified&&ready.can_undo==s.resources.can_undo&&ready.focus==s.resources.focus,"owner GUI thread processed managed mouse move/barrier and native control readback without changing Snapshot state");
            demand(failed_==0,"no failed evidence before View publication");v.published=true;actions_.state(ticket,3,"{\"pid\":"+std::to_string(v.p.pid)+"}");
            log_.emit("view_published","\"view\":"+json(name)+",\"pid\":"+std::to_string(v.p.pid)+",\"resourceExecutionFrameAndInputChannelVerified\":true");
        }catch(const Error& e){
            if(result.child){if(result.input){close_input(*result.child,*result.input);result.input.reset();}result.child->stop();}
            if(workspace_owned)std::filesystem::remove_all(workspace);
            actions_.fail(ticket,e);log_.emit("view_restore_rejected","\"view\":"+json(name)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"published\":false,\"workspaceExists\":"+(std::filesystem::exists(workspace)?"true":"false"));throw;
        }
    }
    struct Observed {Resources resource;Work work;};
    Observed observe(Child& v,const char* phase){
        auto r=resource(v,3,phase);auto w=v.state();
        log_.emit("branch_observed","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"work\":"+work_json(w)+",\"resources\":"+resources_json(r)+",\"pending\":"+(v.pending?"true":"false")+",\"frozen\":"+std::to_string(v.m.frozen));
        return {r,w};
    }
    void stable(const Observed& before,const Observed& after,const std::string& label){
        const auto& a=before.resource;const auto& b=after.resource;
        verify(label,!wcscmp(a.nodes[1].caption,b.nodes[1].caption)&&a.selection_start==b.selection_start&&a.selection_end==b.selection_end&&a.modified==b.modified&&a.can_undo==b.can_undo&&a.focus==b.focus&&a.nodes[0].hwnd==b.nodes[0].hwnd&&a.nodes[1].hwnd==b.nodes[1].hwnd&&before.work.seed==after.work.seed&&before.work.receipt==after.work.receipt&&before.work.clicks==after.work.clicks&&!wcscmp(before.work.text,after.work.text)&&after.work.counter>=before.work.counter,"native EDIT/selection/focus/undo and non-clock managed state unchanged; autonomous counter may advance");
    }
    void edit(View& view,wchar_t character_value,const wchar_t* expected,const char* phase){
        auto& v=*view.child;character(v,*view.input,character_value);
        // Root WM_CHAR is a normal application input too: independently mutate
        // the restored data page, not just the OS-owned EDIT resource.
        const auto old=v.state();post(v,WM_CHAR,character_value);
        run_until(v,[&]{return v.state().length==old.length+1;},"input.root-character");
        const auto now=observe(v,phase);
        verify(v.name+"."+phase,!wcscmp(now.resource.nodes[1].caption,expected)&&now.resource.selection_start==wcslen(expected)&&now.resource.selection_end==wcslen(expected)&&now.resource.can_undo&&now.resource.modified&&now.resource.focus==2&&now.work.length==old.length+1&&now.work.text[old.length]==character_value,"real native EDIT receives WM_CHAR and root application independently updates managed text through its original input handler");
        capture(v,phase);
    }
    struct Produced {Actions::Ticket ticket;FileRequest receipt;};
    Produced file_action(View& view,unsigned operation,const std::filesystem::path& output,const std::filesystem::path& a={},const std::filesystem::path& b={}){
        auto& v=*view.child;const auto ticket=actions_.begin(v.name,operation==save_document?"application.save":"application.process_private_files");
        try{
            std::filesystem::create_directories(output.parent_path());
            FileRequest request{};request.action=ticket.sequence;request.operation=operation;
            wcscpy_s(request.path[0],a.c_str());wcscpy_s(request.path[1],b.c_str());wcscpy_s(request.path[2],output.c_str());
            wcscpy_s(request.evidence,(out_/v.name/"file-effects.jsonl").c_str());
            actions_.state(ticket,1,"{\"output\":"+json(output.wstring().c_str())+"}");
            native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,file),&request,sizeof(request));
            mouse_click(v,*view.input); // real native BUTTON / BN_CLICKED
            actions_.state(ticket,2,"{\"via\":\"managed-mouse/native-BUTTON/BN_CLICKED\"}");
            run_until(v,[&]{return v.metadata().file.done==request.action;},"application.file_completion");
            const auto receipt=v.metadata().file;
            log_.emit("application_file_effect","\"view\":"+json(v.name)+",\"action\":"+json(ticket.id)+",\"effect\":"+file_effect_json(receipt));
            if(receipt.error)throw Error(receipt.api,receipt.error);
            demand(receipt.pid==v.p.pid&&receipt.tid==v.p.tid&&!receipt.live_handles&&receipt.flushes==receipt.writes,"native file completion identity and closed handles");
            verify(v.name+".native-file-effect-"+std::to_string(ticket.sequence),Artifacts::digest(output)==receipt.sha256,"actual completed file bytes match target native WriteFile receipt");

            // Restore focus through ordinary mouse routing and End, preserving
            // undo; never use EM_SETSEL/WM_SETTEXT to manufacture the result.
            r304::Packet down{};down.kind=r304::mouse;down.action=r304::left_down;down.x=80;down.y=85;
            auto up=down;up.action=r304::left_up;submit(v,*view.input,{down,up});
            keys(v,*view.input,{{VK_END,true},{VK_END,false}});
            actions_.state(ticket,3,"{\"nativeEvidence\":"+json(v.name+"/file-effects.jsonl")+",\"effect\":"+file_effect_json(receipt)+"}");
            return {ticket,receipt};
        }catch(const Error& e){actions_.fail(ticket,e);throw;}
    }
    void persist(const Snapshot& s,const std::string& label){
        write_bytes(out_/(label+"-full-stack.bin"),s.stack.data(),s.stack.size());
        write_bytes(out_/(label+"-resources.bin"),&s.resources,sizeof(s.resources));
        write_bytes(out_/(label+"-returns.bin"),s.returns.data(),sizeof(s.returns));
        const std::array<std::uint64_t,3> abi{s.cookie,s.complement,s.expected};
        write_bytes(out_/(label+"-abi.bin"),abi.data(),sizeof(abi));
        write_bytes(out_/(label+"-executable.bin"),s.executable.data(),s.executable.size());
        const auto manifest=s.file.describe();write_bytes(out_/(label+"-file.json"),manifest.data(),manifest.size());write_bytes(out_/(label+"-file.bin"),s.file.content.data(),s.file.content.size());
        std::ofstream f(out_/(label+"-resources.json"),std::ios::binary);f<<resources_json(s.resources)<<'\n';
    }
    Snapshot terminal(View& view,const std::string& label){
        auto& v=*view.child;const auto ticket=actions_.begin(v.name,"snapshot."+label);actions_.state(ticket,1);
        try{close_input(v,*view.input);view.input.reset();freeze(v);
        const auto r=resource(v,1,label.c_str());
        const auto file=Files::capture(v.name,v.p.pid,v.metadata().file,out_/"workspace"/v.name);
        actions_.state(ticket,2);log_.emit("workspace_snapshot","\"snapshot\":"+json(label)+",\"file\":"+file.describe());capture(v,label);
        post(v,checkpoint_message);until_checkpoint(v);
        auto s=save(v,label);s.resources=r;s.file=file;persist(s,label);
        verify(label+".terminal-checkpoint",v.initialized==0&&v.checkpoints==2&&v.business[0]==1&&v.business[1]==1&&v.business[2]==1&&r.can_undo&&r.undo.valid&&r.undo.operations==1,"later GUI safe point captures actual modified data and fresh computation locals without re-running original initialization");
        actions_.state(ticket,3,file.describe());
        log_.emit("snapshot_published","\"label\":"+json(label)+",\"view\":"+json(v.name)+",\"file\":"+file.describe());return s;
        }catch(const Error& e){actions_.fail(ticket,e);throw;}
    }
    void verify_undo(View& view,const Snapshot& original,const wchar_t* replaced){
        auto& v=*view.child;
        const auto first=resource(v,4,"undo1");capture(v,"undo1");
        verify(v.name+".undo-restores-actual-preimage",!wcscmp(first.nodes[1].caption,original.resources.nodes[1].caption)&&first.selection_start==0&&first.selection_end==wcslen(first.nodes[1].caption)&&first.can_undo&&first.modified&&v.metadata().service.native_result!=0,"native EM_UNDO restores actual random preimage and selects restored text");
        const auto second=resource(v,4,"undo2");capture(v,"undo2");
        verify(v.name+".second-native-undo",!wcscmp(second.nodes[1].caption,replaced)&&second.selection_start==0&&second.selection_end==1&&second.can_undo&&second.modified&&v.metadata().service.native_result!=0,"second EM_UNDO reverses previous Undo using native EDIT semantics");
        character(v,*view.input,L'Z');const auto continued=resource(v,3,"continued-input");capture(v,"continued-input");
        verify(v.name+".input-after-Undo",!wcscmp(continued.nodes[1].caption,L"Z")&&continued.selection_start==1&&continued.selection_end==1&&continued.focus==2,"original native EDIT processes managed WM_CHAR after both Undo operations");
    }
    int run(const std::wstring& scenario){
        gui_=true;scenario_=scenario;bool completed=false,rejected=false,full=false;
        log_.emit("case","\"scenario\":"+json(scenario.c_str())+",\"gateClosed\":false,\"generalGUIRestore\":false");
        Artifacts artifacts(out_/"artifacts",actions_,[&](const char* type,const std::string& fields){log_.emit(type,fields);});
        std::unique_ptr<Child> source;View a,b,ra,rb,c;
        auto dispose=[&](View& v){if(v.child){if(v.input){close_input(*v.child,*v.input);v.input.reset();}v.child->stop();}};
        try{
            source=start("source",true);source->resume();source->wait("init");source->resume();
            run_until(*source,[&]{return source->metadata().published==1;},"source.business_initialization");
            auto in=input(*source);select_prefix(*source,*in);close_input(*source,*in);in.reset();freeze(*source);
            const auto resources=resource(*source,1,"S0");capture(*source,"S0");post(*source,checkpoint_message);until_checkpoint(*source);
            auto captured=save(*source);captured.resources=resources;const Snapshot s=std::move(captured);persist(s,"S0");const auto original=fingerprint(s,"S0-published");
            verify("source.initialization-once",source->initialized==1&&source->business[0]==1&&source->business[1]==1&&source->business[2]==1,"debugger independently observes source business only once");
            source->resume();source->wait("continued");source->resume();running(*source);post(*source,WM_CHAR,L'!');
            verify("source.changed-after-S0",source->state().length>s.work.length,"original execution advances after S0 before confirmed exit");source->stop();
            build_view(a,"A",s);build_view(b,"B",s,a.child.get());alive({a.child.get(),b.child.get()},"AB");
            auto unchanged=observe(*b.child,"before-A");edit(a,L'A',L"A","single-edit");stable(unchanged,observe(*b.child,"after-A"),"B.unchanged-by-A");
            unchanged=observe(*a.child,"before-B");edit(b,L'B',L"B","single-edit");stable(unchanged,observe(*a.child,"after-B"),"A.unchanged-by-B");
            const auto undo_a=resource(*a.child,3,"before-save"),undo_b=resource(*b.child,3,"before-save");
            const auto made_a=file_action(a,save_document,out_/"workspace/A/document.txt");
            const auto made_b=file_action(b,save_document,out_/"workspace/B/document.txt");
            const auto after_a=resource(*a.child,3,"after-save"),after_b=resource(*b.child,3,"after-save");
            auto undo_preserved=[](const Resources& x,const Resources& y){return !wcscmp(x.nodes[1].caption,y.nodes[1].caption)&&x.can_undo==y.can_undo&&x.selection_start==y.selection_start&&x.selection_end==y.selection_end&&x.modified==y.modified&&x.undo.valid&&y.undo.valid&&x.undo.operations==1&&y.undo.operations==1;};
            verify("native-BUTTON-preserves-Undo",undo_preserved(undo_a,after_a)&&undo_preserved(undo_b,after_b),"native save and mouse focus restoration do not edit the control or destroy its observed single Undo");
            auto& artifact_a=artifacts.publish("A",a.child->p.pid,1,made_a.ticket,made_a.receipt,out_/"workspace/A/document.txt");
            auto& artifact_b=artifacts.publish("B",b.child->p.pid,1,made_b.ticket,made_b.receipt,out_/"workspace/B/document.txt");
            artifacts.verify_all("published");
            const bool snapshot_fault=scenario==L"undo-unsupported"||scenario==L"file-open";
            if(snapshot_fault){
                if(scenario==L"undo-unsupported")character(*a.child,*a.input,L'2');
                Handle held;
                if(scenario==L"file-open"){held.value=CreateFileW((out_/"workspace/A/document.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held.value!=INVALID_HANDLE_VALUE,"CreateFileW.negative_outstanding_workspace_writer");log_.emit("outstanding_file_handle","\"view\":\"A\",\"ownerPid\":"+std::to_string(GetCurrentProcessId())+",\"access\":\"GENERIC_WRITE\",\"realHandle\":true");}
                
                try{terminal(a,"SA");throw Error("expected_snapshot_rejection_missing",ERROR_INVALID_STATE);}
                catch(const Error& e){rejected=scenario==L"undo-unsupported"?(e.api=="undo.unsupported_or_multiple_edits"&&e.code==ERROR_NOT_SUPPORTED):(e.api=="CreateFileW.snapshot_exclusive_closed_file"&&e.code==ERROR_SHARING_VIOLATION);verify("snapshot.real-rejection",rejected,e.api+" code="+std::to_string(e.code));if(!rejected)throw;log_.emit("snapshot_rejected","\"snapshot\":\"SA\",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"published\":false");}
                held.close();running(*a.child);a.input=input(*a.child);character(*a.child,*a.input,L'X');capture(*a.child,"source-after-rejection");
                const Snapshot sb=terminal(b,"SB");const auto bh=fingerprint(sb,"SB-published");b.child->stop();build_view(rb,"RB",sb,a.child.get());build_view(c,"C",s,rb.child.get());
                const auto imports=artifacts.import_private("C",artifact_a,artifact_b,out_/"workspace/C");file_action(c,process_documents,out_/"workspace/C/processed.txt",imports/"A.txt",imports/"B.txt");
                verify("failed-Snapshot-not-published",!std::filesystem::exists(out_/"SA-data.bin"),"unsupported GUI/file resource never publishes raw SA or a usable restore");
                verify("SB.immutable",fingerprint(sb,"final-SB")==bh,"other branch's saved state retained");alive({a.child.get(),rb.child.get(),c.child.get()},"negative-snapshot-survivors");
            }else{
                const Snapshot sa=terminal(a,"SA"),sb=terminal(b,"SB");const auto ah=fingerprint(sa,"SA-published"),bh=fingerprint(sb,"SB-published");
                verify("distinct-checkpoints",ah!=bh&&ah!=original&&bh!=original,"actual branch states, file versions and Undo graphs have distinct fingerprints");
                const auto apid=a.child->p.pid,bpid=b.child->p.pid;a.child->stop();b.child->stop();
                verify("original-AB-exited",WaitForSingleObject(a.child->p.process.value,0)==WAIT_OBJECT_0&&WaitForSingleObject(b.child->p.process.value,0)==WAIT_OBJECT_0,"owned A/B handles signal termination before terminal restore");
                log_.emit("terminal_sources_exited","\"A\":"+std::to_string(apid)+",\"B\":"+std::to_string(bpid));
                build_view(ra,"RA",sa);
                const auto ra_before=observe(*ra.child,"before-RB");
                const bool restore_fault=scenario==L"file-version"||scenario==L"file-write"||scenario==L"branch-failure";
                try{build_view(rb,"RB",sb,ra.child.get());if(restore_fault)throw Error("expected_restore_rejection_missing",ERROR_INVALID_STATE);}
                catch(const Error& e){if(!restore_fault)throw;const auto expected=scenario==L"file-version"?"restore.private_file_version":scenario==L"file-write"?"WriteFile.private_workspace":"restore.call_chain_mismatch";rejected=e.api==expected&&rb.child&&!rb.child->published&&!std::filesystem::exists(out_/"workspace/RB")&&!std::filesystem::exists(out_/"workspace/RB.restore-partial");verify("RB.private-restore-rejected",rejected,e.api+" code="+std::to_string(e.code));if(!rejected)throw;}
                stable(ra_before,observe(*ra.child,"after-RB"),"RA.unchanged-by-RB-restore");
                build_view(c,"C",s,ra.child.get());const auto c_initial=resource(*c.child,3,"original-S0");
                verify("C.original-state",!wcscmp(c_initial.nodes[1].caption,s.resources.nodes[1].caption)&&!c_initial.can_undo&&c.child->state().document_reads==0&&c.child->state().document_writes==0,"C resumes S0 with no later document state or Undo history");
                if(scenario==L"artifact-hash"){
                    const auto before=observe(*c.child,"before-bad-import");
                    try{artifacts.import_private("C",artifact_a,artifact_b,out_/"workspace/C","hash");throw Error("expected_import_rejection_missing",ERROR_INVALID_STATE);}
                    catch(const Error& e){rejected=e.api=="artifact.import_hash"&&e.code==ERROR_CRC;verify("C.hash-rejection",rejected&&!std::filesystem::exists(out_/"workspace/C/imports"),"failed batch rolls back and does not expose partial import");if(!rejected)throw;}
                    stable(before,observe(*c.child,"after-bad-import"),"C.unchanged-by-rejected-import");
                    // A separate, explicitly new Action imports the unchanged versions.
                }
                const auto imports=artifacts.import_private("C",artifact_a,artifact_b,out_/"workspace/C");
                const auto ra_hold=observe(*ra.child,"before-C-process");Observed rb_hold{};if(rb.child&&rb.child->published)rb_hold=observe(*rb.child,"before-C-process");
                const auto output=file_action(c,process_documents,out_/"workspace/C/processed.txt",imports/"A.txt",imports/"B.txt");
                verify("C.real-native-file-processing",output.receipt.reads==2&&output.receipt.writes==3&&output.receipt.closes==5&&output.receipt.live_handles==0&&std::string(output.receipt.read_bytes[0],output.receipt.read_size[0])==std::string(sa.file.content.begin(),sa.file.content.end())&&std::string(output.receipt.read_bytes[1],output.receipt.read_size[1])==std::string(sb.file.content.begin(),sb.file.content.end()),"application reads both fixed versions via actual ReadFile and writes only private copies/result");
                verify("C.does-not-write-restored-workspaces",Artifacts::digest(out_/"workspace/RA/document.txt")==sa.file.sha256&&(!(rb.child&&rb.child->published)||Artifacts::digest(out_/"workspace/RB/document.txt")==sb.file.sha256),"C native writes touch only its independent imports and result");
                stable(ra_hold,observe(*ra.child,"after-C-process"),"RA.unchanged-by-C");if(rb.child&&rb.child->published)stable(rb_hold,observe(*rb.child,"after-C-process"),"RB.unchanged-by-C");
                verify("work-file-native-identities",Files::distinct(out_/"workspace/RA/document.txt",out_/"workspace/A/document.txt")&&Files::distinct(out_/"workspace/RA/document.txt",artifact_a.directory/"data.bin")&&Files::distinct(imports/"A.txt",artifact_a.directory/"data.bin"),"restore/import create new native file objects, not shared paths or hardlinks");
                if(rb.child&&rb.child->published)verify("RB.private-work-file",Files::distinct(out_/"workspace/RB/document.txt",out_/"workspace/B/document.txt")&&Files::distinct(out_/"workspace/RA/document.txt",out_/"workspace/RB/document.txt"),"B terminal file version belongs to its new process workspace");
                verify_undo(ra,s,L"A");
                const auto changed_a=file_action(ra,save_document,out_/"workspace/RA/document.txt");
                verify("RA.private-write-isolation",changed_a.receipt.sha256!=sa.file.sha256&&Artifacts::digest(out_/"workspace/A/document.txt")==sa.file.sha256&&Artifacts::digest(out_/"workspace/B/document.txt")==sb.file.sha256,"new application save changes RA file without changing old work files or immutable versions");
                if(rb.child&&rb.child->published){
                    verify("RB.file-unaffected-by-RA-save",Artifacts::digest(out_/"workspace/RB/document.txt")==sb.file.sha256,"RA writes cannot change RB private file");
                    verify_undo(rb,s,L"B");file_action(rb,save_document,out_/"workspace/RB/document.txt");
                    verify("RA.file-unaffected-by-RB-save",Artifacts::digest(out_/"workspace/RA/document.txt")==changed_a.receipt.sha256,"RB writes cannot change RA private file");
                }
                select_prefix(*c.child,*c.input);edit(c,L'C',L"C","final-edit");
                alive(rb.child&&rb.child->published?std::vector<Child*>{ra.child.get(),rb.child.get(),c.child.get()}:std::vector<Child*>{ra.child.get(),c.child.get()},"final");
                verify("SA.immutable",fingerprint(sa,"final-SA")==ah,"terminal data/stack/CPU/Undo and workspace version unchanged");verify("SB.immutable",fingerprint(sb,"final-SB")==bh,"B terminal Snapshot unaffected by all new writes");full=scenario==L"positive";
            }
            verify("S0.immutable",fingerprint(s,"final-S0")==original,"C and all branch changes preserve original start");artifacts.verify_all("final");
            for(auto* v:{&a,&b,&ra,&rb,&c})if(v->child&&v->child->published&&!v->child->p.exited){observe(*v->child,"final");capture(*v->child,"final");}
            completed=true;
        }catch(const Error& e){log_.emit("experiment_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"message\":"+json(e.what()));}
        for(auto* v:{&c,&rb,&ra,&b,&a}){try{dispose(*v);v->child.reset();}catch(const std::exception& e){++failed_;log_.emit("cleanup_error","\"error\":"+json(e.what()));}}
        if(source){source->stop();source.reset();}primary_passed_=completed&&!failed_;
        log_.emit("experiment_complete","\"evidencePassed\":"+std::string(primary_passed_?"true":"false")+",\"declaredR4IntegrationPassed\":"+((primary_passed_&&full)?"true":"false")+",\"expectedRejectionDetected\":"+(rejected?"true":"false")+",\"failedAssertions\":"+std::to_string(failed_)+",\"generalGUIRestore\":false,\"projectComplete\":false,\"gateClosed\":false,\"manualHostTest\":\"not_executed\"");
        return primary_passed_?0:2;
    }

};
}
}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;try{const r3::Args args(argc,argv);const auto role=args.get(L"--role",L"controller");if(role==L"undo-probe")return r305::undo_probe(args);if(role==L"fixture")return r305::fixture_main(args);if(role==L"bridge")return r3::bridge_main(args);if(role==L"input")return r304::input_main(args);if(role==L"courier")return gate001::courier_main(args);
    r3::demand(role==L"controller","unknown role");const auto output=std::filesystem::absolute(args.get(L"--output"));r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output),"new output directory required");std::filesystem::create_directories(output);wchar_t path[32768]{};r3::check(GetModuleFileNameW(nullptr,path,32768)!=0,"GetModuleFileNameW");r305::Experiment experiment(path,output);const auto scenario=args.get(L"--scenario",L"positive");r3::demand(scenario==L"positive"||scenario==L"undo-unsupported"||scenario==L"file-open"||scenario==L"file-version"||scenario==L"file-write"||scenario==L"branch-failure"||scenario==L"artifact-hash","unknown scenario");return experiment.run(scenario);
}catch(const std::exception& e){
    try{const r3::Args args(argc,argv);const auto path=args.get(L"--failure-log");if(!path.empty()){
        const auto native=dynamic_cast<const r3::Error*>(&e);
        std::ofstream file(std::filesystem::path(path),std::ios::binary);
        file<<"{\"message\":"<<r3::json(e.what())<<",\"api\":"<<r3::json(native?native->api:"std.exception")<<",\"error\":"<<(native?native->code:ERROR_UNHANDLED_EXCEPTION)<<"}\n";
    }}catch(...){}
    std::cout<<"R305A_FAILURE "<<e.what()<<'\n';return 1;
}}
