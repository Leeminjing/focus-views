#pragma once
#include "../../r5-002/clients/host.hpp"
#include "../runtime/store.hpp"
namespace r5003 {
inline std::uint64_t creation(HANDLE handle){FILETIME c{},e{},k{},u{};check(GetProcessTimes(handle,&c,&e,&k,&u),"GetProcessTimes.restart_identity");return (static_cast<std::uint64_t>(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
struct Instance {DWORD pid=0;std::uint64_t created=0;};
inline bool different_instance(const Instance& a,const Instance& b){return a.pid&&a.created&&b.pid&&b.created&&(a.pid!=b.pid||a.created!=b.created);}
inline bool gone(const Instance& x){
    if(!x.pid||!x.created)throw Error("snapshot.instance_identity",ERROR_INVALID_DATA);
    Handle p(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,x.pid));
    if(!p.value){const auto error=GetLastError();if(error==ERROR_INVALID_PARAMETER)return true;throw Error("OpenProcess.previous_instance",error);}
    if(creation(p.value)!=x.created)return true;
    const auto waited=WaitForSingleObject(p.value,0);
    if(waited==WAIT_FAILED)throw Error("WaitForSingleObject.previous_instance",GetLastError());
    if(waited!=WAIT_TIMEOUT&&waited!=WAIT_OBJECT_0)throw Error("WaitForSingleObject.previous_instance",ERROR_INVALID_DATA);
    return waited==WAIT_OBJECT_0;
}
struct FixedArtifact {std::string id,view,action,sha256,manifest_sha256;unsigned version=0;DWORD pid=0;std::uint64_t bytes=0;};
struct Session {
    Instance manager,source,a,b;
    std::array<BYTE,32> executable{};
    std::string client;
    std::array<std::string,3> packets,fingerprints;
    std::array<FixedArtifact,2> artifacts;
};
inline Bytes encode_session(const Session& s){Writer w;for(const auto& instance:{s.manager,s.source,s.a,s.b}){w.pod(instance.pid);w.pod(instance.created);}w.pod(s.executable);w.text(s.client);
    for(unsigned i=0;i<3;++i){w.text(s.packets[i]);w.text(s.fingerprints[i]);}
    for(const auto& a:s.artifacts){w.text(a.id);w.text(a.view);w.text(a.action);w.text(a.sha256);w.text(a.manifest_sha256);w.pod(a.version);w.pod(a.pid);w.pod(a.bytes);}return w.bytes;
}
inline Session decode_session(const Bytes& b){Reader r{b};Session s;for(auto* instance:{&s.manager,&s.source,&s.a,&s.b}){instance->pid=r.pod<DWORD>();instance->created=r.pod<std::uint64_t>();if(!instance->pid||!instance->created)throw Error("snapshot.instance_identity",ERROR_INVALID_DATA);}s.executable=r.pod<std::array<BYTE,32>>();s.client=r.text(128);
    for(unsigned i=0;i<3;++i){s.packets[i]=r.text(64);s.fingerprints[i]=r.text(64);}
    for(auto& a:s.artifacts){a.id=r.text(128);a.view=r.text(128);a.action=r.text(128);a.sha256=r.text(64);a.manifest_sha256=r.text(64);a.version=r.pod<unsigned>();a.pid=r.pod<DWORD>();a.bytes=r.pod<std::uint64_t>();}
    r.end();return s;
}
template<class Client>class Restart : public Experiment<Client> {
    using Base=Experiment<Client>;using View=typename Base::View;
    using Base::out_;using Base::log_;using Base::actions_;using Base::execution_;using Base::failed_;using Base::executable_;using Base::verify;using Base::run_until;
    std::filesystem::path store_;
    static Instance instance(const Process& p){return {p.pid,creation(p.process.value)};}
    Instance record_instance(const Process& p,const char* view){const auto x=instance(p);log_.emit("instance_recorded","\"view\":"+json(view)+",\"pid\":"+std::to_string(x.pid)+",\"created\":"+std::to_string(x.created));return x;}
    static Instance manager(){return {GetCurrentProcessId(),creation(GetCurrentProcess())};}
    void dispose(View& v){if(!v.child)return;std::exception_ptr failure;
        if(v.input){try{const auto& channel=v.input->c();if(channel.health!=1)throw Error(channel.error_api,static_cast<DWORD>(channel.error));this->close_input(*v.child,*v.input);}
            catch(...){failure=std::current_exception();}
            // Even an invalid channel has an owned helper whose exit needs an
            // independent acknowledgement. Never abandon it after close fails.
            try{this->confirm(v.input->bridge,*v.child);}catch(...){if(!failure)failure=std::current_exception();}
            v.input.reset();}
        try{v.child->stop();}catch(...){if(!failure)failure=std::current_exception();}v.child.reset();if(failure)std::rethrow_exception(failure);
    }
    void record_snapshot(const CaseSnapshot& s,const char* label,Session& session,unsigned slot){
        if(failed_)throw Error("snapshot.failed_evidence",ERROR_INVALID_DATA);
        const auto path=store_/(std::string(label)+".fvs");r5003::save(path,s);session.packets[slot]=hash(read(path));session.fingerprints[slot]=this->fingerprint(s,std::string(label)+"-disk-published");
        log_.emit("snapshot_disk_published","\"label\":"+json(label)+",\"file\":"+json(path.wstring().c_str())+",\"sha256\":"+json(session.packets[slot])+",\"fingerprint\":"+json(session.fingerprints[slot])+",\"sourcePid\":"+std::to_string(s.meta.pid)+",\"managerPid\":"+std::to_string(GetCurrentProcessId()));
    }
    CaseSnapshot read_snapshot(const char* label,const Session& session,unsigned slot){
        const auto path=store_/(std::string(label)+".fvs");const auto bytes=read(path);
        // Parse/validate first so a malformed packet retains its precise failure.
        CaseSnapshot s;static_cast<Snapshot&>(s)=decode(unpack(Kind::snapshot,bytes),executable_);
        if(hash(bytes)!=session.packets[slot])throw Error("snapshot.session_hash",ERROR_CRC);
        s.work=Client::saved(s);s.expected=Client::expected(s);
        if(this->fingerprint(s,std::string(label)+"-loaded")!=session.fingerprints[slot])throw Error("snapshot.loaded_fingerprint",ERROR_CRC);
        // Validate target PE and actual saved unwind without creating an app.
        execution_.unwind(s);
        this->persist(s,label);
        write_bytes(out_/(std::string(label)+"-data.bin"),s.memory.data(),s.memory.size());write_bytes(out_/(std::string(label)+"-context.bin"),&s.cpu,sizeof(s.cpu));write_bytes(out_/(std::string(label)+"-metadata.bin"),&s.meta,sizeof(s.meta));
        log_.emit("snapshot_disk_loaded","\"label\":"+json(label)+",\"sha256\":"+json(hash(bytes))+",\"sourcePid\":"+std::to_string(s.meta.pid)+",\"managerPid\":"+std::to_string(GetCurrentProcessId())+",\"abi\":"+std::to_string(s.meta.abi_version)+",\"frames\":"+std::to_string(s.layout.frames)+",\"work\":"+work_json(s.work));return s;
    }
    std::unique_ptr<Artifacts::Image> open_artifact(const FixedArtifact& saved,Artifacts& verifier){
        if((saved.id!="A-v1"&&saved.id!="B-v1")||saved.version!=1||saved.bytes==0||saved.bytes>4096)throw Error("artifact.persisted_descriptor",ERROR_INVALID_DATA);
        auto a=std::make_unique<Artifacts::Image>();a->id=saved.id;a->view=saved.view;a->action=saved.action;a->sha256=saved.sha256;a->pid=saved.pid;a->version=saved.version;a->bytes=saved.bytes;a->directory=store_/"artifacts"/a->id;
        auto pin=[](const std::filesystem::path& p){Handle h(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(h.value!=INVALID_HANDLE_VALUE,"CreateFileW.persisted_artifact_guard");return h;};
        a->data_guard=pin(a->directory/"data.bin");a->manifest_guard=pin(a->directory/"manifest.json");Files::identity(a->data_guard.value);Files::identity(a->manifest_guard.value);
        const auto bytes=Artifacts::bytes(a->directory/"data.bin"),manifest=Artifacts::bytes(a->directory/"manifest.json");
        if(hash(bytes)!=a->sha256||bytes.size()!=a->bytes||hash(manifest)!=saved.manifest_sha256||std::string(manifest.begin(),manifest.end())!=verifier.describe(*a)+"\n")throw Error("artifact.persisted_hash",ERROR_CRC);
        return a;
    }
public:
    Restart(const std::filesystem::path& exe,const std::filesystem::path& app,const std::filesystem::path& out,const std::filesystem::path& store):Base(exe,app,out),store_(store){this->gui_=true;this->scenario_=L"positive";}
    int save_phase(){bool completed=false;Session session;session.manager=manager();session.executable=executable_;session.client=Client::kind();
        std::unique_ptr<Child> source;View a,b;
        try{
            if(!std::filesystem::create_directory(store_))throw Error("snapshot.store_exists",ERROR_ALREADY_EXISTS);
            Artifacts artifacts(store_/"artifacts",actions_,[&](const char* t,const std::string& f){log_.emit(t,f);});
            source=this->start("source",true);session.source=record_instance(source->p,"source");source->resume();source->wait("init");source->resume();
            run_until(*source,[&]{return source->metadata().published==1;},"source.business_initialization");
            auto in=this->input(*source);this->select_prefix(*source,*in);this->close_input(*source,*in);in.reset();this->freeze(*source);
            const auto resources=this->resource(*source,1,"S0");this->capture(*source,"S0");this->post(*source,checkpoint_message);this->until_checkpoint(*source);
            auto s=this->save(*source);s.resources=resources;this->persist(s,"S0");
            verify("source.initialization-once",source->initialized==1&&source->prefixes_once(),"external debug events confirm source work once");
            record_snapshot(s,"S0",session,0);
            source->resume();source->wait("continued");source->resume();this->running(*source);this->post(*source,WM_CHAR,L'!');
            verify("source.changed-after-S0",this->state(*source).length>s.work.length,"source evolves after immutable S0");source->stop();source.reset();
            this->build_view(a,"A",s);this->build_view(b,"B",s,a.child.get());session.a=record_instance(a.child->p,"A");session.b=record_instance(b.child->p,"B");this->alive({a.child.get(),b.child.get()},"save-AB");
            const auto b_before=this->observe(*b.child,"before-A");this->edit(a,L'A',L"A","single-edit");this->stable(b_before,this->observe(*b.child,"after-A"),"B.independent");
            const auto a_before=this->observe(*a.child,"before-B");this->edit(b,L'B',L"B","single-edit");this->stable(a_before,this->observe(*a.child,"after-B"),"A.independent");
            const auto made_a=this->file_action(a,client_save,out_/"workspace/A"/Client::file_name()),made_b=this->file_action(b,client_save,out_/"workspace/B"/Client::file_name());
            const auto& artifact_a=artifacts.publish("A",a.child->p.pid,1,made_a.ticket,made_a.receipt,out_/"workspace/A"/Client::file_name());
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
            experimental::point("artifact-published","A-v1");
#endif
            const auto& artifact_b=artifacts.publish("B",b.child->p.pid,1,made_b.ticket,made_b.receipt,out_/"workspace/B"/Client::file_name());
            unsigned i=0;for(const auto* p:{&artifact_a,&artifact_b})session.artifacts[i++]={p->id,p->view,p->action,p->sha256,hash(Artifacts::bytes(p->directory/"manifest.json")),p->version,p->pid,p->bytes};
            const auto sa=this->terminal(a,"SA"),sb=this->terminal(b,"SB");record_snapshot(sa,"SA",session,1);record_snapshot(sb,"SB",session,2);
            auto commit=[&]{
                if(failed_)throw Error("snapshot.failed_evidence",ERROR_INVALID_DATA);
                publish(store_/"session.fvs",Kind::session,encode_session(session));completed=true;
                log_.emit("session_published","\"managerPid\":"+std::to_string(session.manager.pid)+",\"managerCreated\":"+std::to_string(session.manager.created)+",\"sourcePid\":"+std::to_string(session.source.pid)+",\"aPid\":"+std::to_string(session.a.pid)+",\"bPid\":"+std::to_string(session.b.pid)+",\"sha256\":"+json(hash(read(store_/"session.fvs"))));
            };
#ifdef FOCUS_VIEWS_CRASH_EXPERIMENT
            // This explicit experiment commits completed data while A/B are
            // still frozen/live, so forced death exercises resource cleanup.
            if(experimental::observer){commit();experimental::point("session-committed");}
#endif
            dispose(a);dispose(b);artifacts.verify_all("manager1-final");
            verify("all-source-instances-exited",gone(session.source)&&gone(session.a)&&gone(session.b),"PID and creation-time identities confirm source/A/B terminated");
            if(!completed)commit();
        }catch(const Error& e){log_.emit("experiment_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"message\":"+json(e.what()));}
        for(auto* v:{&b,&a})try{dispose(*v);}catch(const std::exception& e){++failed_;log_.emit("cleanup_error","\"message\":"+json(e.what()));}if(source){source->stop();source.reset();}
        log_.emit("restart_complete","\"phase\":\"save\",\"passed\":"+std::string(completed&&!failed_?"true":"false")+",\"managerPid\":"+std::to_string(GetCurrentProcessId()));return completed&&!failed_?0:2;
    }
    int restore_phase(const std::wstring& scenario){bool completed=false,rejected=false;View ra,rb,c;
        try{
            const auto session=decode_session(payload(store_/"session.fvs",Kind::session));
            if(session.executable!=executable_||session.client!=Client::kind())throw Error("snapshot.target_image",ERROR_REVISION_MISMATCH);
            if(!gone(session.manager)||!gone(session.source)||!gone(session.a)||!gone(session.b))throw Error("snapshot.previous_lifetime_alive",ERROR_BUSY);
            const auto current=manager();
            log_.emit("previous_manager_exited","\"oldPid\":"+std::to_string(session.manager.pid)+",\"oldCreated\":"+std::to_string(session.manager.created)+",\"newPid\":"+std::to_string(current.pid)+",\"newCreated\":"+std::to_string(current.created)+",\"oldSourcePid\":"+std::to_string(session.source.pid)+",\"oldAPid\":"+std::to_string(session.a.pid)+",\"oldBPid\":"+std::to_string(session.b.pid));
            const auto s=read_snapshot("S0",session,0),sa=read_snapshot("SA",session,1),sb=read_snapshot("SB",session,2);
            Artifacts artifacts(out_/"new-artifacts",actions_,[&](const char* t,const std::string& f){log_.emit(t,f);});
            auto artifact_a=open_artifact(session.artifacts[0],artifacts),artifact_b=open_artifact(session.artifacts[1],artifacts);
            this->build_view(ra,"RA",sa);
            if(scenario==L"query-capacity"){
                const auto until=GetTickCount64()+7000;while(GetTickCount64()<until)ra.child->poll(10);
                const auto& channel=ra.input->c();log_.emit("query_capacity_probe","\"count\":"+std::to_string(channel.query_count)+",\"capacity\":"+std::to_string(r304::query_capacity)+",\"health\":"+std::to_string(channel.health)+",\"bridgeAlive\":"+(WaitForSingleObject(ra.input->bridge.process.value,0)==WAIT_TIMEOUT?"true":"false"));
                if(channel.health!=1)throw Error(channel.error_api,static_cast<DWORD>(channel.error));
            }
            const auto before=this->observe(*ra.child,"before-RB");
            if(scenario==L"resource-failure")this->scenario_=L"resource-failure";
            try{this->build_view(rb,"RB",sb,ra.child.get());if(scenario==L"resource-failure")throw Error("expected_resource_rejection_missing",ERROR_INVALID_STATE);}
            catch(const Error& e){if(scenario!=L"resource-failure"||e.api!="resource.restore.parent_relation")throw;rejected=true;verify("RB.failure-not-published",!rb.child->published&&!std::filesystem::exists(out_/"workspace/RB"),"unsupported native resource rejects only RB");}
            this->stable(before,this->observe(*ra.child,"after-RB"),"RA.survives-RB");this->build_view(c,"C",s,ra.child.get());
            const auto ra_instance=record_instance(ra.child->p,"RA"),c_instance=record_instance(c.child->p,"C");
            const bool rb_new=!rb.child->published||different_instance(record_instance(rb.child->p,"RB"),session.b);
            verify("restored-instances-are-new",different_instance(ra_instance,session.a)&&different_instance(c_instance,session.source)&&rb_new,"native PID plus creation time distinguishes lifetimes, including numeric PID reuse");
            const auto original=this->resource(*c.child,3,"original-S0");verify("C.original-S0",!wcscmp(original.nodes[1].caption,s.resources.nodes[1].caption)&&!original.can_undo&&this->state(*c.child).document_reads==0,"disk S0 has neither later text, Undo nor file operations");
            const auto imports=artifacts.import_private("C",*artifact_a,*artifact_b,out_/"workspace/C");
            const auto receipt=this->file_action(c,client_process,out_/"workspace/C/processed.txt",imports/"A.txt",imports/"B.txt");
            verify("C.native-reads-fixed-artifacts",receipt.receipt.reads==2&&receipt.receipt.writes==3&&receipt.receipt.closes==5&&!receipt.receipt.live_handles&&std::string(receipt.receipt.read_bytes[0],receipt.receipt.read_size[0])==std::string(sa.file.content.begin(),sa.file.content.end())&&std::string(receipt.receipt.read_bytes[1],receipt.receipt.read_size[1])==std::string(sb.file.content.begin(),sb.file.content.end()),"real target ReadFile/WriteFile completes after disk-loaded execution restore");
            for(auto* view:{&ra,&rb})if(view->child&&view->child->published){const auto& snap=view==&ra?sa:sb;const auto fixed=view==&ra?artifact_a.get():artifact_b.get();
                verify(view->child->name+".file-objects-private",Files::distinct(out_/"workspace"/view->child->name/Client::file_name(),fixed->directory/"data.bin"),"restored workspace has independent native identity");
                this->verify_undo(*view,s,view==&ra?L"A":L"B");this->file_action(*view,client_save,out_/"workspace"/view->child->name/Client::file_name());
                verify(view->child->name+".new-file-write",Artifacts::digest(out_/"workspace"/view->child->name/Client::file_name())!=snap.file.sha256&&Artifacts::digest(fixed->directory/"data.bin")==snap.file.sha256,"new process changes only its private version");}
            this->select_prefix(*c.child,*c.input);this->edit(c,L'C',L"C","final-edit");this->alive(rb.child&&rb.child->published?std::vector<Child*>{ra.child.get(),rb.child.get(),c.child.get()}:std::vector<Child*>{ra.child.get(),c.child.get()},"restart-final");
            unsigned i=0;for(const auto* snap:{&s,&sa,&sb}){const auto label=i==0?"S0":i==1?"SA":"SB";verify(std::string(label)+".disk-immutable",hash(read(store_/(std::string(label)+".fvs")))==session.packets[i]&&this->fingerprint(*snap,std::string(label)+"-after-restart")==session.fingerprints[i],"loaded state and original packet stay fixed");++i;}
            for(auto* view:{&ra,&rb,&c})if(view->child&&view->child->published){this->observe(*view->child,"final");this->capture(*view->child,"final");}
            verify("Artifact.fixed-after-C",Artifacts::digest(artifact_a->directory/"data.bin")==sa.file.sha256&&Artifacts::digest(artifact_b->directory/"data.bin")==sb.file.sha256,"private processing and Undo do not change fixed versions");completed=true;
        }catch(const Error& e){log_.emit("experiment_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"message\":"+json(e.what()));}
        for(auto* v:{&c,&rb,&ra})try{dispose(*v);}catch(const std::exception& e){++failed_;log_.emit("cleanup_error","\"message\":"+json(e.what()));}
        log_.emit("restart_complete","\"phase\":\"restore\",\"passed\":"+std::string(completed&&!failed_?"true":"false")+",\"expectedRejectionDetected\":"+(rejected?"true":"false")+",\"managerPid\":"+std::to_string(GetCurrentProcessId()));return completed&&!failed_?0:2;
    }
};
}
