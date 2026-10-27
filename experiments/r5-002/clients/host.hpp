#pragma once
#include "observation.hpp"
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
#include "../runtime/experiment-points.hpp"
#endif
namespace r305 {
struct Input {std::unique_ptr<Mapping> mapping;Process bridge;std::uint64_t sequence=0;LONG request=0;r304::Channel& c(){return *static_cast<r304::Channel*>(mapping->view);}};
struct CaseSnapshot : Snapshot {Observation work{};std::uint64_t expected=0;};
void write_bytes(const std::filesystem::path& p,const void* data,size_t n){std::ofstream out(p,std::ios::binary);demand(out.good(),"evidence file");out.write(static_cast<const char*>(data),static_cast<std::streamsize>(n));demand(out.good(),"evidence bytes");}
template<class Client> class Experiment {
protected:
    std::filesystem::path exe_,app_,helper_,out_;DWORD mailbox_rva_=0,contract_rva_=0;Log log_;Execution execution_;Actions actions_;DebugPump pump_;Handle job_;DWORD metadata_rva_;std::wstring prefix_;
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
        v.courier=launch(helper_,v.desktop->full,{L"--role",L"courier",L"--courier",name,L"--state",v.shared_name,L"--pid",std::to_wstring(v.p.pid)},0,log_,v.name+"-courier");assign(v.courier);
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
        const auto before=application_input?state(v):Observation{};
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
            run_until(v,[&]{return state(v).length==before.length+1;},"window.character_effect");
            const auto now=state(v);demand(now.text[before.length]==wp,"actual root character handler");
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
    void until_checkpoint(Child& v){
        for(unsigned i=0;i<execution_capacity+1;++i){v.wait("business");v.resume();
            const auto end=GetTickCount64()+8000;while(!v.pending && GetTickCount64()<end)v.poll();
            if(v.pending && v.halt=="checkpoint")return;
        }
        throw Error("checkpoint.business_prefix",ERROR_INVALID_STATE);
    }
    Experiment(const std::filesystem::path& exe,const std::filesystem::path& app,const std::filesystem::path& out):exe_(exe),app_(app),helper_(exe.parent_path()/"helper.exe"),out_(out),log_(out/"controller.jsonl"),execution_(log_,app,out),actions_(out/"actions.jsonl"),pump_(log_),metadata_rva_(native::metadata_rva(native::read_file(app),"R305Metadata")),prefix_(L"fv_r305_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64())),executable_(native::digest(native::read_file(app))){
        contract_rva_=native::metadata_rva(native::read_file(app_),Client::contract_export());
        mailbox_rva_=native::metadata_rva(native::read_file(app_),Client::mailbox_export());
        log_.emit("binary_split","\"manager\":"+json(exe_.wstring().c_str())+",\"application\":"+json(app_.wstring().c_str())+",\"helper\":"+json(helper_.wstring().c_str())+",\"managerSha256\":"+json(native::digest_text(native::digest(native::read_file(exe_))))+",\"applicationSha256\":"+json(native::digest_text(executable_)));
        log_.emit("client_schema",Client::schema());
        job_.value=CreateJobObjectW(nullptr,nullptr);check(job_.value!=nullptr,"CreateJobObjectW");JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&info,sizeof(info)),"SetInformationJobObject");
    }
    std::unique_ptr<Child> start(const std::string& name,bool source=false,bool wrong_chain=false){
        pump_.owner();std::filesystem::create_directories(out_/name);auto v=std::make_unique<Child>(log_,pump_,metadata_rva_);v->name=name;auto wide=std::wstring(name.begin(),name.end());v->desktop=std::make_unique<Desktop>(prefix_+L"_"+wide,log_);
        v->shared_name=L"Local\\"+prefix_+L"_"+wide;v->shared=std::make_unique<Mapping>(v->shared_name,sizeof(Shared),true);new(v->shared->view)Shared{};
        auto target_file=app_;
        if(name=="RB"&&scenario_==L"wrong-image"){
            target_file=out_/"wrong-image.exe";auto bytes=native::read_file(app_);const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());bytes[static_cast<size_t>(dos->e_lfanew)+8]^=1;write_bytes(target_file,bytes.data(),bytes.size());
        }
        execution_.verify_file(target_file,executable_);
        v->p=launch(target_file,v->desktop->full,{L"--role",L"fixture",L"--mode",source?L"source":L"restore",L"--state",v->shared_name,L"--log",(out_/name/"events.jsonl").wstring(),L"--gui",gui_?L"1":L"0",L"--checkpoint-probe",source&&scenario_==L"queued-paint"?L"1":source&&scenario_==L"queued-unknown"?L"2":source&&scenario_==L"queued-quit"?L"3":L"0",L"--missing-frame",name=="RB"&&scenario_==L"missing-frame"?L"1":L"0",L"--bad-unwind",name=="RB"&&scenario_==L"bad-unwind"?L"1":L"0",L"--bad-layout",name=="RB"&&scenario_==L"bad-layout"?L"1":L"0",L"--bad-abi",name=="RB"&&scenario_==L"abi-mismatch"?L"1":L"0",L"--wrong-chain",wrong_chain?L"1":L"0",L"--omit-ref",!source&&scenario_==L"unregistered-ref"?L"1":L"0",L"--unknown-child",name=="RB"&&scenario_==L"unknown-resource"?L"1":L"0",L"--bad-parent",!source&&scenario_==L"fail-B"&&name=="B"?L"1":L"0"},DEBUG_ONLY_THIS_PROCESS,log_,name);pump_.add(*v);assign(v->p);
        v->wait("ready");
        Client::validate(*v,contract_rva_);
        log_.emit("client_contract","\"view\":"+json(name)+",\"version\":"+std::to_string(Client::contract_version)+",\"workBytes\":"+std::to_string(Client::state_bytes)+",\"mailboxBytes\":"+std::to_string(sizeof(ClientCommand))+",\"applicationKind\":"+json(Client::kind()));
        verify(name+".CET-and-CFG-enabled",(v->m.shadow_policy&1) && (v->m.cfg_policy&1),"read actual process policies; no mitigation policy is modified");
        log_.emit("security_policy","\"view\":"+json(name)+",\"shadowPolicy\":"+std::to_string(v->m.shadow_policy)+",\"cfgPolicy\":"+std::to_string(v->m.cfg_policy));
        log_.emit("runtime_prepared","\"view\":"+json(name)+",\"pid\":"+std::to_string(v->p.pid)+",\"image\":"+std::to_string(v->image)+",\"window\":"+std::to_string(v->m.window)+",\"shadowPolicy\":"+std::to_string(v->m.shadow_policy)+",\"originalStackLow\":"+std::to_string(v->m.original_stack_low)+",\"originalStackHigh\":"+std::to_string(v->m.original_stack_high)+",\"managedStackLow\":"+std::to_string(v->m.active_stack_low)+",\"managedStackHigh\":"+std::to_string(v->m.active_stack_high)+",\"reportedStackLow\":"+std::to_string(v->m.reported_stack_low)+",\"reportedStackHigh\":"+std::to_string(v->m.reported_stack_high));
        return v;
    }
    std::unique_ptr<Input> input(Child& v){
        const unsigned no=0;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,input_closed),&no,sizeof(no));
        auto in=std::make_unique<Input>();in->mapping=std::make_unique<Mapping>(r304::channel_name(v.p.pid,v.p.tid),sizeof(r304::Channel),true);auto& c=*new(in->mapping->view)r304::Channel{};
        c.pid=v.p.pid;c.tid=v.p.tid;c.top=v.m.window;c.edit=v.m.edit;c.enabled=1;c.virtualize=1;c.token=(GetTickCount64()<<16)^v.p.pid;
        in->bridge=launch(helper_,v.desktop->full,{L"--role",L"input",L"--channel",r304::channel_name(v.p.pid,v.p.tid),L"--dll",(exe_.parent_path()/L"runtime304.dll").wstring(),L"--log",(out_/v.name/("input-"+std::to_string(++v.input_epoch)+".jsonl")).wstring()},0,log_,v.name+"-input");assign(in->bridge);
        run_until(v,[&]{return c.ready!=0;},"input.install");if(c.health==-1)throw Error(c.error_api,static_cast<DWORD>(c.error));demand(c.health==1 && c.ready==1,"existing R304 runtime installed");
        // A fresh Windows GUI queue may retain unowned modifier bits. Acquire
        // the supported subset through ordinary managed key-up events before
        // admitting application input; do not patch the DLL or native queries.
        submit(v,*in,{});demand(c.trace_count>0,"initial native queue observation");const auto before=c.traces[c.trace_count-1];
        keys(v,*in,{{VK_LCONTROL,false},{VK_RCONTROL,false},{VK_LSHIFT,false},{VK_RSHIFT,false}});
        r304::Packet release{};release.kind=r304::release;submit(v,*in,{release});
        const auto after=c.traces[c.trace_count-1];
        const bool neutral=!(after.ctrl&0x8000)&&!(after.shift&0x8000)&&!(after.left&0x8000)&&!(after.left_ctrl&0x80)&&!(after.right_ctrl&0x80)&&!(after.left_shift&0x80)&&!(after.right_shift&0x80);
        log_.emit("input_session_boundary","\"view\":"+json(v.name)+",\"initialCtrl\":"+std::to_string(before.ctrl)+",\"initialShift\":"+std::to_string(before.shift)+",\"initialLeft\":"+std::to_string(before.left)+",\"beforeApplied\":"+std::to_string(before.applied)+",\"afterCtrl\":"+std::to_string(after.ctrl)+",\"afterShift\":"+std::to_string(after.shift)+",\"afterLeft\":"+std::to_string(after.left)+",\"neutral\":"+(neutral?"true":"false")+",\"mechanism\":\"explicit-managed-key-up-and-release\"");
        verify(v.name+".input-session-neutral",neutral,"target GUI-thread native queries after acknowledged key-up/release handshake, before application input");
        if(!neutral)throw Error("input.session_not_neutral",ERROR_INVALID_STATE);return in;
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
    Observation state(Child& v){return Client::observe(v);}
    ClientCommand mailbox(Child& v){return native::read_value<ClientCommand>(v.p.process.value,v.image+mailbox_rva_);}
    static std::string local_addresses(const Snapshot& s){std::string result;for(unsigned i=0;i<s.layout.frames;++i){if(i)result+=',';result+=std::to_string(s.layout.frame[i].local);}return result;}
    CaseSnapshot save(Child& v,const std::string& label="S0"){
        CaseSnapshot result;static_cast<Snapshot&>(result)=execution_.save(v,label);auto& s=result;s.work=state(v);
        s.expected=Client::expected(s);
        log_.emit("snapshot","\"label\":"+json(label)+",\"view\":"+json(v.name)+",\"work\":"+work_json(s.work)+",\"rip\":"+std::to_string(s.cpu.Rip)+",\"rsp\":"+std::to_string(s.cpu.Rsp)+",\"registerR10\":"+std::to_string(s.cpu.R10)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"localAddresses\":["+local_addresses(s)+"]");
        log_.emit("execution_profile","\"view\":"+json(v.name)+",\"frames\":"+std::to_string(s.layout.frames)+",\"returns\":"+std::to_string(s.layout.returns)+",\"abi\":"+std::to_string(s.meta.abi_version));
        log_.emit("raw_layout","\"dataAddress\":"+std::to_string(s.meta.root)+",\"r10Offset\":"+std::to_string(offsetof(CONTEXT,R10)));
        write_bytes(out_/(label+"-data.bin"),s.memory.data(),s.memory.size());write_bytes(out_/(label+"-context.bin"),&s.cpu,sizeof(s.cpu));write_bytes(out_/(label+"-metadata.bin"),&s.meta,sizeof(s.meta));
        const auto active=static_cast<size_t>(s.cpu.Rsp-s.meta.stack-4096);write_bytes(out_/(label+"-active-stack.bin"),s.stack.data()+active,s.stack.size()-active);return result;
    }
    void restore(Child& v,const CaseSnapshot& s){
        const auto fault=v.name=="RB"&&scenario_==L"omit-fourth"?"stack-last":v.name=="RB"&&scenario_==L"omit-register"?"register":"";
        execution_.restore(v,s,fault);const auto actual=state(v);
        log_.emit("execution_result","\"view\":"+json(v.name)+",\"work\":"+work_json(actual)+",\"expectedReceipt\":"+std::to_string(s.expected)+",\"fault\":"+json(fault));
        if(actual.receipt!=s.expected||actual.returned!=Client::return_mask)throw Error("oracle.execution_receipt",ERROR_CRC);
        verify(v.name+".ordinary-CPP-return-chain",actual.receipt==s.expected&&actual.returned==Client::return_mask,"independent fixture oracle confirms real C++ returns and original computation");
        verify(v.name+".original-S0-state",actual.seed==s.work.seed&&actual.counter==s.work.counter&&actual.details==s.work.details&&!wcscmp(actual.text,s.work.text),"test-client business state matches raw capture");
        const unsigned yes=1;native::write_memory(v.p.process.value,v.image+metadata_rva_+offsetof(Metadata,published),&yes,sizeof(yes));v.resume();
    }
    std::string fingerprint(const CaseSnapshot& s,const std::string& phase){
        std::vector<BYTE> material;
        std::string fields="\"phase\":"+json(phase);
        auto part=[&](const char* name,const void* data,size_t size){
            const auto first=static_cast<const BYTE*>(data);std::vector<BYTE> bytes;if(size)bytes.assign(first,first+size);
            const auto hash=native::digest_text(native::digest(bytes));fields+=",\""+std::string(name)+"\":"+json(hash);
            material.insert(material.end(),bytes.begin(),bytes.end());
        };
        const auto file_manifest=s.file.describe();part("fileManifest",file_manifest.data(),file_manifest.size());part("fileContent",s.file.content.data(),s.file.content.size());
        part("targetImage",s.image.bytes.data(),s.image.bytes.size());part("memory",s.memory.data(),s.memory.size());part("stack",s.stack.data(),s.stack.size());
        part("context",&s.cpu,sizeof(s.cpu));part("resources",&s.resources,sizeof(s.resources));
        part("metadata",&s.meta,sizeof(s.meta));part("executionLayout",&s.layout,sizeof(s.layout));part("returns",s.returns.data(),s.returns.size()*sizeof(std::uint64_t));
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
    void build_view(View& result,const std::string& name,const CaseSnapshot& s,Child* other=nullptr){
        bool workspace_owned=false;const auto workspace=out_/"workspace"/name;
        const auto ticket=actions_.begin(name,"snapshot.restore");actions_.state(ticket,1);
        try{
            result.child=start(name,false,name=="RB"&&scenario_==L"branch-failure");auto& v=*result.child;
            if(other)routing_guard(*other,v);
            auto resources=s.resources;if(name=="RB"&&scenario_==L"resource-failure"){resources.nodes[2].parent=0;log_.emit("resource_rebuild_fault","\"view\":\"RB\",\"field\":\"BUTTON.parent\",\"privatePayloadOnly\":true");}v.resume();resource(v,2,"prepared",&resources);
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
            experimental::point("view-resources",name);
#endif
            Files::restore(s.file,workspace,name=="RB"&&scenario_==L"file-version"?"version":name=="RB"&&scenario_==L"file-write"?"write":"");workspace_owned=true;
            const auto id=s.file.present?Files::identity(workspace/Client::file_name()):Files::Identity{};
            log_.emit("workspace_restored","\"view\":"+json(name)+",\"file\":"+s.file.describe()+",\"targetVolume\":"+std::to_string(id.volume)+",\"targetObject\":"+std::to_string(id.object)+",\"directory\":"+json(workspace.wstring().c_str()));
            post(v,prepare_stop_message);v.wait("checkpoint");actions_.state(ticket,2);restore(v,s);
            const auto r=resource(v,3,"initial");
            verify(name+".native-initial-snapshot",!wcscmp(r.nodes[1].caption,s.resources.nodes[1].caption)&&r.selection_start==s.resources.selection_start&&r.selection_end==s.resources.selection_end&&r.modified==s.resources.modified&&r.focus==s.resources.focus&&r.can_undo==s.resources.can_undo,"native control and original Undo match selected Snapshot after actual C++ returns");
            if(s.file.present)verify(name+".private-file-version",Artifacts::digest(workspace/Client::file_name())==s.file.sha256,"private workspace bytes match captured completed file version");
            else verify(name+".empty-original-workspace",std::filesystem::is_empty(workspace),"S0 carrier receives no later branch file");
            capture(v,"initial");running(v);result.input=input(v);
            r304::Packet move{};move.kind=r304::mouse;move.action=r304::move;move.x=40;move.y=85;submit(v,*result.input,{move});
            const auto ready=resource(v,3,"input-ready");
            verify(name+".input-processed-before-publication",static_cast<std::uint64_t>(result.input->c().consumed)==result.input->sequence&&!wcscmp(ready.nodes[1].caption,s.resources.nodes[1].caption)&&ready.selection_start==s.resources.selection_start&&ready.selection_end==s.resources.selection_end&&ready.modified==s.resources.modified&&ready.can_undo==s.resources.can_undo&&ready.focus==s.resources.focus,"owner GUI thread processed managed mouse move/barrier and native control readback without changing Snapshot state");
            demand(failed_==0,"no failed evidence before View publication");v.published=true;actions_.state(ticket,3,"{\"pid\":"+std::to_string(v.p.pid)+"}");
            log_.emit("view_published","\"view\":"+json(name)+",\"pid\":"+std::to_string(v.p.pid)+",\"resourceExecutionFrameAndInputChannelVerified\":true");
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
            experimental::point("view-published",name);
#endif
        }catch(const Error& e){
            if(result.child){if(result.input){close_input(*result.child,*result.input);result.input.reset();}
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
                try{close_unpublished_window(*result.child);}catch(const Error& cleanup){log_.emit("discard_window_cleanup_error","\"view\":"+json(name)+",\"api\":"+json(cleanup.api)+",\"error\":"+std::to_string(cleanup.code)+",\"primaryApi\":"+json(e.api));}
#endif
                result.child->stop();}
            if(workspace_owned)std::filesystem::remove_all(workspace);
            actions_.fail(ticket,e);log_.emit("view_restore_rejected","\"view\":"+json(name)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"published\":false,\"workspaceExists\":"+(std::filesystem::exists(workspace)?"true":"false"));throw;
        }
    }
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
    void close_unpublished_window(Child& v){
        if(v.pending||v.published||!v.m.window||v.p.exited)return;
        auto send=[&](const wchar_t* op,UINT message){const auto name=L"Local\\"+prefix_+L"_discard_"+std::to_wstring(++serial_);Mapping reply(name,sizeof(Reply),true);auto& r=*new(reply.view)Reply{};
            auto worker=launch(helper_,v.desktop->full,{L"--role",L"bridge",L"--state",v.shared_name,L"--reply",name,L"--target",L"top",L"--op",op,L"--message",std::to_wstring(message)},0,log_,"helper");assign(worker);confirm(worker,v);
            DWORD code=0;check(GetExitCodeProcess(worker.process.value,&code),"GetExitCodeProcess.discard_window");if(!r.done)throw Error("discard.window.no_reply",ERROR_INVALID_STATE);
            log_.emit("discard_window_native_result","\"view\":"+json(v.name)+",\"operation\":"+json(op)+",\"api\":"+json(r.api)+",\"error\":"+std::to_string(r.error)+",\"exit\":"+std::to_string(code)+",\"result\":"+std::to_string(r.api_result));
            if(!wcscmp(op,L"send")){if(code||r.error||!r.api_result)throw Error("discard.window.close",r.error?r.error:ERROR_INVALID_STATE);}
            else if(code!=2||r.error!=ERROR_INVALID_WINDOW_HANDLE||strcmp(r.api,"validate_target_window"))throw Error("discard.window.still_present",ERROR_BUSY);
        };
        send(L"send",WM_CLOSE);send(L"observe",0);
        log_.emit("unpublished_window_discarded","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.p.pid)+",\"hwnd\":"+std::to_string(v.m.window)+",\"nativeCloseAndAbsenceConfirmed\":true");
    }
#endif
    struct Observed {Resources resource;Observation work;};
    Observed observe(Child& v,const char* phase){
        auto r=resource(v,3,phase);auto w=state(v);
        log_.emit("branch_observed","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"work\":"+work_json(w)+",\"resources\":"+resources_json(r)+",\"pending\":"+(v.pending?"true":"false")+",\"frozen\":"+std::to_string(v.m.frozen));
        return {r,w};
    }
    void stable(const Observed& before,const Observed& after,const std::string& label){
        const auto& a=before.resource;const auto& b=after.resource;
        verify(label,!wcscmp(a.nodes[1].caption,b.nodes[1].caption)&&a.selection_start==b.selection_start&&a.selection_end==b.selection_end&&a.modified==b.modified&&a.can_undo==b.can_undo&&a.focus==b.focus&&a.nodes[0].hwnd==b.nodes[0].hwnd&&a.nodes[1].hwnd==b.nodes[1].hwnd&&before.work.details==after.work.details&&before.work.seed==after.work.seed&&before.work.receipt==after.work.receipt&&before.work.clicks==after.work.clicks&&!wcscmp(before.work.text,after.work.text)&&after.work.counter>=before.work.counter,"native EDIT/selection/focus/undo and non-clock managed state unchanged; autonomous counter may advance");
    }
    void edit(View& view,wchar_t character_value,const wchar_t* expected,const char* phase){
        auto& v=*view.child;character(v,*view.input,character_value);
        // Root WM_CHAR is a normal application input too: independently mutate
        // the restored data page, not just the OS-owned EDIT resource.
        const auto old=state(v);post(v,WM_CHAR,character_value);
        run_until(v,[&]{return state(v).length==old.length+1;},"input.root-character");
        const auto now=observe(v,phase);
        verify(v.name+"."+phase,!wcscmp(now.resource.nodes[1].caption,expected)&&now.resource.selection_start==wcslen(expected)&&now.resource.selection_end==wcslen(expected)&&now.resource.can_undo&&now.resource.modified&&now.resource.focus==2&&now.work.length==old.length+1&&now.work.text[old.length]==character_value,"real native EDIT receives WM_CHAR and root application independently updates managed text through its original input handler");
        capture(v,phase);
    }
    struct Produced {Actions::Ticket ticket;ClientCommand receipt;};
    Produced file_action(View& view,unsigned operation,const std::filesystem::path& output,const std::filesystem::path& a={},const std::filesystem::path& b={}){
        auto& v=*view.child;const auto ticket=actions_.begin(v.name,operation==client_save?"application.save":"application.process_private_files");
        try{
            std::filesystem::create_directories(output.parent_path());
            ClientCommand request{};request.action=ticket.sequence;request.operation=operation;
            wcscpy_s(request.path[0],a.c_str());wcscpy_s(request.path[1],b.c_str());wcscpy_s(request.path[2],output.c_str());
            wcscpy_s(request.evidence,(out_/v.name/"file-effects.jsonl").c_str());
            actions_.state(ticket,1,"{\"output\":"+json(output.wstring().c_str())+"}");
            native::write_memory(v.p.process.value,v.image+mailbox_rva_,&request,sizeof(request));
            mouse_click(v,*view.input); // real native BUTTON / BN_CLICKED
            actions_.state(ticket,2,"{\"via\":\"managed-mouse/native-BUTTON/BN_CLICKED\"}");
            run_until(v,[&]{return mailbox(v).done==request.action;},"application.file_completion");
            const auto receipt=mailbox(v);
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
    void persist(const CaseSnapshot& s,const std::string& label){
        write_bytes(out_/(label+"-target-image.bin"),s.image.bytes.data(),s.image.bytes.size());
        write_bytes(out_/(label+"-full-stack.bin"),s.stack.data(),s.stack.size());
        write_bytes(out_/(label+"-resources.bin"),&s.resources,sizeof(s.resources));
        write_bytes(out_/(label+"-execution-layout.bin"),&s.layout,sizeof(s.layout));
        write_bytes(out_/(label+"-returns.bin"),s.returns.data(),s.returns.size()*sizeof(std::uint64_t));
        const std::array<std::uint64_t,3> abi{s.cookie,s.complement,s.expected};
        write_bytes(out_/(label+"-abi.bin"),abi.data(),sizeof(abi));
        write_bytes(out_/(label+"-executable.bin"),s.executable.data(),s.executable.size());
        const auto manifest=s.file.describe();write_bytes(out_/(label+"-file.json"),manifest.data(),manifest.size());write_bytes(out_/(label+"-file.bin"),s.file.content.data(),s.file.content.size());
        std::ofstream f(out_/(label+"-resources.json"),std::ios::binary);f<<resources_json(s.resources)<<'\n';
    }
    CaseSnapshot terminal(View& view,const std::string& label){
        auto& v=*view.child;const auto ticket=actions_.begin(v.name,"snapshot."+label);actions_.state(ticket,1);
        try{close_input(v,*view.input);view.input.reset();freeze(v);
        const auto r=resource(v,1,label.c_str());
        const auto file=Files::capture(v.name,v.p.pid,mailbox(v),out_/"workspace"/v.name,Client::file_name());
        actions_.state(ticket,2);log_.emit("workspace_snapshot","\"snapshot\":"+json(label)+",\"file\":"+file.describe());capture(v,label);
        post(v,checkpoint_message);until_checkpoint(v);
        auto s=save(v,label);s.resources=r;s.file=file;persist(s,label);
        verify(label+".terminal-checkpoint",v.initialized==0&&v.checkpoints==2&&v.prefixes_once()&&r.can_undo&&r.undo.valid&&r.undo.operations==1,"later GUI safe point captures actual modified data and fresh computation locals without re-running original initialization");
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
            auto captured=save(*source);captured.resources=resources;const CaseSnapshot s=std::move(captured);persist(s,"S0");const auto original=fingerprint(s,"S0-published");
            verify("source.initialization-once",source->initialized==1&&source->prefixes_once(),"debugger independently observes source business only once");
            source->resume();source->wait("continued");source->resume();running(*source);post(*source,WM_CHAR,L'!');
            verify("source.changed-after-S0",state(*source).length>s.work.length,"original execution advances after S0 before confirmed exit");source->stop();
            build_view(a,"A",s);build_view(b,"B",s,a.child.get());alive({a.child.get(),b.child.get()},"AB");
            auto unchanged=observe(*b.child,"before-A");edit(a,L'A',L"A","single-edit");stable(unchanged,observe(*b.child,"after-A"),"B.unchanged-by-A");
            unchanged=observe(*a.child,"before-B");edit(b,L'B',L"B","single-edit");stable(unchanged,observe(*a.child,"after-B"),"A.unchanged-by-B");
            const auto undo_a=resource(*a.child,3,"before-save"),undo_b=resource(*b.child,3,"before-save");
            const auto made_a=file_action(a,client_save,out_/(std::filesystem::path("workspace/A")/Client::file_name()));
            const auto made_b=file_action(b,client_save,out_/(std::filesystem::path("workspace/B")/Client::file_name()));
            const auto after_a=resource(*a.child,3,"after-save"),after_b=resource(*b.child,3,"after-save");
            auto undo_preserved=[](const Resources& x,const Resources& y){return !wcscmp(x.nodes[1].caption,y.nodes[1].caption)&&x.can_undo==y.can_undo&&x.selection_start==y.selection_start&&x.selection_end==y.selection_end&&x.modified==y.modified&&x.undo.valid&&y.undo.valid&&x.undo.operations==1&&y.undo.operations==1;};
            verify("native-BUTTON-preserves-Undo",undo_preserved(undo_a,after_a)&&undo_preserved(undo_b,after_b),"native save and mouse focus restoration do not edit the control or destroy its observed single Undo");
            auto& artifact_a=artifacts.publish("A",a.child->p.pid,1,made_a.ticket,made_a.receipt,out_/(std::filesystem::path("workspace/A")/Client::file_name()));
            auto& artifact_b=artifacts.publish("B",b.child->p.pid,1,made_b.ticket,made_b.receipt,out_/(std::filesystem::path("workspace/B")/Client::file_name()));
            artifacts.verify_all("published");
            const bool snapshot_fault=scenario==L"undo-unsupported"||scenario==L"file-open";
            if(snapshot_fault){
                if(scenario==L"undo-unsupported")character(*a.child,*a.input,L'2');
                Handle held;
                if(scenario==L"file-open"){held.value=CreateFileW((out_/(std::filesystem::path("workspace/A")/Client::file_name())).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held.value!=INVALID_HANDLE_VALUE,"CreateFileW.negative_outstanding_workspace_writer");log_.emit("outstanding_file_handle","\"view\":\"A\",\"ownerPid\":"+std::to_string(GetCurrentProcessId())+",\"access\":\"GENERIC_WRITE\",\"realHandle\":true");}

                try{terminal(a,"SA");throw Error("expected_snapshot_rejection_missing",ERROR_INVALID_STATE);}
                catch(const Error& e){rejected=scenario==L"undo-unsupported"?(e.api=="undo.unsupported_or_multiple_edits"&&e.code==ERROR_NOT_SUPPORTED):(e.api=="CreateFileW.snapshot_exclusive_closed_file"&&e.code==ERROR_SHARING_VIOLATION);verify("snapshot.real-rejection",rejected,e.api+" code="+std::to_string(e.code));if(!rejected)throw;log_.emit("snapshot_rejected","\"snapshot\":\"SA\",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"published\":false");}
                held.close();running(*a.child);a.input=input(*a.child);character(*a.child,*a.input,L'X');capture(*a.child,"source-after-rejection");
                const CaseSnapshot sb=terminal(b,"SB");const auto bh=fingerprint(sb,"SB-published");b.child->stop();build_view(rb,"RB",sb,a.child.get());build_view(c,"C",s,rb.child.get());
                const auto imports=artifacts.import_private("C",artifact_a,artifact_b,out_/"workspace/C");file_action(c,client_process,out_/"workspace/C/processed.txt",imports/"A.txt",imports/"B.txt");
                verify("failed-Snapshot-not-published",!std::filesystem::exists(out_/"SA-data.bin"),"unsupported GUI/file resource never publishes raw SA or a usable restore");
                verify("SB.immutable",fingerprint(sb,"final-SB")==bh,"other branch's saved state retained");alive({a.child.get(),rb.child.get(),c.child.get()},"negative-snapshot-survivors");
            }else{
                const CaseSnapshot sa=terminal(a,"SA"),sb=terminal(b,"SB");const auto ah=fingerprint(sa,"SA-published"),bh=fingerprint(sb,"SB-published");
                verify("distinct-checkpoints",ah!=bh&&ah!=original&&bh!=original,"actual branch states, file versions and Undo graphs have distinct fingerprints");
                const auto apid=a.child->p.pid,bpid=b.child->p.pid;a.child->stop();b.child->stop();
                verify("original-AB-exited",WaitForSingleObject(a.child->p.process.value,0)==WAIT_OBJECT_0&&WaitForSingleObject(b.child->p.process.value,0)==WAIT_OBJECT_0,"owned A/B handles signal termination before terminal restore");
                log_.emit("terminal_sources_exited","\"A\":"+std::to_string(apid)+",\"B\":"+std::to_string(bpid));
                build_view(ra,"RA",sa);
                const auto ra_before=observe(*ra.child,"before-RB");
                const bool restore_fault=scenario==L"file-version"||scenario==L"file-write"||scenario==L"branch-failure"||scenario==L"wrong-image"||scenario==L"abi-mismatch"||scenario==L"unknown-resource"||scenario==L"bad-layout"||scenario==L"missing-frame"||scenario==L"bad-unwind"||scenario==L"omit-fourth"||scenario==L"omit-register";
                try{build_view(rb,"RB",sb,ra.child.get());if(restore_fault)throw Error("expected_restore_rejection_missing",ERROR_INVALID_STATE);}
                catch(const Error& e){if(!restore_fault)throw;const auto expected=scenario==L"omit-fourth"||scenario==L"omit-register"?"oracle.execution_receipt":scenario==L"bad-layout"?"unwind.declared_depth_mismatch":scenario==L"missing-frame"?"abi.execution_frame_missing":scenario==L"bad-unwind"?"unwind.declared_frame_mismatch":scenario==L"wrong-image"?"image.file_identity_mismatch":scenario==L"abi-mismatch"?"abi.header_mismatch":scenario==L"unknown-resource"?"abi.unknown_required_resource":scenario==L"file-version"?"restore.private_file_version":scenario==L"file-write"?"WriteFile.private_workspace":"restore.call_chain_mismatch";rejected=e.api==expected&&(!rb.child||!rb.child->published)&&!std::filesystem::exists(out_/"workspace/RB")&&!std::filesystem::exists(out_/"workspace/RB.restore-partial");verify("RB.private-restore-rejected",rejected,e.api+" code="+std::to_string(e.code));if(!rejected)throw;}
                stable(ra_before,observe(*ra.child,"after-RB"),"RA.unchanged-by-RB-restore");
                build_view(c,"C",s,ra.child.get());const auto c_initial=resource(*c.child,3,"original-S0");
                verify("C.original-state",!wcscmp(c_initial.nodes[1].caption,s.resources.nodes[1].caption)&&!c_initial.can_undo&&state(*c.child).document_reads==0&&state(*c.child).document_writes==0,"C resumes S0 with no later document state or Undo history");
                if(scenario==L"artifact-hash"){
                    const auto before=observe(*c.child,"before-bad-import");
                    try{artifacts.import_private("C",artifact_a,artifact_b,out_/"workspace/C","hash");throw Error("expected_import_rejection_missing",ERROR_INVALID_STATE);}
                    catch(const Error& e){rejected=e.api=="artifact.import_hash"&&e.code==ERROR_CRC;verify("C.hash-rejection",rejected&&!std::filesystem::exists(out_/"workspace/C/imports"),"failed batch rolls back and does not expose partial import");if(!rejected)throw;}
                    stable(before,observe(*c.child,"after-bad-import"),"C.unchanged-by-rejected-import");
                    // A separate, explicitly new Action imports the unchanged versions.
                }
                const auto imports=artifacts.import_private("C",artifact_a,artifact_b,out_/"workspace/C");
                const auto ra_hold=observe(*ra.child,"before-C-process");Observed rb_hold{};if(rb.child&&rb.child->published)rb_hold=observe(*rb.child,"before-C-process");
                const auto output=file_action(c,client_process,out_/"workspace/C/processed.txt",imports/"A.txt",imports/"B.txt");
                verify("C.real-native-file-processing",output.receipt.reads==2&&output.receipt.writes==3&&output.receipt.closes==5&&output.receipt.live_handles==0&&std::string(output.receipt.read_bytes[0],output.receipt.read_size[0])==std::string(sa.file.content.begin(),sa.file.content.end())&&std::string(output.receipt.read_bytes[1],output.receipt.read_size[1])==std::string(sb.file.content.begin(),sb.file.content.end()),"application reads both fixed versions via actual ReadFile and writes only private copies/result");
                verify("C.does-not-write-restored-workspaces",Artifacts::digest(out_/(std::filesystem::path("workspace/RA")/Client::file_name()))==sa.file.sha256&&(!(rb.child&&rb.child->published)||Artifacts::digest(out_/(std::filesystem::path("workspace/RB")/Client::file_name()))==sb.file.sha256),"C native writes touch only its independent imports and result");
                stable(ra_hold,observe(*ra.child,"after-C-process"),"RA.unchanged-by-C");if(rb.child&&rb.child->published)stable(rb_hold,observe(*rb.child,"after-C-process"),"RB.unchanged-by-C");
                verify("work-file-native-identities",Files::distinct(out_/(std::filesystem::path("workspace/RA")/Client::file_name()),out_/(std::filesystem::path("workspace/A")/Client::file_name()))&&Files::distinct(out_/(std::filesystem::path("workspace/RA")/Client::file_name()),artifact_a.directory/"data.bin")&&Files::distinct(imports/"A.txt",artifact_a.directory/"data.bin"),"restore/import create new native file objects, not shared paths or hardlinks");
                if(rb.child&&rb.child->published)verify("RB.private-work-file",Files::distinct(out_/(std::filesystem::path("workspace/RB")/Client::file_name()),out_/(std::filesystem::path("workspace/B")/Client::file_name()))&&Files::distinct(out_/(std::filesystem::path("workspace/RA")/Client::file_name()),out_/(std::filesystem::path("workspace/RB")/Client::file_name())),"B terminal file version belongs to its new process workspace");
                verify_undo(ra,s,L"A");
                const auto changed_a=file_action(ra,client_save,out_/(std::filesystem::path("workspace/RA")/Client::file_name()));
                verify("RA.private-write-isolation",changed_a.receipt.sha256!=sa.file.sha256&&Artifacts::digest(out_/(std::filesystem::path("workspace/A")/Client::file_name()))==sa.file.sha256&&Artifacts::digest(out_/(std::filesystem::path("workspace/B")/Client::file_name()))==sb.file.sha256,"new application save changes RA file without changing old work files or immutable versions");
                if(rb.child&&rb.child->published){
                    verify("RB.file-unaffected-by-RA-save",Artifacts::digest(out_/(std::filesystem::path("workspace/RB")/Client::file_name()))==sb.file.sha256,"RA writes cannot change RB private file");
                    verify_undo(rb,s,L"B");file_action(rb,client_save,out_/(std::filesystem::path("workspace/RB")/Client::file_name()));
                    verify("RA.file-unaffected-by-RB-save",Artifacts::digest(out_/(std::filesystem::path("workspace/RA")/Client::file_name()))==changed_a.receipt.sha256,"RB writes cannot change RA private file");
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
