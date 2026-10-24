#include "protocol.hpp"
#include "../common/native.hpp"
#include <functional>
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
class Child {
    Log& log_; DWORD metadata_rva_; bool stopping_=false,startup_=false;
    std::uint64_t ntdll_=0,ntdll_end_=0;
    struct Module { std::uint64_t base,end;std::wstring name; };
    std::vector<Module> modules_;
public:
    Process p;
    std::unique_ptr<Desktop> desktop;
    std::unique_ptr<Mapping> shared;
    std::wstring shared_name;
    std::string name,halt;
    Metadata m{};
    std::uint64_t image=0,teb=0;
    DEBUG_EVENT event{};bool pending=false;
    unsigned initialized=0,checkpoints=0;
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
        if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){image=reinterpret_cast<std::uint64_t>(event.u.CreateProcessInfo.lpBaseOfImage);teb=reinterpret_cast<std::uint64_t>(event.u.CreateProcessInfo.lpThreadLocalBase);if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
            const auto dos=native::read_value<IMAGE_DOS_HEADER>(p.process.value,image);const auto size=native::read_value<IMAGE_NT_HEADERS64>(p.process.value,image+dos.e_lfanew).OptionalHeader.SizeOfImage;modules_.push_back({image,image+size,L"r305a.exe"});
            if(event.u.CreateProcessInfo.hProcess!=p.process.value)CloseHandle(event.u.CreateProcessInfo.hProcess);if(event.u.CreateProcessInfo.hThread!=p.thread.value)CloseHandle(event.u.CreateProcessInfo.hThread);}
        else if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT){if(event.u.CreateThread.hThread!=p.thread.value)CloseHandle(event.u.CreateThread.hThread);}
        else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){
            Handle file(event.u.LoadDll.hFile);if(file.value){wchar_t path[32768]{};if(GetFinalPathNameByHandleW(file.value,path,32768,FILE_NAME_NORMALIZED)){
                auto base=reinterpret_cast<std::uint64_t>(event.u.LoadDll.lpBaseOfDll);
                const auto dos=native::read_value<IMAGE_DOS_HEADER>(p.process.value,base);const auto size=native::read_value<IMAGE_NT_HEADERS64>(p.process.value,base+dos.e_lfanew).OptionalHeader.SizeOfImage;
                modules_.push_back({base,base+size,std::filesystem::path(path).filename().wstring()});
                if(_wcsicmp(std::filesystem::path(path).filename().c_str(),L"ntdll.dll")==0){ntdll_=base;ntdll_end_=base+size;}
            }}
        } else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){
            const auto code=event.u.ExitProcess.dwExitCode;continue_event();check(WaitForSingleObject(p.process.value,1000)==WAIT_OBJECT_0,"WaitForSingleObject.debug_exit");p.exited=true;
            log_.emit("process_exited","\"name\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"exitCode\":"+std::to_string(code));if(!stopping_)throw Error("unexpected_debug_exit",code);return;
        } else if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT && !stopping_){
            auto& e=event.u.Exception;auto address=reinterpret_cast<std::uint64_t>(e.ExceptionRecord.ExceptionAddress);
            log_.emit("exception","\"view\":"+json(name)+",\"pid\":"+std::to_string(p.pid)+",\"code\":"+std::to_string(e.ExceptionRecord.ExceptionCode)+",\"address\":"+std::to_string(address)+",\"firstChance\":"+std::to_string(e.dwFirstChance));
            if(e.ExceptionRecord.ExceptionCode==0xE305C001 && e.dwFirstChance && name=="native-seh") {
                continue_event(DBG_EXCEPTION_NOT_HANDLED);return; // allow the real SEH handler
            }
            if(e.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT){
                std::ostringstream parameters;parameters<<'[';for(DWORD i=0;i<e.ExceptionRecord.NumberParameters;++i){if(i)parameters<<',';parameters<<e.ExceptionRecord.ExceptionInformation[i];}parameters<<']';
                auto cpu=context();std::uint64_t normal_return=0;SIZE_T count=0;
                const auto readable=ReadProcessMemory(p.process.value,reinterpret_cast<void*>(cpu.Rsp),&normal_return,8,&count);
                BYTE instruction[8]{};SIZE_T instruction_count=0;
                ReadProcessMemory(p.process.value,reinterpret_cast<void*>(cpu.Rip),instruction,8,&instruction_count);
                std::ostringstream bytes;for(SIZE_T i=0;i<instruction_count;++i)bytes<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(instruction[i]);
                std::wstring module=L"unknown";std::uint64_t offset=0;
                for(const auto& item:modules_)if(cpu.Rip>=item.base && cpu.Rip<item.end){module=item.name;offset=cpu.Rip-item.base;break;}
                log_.emit("continuation_fault","\"view\":"+json(name)+",\"exceptionCode\":"+std::to_string(e.ExceptionRecord.ExceptionCode)+",\"parameters\":"+parameters.str()+",\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"module\":"+json(module.c_str())+",\"moduleOffset\":"+std::to_string(offset)+",\"normalReturnReadable\":"+std::to_string(readable)+",\"normalReturn\":"+std::to_string(normal_return)+",\"instructionHex\":"+json(bytes.str()));
                throw Error("debug.exception",e.ExceptionRecord.ExceptionCode);
            }
            if(!startup_ && address>=ntdll_ && address<ntdll_end_)startup_=true;
            else{
                metadata();
                demand(event.dwThreadId==p.tid,"experimental trap belongs to owned main GUI thread");
                if(address==m.ready)halt="ready";else if(address==m.init){halt="init";++initialized;}else if(address==m.checkpoint){halt="checkpoint";++checkpoints;}else if(address==m.continued)halt="continued";else if(address==m.rejected)halt="rejected";else if(address==m.api_site)halt="api";else if(address==m.diagnostic_site)halt="diagnostic";
                else throw Error("unknown_experiment_breakpoint",ERROR_INVALID_DATA);
                log_.emit("halt","\"view\":"+json(name)+",\"phase\":"+json(halt)+",\"pid\":"+std::to_string(p.pid)+",\"initializations\":"+std::to_string(initialized)+",\"checkpoints\":"+std::to_string(checkpoints)+",\"published\":"+std::to_string(m.published)+",\"shadowPolicy\":"+std::to_string(m.shadow_policy)+",\"error\":"+std::to_string(m.rejected_error));return;
            }
        }
        continue_event();
    }
    void wait(const char* expected){const auto deadline=GetTickCount64()+8000;while(!pending && GetTickCount64()<deadline)poll();demand(pending && halt==expected,(std::string("expected debug halt ")+expected+", got "+halt).c_str());}
    void stop(){if(!p.process.value||p.exited)return;stopping_=true;check(TerminateProcess(p.process.value,0xE3050001),"TerminateProcess.owned_GUI");if(pending)continue_event();const auto deadline=GetTickCount64()+5000;while(!p.exited&&GetTickCount64()<deadline)poll();demand(p.exited,"GUI exit confirmed");}
    Work state(){return native::read_value<Work>(p.process.value,data_address);}
};
struct Snapshot {
    Metadata meta; Work work; CONTEXT cpu{};
    std::vector<std::uint8_t> memory,stack;
    std::array<std::uint8_t,32> executable{};
    std::uint64_t cookie=0,complement=0,expected=0;
};
void write_bytes(const std::filesystem::path& path,const void* data,size_t size){std::ofstream f(path,std::ios::binary);demand(f.good(),"raw evidence file");f.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));demand(f.good(),"raw evidence write");}
struct Input {
    std::unique_ptr<Mapping> mapping;Process bridge;std::uint64_t sequence=0;LONG request=0;
    r304::Channel& c(){return *static_cast<r304::Channel*>(mapping->view);}
};
class Experiment {
    std::filesystem::path exe_,out_;Log log_;Handle job_;DWORD metadata_rva_;std::wstring prefix_;
    unsigned failed_=0,serial_=0; bool primary_passed_=false; std::array<std::uint8_t,32> executable_;
    std::vector<Process> helpers_;
    std::wstring method_;
    void verify(const std::string& name,bool okay,const std::string& detail){if(!okay)++failed_;log_.emit("assertion","\"name\":"+json(name)+",\"passed\":"+(okay?"true":"false")+",\"detail\":"+json(detail));std::cout<<name<<": "<<(okay?"pass":"FAIL")<<" ("<<detail<<")\n";}
    void assign(Process& p){check(AssignProcessToJobObject(job_.value,p.process.value),"AssignProcessToJobObject");}
    Process& helper(Child& v,std::vector<std::wstring> args,const std::string& name){helpers_.push_back(launch(exe_,v.desktop->full,args,0,log_,name));auto& p=helpers_.back();assign(p);return p;}
    void confirm(Process& p,Child& v,DWORD timeout=5000){const auto end=GetTickCount64()+timeout;while(WaitForSingleObject(p.process.value,0)==WAIT_TIMEOUT && GetTickCount64()<end)v.poll();if(WaitForSingleObject(p.process.value,0)!=WAIT_OBJECT_0){TerminateProcess(p.process.value,ERROR_TIMEOUT);throw Error("helper_timeout",ERROR_TIMEOUT);}if(!p.exited){DWORD code=0;check(GetExitCodeProcess(p.process.value,&code),"GetExitCodeProcess.helper");p.exited=true;log_.emit("process_exited","\"name\":\"helper\",\"pid\":"+std::to_string(p.pid)+",\"exitCode\":"+std::to_string(code));}}
    void post(Child& v,UINT message,WPARAM wp=0){
        const auto name=L"Local\\"+prefix_+L"_reply_"+std::to_wstring(++serial_);Mapping reply(name,sizeof(Reply),true);new(reply.view)Reply{};
        auto& p=helper(v,{L"--role",L"bridge",L"--state",v.shared_name,L"--reply",name,L"--op",L"post",L"--target",L"top",L"--message",std::to_wstring(message),L"--wparam",std::to_wstring(wp)},"post");confirm(p,v);
        auto& r=*static_cast<Reply*>(reply.view);check(r.done && !r.error,"bridge.post");
    }
    void run_until(Child& v,const std::function<bool()>& done,const char* api){const auto end=GetTickCount64()+7000;while(!done()&&GetTickCount64()<end){v.poll();if(v.pending)throw Error(api,ERROR_INVALID_STATE,"unexpected debug halt "+v.halt);}if(!done())throw Error(api,ERROR_TIMEOUT);}
public:
    void xstate(Child& v,const char* phase){
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
            std::uint64_t shadow_return=0;SIZE_T got=0;const bool readable=present && ReadProcessMemory(v.p.process.value,reinterpret_cast<void*>(cet->Ia32Pl3SspMsr),&shadow_return,8,&got) && got==8;
            log_.emit("xstate_read","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"api\":\"GetThreadContext/LocateXStateFeature\",\"enabledMask\":"+std::to_string(enabled)+",\"returnedMask\":"+std::to_string(mask)+",\"cetPresent\":"+(present?"true":"false")+",\"cetControl\":"+std::to_string(present?cet->Ia32CetUMsr:0)+",\"ssp\":"+std::to_string(present?cet->Ia32Pl3SspMsr:0)+",\"shadowReturnReadable\":"+(readable?"true":"false")+",\"shadowReturn\":"+std::to_string(shadow_return));
        }catch(const Error& e){log_.emit("xstate_read_unavailable","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}
    }
    void extended_restore(Child& v,const CONTEXT& saved){
        using Init=BOOL(WINAPI*)(PVOID,DWORD,PCONTEXT*,PDWORD,ULONG64);
        const auto init=reinterpret_cast<Init>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"InitializeContext2"));check(init!=nullptr,"GetProcAddress.InitializeContext2");
        const auto enabled=GetEnabledXStateFeatures();DWORD size=0;PCONTEXT cpu=nullptr;
        SetLastError(0);const auto sized=init(nullptr,cpu_flags|CONTEXT_XSTATE,&cpu,&size,enabled|XSTATE_MASK_CET_U);
        demand(!sized && GetLastError()==ERROR_INSUFFICIENT_BUFFER,"InitializeContext2 sizing");
        std::vector<BYTE> bytes(size);check(init(bytes.data(),cpu_flags|CONTEXT_XSTATE,&cpu,&size,enabled|XSTATE_MASK_CET_U),"InitializeContext2.extended");
        check(SetXStateFeaturesMask(cpu,XSTATE_MASK_CET_U),"SetXStateFeaturesMask.extended");check(GetThreadContext(v.p.thread.value,cpu),"GetThreadContext.extended");
        DWORD64 mask=0;check(GetXStateFeaturesMask(cpu,&mask),"GetXStateFeaturesMask.extended");DWORD length=0;
        auto cet=static_cast<PXSAVE_CET_U_FORMAT>(LocateXStateFeature(cpu,XSTATE_CET_U,&length));
        demand(cet && length>=sizeof(*cet) && (mask&XSTATE_MASK_CET_U),"CET_U available for bounded no-op setter");
        const auto own=*cet;
        // Only target-owned state read at this exact stop is supplied. Never
        // copy source SSP or change CET control bits or shadow-stack memory.
        SetLastError(0);const auto noop=SetThreadContext(v.p.thread.value,cpu);const auto noop_error=noop?0:GetLastError();
        log_.emit("xstate_set","\"phase\":\"target-owned-no-op\",\"api\":\"SetThreadContext\",\"success\":"+std::string(noop?"true":"false")+",\"error\":"+std::to_string(noop_error)+",\"mask\":"+std::to_string(mask)+",\"cetControl\":"+std::to_string(own.Ia32CetUMsr)+",\"ssp\":"+std::to_string(own.Ia32Pl3SspMsr));
        if(!noop)throw Error("SetThreadContext.CET_U.no_op",noop_error);
        xstate(v,"after-target-owned-no-op");
        memcpy(cpu,&saved,sizeof(CONTEXT));cpu->ContextFlags=cpu_flags|CONTEXT_XSTATE;
        check(SetXStateFeaturesMask(cpu,XSTATE_MASK_CET_U),"SetXStateFeaturesMask.restored_legacy");
        cet=static_cast<PXSAVE_CET_U_FORMAT>(LocateXStateFeature(cpu,XSTATE_CET_U,&length));demand(cet!=nullptr,"CET_U retained");*cet=own;
        write_bytes(out_/"target-extended-context-request.bin",bytes.data(),bytes.size());
        SetLastError(0);const auto okay=SetThreadContext(v.p.thread.value,cpu);const auto error=okay?0:GetLastError();
        log_.emit("xstate_set","\"phase\":\"source-legacy-target-CET_U\",\"api\":\"SetThreadContext\",\"success\":"+std::string(okay?"true":"false")+",\"error\":"+std::to_string(error)+",\"rip\":"+std::to_string(cpu->Rip)+",\"rsp\":"+std::to_string(cpu->Rsp)+",\"cetControl\":"+std::to_string(own.Ia32CetUMsr)+",\"ssp\":"+std::to_string(own.Ia32Pl3SspMsr));
        if(!okay)throw Error("SetThreadContext.extended_restore",error);
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
    ~Experiment(){for(auto& p:helpers_)if(!p.exited){TerminateProcess(p.process.value,0xE3050002);WaitForSingleObject(p.process.value,3000);}}
    std::unique_ptr<Child> start(const std::string& name,bool source=false,bool fail_window=false,const std::wstring& mode=L""){
        auto v=std::make_unique<Child>(log_,metadata_rva_);v->name=name;auto wide=std::wstring(name.begin(),name.end());v->desktop=std::make_unique<Desktop>(prefix_+L"_"+wide,log_);
        v->shared_name=L"Local\\"+prefix_+L"_"+wide;v->shared=std::make_unique<Mapping>(v->shared_name,sizeof(Shared),true);new(v->shared->view)Shared{};
        v->p=launch(exe_,v->desktop->full,{L"--role",L"fixture",L"--mode",mode.empty()?(source?L"source":L"restore"):mode,L"--state",v->shared_name,L"--log",(out_/(name+"-events.jsonl")).wstring(),L"--fail-window",fail_window?L"1":L"0"},DEBUG_ONLY_THIS_PROCESS,log_,name);assign(v->p);
        v->wait(fail_window?"rejected":"ready");
        log_.emit("runtime_prepared","\"view\":"+json(name)+",\"pid\":"+std::to_string(v->p.pid)+",\"image\":"+std::to_string(v->image)+",\"window\":"+std::to_string(v->m.window)+",\"shadowPolicy\":"+std::to_string(v->m.shadow_policy)+",\"originalStackLow\":"+std::to_string(v->m.original_stack_low)+",\"originalStackHigh\":"+std::to_string(v->m.original_stack_high)+",\"managedStackLow\":"+std::to_string(v->m.active_stack_low)+",\"managedStackHigh\":"+std::to_string(v->m.active_stack_high)+",\"reportedStackLow\":"+std::to_string(v->m.reported_stack_low)+",\"reportedStackHigh\":"+std::to_string(v->m.reported_stack_high));
        return v;
    }
    std::unique_ptr<Input> input(Child& v){
        auto in=std::make_unique<Input>();in->mapping=std::make_unique<Mapping>(r304::channel_name(v.p.pid,v.p.tid),sizeof(r304::Channel),true);auto& c=*new(in->mapping->view)r304::Channel{};
        c.pid=v.p.pid;c.tid=v.p.tid;c.top=v.m.window;c.edit=c.top;c.enabled=1;c.virtualize=1;c.token=(GetTickCount64()<<16)^v.p.pid;
        in->bridge=launch(exe_,v.desktop->full,{L"--role",L"input",L"--channel",r304::channel_name(v.p.pid,v.p.tid),L"--dll",(exe_.parent_path()/L"runtime304.dll").wstring(),L"--log",(out_/(v.name+"-input.jsonl")).wstring()},0,log_,v.name+"-input");assign(in->bridge);
        run_until(v,[&]{return c.ready!=0;},"input.install");demand(c.health==1 && c.ready==1,"existing R304 runtime installed");return in;
    }
    void submit(Child& v,Input& in,std::vector<r304::Packet> packets,bool close=false){auto& c=in.c();demand(c.health==1,"input healthy");r304::Packet end{};end.kind=r304::barrier;packets.push_back(end);c.count=static_cast<unsigned>(packets.size());c.command=close?2:1;
        for(unsigned i=0;i<c.count;++i){packets[i].sequence=++in.sequence;c.packets[i]=packets[i];}MemoryBarrier();InterlockedExchange(&c.request,++in.request);
        run_until(v,[&]{return c.command_done==in.request||c.health==-1;},"input.processed");if(c.health==-1)throw Error(c.error_api,static_cast<DWORD>(c.error));
        log_.emit("input_processed","\"view\":"+json(v.name)+",\"sequence\":"+std::to_string(in.sequence)+",\"consumed\":"+std::to_string(c.consumed)+",\"closed\":"+(close?"true":"false"));
        if(close)confirm(in.bridge,v);
    }
    void character(Child& v,Input& in,wchar_t ch){r304::Packet p{};p.kind=r304::character;p.wp=ch;submit(v,in,{p});}
    void mouse_click(Child& v,Input& in){r304::Packet down{};down.kind=r304::mouse;down.action=r304::left_down;down.x=90;down.y=170;auto up=down;up.action=r304::left_up;submit(v,in,{down,up});}
    void close_input(Child& v,Input& in){r304::Packet p{};p.kind=r304::release;submit(v,in,{p},true);verify(v.name+".input-clean-close",in.c().health==0 && in.c().restored && !in.c().pinned && !in.c().iat_installed,"unmodified R304 lifecycle releases state and restores both imports before checkpoint/exit");}
    void freeze(Child& v){post(v,freeze_message);run_until(v,[&]{return v.metadata().frozen==1;},"runtime.freeze");}
    void capture(Child& v,const std::string& label){const auto reply_name=L"Local\\"+prefix_+L"_png_"+std::to_wstring(++serial_);Mapping reply(reply_name,sizeof(Reply),true);auto* r=new(reply.view)Reply{};auto wide=std::wstring(label.begin(),label.end());
        auto& p=helper(v,{L"--role",L"bridge",L"--state",v.shared_name,L"--reply",reply_name,L"--op",L"capture",L"--target",L"top",L"--path",(out_/(label+".png")).wstring()},label+"-capture");confirm(p,v,5000);
        log_.emit("capture","\"name\":"+json(label)+",\"view\":"+json(v.name)+",\"error\":"+std::to_string(r->error)+",\"counter\":"+std::to_string(r->print_update)+",\"beforeDraws\":"+std::to_string(r->before_draws)+",\"afterDraws\":"+std::to_string(r->after_draws)+",\"apiBeginMs\":"+std::to_string(r->api_begin_ms)+",\"apiEndMs\":"+std::to_string(r->api_end_ms)+",\"clientDx\":"+std::to_string(r->client_dx)+",\"clientDy\":"+std::to_string(r->client_dy));
        verify(label+".actual-GDI",r->done && !r->error && r->after_draws>r->before_draws,"unchanged PrintWindow helper checks actual target paint and counter pixels");
    }
    Snapshot save(Child& v){v.wait("checkpoint");Snapshot s;s.meta=v.metadata();s.work=v.state();s.cpu=v.context();s.cpu.Rip=s.meta.checkpoint+1;s.executable=executable_;
        demand(s.meta.frozen && !s.meta.in_dispatch && !s.meta.paint_depth && s.cpu.Rsp>=stack_address+4096 && s.cpu.Rsp<stack_address+stack_size,"snapshot safe point bounds");
        s.memory.resize(data_size);native::read_memory(v.p.process.value,data_address,s.memory.data(),s.memory.size());
        s.stack.resize(stack_size-4096);native::read_memory(v.p.process.value,stack_address+4096,s.stack.data(),s.stack.size());
        s.cookie=native::read_value<std::uint64_t>(v.p.process.value,s.meta.cookie_address);s.complement=native::read_value<std::uint64_t>(v.p.process.value,s.meta.complement_address);
        const auto l=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[0]);const auto m=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[1]);const auto o=native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[2]);s.expected=receipt(s.cpu.R10,l,m,o);
        log_.emit("snapshot","\"work\":"+work_json(s.work)+",\"rip\":"+std::to_string(s.cpu.Rip)+",\"rsp\":"+std::to_string(s.cpu.Rsp)+",\"registerR10\":"+std::to_string(s.cpu.R10)+",\"leaf\":"+std::to_string(l)+",\"middle\":"+std::to_string(m)+",\"outer\":"+std::to_string(o)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"refs\":"+std::to_string(s.meta.ref_count)+",\"localAddresses\":["+std::to_string(s.meta.local[0])+","+std::to_string(s.meta.local[1])+","+std::to_string(s.meta.local[2])+"]");
        log_.emit("raw_layout","\"dataAddress\":"+std::to_string(data_address)+",\"windowOffset\":"+std::to_string(offsetof(Work,window))+",\"ripOffset\":"+std::to_string(offsetof(CONTEXT,Rip))+",\"rspOffset\":"+std::to_string(offsetof(CONTEXT,Rsp))+",\"r10Offset\":"+std::to_string(offsetof(CONTEXT,R10))+",\"xmmOffset\":"+std::to_string(offsetof(CONTEXT,Xmm0)));
        write_bytes(out_/"S0-data.bin",s.memory.data(),s.memory.size());write_bytes(out_/"S0-context.bin",&s.cpu,sizeof(s.cpu));
        const auto active_offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/"S0-active-stack.bin",s.stack.data()+active_offset,s.stack.size()-active_offset);
        xstate(v,"source-checkpoint");unwind(s);return s;
    }
    void restore(Child& v,const Snapshot& s,const std::string& fault){
        demand(v.pending && v.halt=="ready" && !v.m.published,"restore private prepared View only");
        if(strcmp(v.m.build,s.meta.build) || s.executable!=executable_ || v.image!=s.meta.image)throw Error("restore.build_or_image_identity",ERROR_REVISION_MISMATCH);
        demand(v.m.window_pid==v.p.pid && v.m.window_tid==v.p.tid && v.m.window_user_data==data_address &&
            !wcscmp(v.m.window_class,s.meta.window_class) && v.m.window_style==s.meta.window_style && v.m.window_ex_style==s.meta.window_ex_style &&
            !memcmp(&v.m.window_rect,&s.meta.window_rect,sizeof(RECT)),"native window descriptor/ownership matches supported fixed resource");
        log_.emit("window_resource_verified","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.m.window_pid)+",\"tid\":"+std::to_string(v.m.window_tid)+",\"class\":"+json(v.m.window_class)+",\"style\":"+std::to_string(v.m.window_style)+",\"exStyle\":"+std::to_string(v.m.window_ex_style)+",\"userData\":"+std::to_string(v.m.window_user_data)+",\"left\":"+std::to_string(v.m.window_rect.left)+",\"top\":"+std::to_string(v.m.window_rect.top)+",\"right\":"+std::to_string(v.m.window_rect.right)+",\"bottom\":"+std::to_string(v.m.window_rect.bottom));
        demand(v.m.stack==s.meta.stack && v.m.stack_bytes==s.meta.stack_bytes,"allocated managed layout matches; carrier remains on native bootstrap stack");
        const auto boot_cpu=v.context();
        if((v.m.shadow_policy&1) && boot_cpu.R11 && s.cpu.R11){
            const auto target_return=native::read_value<std::uint64_t>(v.p.process.value,boot_cpu.R11);
            const auto expected_return=*reinterpret_cast<const std::uint64_t*>(s.stack.data()+static_cast<size_t>(s.cpu.Rsp-stack_address-4096));
            log_.emit("shadow_stack_diagnostic","\"view\":"+json(v.name)+",\"sourceSsp\":"+std::to_string(s.cpu.R11)+",\"targetBootSsp\":"+std::to_string(boot_cpu.R11)+",\"targetShadowReturn\":"+std::to_string(target_return)+",\"savedNormalReturn\":"+std::to_string(expected_return)+",\"policy\":"+std::to_string(v.m.shadow_policy));
        }
        demand(s.meta.ref_count>0 && s.meta.ref_count<=8,"bounded resource reference count");
        auto refs=std::vector<Reference>(s.meta.refs,s.meta.refs+s.meta.ref_count);
        for(auto ref:refs){const bool data=ref.address>=data_address && ref.address+sizeof(HWND)<=data_address+data_size;const bool stack=ref.address>=s.cpu.Rsp && ref.address+sizeof(HWND)<=stack_address+stack_size;
            if(ref.logical!=1 || !(data||stack))throw Error("restore.unresolved_reference",ERROR_INVALID_DATA);
            const auto offset=static_cast<size_t>(ref.address-(data?data_address:stack_address+4096));std::uint64_t old=0;memcpy(&old,(data?s.memory:s.stack).data()+offset,8);if(old!=s.meta.window)throw Error("restore.reference_preimage",ERROR_INVALID_DATA);
        }
        demand(v.m.window!=s.meta.window && v.m.window!=0,"new native window identity");
        native::write_memory(v.p.process.value,data_address,s.memory.data(),s.memory.size());native::write_memory(v.p.process.value,stack_address+4096,s.stack.data(),s.stack.size());
        for(auto ref:refs){native::write_memory(v.p.process.value,ref.address,&v.m.window,8);log_.emit("resource_rebind","\"view\":"+json(v.name)+",\"logicalWindow\":1,\"address\":"+std::to_string(ref.address)+",\"oldHwnd\":"+std::to_string(s.meta.window)+",\"newHwnd\":"+std::to_string(v.m.window));}
        if(fault!="cookie"){native::write_memory(v.p.process.value,v.m.cookie_address,&s.cookie,8);native::write_memory(v.p.process.value,v.m.complement_address,&s.complement,8);}
        auto memory=s.memory;memcpy(memory.data()+offsetof(Work,window),&v.m.window,8);std::vector<std::uint8_t> readback(data_size);native::read_memory(v.p.process.value,data_address,readback.data(),data_size);
        verify(v.name+".raw-memory-restored",readback==memory,"entire data page read back equals S0 except declared HWND relocation");
        auto expected_stack=s.stack;for(auto ref:refs)if(ref.address>=stack_address+4096)memcpy(expected_stack.data()+static_cast<size_t>(ref.address-stack_address-4096),&v.m.window,8);
        std::vector<std::uint8_t> stack_readback(s.stack.size());native::read_memory(v.p.process.value,stack_address+4096,stack_readback.data(),stack_readback.size());
        verify(v.name+".raw-stack-restored",stack_readback==expected_stack,"entire managed stack read back equals S0 except explicitly registered live HWND references");
        if(fault.empty()){write_bytes(out_/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-stack_address-4096);write_bytes(out_/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);}
        if(fault=="stack"){const auto bad=~native::read_value<std::uint64_t>(v.p.process.value,s.meta.local[0]);native::write_memory(v.p.process.value,s.meta.local[0],&bad,8);}
        auto cpu=s.cpu;if(fault=="register")cpu.R10=0;
        if(fault=="context"){Handle denied(OpenThread(THREAD_GET_CONTEXT,FALSE,v.p.tid));check(denied.value!=nullptr,"OpenThread.without_SET_CONTEXT");if(!SetThreadContext(denied.value,&cpu))throw Error("SetThreadContext.restore.denied",GetLastError());throw Error("context_fault_not_rejected",ERROR_INVALID_STATE);}
        if(method_==L"rtl" || method_==L"rtl-native") {
            const unsigned rtl=method_==L"rtl"?1:2;
            native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,staged_cpu),&cpu,sizeof(cpu));
            native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,restore_method),&rtl,sizeof(rtl));
            log_.emit("rtl_request","\"api\":\"RtlRestoreContext\",\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"recordAddress\":"+std::to_string(v.image+metadata_rva_+offsetof(Metadata,staged_cpu))+",\"exceptionRecord\":null,\"managedTransferStack\":"+std::string(rtl==1?"true":"false"));
            v.resume();v.wait("api");xstate(v,"before-RtlRestoreContext");
            const auto before=v.context();
            const auto tib=native::read_value<NT_TIB>(v.p.process.value,v.teb);
            log_.emit("rtl_call_site","\"rip\":"+std::to_string(before.Rip)+",\"rsp\":"+std::to_string(before.Rsp)+",\"ssp\":"+std::to_string(before.R11)+",\"tibStackLow\":"+std::to_string(reinterpret_cast<std::uint64_t>(tib.StackLimit))+",\"tibStackHigh\":"+std::to_string(reinterpret_cast<std::uint64_t>(tib.StackBase)));
            v.resume();
        } else {
        if(method_==L"xstate")extended_restore(v,cpu);
        else { SetLastError(0);const auto okay=SetThreadContext(v.p.thread.value,&cpu);const auto error=okay?0:GetLastError();log_.emit("legacy_set","\"api\":\"SetThreadContext\",\"success\":"+std::string(okay?"true":"false")+",\"error\":"+std::to_string(error));if(!okay)throw Error("SetThreadContext.restore",error); }
        xstate(v,"after-ordinary-context-restore");
        const auto got=v.context();verify(v.name+".CPU-context-readback",got.Rip==cpu.Rip && got.Rsp==cpu.Rsp && got.R10==cpu.R10 && got.MxCsr==cpu.MxCsr,"stopped target RIP/RSP/register and floating point control read back after SetThreadContext");
        if(fault.empty())write_bytes(out_/"target-context-before-continue.bin",&got,sizeof(got));
        log_.emit("restored_context","\"view\":"+json(v.name)+",\"rip\":"+std::to_string(got.Rip)+",\"rsp\":"+std::to_string(got.Rsp)+",\"r10\":"+std::to_string(got.R10)+",\"published\":false");
        v.continue_event();
        }
        v.wait("continued");const auto actual=v.state();
        if(actual.receipt!=s.expected || actual.returned!=7)throw Error("restore.execution_receipt",ERROR_CRC);
        verify(v.name+".ordinary-CPP-return-chain",actual.receipt==s.expected && actual.returned==7 && v.initialized==0 && v.checkpoints==0,"actual leaf/middle/outer CALL/RET chain completed with restored register and volatile stack locals; no business init/checkpoint replay");
        verify(v.name+".original-S0-state",actual.seed==s.work.seed && actual.counter==s.work.counter && actual.clicks==s.work.clicks && !wcscmp(actual.text,s.work.text),"counter and unsaved text match S0 at continuation before next GUI iteration");
        log_.emit("continuation","\"view\":"+json(v.name)+",\"work\":"+work_json(actual)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"initializations\":"+std::to_string(v.initialized));
        unsigned yes=1;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,published),&yes,sizeof(yes));
        log_.emit("view_published","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.p.pid));v.resume();
    }
    int run(const std::wstring& scenario){
        method_=scenario;
        log_.emit("case","\"method\":"+json(scenario.c_str())+",\"EHCONT\":"+std::string(R305_EHCONT?"true":"false")+",\"crossProcessRestore\":"+((scenario==L"native-chain"||scenario==L"native-seh"||scenario==L"rtl-local")?"false":"true"));
        if(scenario==L"native-chain" || scenario==L"native-seh" || scenario==L"rtl-local") {
            auto own=start(scenario==L"native-chain"?"native-chain":scenario==L"native-seh"?"native-seh":"rtl-local",false,false,scenario);
            xstate(*own,"native-ready");own->resume();own->wait("diagnostic");xstate(*own,"native-leaf");
            const auto cpu=own->context();log_.emit("native_chain","\"rip\":"+std::to_string(cpu.Rip)+",\"rsp\":"+std::to_string(cpu.Rsp)+",\"ssp\":"+std::to_string(cpu.R11)+",\"normalReturn\":"+std::to_string(native::read_value<std::uint64_t>(own->p.process.value,cpu.Rsp)));
            try {
                own->resume();if(scenario==L"rtl-local"){own->wait("api");xstate(*own,"native-before-RtlRestoreContext");own->resume();}
                own->wait("continued");const auto m=own->metadata();
                verify("native.ordinary-CPP-return-chain",m.diagnostic_returns==3 && m.diagnostic_result==m.diagnostic_expected && own->initialized==0,"diagnostic leaf/middle/outer genuinely return on the native stack; this is not source restoration");
                log_.emit("native_result","\"result\":"+std::to_string(m.diagnostic_result)+",\"expected\":"+std::to_string(m.diagnostic_expected)+",\"returns\":"+std::to_string(m.diagnostic_returns)+",\"restoreCalls\":"+std::to_string(m.diagnostic_restore_calls)+",\"phase\":"+std::to_string(m.diagnostic_phase));
                xstate(*own,"native-after-return");log_.emit("diagnostic_complete","\"passed\":"+(failed_?std::string("false"):std::string("true"))+",\"sourceRestored\":false");
            }catch(const Error& e){xstate(*own,"native-after-fault");log_.emit("diagnostic_failed","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));own->stop();return 2;}
            own->stop();return failed_?1:0;
        }
        const bool ret_probe=true;
        auto source=start("source",true);source->resume();source->wait("init");source->resume();
        run_until(*source,[&]{return source->metadata().published==1;},"source.business_initialization");
        if(!ret_probe){auto source_input=input(*source);character(*source,*source_input,L'p');mouse_click(*source,*source_input);close_input(*source,*source_input);}
        freeze(*source);if(!ret_probe)capture(*source,"source-S0");post(*source,checkpoint_message);auto s=save(*source);
        verify("source.initialization-once",source->initialized==1 && source->checkpoints==1,"controller independently observes original init and safe checkpoint exactly once");
        source->resume();source->wait("continued");verify("source.normal-return-chain",source->state().receipt==s.expected && source->state().returned==7,"source executes the same ordinary C++ returns before changing further");source->resume();
        post(*source,WM_CHAR,L'!');run_until(*source,[&]{return source->state().length>s.work.length && source->state().counter>s.work.counter;},"source.changed_after_S0");freeze(*source);if(!ret_probe)capture(*source,"source-CHANGED");
        log_.emit("source_changed","\"work\":"+work_json(source->state()));source->stop();
        verify("source.terminated-before-restore",source->p.exited,"WaitForDebugEvent EXIT and signaled source process handle precede creation of new carrier");
        auto target=start("restored");verify("new-PID",target->p.pid!=source->p.pid,"different native process, source is dead");
        if(!ret_probe){target->resume();run_until(*target,[&]{return target->metadata().prepare_pumping!=0;},"runtime.private_prepare_loop");
            capture(*target,"target-runtime-only");post(*target,prepare_stop_message);target->wait("ready");}
        try {
            restore(*target,s,"");
            auto target_input=input(*target);freeze(*target);capture(*target,"restored-S0");
            unsigned zero=0;native::write_memory(target->p.process.value,target->image+metadata_rva_+offsetof(Metadata,frozen),&zero,sizeof(zero));
            const auto before=target->state();character(*target,*target_input,L'R');mouse_click(*target,*target_input);
            run_until(*target,[&]{return target->state().counter>before.counter;},"restored.counter_continues");
            const auto after=target->state();verify("restored.real-GUI-input",after.length==s.work.length+1 && after.text[after.length-1]==L'R' && after.clicks==s.work.clicks+1,"same supported character/left-button transport reaches restored native window and application procedures");
            close_input(*target,*target_input);freeze(*target);capture(*target,"restored-after-input");primary_passed_=true;
        } catch(const Error& e) {
            xstate(*target,"after-continuation-fault");
            log_.emit("primary_restore_failed","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"published\":"+std::to_string(target->metadata().published));
            log_.emit("capability","\"name\":\"GUI_execution_restore\",\"status\":\"fail\",\"detail\":\"actual continuation failed; no usable View published\"");
        }
        target->stop();        if(scenario==L"all")for(const auto& mode:std::vector<std::string>{"build","window","reference","context","stack","register","cookie"}){
            if(!primary_passed_ && (mode=="stack"||mode=="register"||mode=="cookie")){
                log_.emit("negative_skipped","\"case\":"+json(mode)+",\"reason\":\"primary continuation blocked before this state can be independently tested\"");continue;
            }
            std::unique_ptr<Child> rejected;bool caught=false;DWORD code=0;std::string api;
            try{rejected=start("negative-"+mode,false,mode=="window");if(mode=="window")throw Error("CreateWindowExW.restore",rejected->m.rejected_error);
                auto candidate=s;if(mode=="build")candidate.meta.build[0]=candidate.meta.build[0]=='f'?'0':'f';if(mode=="reference")candidate.meta.refs[candidate.meta.ref_count-1].address=stack_address+stack_size+8;
                restore(*rejected,candidate,mode);}
            catch(const Error& e){caught=true;code=e.code;api=e.api;log_.emit("restore_rejected","\"case\":"+json(mode)+",\"api\":"+json(api)+",\"error\":"+std::to_string(code));}
            const auto expected=mode=="build"?ERROR_REVISION_MISMATCH:mode=="window"?ERROR_CANNOT_FIND_WND_CLASS:mode=="reference"?ERROR_INVALID_DATA:mode=="context"?ERROR_ACCESS_DENIED:0u;
            verify("negative."+mode,caught && (!expected || code==expected) && (!rejected || !rejected->metadata().published),api+" error="+std::to_string(code)+"; no usable View published");if(rejected)rejected->stop();
        }
        log_.emit("experiment_complete","\"failedAssertions\":"+std::to_string(failed_)+",\"primaryRestorePassed\":"+(primary_passed_?"true":"false")+",\"R3Complete\":false,\"generalGUIRestore\":false,\"manualHostTest\":\"not_executed\"");return failed_ || !primary_passed_?2:0;
    }
};
}
}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;try{const r3::Args args(argc,argv);const auto role=args.get(L"--role",L"controller");if(role==L"fixture")return r305::fixture_main(args);if(role==L"bridge")return r3::bridge_main(args);if(role==L"input")return r304::input_main(args);
    r3::demand(role==L"controller","unknown role");const auto output=std::filesystem::absolute(args.get(L"--output"));r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output),"new output directory required");std::filesystem::create_directories(output);wchar_t path[32768]{};r3::check(GetModuleFileNameW(nullptr,path,32768)!=0,"GetModuleFileNameW");r305::Experiment experiment(path,output);const auto scenario=args.get(L"--scenario",L"legacy");r3::demand(scenario==L"legacy" || scenario==L"rtl" || scenario==L"rtl-native" || scenario==L"xstate" || scenario==L"native-chain" || scenario==L"native-seh" || scenario==L"rtl-local","unknown scenario");return experiment.run(scenario);
}catch(const std::exception& e){std::cout<<"R305A_FAILURE "<<e.what()<<'\n';return 1;}}
