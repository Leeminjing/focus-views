#include "protocol.hpp"
#include "../common/native.hpp"
#include <functional>
#include <map>
#include <set>
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
    check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,flags|CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&startup,&info),"CreateProcessW");
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
    Process p;
    std::unique_ptr<Desktop> desktop;
    std::unique_ptr<Mapping> shared;
    std::wstring shared_name;
    std::string name,halt;
    Metadata m{};
    std::uint64_t image=0;
    DEBUG_EVENT event{};bool pending=false;
    unsigned initialized=0,checkpoints=0;
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
    void stop(){if(!p.process.value||p.exited)return;stopping_=true;deadline=0;published=false;check(TerminateProcess(p.process.value,0xE3050001),"TerminateProcess.owned_GUI");if(pending)continue_event();const auto stop_end=GetTickCount64()+5000;while(!p.exited&&GetTickCount64()<stop_end)poll();demand(p.exited,"GUI exit confirmed");}
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
};
void write_bytes(const std::filesystem::path& path,const void* data,size_t size){std::ofstream f(path,std::ios::binary);demand(f.good(),"raw evidence file");f.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));demand(f.good(),"raw evidence write");}
struct Input {
    std::unique_ptr<Mapping> mapping;Process bridge;std::uint64_t sequence=0;LONG request=0;
    r304::Channel& c(){return *static_cast<r304::Channel*>(mapping->view);}
};
class Experiment {
    std::filesystem::path exe_,out_;Log log_;DebugPump pump_;Handle job_;DWORD metadata_rva_;std::wstring prefix_;
    unsigned failed_=0,serial_=0; bool primary_passed_=false; std::array<std::uint8_t,32> executable_;
    std::vector<Process> helpers_;
    bool gui_=false;
    std::wstring scenario_;
    void verify(const std::string& name,bool okay,const std::string& detail){if(!okay)++failed_;log_.emit("assertion","\"name\":"+json(name)+",\"passed\":"+(okay?"true":"false")+",\"detail\":"+json(detail));std::cout<<name<<": "<<(okay?"pass":"FAIL")<<" ("<<detail<<")\n";}
    void assign(Process& p){check(AssignProcessToJobObject(job_.value,p.process.value),"AssignProcessToJobObject");}
    Process& helper(Child& v,std::vector<std::wstring> args,const std::string& name){
        args.insert(args.end(),{L"--failure-log",(out_/v.name/("helper-"+std::to_string(++serial_)+"-failure.json")).wstring()});
        helpers_.push_back(launch(exe_,v.desktop->full,args,0,log_,name));auto& p=helpers_.back();assign(p);return p;
    }
    void confirm(Process& p,Child& v,DWORD timeout=5000){const auto end=GetTickCount64()+timeout;while(WaitForSingleObject(p.process.value,0)==WAIT_TIMEOUT && GetTickCount64()<end)v.poll();if(WaitForSingleObject(p.process.value,0)!=WAIT_OBJECT_0){TerminateProcess(p.process.value,ERROR_TIMEOUT);throw Error("helper_timeout",ERROR_TIMEOUT);}if(!p.exited){DWORD code=0;check(GetExitCodeProcess(p.process.value,&code),"GetExitCodeProcess.helper");p.exited=true;log_.emit("process_exited","\"name\":\"helper\",\"pid\":"+std::to_string(p.pid)+",\"exitCode\":"+std::to_string(code));}}
    void post(Child& v,UINT message,WPARAM wp=0){
        const auto name=L"Local\\"+prefix_+L"_reply_"+std::to_wstring(++serial_);Mapping reply(name,sizeof(Reply),true);new(reply.view)Reply{};
        auto& p=helper(v,{L"--role",L"bridge",L"--state",v.shared_name,L"--reply",name,L"--op",L"post",L"--target",L"top",L"--message",std::to_wstring(message),L"--wparam",std::to_wstring(wp)},"post");confirm(p,v);
        auto& r=*static_cast<Reply*>(reply.view);
        log_.emit("post_reply","\"view\":"+json(v.name)+",\"message\":"+std::to_string(message)+",\"done\":"+std::to_string(r.done)+",\"api\":"+json(r.api)+",\"error\":"+std::to_string(r.error));
        if(!r.done)throw Error("bridge.post.no_ack",ERROR_INVALID_STATE);
        if(r.error)throw Error("bridge.post/"+std::string(r.api),r.error);
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
        for(unsigned i=0;i<5;++i){v.wait("business");v.resume();if(v.checkpoints)break;
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
    Experiment(const std::filesystem::path& exe,const std::filesystem::path& out):exe_(exe),out_(out),log_(out/"controller.jsonl"),pump_(log_),metadata_rva_(native::metadata_rva(native::read_file(exe),"R305Metadata")),prefix_(L"fv_r305_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64())),executable_(native::digest(native::read_file(exe))){
        job_.value=CreateJobObjectW(nullptr,nullptr);check(job_.value!=nullptr,"CreateJobObjectW");JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&info,sizeof(info)),"SetInformationJobObject");
    }
    ~Experiment(){for(auto& p:helpers_)if(!p.exited){TerminateProcess(p.process.value,0xE3050002);WaitForSingleObject(p.process.value,3000);}}
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
        in->bridge=launch(exe_,v.desktop->full,{L"--role",L"input",L"--channel",r304::channel_name(v.p.pid,v.p.tid),L"--dll",(exe_.parent_path()/L"runtime304.dll").wstring(),L"--log",(out_/v.name/"input.jsonl").wstring()},0,log_,v.name+"-input");assign(in->bridge);
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
        log_.emit("resource_result","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"operation\":"+std::to_string(operation)+",\"sequence\":"+std::to_string(reply.done)+",\"error\":"+std::to_string(reply.error)+",\"api\":"+json(reply.api)+",\"resources\":"+resources_json(reply.observed));
        {std::ofstream f(out_/v.name/(std::string(phase)+"-resources.json"),std::ios::binary);f<<resources_json(reply.observed)<<'\n';}
        write_bytes(out_/v.name/(std::string(phase)+"-resources.bin"),&reply.observed,sizeof(reply.observed));
        if(reply.error)throw Error(reply.api,reply.error);return reply.observed;
    }
    void freeze(Child& v){post(v,freeze_message);run_until(v,[&]{return v.metadata().frozen==1;},"runtime.freeze");}
    void capture(Child& v,const std::string& label){const auto reply_name=L"Local\\"+prefix_+L"_png_"+std::to_wstring(++serial_);Mapping reply(reply_name,sizeof(Reply),true);auto* r=new(reply.view)Reply{};auto wide=std::wstring(label.begin(),label.end());
        auto& p=helper(v,{L"--role",L"bridge",L"--state",v.shared_name,L"--reply",reply_name,L"--op",L"capture",L"--target",L"top",L"--path",(out_/v.name/(label+".png")).wstring()},label+"-capture");confirm(p,v,5000);
        log_.emit("capture","\"name\":"+json(label)+",\"file\":"+json(v.name+"/"+label+".png")+",\"view\":"+json(v.name)+",\"api\":"+json(r->api)+",\"error\":"+std::to_string(r->error)+",\"counter\":"+std::to_string(r->print_update)+",\"beforeDraws\":"+std::to_string(r->before_draws)+",\"afterDraws\":"+std::to_string(r->after_draws)+",\"apiBeginMs\":"+std::to_string(r->api_begin_ms)+",\"apiEndMs\":"+std::to_string(r->api_end_ms)+",\"clientDx\":"+std::to_string(r->client_dx)+",\"clientDy\":"+std::to_string(r->client_dy));
        if(!r->done)throw Error("capture.no_ack",ERROR_INVALID_STATE);
        if(r->error)throw Error("capture/"+std::string(r->api),r->error);
        verify(label+".actual-GDI",r->after_draws>r->before_draws,"private native PrintWindow/WM_PRINT helper records actual target paint; script checks counter/control pixels");
    }
    Snapshot save(Child& v){v.wait("checkpoint");Snapshot s;s.meta=v.metadata();s.work=v.state();s.cpu=v.context();s.cpu.Rip=s.meta.checkpoint+1;s.executable=executable_;
        demand(s.meta.frozen && !s.meta.in_dispatch && !s.meta.paint_depth && s.cpu.Rsp>=stack_address+4096 && s.cpu.Rsp<stack_address+stack_size,"snapshot safe point bounds");
        s.memory.resize(data_size);native::read_memory(v.p.process.value,data_address,s.memory.data(),s.memory.size());
        s.stack.resize(stack_size-4096);native::read_memory(v.p.process.value,stack_address+4096,s.stack.data(),s.stack.size());
        s.cookie=native::read_value<std::uint64_t>(v.p.process.value,s.meta.cookie_address);s.complement=native::read_value<std::uint64_t>(v.p.process.value,s.meta.complement_address);
        const auto l=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[0]);const auto m=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[1]);const auto o=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[2]);s.expected=receipt(s.cpu.R10,l,m,o)^s.work.seed;
        log_.emit("snapshot","\"work\":"+work_json(s.work)+",\"rip\":"+std::to_string(s.cpu.Rip)+",\"rsp\":"+std::to_string(s.cpu.Rsp)+",\"registerR10\":"+std::to_string(s.cpu.R10)+",\"leaf\":"+std::to_string(l)+",\"middle\":"+std::to_string(m)+",\"outer\":"+std::to_string(o)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"refs\":"+std::to_string(s.meta.ref_count)+",\"localAddresses\":["+std::to_string(s.meta.local[0])+","+std::to_string(s.meta.local[1])+","+std::to_string(s.meta.local[2])+"]");
        log_.emit("raw_layout","\"dataAddress\":"+std::to_string(data_address)+",\"windowOffset\":"+std::to_string(offsetof(Work,window))+",\"ripOffset\":"+std::to_string(offsetof(CONTEXT,Rip))+",\"rspOffset\":"+std::to_string(offsetof(CONTEXT,Rsp))+",\"r10Offset\":"+std::to_string(offsetof(CONTEXT,R10))+",\"xmmOffset\":"+std::to_string(offsetof(CONTEXT,Xmm0)));
        write_bytes(out_/"S0-data.bin",s.memory.data(),s.memory.size());write_bytes(out_/"S0-context.bin",&s.cpu,sizeof(s.cpu));
        write_bytes(out_/"S0-metadata.bin",&s.meta,sizeof(s.meta));
        const auto active_offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/"S0-active-stack.bin",s.stack.data()+active_offset,s.stack.size()-active_offset);
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
            const auto first=static_cast<const BYTE*>(data);std::vector<BYTE> bytes(first,first+size);
            const auto hash=native::digest_text(native::digest(bytes));fields+=",\""+std::string(name)+"\":"+json(hash);
            material.insert(material.end(),bytes.begin(),bytes.end());
        };
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
        result.child=start(name);auto& v=*result.child;
        if(other)routing_guard(*other,v);
        v.resume();resource(v,2,"prepared",&s.resources);
        post(v,prepare_stop_message);v.wait("checkpoint");restore(v,s,"");
        const auto resources=resource(v,3,"initial");
        verify(name+".native-initial-S0",!wcscmp(resources.nodes[1].caption,s.resources.nodes[1].caption)&&resources.selection_start==s.resources.selection_start&&resources.selection_end==s.resources.selection_end&&resources.modified==s.resources.modified&&resources.focus==s.resources.focus&&!resources.can_undo,"original EDIT native state after actual restored C++ returns");
        capture(v,"initial");
        running(v);result.input=input(v);
        demand(failed_==0,"no failed evidence before publication");v.published=true;
        log_.emit("view_published","\"view\":"+json(name)+",\"pid\":"+std::to_string(v.p.pid)+",\"resourceExecutionFrameAndInputChannelVerified\":true");
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
    int run(const std::wstring& scenario){
        gui_=true;scenario_=scenario;
        log_.emit("case","\"scenario\":"+json(scenario.c_str())+",\"R4\":false,\"SA_SB_supported\":false");
        std::unique_ptr<Child> source;View a,b,c;bool completed=false,b_failed=false;
        try{
            source=start("source",true);source->resume();source->wait("init");source->resume();
            run_until(*source,[&]{return source->metadata().published==1;},"source.business_initialization");
            auto in=input(*source);select_prefix(*source,*in);close_input(*source,*in);in.reset();freeze(*source);
            const auto saved_resources=resource(*source,1,"S0");
            verify("source.native-EDIT-preconditions",saved_resources.focus==2&&saved_resources.selection_start==0&&saved_resources.selection_end==wcslen(saved_resources.nodes[1].caption)&&saved_resources.modified&&!saved_resources.can_undo,"native random unsaved text, nonempty selection and naturally empty undo; no source state rewriting by snapshotter");
            capture(*source,"S0");post(*source,checkpoint_message);until_checkpoint(*source);
            auto captured=save(*source);captured.resources=saved_resources;
            const Snapshot s=std::move(captured); // the only S0; every restore receives const Snapshot&
            write_bytes(out_/"S0-full-stack.bin",s.stack.data(),s.stack.size());
            write_bytes(out_/"S0-resources.bin",&s.resources,sizeof(s.resources));
            write_bytes(out_/"S0-returns.bin",s.returns.data(),sizeof(s.returns));
            const std::array<std::uint64_t,3> abi{s.cookie,s.complement,s.expected};
            write_bytes(out_/"S0-abi.bin",abi.data(),sizeof(abi));
            write_bytes(out_/"S0-executable.bin",s.executable.data(),s.executable.size());
            log_.emit("snapshot_published","\"count\":1,\"afterResourceCapture\":true");
            const auto original=fingerprint(s,"published");
            auto unchanged=[&](const char* phase){verify(std::string("S0.immutable-")+phase,fingerprint(s,phase)==original,"all original raw bytes, CPU, resources, references, returns and ABI remain unchanged");};
            verify("source.initialization-and-prefixes-once",source->initialized==1&&source->business[0]==1&&source->business[1]==1&&source->business[2]==1,"independent source breakpoint record");
            source->resume();source->wait("continued");verify("source.normal-return-chain",source->state().receipt==s.expected&&source->state().returned==7,"ordinary source C++ chain");source->resume();running(*source);
            auto changes=input(*source);character(*source,*changes,L'!');close_input(*source,*changes);changes.reset();post(*source,WM_CHAR,L'!');
            run_until(*source,[&]{return source->state().counter>s.work.counter&&source->state().length>s.work.length;},"source.changed");
            freeze(*source);const auto changed=resource(*source,3,"changed");capture(*source,"changed");
            verify("source.actually-changed",wcscmp(changed.nodes[1].caption,s.resources.nodes[1].caption)&&changed.can_undo,"source modified real EDIT after snapshot");
            log_.emit("source_changed","\"work\":"+work_json(source->state()));source->stop();unchanged("source-exited");
            build_view(a,"A",s);unchanged("A-created");
            if(scenario==L"fail-B"){
                const auto before_failure=observe(*a.child,"before-B-failure");
                try{build_view(b,"B",s,a.child.get());throw Error("B.failure-not-injected",ERROR_INVALID_STATE);}
                catch(const Error& e){
                    b_failed=e.api=="resource.restore.parent_relation"&&e.code==ERROR_INVALID_DATA&&b.child&&!b.child->published;
                    verify("B.expected-resource-failure",b_failed,e.api+" code="+std::to_string(e.code));
                    log_.emit("branch_creation_failed","\"view\":\"B\",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"published\":false");
                    if(!b_failed)throw;
                    b.child->stop();b.child.reset();unchanged("B-failure");
                }
                const auto after_failure=observe(*a.child,"after-B-failure-preserved");
                stable(before_failure,after_failure,"A.preserved-through-B-failure");
                verify("A.runs-through-B-failure",after_failure.work.counter>before_failure.work.counter,"A autonomous execution continues while failed B resources are cleaned");
                edit(a,L'A',L"A","after-B-failure");alive({a.child.get()},"A-survives-B");
            }else{
                build_view(b,"B",s,a.child.get());unchanged("B-created");
                alive({a.child.get(),b.child.get()},"AB-before-edits");
                const auto b_before=observe(*b.child,"before-A");
                edit(a,L'A',L"A","first-edit");
                stable(b_before,observe(*b.child,"after-A"),"B.unaffected-by-A");
                const auto a_before=observe(*a.child,"before-B");
                edit(b,L'B',L"B","first-edit");
                stable(a_before,observe(*a.child,"after-B"),"A.unaffected-by-B");
                const auto b_again=observe(*b.child,"before-A-again");
                edit(a,L'1',L"A1","second-edit");
                stable(b_again,observe(*b.child,"after-A-again"),"B.unaffected-by-A-again");
                const auto a_again=observe(*a.child,"before-B-again");
                edit(b,L'2',L"B2","second-edit");
                stable(a_again,observe(*a.child,"after-B-again"),"A.unaffected-by-B-again");
                alive({a.child.get(),b.child.get()},"AB-after-edits");unchanged("AB-edited");
            }
            if(scenario!=L"AB"){
                const auto a_before=observe(*a.child,"before-C");
                Observed b_before{};if(b.child)b_before=observe(*b.child,"before-C");
                build_view(c,"C",s,a.child.get());unchanged("C-created");
                const auto a_after=observe(*a.child,"after-C");stable(a_before,a_after,"A.preserved-through-C-creation");
                verify("A.runs-through-C-creation",a_after.work.counter>a_before.work.counter,"A autonomous counter advances while C resources and CET chain are restored");
                if(b.child){const auto b_after=observe(*b.child,"after-C");stable(b_before,b_after,"B.preserved-through-C-creation");verify("B.runs-through-C-creation",b_after.work.counter>b_before.work.counter,"B remains running during C creation");}
                const auto c_before=observe(*c.child,"before-final-A");
                edit(a,L'3',b_failed?L"A3":L"A13","final-edit");
                stable(c_before,observe(*c.child,"after-final-A"),"C.unaffected-by-A");
                if(b.child){const auto a_hold=observe(*a.child,"before-final-B");edit(b,L'4',L"B24","final-edit");stable(a_hold,observe(*a.child,"after-final-B"),"A.unaffected-by-final-B");}
                const auto a_hold=observe(*a.child,"before-C-edit");Observed b_hold{};if(b.child)b_hold=observe(*b.child,"before-C-edit");
                edit(c,L'C',L"C","first-edit");
                stable(a_hold,observe(*a.child,"after-C-edit"),"A.unaffected-by-C");if(b.child)stable(b_hold,observe(*b.child,"after-C-edit"),"B.unaffected-by-C");
                alive(b.child?std::vector<Child*>{a.child.get(),b.child.get(),c.child.get()}:std::vector<Child*>{a.child.get(),c.child.get()},"final-concurrent");
            }
            unchanged("final");
            for(auto* v:{&a,&b,&c})if(v->child){observe(*v->child,"final");capture(*v->child,"final");}
            completed=true;
        }catch(const Error& e){log_.emit("experiment_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}
        for(auto* v:{&c,&b,&a})if(v->child){
            try{if(v->input){close_input(*v->child,*v->input);v->input.reset();}v->child->stop();v->child.reset();}
            catch(const std::exception& e){++failed_;log_.emit("cleanup_error","\"error\":"+json(e.what()));}
        }
        if(source){source->stop();source.reset();}
        primary_passed_=completed&&!failed_;
        log_.emit("experiment_complete","\"failedAssertions\":"+std::to_string(failed_)+",\"multiViewRestorePassed\":"+(primary_passed_?"true":"false")+",\"BFailureIsolated\":"+(b_failed?"true":"false")+",\"generalGUIRestore\":false,\"SA_SB_restore\":false,\"R3Complete\":false,\"R4Started\":false,\"manualHostTest\":\"not_executed\"");
        return primary_passed_?0:2;
    }

};
}
}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;try{const r3::Args args(argc,argv);const auto role=args.get(L"--role",L"controller");if(role==L"fixture")return r305::fixture_main(args);if(role==L"bridge")return r3::bridge_main(args);if(role==L"input")return r304::input_main(args);
    r3::demand(role==L"controller","unknown role");const auto output=std::filesystem::absolute(args.get(L"--output"));r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output),"new output directory required");std::filesystem::create_directories(output);wchar_t path[32768]{};r3::check(GetModuleFileNameW(nullptr,path,32768)!=0,"GetModuleFileNameW");r305::Experiment experiment(path,output);const auto scenario=args.get(L"--scenario",L"ABC");r3::demand(scenario==L"AB" || scenario==L"ABC" || scenario==L"fail-B","unknown scenario");return experiment.run(scenario);
}catch(const std::exception& e){
    try{const r3::Args args(argc,argv);const auto path=args.get(L"--failure-log");if(!path.empty()){
        const auto native=dynamic_cast<const r3::Error*>(&e);
        std::ofstream file(std::filesystem::path(path),std::ios::binary);
        file<<"{\"message\":"<<r3::json(e.what())<<",\"api\":"<<r3::json(native?native->api:"std.exception")<<",\"error\":"<<(native?native->code:ERROR_UNHANDLED_EXCEPTION)<<"}\n";
    }}catch(...){}
    std::cout<<"R305A_FAILURE "<<e.what()<<'\n';return 1;
}}
