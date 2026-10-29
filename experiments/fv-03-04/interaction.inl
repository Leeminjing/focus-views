// Included only in the opt-in experiment's Restart<Client> class.
void interaction_stage(View& ra,View& rb,View& c){
    using namespace fv_live;
    demand(!failed_,"all restore acceptance checks must pass before the live stage");
    std::array<View*,3> views{&ra,&rb,&c};
    for(auto* v:views)demand(v->child&&v->input&&v->child->published,"live stage requires three restored published Views");
    const auto suffix=std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    const auto feed_name=L"Local\\fv_live_feed_"+suffix;
    Mapping mapping(feed_name,sizeof(Feed),true);auto& feed=*new(mapping.view)Feed{};
    feed.manager_pid=GetCurrentProcessId();feed.manager_created=fv_live::creation(GetCurrentProcess());feed.heartbeat=GetTickCount64();
    struct Aux {std::unique_ptr<Mapping> mapping;Process process;Control* control=nullptr;std::string name;};
    std::vector<std::unique_ptr<Aux>> auxiliaries;
    auto pump=[&](const std::function<bool()>& done,const char* api,DWORD timeout=5000){
        const auto until=GetTickCount64()+timeout;
        while(!done()){if(GetTickCount64()>=until)throw Error(api,ERROR_TIMEOUT);feed.heartbeat=GetTickCount64();ra.child->poll(10);}
    };
    auto wait=[&](DWORD ms){const auto end=GetTickCount64()+ms;pump([&]{return GetTickCount64()>=end;},"live.interval",ms+1000);};
    auto start_aux=[&](bool host)->Aux&{
        auto item=std::make_unique<Aux>();item->name=(host?"host-":"preview-")+std::to_string(auxiliaries.size());
        const auto control_name=L"Local\\fv_live_control_"+suffix+L"_"+std::to_wstring(auxiliaries.size());
        item->mapping=std::make_unique<Mapping>(control_name,sizeof(Control),true);item->control=new(item->mapping->view)Control{};
        if(host)wcscpy_s(item->control->challenge,(L"FV-"+suffix).c_str());
        auto& aux=*item;auxiliaries.push_back(std::move(item));
        const auto desktop=user_object_name(GetProcessWindowStation())+L"\\"+thread_desktop();
        aux.process=launch(this->exe_.parent_path()/"auxiliary.exe",desktop,{L"--role",host?L"host":L"preview",L"--feed",feed_name,L"--control",control_name,L"--log",(out_/(aux.name+".jsonl")).wstring()},CREATE_SUSPENDED,log_,aux.name);
        this->assign(aux.process);check(ResumeThread(aux.process.thread.value)!=DWORD(-1),"ResumeThread.live_aux");
        pump([&]{if(WaitForSingleObject(aux.process.process.value,0)==WAIT_OBJECT_0)throw Error("live.aux_exited_before_ready",ERROR_PROCESS_ABORTED);return aux.control->ready!=0;},"live.aux_ready");
        verify(aux.name+".instance",aux.control->pid==aux.process.pid&&aux.control->created==fv_live::creation(aux.process.process.value),"auxiliary instance pinned by PID plus creation time");
        if(!host)verify(aux.name+".read-only-feed",aux.control->read_only==1,"preview maps the producer feed with FILE_MAP_READ, verified PAGE_READONLY");
        return aux;
    };
    auto stop_aux=[&](Aux& aux,bool crash){
        if(crash){check(TerminateProcess(aux.process.process.value,0xE0030401),"TerminateProcess.preview_test");}
        else{aux.control->operation=2;MemoryBarrier();InterlockedIncrement(&aux.control->request);}
        pump([&]{return WaitForSingleObject(aux.process.process.value,0)==WAIT_OBJECT_0;},"live.aux_exit");
        DWORD exit=0;check(GetExitCodeProcess(aux.process.process.value,&exit),"GetExitCodeProcess.live_aux");aux.process.exited=true;
        verify(aux.name+".exit",crash?exit==0xE0030401:exit==0&&aux.control->closed&&!aux.control->error,"explicit preview fault is independent from a normal, acknowledged window/process exit");
        log_.emit("live_aux_exit","\"name\":"+json(aux.name)+",\"pid\":"+std::to_string(aux.process.pid)+",\"created\":"+std::to_string(aux.control->created)+",\"forced\":"+(crash?"true":"false")+",\"exit\":"+std::to_string(exit));
        log_.emit("process_exited","\"name\":"+json(aux.name)+",\"pid\":"+std::to_string(aux.process.pid)+",\"exitCode\":"+std::to_string(exit));
        aux.process.process=Handle{};aux.process.thread=Handle{};
    };
    std::uint64_t sequences[3]{},cycles=0;
    auto frames=[&](const char* phase){
        for(unsigned i=0;i<views.size();++i){auto& v=*views[i]->child;Frame f{};f.sequence=++sequences[i];f.pid=v.p.pid;f.tid=v.p.tid;f.created=fv_live::creation(v.p.process.value);strcpy_s(f.view,v.name.c_str());
            const auto label="live-frame-"+std::to_string(f.sequence);f.begin=GetTickCount64();const auto captured=this->capture(v,label);f.end=GetTickCount64();f.counter=captured.print_update;
            const auto path=out_/v.name/(label+".png");wcscpy_s(f.path,path.c_str());strcpy_s(f.sha256,native::digest_text(native::digest(native::read_file(path))).c_str());
            InterlockedIncrement(&feed.epoch);feed.frames[i]=f;feed.heartbeat=GetTickCount64();MemoryBarrier();InterlockedIncrement(&feed.epoch);
            log_.emit("live_frame",frame_json(f)+",\"phase\":"+json(phase));
        }
    };
    auto host_sample=[&](const char* phase){
        POINT cursor{};check(GetCursorPos(&cursor),"GetCursorPos.live_host_sample");LASTINPUTINFO input{sizeof(input)};check(GetLastInputInfo(&input),"GetLastInputInfo.live_host_sample");
        const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);check(desktop!=nullptr,"OpenInputDesktop.live_host_sample");const auto name=user_object_name(desktop);check(CloseDesktop(desktop),"CloseDesktop.live_host_sample");
        log_.emit("live_host_sample","\"phase\":"+json(phase)+",\"foreground\":"+std::to_string(reinterpret_cast<std::uint64_t>(GetForegroundWindow()))+",\"cursorX\":"+std::to_string(cursor.x)+",\"cursorY\":"+std::to_string(cursor.y)+",\"lastInput\":"+std::to_string(input.dwTime)+",\"inputDesktop\":"+json(name.c_str())+",\"noWindowTitlesOrTypedText\":true");
    };
    auto cycle=[&](const char* phase){
        host_sample("before-action");
        const auto target=static_cast<unsigned>(cycles%3);std::array<typename Base::Observed,3> before{};
        for(unsigned i=0;i<3;++i)before[i]=this->observe(*views[i]->child,"live-before");
        auto& selected=*views[target];const auto ch=static_cast<wchar_t>(L'K'+cycles%12);wchar_t expected[2]{ch,0};
        const auto edit_phase="live-edit-"+std::to_string(cycles);
        this->select_prefix(*selected.child,*selected.input);this->edit(selected,ch,expected,edit_phase.c_str());
        if(cycles%2==0)this->file_action(selected,client_save,out_/"workspace"/selected.child->name/Client::file_name());
        for(unsigned i=0;i<3;++i)if(i!=target)this->stable(before[i],this->observe(*views[i]->child,"live-after"),views[i]->child->name+".isolated-live-"+std::to_string(cycles));
        wait(400);for(unsigned i=0;i<3;++i)verify(views[i]->child->name+".progress-"+std::to_string(cycles),this->state(*views[i]->child).counter>before[i].work.counter,"actual restored autonomous computation advances, even without a preview");
        this->alive({ra.child.get(),rb.child.get(),c.child.get()},phase);frames(phase);++cycles;
        log_.emit("live_cycle","\"cycle\":"+std::to_string(cycles)+",\"phase\":"+json(phase)+",\"target\":"+json(selected.child->name)+",\"processed\":true");host_sample("after-action");
    };
    auto select=[&](Aux& aux,unsigned index){
        aux.control->argument=static_cast<LONG>(index);aux.control->operation=1;MemoryBarrier();const auto request=InterlockedIncrement(&aux.control->request);
        pump([&]{if(aux.control->error)throw Error("live.preview_error",static_cast<DWORD>(aux.control->error));if(WaitForSingleObject(aux.process.process.value,0)!=WAIT_TIMEOUT)throw Error("live.preview_died",ERROR_PROCESS_ABORTED);return aux.control->applied==request&&aux.control->displayed[index]==sequences[index];},"live.preview_actual_paint");
        verify(aux.name+".fresh-"+std::to_string(index),aux.control->read_only&&std::string(aux.control->displayed_sha[index])==feed.frames[index].sha256&&aux.control->last_display_tick>=feed.frames[index].end,"actual consumer WM_PAINT acknowledges this exact fresh producer Frame and SHA256");
    };
    log_.emit("live_started","\"mode\":"+json(fv_live::mode.c_str())+",\"restoredViews\":3,\"previewOwnsViewLifecycle\":false,\"newSnapshotsAfterLiveEdits\":false");
    cycle("no-preview");
    if(fv_live::mode==L"live-manager-exit"){auto& p=start_aux(false);select(p,0);experimental::point("live-ready");throw Error("live.expected_manager_termination",ERROR_INVALID_STATE);}
    auto& first=start_aux(false);for(unsigned i=0;i<3;++i){cycle("preview-open");select(first,i);}stop_aux(first,false);
    cycle("preview-closed");cycle("preview-closed");
    auto& second=start_aux(false);select(second,0);stop_aux(second,true);cycle("preview-crashed");cycle("preview-crashed");
    auto& third=start_aux(false);select(third,0);cycle("preview-reopened");select(third,1);select(third,2);
    if(fv_live::mode==L"live-input-exit"){
        check(TerminateProcess(rb.input->bridge.process.value,0xE0030402),"TerminateProcess.input_test");pump([&]{return WaitForSingleObject(rb.input->bridge.process.value,0)==WAIT_OBJECT_0;},"live.input_bridge_exit");
        std::string first_api;DWORD first_error=0;
        try{this->keys(*rb.child,*rb.input,{{VK_END,true},{VK_END,false}});}catch(const Error& e){first_api=e.api;first_error=e.code;log_.emit("live_input_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"injectedBridgeExit\":true,\"notReinstalled\":true");}
        verify("live.input-exit-rejected",!first_api.empty(),"actual dead input bridge rejects the operation; no replacement bridge or host input fallback");
        demand(!first_api.empty(),"injected input failure must be detected");
        try{
            const auto before_c=this->state(*c.child).counter;
            this->post(*ra.child,WM_CHAR,L'Z');wait(150);this->capture(*c.child,"after-input-failure");
            verify("live.other-Views-survive-input-failure",this->state(*c.child).counter>before_c&&WaitForSingleObject(ra.child->p.process.value,0)==WAIT_TIMEOUT,"RA processes a real application character and C computes/paints after RB input failure");
            log_.emit("live_other_views_survived_input_failure","\"RAApplicationInputConfirmed\":true,\"CFreshFrameConfirmed\":true,\"failedBridgeRecreated\":false");
        }catch(const Error& e){log_.emit("live_secondary_error","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"primaryPreserved\":true");}
        try{stop_aux(third,false);}catch(const Error& e){log_.emit("live_secondary_error","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"primaryPreserved\":true");}
        throw Error(first_api,first_error);
    }
    std::string manual="not_executed";
    if(fv_live::mode==L"live-manual"){
        auto& host=start_aux(true);const auto end=GetTickCount64()+60000;while(GetTickCount64()<end){cycle("manual-host-window");select(third,static_cast<unsigned>(cycles%3));wait(500);if(cycles>=90)break;}
        stop_aux(host,false);const auto& h=*host.control;const bool passed=h.confirmed&&h.matched&&h.no_interference&&h.characters>0&&h.keys>0&&h.moves>0&&h.clicks>0&&h.confirmed_tick>=h.started+20000;
        manual=passed?"passed":h.confirmed?"not_verified":"not_executed";
        log_.emit("host_acceptance","\"result\":"+json(manual)+",\"explicitConfirmation\":"+std::string(h.confirmed?"true":"false")+",\"challengeMatched\":"+(h.matched?"true":"false")+",\"characters\":"+std::to_string(h.characters)+",\"moves\":"+std::to_string(h.moves)+",\"clicks\":"+std::to_string(h.clicks)+",\"scope\":\"dedicated test window only; no raw input text\"");
    }
    stop_aux(third,false);cycle("all-previews-exited");
    InterlockedIncrement(&feed.epoch);feed.valid=0;MemoryBarrier();InterlockedIncrement(&feed.epoch);
    log_.emit("live_complete","\"automaticPassed\":"+std::string(failed_?"false":"true")+",\"manualHostTest\":"+json(manual)+",\"cycles\":"+std::to_string(cycles)+",\"previewCloseAndCrashIndependent\":true,\"cleanupDeferredToExistingSupervisor\":true");
}
