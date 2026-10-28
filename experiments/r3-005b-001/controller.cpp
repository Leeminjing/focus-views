#include "protocol.hpp"
#include "../common/native.hpp"
#include <functional>
#include "../r4-001-gate-001/courier.hpp"
#include "../r4-001-gate-001/launch-diagnostics.hpp"
namespace r305 {
namespace {
bool denied_launch_probe=false;
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
    // Gate-only fault: a directory is not an executable image. This asks the
    // real API to reject a launch without changing any token, ACL or policy.
    const auto image_path=denied_launch_probe && name=="source"?exe.parent_path():exe;
    log.emit("process_launch_attempt","\"name\":"+json(name)+",\"gateDirectoryImage\":"+(image_path!=exe?"true":"false")+","+gate001::launch_state(image_path,desktop,command,flags|CREATE_NO_WINDOW));
    if(!CreateProcessW(image_path.c_str(),command.data(),nullptr,nullptr,FALSE,flags|CREATE_NO_WINDOW,nullptr,image_path.parent_path().c_str(),&startup,&info)){
        const auto error=GetLastError();
        log.emit("process_launch_failed","\"name\":"+json(name)+",\"api\":\"CreateProcessW\",\"error\":"+std::to_string(error)+","+gate001::launch_state(image_path,desktop,command,flags|CREATE_NO_WINDOW));
        throw Error("CreateProcessW",error);
    }
    Process p;p.process.value=info.hProcess;p.thread.value=info.hThread;p.pid=info.dwProcessId;p.tid=info.dwThreadId;
    log.emit("process_created","\"name\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"tid\":"+std::to_string(p.tid)+",\"debugged\":"+((flags&DEBUG_ONLY_THIS_PROCESS)?"true":"false"));return p;
}
class Child {
    Log& log_; DWORD metadata_rva_; bool stopping_=false,startup_=false;
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
    unsigned initialized=0,checkpoints=0;
    unsigned business[3]{};
    Child(Log& log,DWORD rva):log_(log),metadata_rva_(rva){}
    ~Child(){try{stop();}catch(const std::exception& e){log_.emit("cleanup_error","\"error\":"+json(e.what()));}}
    Metadata metadata(){m=native::read_value<Metadata>(p.process.value,image+metadata_rva_);demand(m.magic==signature,"metadata identity");return m;}
    CONTEXT context(){demand(pending,"context requires stopped debuggee");CONTEXT cpu{};cpu.ContextFlags=cpu_flags;check(GetThreadContext(p.thread.value,&cpu),"GetThreadContext");return cpu;}
    void continue_event(DWORD status=DBG_CONTINUE){check(ContinueDebugEvent(p.pid,event.dwThreadId,status),"ContinueDebugEvent");pending=false;halt.clear();}
    void resume(bool trap=true){auto cpu=context();if(trap){cpu.Rip=reinterpret_cast<std::uint64_t>(event.u.Exception.ExceptionRecord.ExceptionAddress)+1;check(SetThreadContext(p.thread.value,&cpu),"SetThreadContext.advance_trap");}continue_event();}
    void poll(DWORD timeout=10){
        if(pending||p.exited)return;
        if(!WaitForDebugEvent(&event,timeout)){auto e=GetLastError();if(e==ERROR_SEM_TIMEOUT)return;throw Error("WaitForDebugEvent",e);}
        demand(event.dwProcessId==p.pid,"serialized debug pump PID ownership");pending=true;
        log_.emit("debug_event","\"view\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"tid\":"+std::to_string(event.dwThreadId)+",\"code\":"+std::to_string(event.dwDebugEventCode));
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){image=reinterpret_cast<std::uint64_t>(event.u.CreateProcessInfo.lpBaseOfImage);if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
            // Windows owns debug-event process/thread handles until ContinueDebugEvent
            // for EXIT. Closing early can recycle a live IPC object's handle slot.
        }
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
        continue_event();
    }
    void wait(const char* expected){const auto deadline=GetTickCount64()+8000;while(!pending && GetTickCount64()<deadline)poll();demand(pending && halt==expected,(std::string("expected debug halt ")+expected+", got "+halt).c_str());}
    void stop(){
        if(courier.process.value && !courier.exited){
            InterlockedExchange(&static_cast<gate001::Courier*>(control->view)->stop,1);
            const auto end=GetTickCount64()+5000;
            while(WaitForSingleObject(courier.process.value,0)==WAIT_TIMEOUT && GetTickCount64()<end)poll();
            if(WaitForSingleObject(courier.process.value,0)!=WAIT_OBJECT_0){TerminateProcess(courier.process.value,ERROR_TIMEOUT);WaitForSingleObject(courier.process.value,1000);throw Error("courier.stop_timeout",ERROR_TIMEOUT);}
            DWORD code=0;check(GetExitCodeProcess(courier.process.value,&code),"GetExitCodeProcess.courier");courier.exited=true;
            log_.emit("process_exited","\"name\":"+json(name+"-courier")+",\"pid\":"+std::to_string(courier.pid)+",\"exitCode\":"+std::to_string(code));
        }
if(!p.process.value||p.exited)return;stopping_=true;check(TerminateProcess(p.process.value,0xE3050001),"TerminateProcess.owned_GUI");if(pending)continue_event();const auto deadline=GetTickCount64()+5000;while(!p.exited&&GetTickCount64()<deadline)poll();demand(p.exited,"GUI exit confirmed");}
    Work state(){return native::read_value<Work>(p.process.value,data_address);}
};
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
    std::filesystem::path exe_,out_;Log log_;Handle job_;DWORD metadata_rva_;std::wstring prefix_;
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
        const auto name=L"Local\\"+prefix_+L"_reply_"+std::to_wstring(++serial_);Mapping reply(name,sizeof(Reply),true);new(reply.view)Reply{};
        courier_command(v,name,false,message,wp);
        auto& r=*static_cast<Reply*>(reply.view);check(r.done && !r.error,"bridge.post");
    }
    void run_until(Child& v,const std::function<bool()>& done,const char* api){const auto end=GetTickCount64()+7000;while(!done()&&GetTickCount64()<end){v.poll();if(v.pending)throw Error(api,ERROR_INVALID_STATE,"unexpected debug halt "+v.halt);}if(!done())throw Error(api,ERROR_TIMEOUT);}
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
    Experiment(const std::filesystem::path& exe,const std::filesystem::path& out):exe_(exe),out_(out),log_(out/"controller.jsonl"),metadata_rva_(native::metadata_rva(native::read_file(exe),"R305Metadata")),prefix_(L"fv_r305_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64())),executable_(native::digest(native::read_file(exe))){
        job_.value=CreateJobObjectW(nullptr,nullptr);check(job_.value!=nullptr,"CreateJobObjectW");JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&info,sizeof(info)),"SetInformationJobObject");
    }
    std::unique_ptr<Child> start(const std::string& name,bool source=false,bool wrong_chain=false){
        auto v=std::make_unique<Child>(log_,metadata_rva_);v->name=name;auto wide=std::wstring(name.begin(),name.end());v->desktop=std::make_unique<Desktop>(prefix_+L"_"+wide,log_);
        v->shared_name=L"Local\\"+prefix_+L"_"+wide;v->shared=std::make_unique<Mapping>(v->shared_name,sizeof(Shared),true);new(v->shared->view)Shared{};
        v->p=launch(exe_,v->desktop->full,{L"--role",L"fixture",L"--mode",source?L"source":L"restore",L"--state",v->shared_name,L"--log",(out_/(name+"-events.jsonl")).wstring(),L"--gui",gui_?L"1":L"0",L"--wrong-chain",wrong_chain?L"1":L"0",L"--omit-ref",!source&&scenario_==L"unregistered-ref"?L"1":L"0",L"--unknown-child",source&&scenario_==L"unknown-child"?L"1":L"0",L"--bad-parent",!source&&scenario_==L"bad-parent"?L"1":L"0"},DEBUG_ONLY_THIS_PROCESS,log_,name);assign(v->p);
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
        in->bridge=launch(exe_,v.desktop->full,{L"--role",L"input",L"--channel",r304::channel_name(v.p.pid,v.p.tid),L"--dll",(exe_.parent_path()/L"runtime304.dll").wstring(),L"--log",(out_/(v.name+"-input.jsonl")).wstring()},0,log_,v.name+"-input");assign(in->bridge);
        try{run_until(v,[&]{return c.ready!=0;},"input.install");if(c.health==-1)throw Error(c.error_api,static_cast<DWORD>(c.error));demand(c.health==1 && c.ready==1,"existing R304 runtime installed");}
        catch(...){try{confirm(in->bridge,v);}catch(const std::exception& e){log_.emit("cleanup_error","\"stage\":\"input-install-exit\",\"error\":"+json(e.what()));}throw;}
        return in;
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
        {std::ofstream f(out_/(std::string(phase)+"-resources.json"),std::ios::binary);f<<resources_json(reply.observed)<<'\n';}
        write_bytes(out_/(std::string(phase)+"-resources.bin"),&reply.observed,sizeof(reply.observed));
        if(reply.error)throw Error(reply.api,reply.error);return reply.observed;
    }
    void freeze(Child& v){post(v,freeze_message);run_until(v,[&]{return v.metadata().frozen==1;},"runtime.freeze");}
    void capture(Child& v,const std::string& label){const auto reply_name=L"Local\\"+prefix_+L"_png_"+std::to_wstring(++serial_);Mapping reply(reply_name,sizeof(Reply),true);auto* r=new(reply.view)Reply{};auto wide=std::wstring(label.begin(),label.end());
        courier_command(v,reply_name,true,0,0,out_/(label+".png"));
        log_.emit("capture","\"name\":"+json(label)+",\"view\":"+json(v.name)+",\"api\":"+json(r->api)+",\"error\":"+std::to_string(r->error)+",\"counter\":"+std::to_string(r->print_update)+",\"beforeDraws\":"+std::to_string(r->before_draws)+",\"afterDraws\":"+std::to_string(r->after_draws)+",\"apiBeginMs\":"+std::to_string(r->api_begin_ms)+",\"apiEndMs\":"+std::to_string(r->api_end_ms)+",\"clientDx\":"+std::to_string(r->client_dx)+",\"clientDy\":"+std::to_string(r->client_dy));
        verify(label+".actual-GDI",r->done && !r->error && r->after_draws>r->before_draws,"owner GUI thread renders native WM_PRINT into its own DIB; bridge encodes actual pixels");
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
        log_.emit("chain_match","\"matched\":"+std::string(matched?"true":"false")+",\"sourceSsp\":"+std::to_string(s.cpu.R11)+",\"targetSsp\":"+std::to_string(boot_cpu.R11)+",\"sourceRsp\":"+std::to_string(s.cpu.Rsp)+",\"targetRsp\":"+std::to_string(boot_cpu.Rsp)+",\"initializations\":"+std::to_string(v.initialized)+",\"businessPrefixes\":"+std::to_string(v.business[0]+v.business[1]+v.business[2]));
        if(!matched)throw Error("restore.call_chain_mismatch",ERROR_INVALID_DATA);
        demand(v.initialized==0 && v.business[0]+v.business[1]+v.business[2]==0,"no source business replay");
        std::vector<BYTE> before_data(data_size);native::read_memory(v.p.process.value,data_address,before_data.data(),data_size);
        std::vector<BYTE> before_stack(s.stack.size());native::read_memory(v.p.process.value,stack_address+4096,before_stack.data(),before_stack.size());
        write_bytes(out_/"target-before-restore-data.bin",before_data.data(),before_data.size());
        const auto active=static_cast<size_t>(boot_cpu.Rsp-stack_address-4096);write_bytes(out_/"target-before-restore-stack.bin",before_stack.data()+active,before_stack.size()-active);write_bytes(out_/"target-before-restore-context.bin",&boot_cpu,sizeof(boot_cpu));
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
        log_.emit("reference_registry","\"sourceTracked\":"+std::to_string(s.meta.ref_count)+",\"sourceRequired\":"+std::to_string(s.meta.required_count)+",\"targetTracked\":"+std::to_string(v.m.ref_count)+",\"targetRequired\":"+std::to_string(v.m.required_count));
        write_bytes(out_/"target-metadata-before-restore.bin",&v.m,sizeof(v.m));
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
        if(fault.empty()){write_bytes(out_/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);}
        if(fault=="stack"){const auto offset=static_cast<size_t>(s.meta.local[0]-stack_address-4096);native::write_memory(v.p.process.value,s.meta.local[0],before_stack.data()+offset,8);}
        if(fault=="memory")native::write_memory(v.p.process.value,data_address,before_data.data(),8);
        auto cpu=s.cpu;cpu.R11=boot_cpu.R11;if(fault=="register")cpu.R10=boot_cpu.R10;
        if(fault=="context"){Handle denied(OpenThread(THREAD_GET_CONTEXT,FALSE,v.p.tid));check(denied.value!=nullptr,"OpenThread.without_SET_CONTEXT");if(!SetThreadContext(denied.value,&cpu))throw Error("SetThreadContext.restore.denied",GetLastError());throw Error("context_fault_not_rejected",ERROR_INVALID_STATE);}
        check(SetThreadContext(v.p.thread.value,&cpu),"SetThreadContext.restore");
        const LONG rebuilt_done=0;native::write_memory(v.p.process.value,v.m.rebuild_address,&rebuilt_done,sizeof(rebuilt_done));
        chain(v,"after-ordinary-context-restore");
        const auto got=v.context();verify(v.name+".CPU-context-readback",got.Rip==cpu.Rip && got.Rsp==cpu.Rsp && got.R10==cpu.R10 && got.MxCsr==cpu.MxCsr,"stopped target RIP/RSP/register and floating point control read back after SetThreadContext");
        write_bytes(out_/"target-context-before-continue.bin",&got,sizeof(got));
        native::read_memory(v.p.process.value,data_address,readback.data(),readback.size());native::read_memory(v.p.process.value,stack_address+4096,stack_readback.data(),stack_readback.size());
        write_bytes(out_/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);
        log_.emit("restored_context","\"view\":"+json(v.name)+",\"rip\":"+std::to_string(got.Rip)+",\"rsp\":"+std::to_string(got.Rsp)+",\"r10\":"+std::to_string(got.R10)+",\"published\":false");
        v.continue_event();v.wait("continued");const auto actual=v.state();
        xstate(v,"after-CPP-returns");
        log_.emit("execution_result","\"work\":"+work_json(actual)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"restoredR10\":"+std::to_string(cpu.R10)+",\"fault\":"+json(fault));
        if(actual.receipt!=s.expected || actual.returned!=7)throw Error("restore.execution_receipt",ERROR_CRC);
        verify(v.name+".ordinary-CPP-return-chain",actual.receipt==s.expected && actual.returned==7 && v.initialized==0 && v.checkpoints==1 && v.business[0]+v.business[1]+v.business[2]==0,"actual leaf/middle/outer CALL/RET chain completed with restored register and volatile stack locals; structural checkpoint only, no business prefixes");
        verify(v.name+".original-S0-state",actual.seed==s.work.seed && actual.counter==s.work.counter && actual.clicks==s.work.clicks && !wcscmp(actual.text,s.work.text),"counter and unsaved text match S0 at continuation before next GUI iteration");
        log_.emit("continuation","\"view\":"+json(v.name)+",\"work\":"+work_json(actual)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"initializations\":"+std::to_string(v.initialized));
        if(gui_){unsigned yes=1;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,published),&yes,sizeof(yes));
        log_.emit("application_activated","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.p.pid)+",\"usableViewPublished\":false");v.resume();}
    }
    int run(const std::wstring& scenario){
        gui_=true;scenario_=scenario;const bool negative=scenario!=L"positive";
        log_.emit("case","\"scenario\":"+json(scenario.c_str())+",\"negative\":"+std::string(negative?"true":"false"));
        std::unique_ptr<Child> source,target;bool caught=false;DWORD error=0;std::string api;
        bool resource_passed=false,execution_passed=false,input_passed=false;
        try{
            source=start("source",true);source->resume();source->wait("init");source->resume();
            run_until(*source,[&]{return source->metadata().published==1;},"source.business_initialization");
            auto in=input(*source);
            if(scenario==L"undo")character(*source,*in,L'U');
            select_prefix(*source,*in);close_input(*source,*in);in.reset();freeze(*source);
            const auto saved_resources=resource(*source,1,"S0");
            verify("source.native-EDIT-preconditions",saved_resources.query_tid==source->p.tid && saved_resources.focus==2 && saved_resources.selection_start==0 && saved_resources.selection_end==wcslen(saved_resources.nodes[1].caption) && saved_resources.modified && !saved_resources.can_undo,"random native EDIT initialized by source business only; Home/Shift+End select text; capture does not clear undo");
            capture(*source,"source-S0");post(*source,checkpoint_message);until_checkpoint(*source);auto s=save(*source);s.resources=saved_resources;
            verify("source.initialization-and-prefixes-once",source->initialized==1 && source->business[0]==1 && source->business[1]==1 && source->business[2]==1,"debugger evidence is separate from restored memory");
            log_.emit("snapshot_published","\"resourceSequence\":"+std::to_string(source->m.service.done)+",\"afterResourceCapture\":true");
            source->resume();source->wait("continued");verify("source.normal-return-chain",source->state().receipt==s.expected && source->state().returned==7,"source ordinary call chain and captured receipt");source->resume();
            unsigned zero=0;native::write_memory(source->p.process.value,source->image+metadata_rva_+offsetof(Metadata,frozen),&zero,sizeof(zero));
            auto changes=input(*source);character(*source,*changes,L'!');close_input(*source,*changes);changes.reset();post(*source,WM_CHAR,L'!');
            run_until(*source,[&]{return source->state().counter>s.work.counter&&source->state().length>s.work.length;},"source.changed_after_S0");freeze(*source);
            const auto changed=resource(*source,3,"source-changed");capture(*source,"source-CHANGED");
            verify("source.actual-EDIT-changed",wcscmp(changed.nodes[1].caption,saved_resources.nodes[1].caption)!=0 && changed.can_undo,"real WM_CHAR changed native EDIT and generated undo after S0");
            log_.emit("source_changed","\"work\":"+work_json(source->state()));source->stop();
            verify("source.terminated-before-restore",source->p.exited,"confirmed process exit before new carrier");
            target=start("restored");verify("new-PID-TID",source->p.pid!=target->p.pid&&source->p.tid!=target->p.tid,"new native GUI thread and process");target->resume();
            unsigned omission=scenario==L"omit-text"?1:scenario==L"omit-selection"?2:scenario==L"omit-modify"?3:scenario==L"omit-focus"?4:0;
            resource(*target,2,"target-prepared",&saved_resources,omission);resource_passed=true;
            post(*target,prepare_stop_message);target->wait("checkpoint");restore(*target,s,"");execution_passed=true;
            const auto resumed=resource(*target,3,"restored-S0");
            verify("restored.native-EDIT-state",!wcscmp(resumed.nodes[1].caption,saved_resources.nodes[1].caption)&&resumed.selection_start==0&&resumed.selection_end==saved_resources.selection_end&&resumed.modified==saved_resources.modified&&resumed.focus==2&&!resumed.can_undo,"native owner-thread readback after real CALL/RET; not just memory state");
            capture(*target,"restored-S0");
            native::write_memory(target->p.process.value,target->image+metadata_rva_+offsetof(Metadata,frozen),&zero,sizeof(zero));
            auto edits=input(*target);select_prefix(*target,*edits);
            const auto selected=resource(*target,3,"selected-after-restore");
            verify("restored.native-keyboard-selection",selected.selection_start==0&&selected.selection_end==saved_resources.selection_end&&selected.focus==2,"original EDIT procedure processes Home and Shift+End through existing managed input");
            character(*target,*edits,L'Z');
            keys(*target,*edits,{{VK_HOME,true},{VK_HOME,false}});const auto home=resource(*target,3,"after-home");verify("restored.native-HOME",home.selection_start==0&&home.selection_end==0,"native EDIT moves caret to start");
            keys(*target,*edits,{{VK_END,true},{VK_END,false}});
            close_input(*target,*edits);freeze(*target);
            const auto final=resource(*target,3,"after-input");
            const auto expected=std::wstring(L"Z");
            verify("restored.native-EDIT-edit-and-caret",!wcscmp(final.nodes[1].caption,expected.c_str())&&final.selection_start==expected.size()&&final.selection_end==expected.size()&&final.modified&&final.can_undo,"real WM_CHAR replaces native selection; Home/End move actual caret, undo appears naturally");
            capture(*target,"restored-after-input");input_passed=true;primary_passed_=true;
            demand(failed_==0,"all native evidence must pass before usable View publication");
            log_.emit("view_published","\"pid\":"+std::to_string(target->p.pid)+",\"resourcesAndExecutionAndInputVerified\":true");
        }catch(const Error& e){caught=true;error=e.code;api=e.api;primary_passed_=false;log_.emit("experiment_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"usableViewPublished\":false");}
        if(negative){const bool source_reject=scenario==L"undo"||scenario==L"unknown-child";const bool omitted=scenario.rfind(L"omit-",0)==0;
            const auto expected=static_cast<DWORD>(source_reject?ERROR_NOT_SUPPORTED:omitted?ERROR_CRC:ERROR_INVALID_DATA);
            verify("negative.explicit-rejection",caught&&error==expected&&!primary_passed_,api+" error="+std::to_string(error)+"; unsupported state or failed reconstruction not published");
            if(source_reject && source && !source->pending){resource(*source,3,"source-after-rejection");verify("negative.source-still-alive",WaitForSingleObject(source->p.process.value,0)==WAIT_TIMEOUT,"failed snapshot does not terminate source until owned cleanup");}
        }
        if(target)target->stop();if(source)source->stop();
        log_.emit("experiment_complete","\"failedAssertions\":"+std::to_string(failed_)+",\"primaryRestorePassed\":"+std::string(primary_passed_?"true":"false")+",\"controlResourceReconstructionPassed\":"+std::string(resource_passed?"true":"false")+",\"executionContinuationPassed\":"+std::string(execution_passed?"true":"false")+",\"postRestoreInputPassed\":"+std::string(input_passed?"true":"false")+",\"generalGUIRestore\":false,\"R3Complete\":false,\"manualHostTest\":\"not_executed\"");
        return failed_||!primary_passed_?2:0;
    }
};
}
}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;try{const r3::Args args(argc,argv);const auto role=args.get(L"--role",L"controller");if(role==L"fixture")return r305::fixture_main(args);if(role==L"bridge")return r3::bridge_main(args);if(role==L"input")return r304::input_main(args);if(role==L"courier")return gate001::courier_main(args);
    r3::demand(role==L"controller","unknown role");r305::denied_launch_probe=args.get(L"--gate-denied-launch",L"0")==L"1";const auto output=std::filesystem::absolute(args.get(L"--output"));r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output),"new output directory required");std::filesystem::create_directories(output);wchar_t path[32768]{};r3::check(GetModuleFileNameW(nullptr,path,32768)!=0,"GetModuleFileNameW");r305::Experiment experiment(path,output);const auto scenario=args.get(L"--scenario",L"positive");r3::demand(scenario==L"positive" || scenario==L"undo" || scenario==L"unknown-child" || scenario==L"bad-parent" || scenario==L"unregistered-ref" || scenario==L"omit-text" || scenario==L"omit-selection" || scenario==L"omit-modify" || scenario==L"omit-focus","unknown scenario");return experiment.run(scenario);
}catch(const std::exception& e){std::cout<<"R305A_FAILURE "<<e.what()<<'\n';return 1;}}
