#include "execution.hpp"
namespace r305 {
Execution::Execution(Log& log,const std::filesystem::path& file,const std::filesystem::path& output):log_(log),file_(file),out_(output),identity_(native::digest(native::read_file(file))){}
void Execution::verify_file(const std::filesystem::path& file,const std::array<BYTE,32>& expected){const auto actual=native::digest(native::read_file(file));log_.emit("application_image_identity","\"file\":"+json(file.wstring().c_str())+",\"expected\":"+json(native::digest_text(expected))+",\"actual\":"+json(native::digest_text(actual)));if(actual!=expected)throw Error("image.file_identity_mismatch",ERROR_REVISION_MISMATCH);}
void Execution::write_bytes(const std::filesystem::path& p,const void* bytes,size_t size){std::ofstream f(p,std::ios::binary);demand(f.good(),"runtime evidence file");f.write(static_cast<const char*>(bytes),static_cast<std::streamsize>(size));demand(f.good(),"runtime evidence write");}
void Execution::verify(const std::string& name,bool okay,const std::string& detail){log_.emit("assertion","\"name\":"+json(name)+",\"passed\":"+(okay?"true":"false")+",\"detail\":"+json(detail));if(!okay)throw Error("runtime.proof_failed",ERROR_INVALID_DATA,name);}
    std::uint64_t Execution::xstate(Child& v,const char* phase){
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
    std::vector<std::uint64_t> Execution::chain(Child& v,const char* phase){
        auto cpu=v.context();const auto ssp=xstate(v,phase);demand(ssp==cpu.R11,"RDSSPQ and kernel CET_U agree");
        const auto image=capture_image(v.p.process.value,v.image,file_);ImageMirror mirror(image);
        log_.emit("target_image_unwind","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"targetBase\":"+std::to_string(v.image)+",\"managerBase\":"+std::to_string(reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)))+",\"functionCount\":"+std::to_string(mirror.count())+",\"mirrorExecutable\":false,\"usesManagerFunctionTable\":false");
        auto copy=VirtualAlloc(reinterpret_cast<void*>(v.m.stack),v.m.stack_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(copy!=nullptr,"VirtualAlloc.declared_stack_mirror");
        struct Release{void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release{copy};
        native::read_memory(v.p.process.value,v.m.stack+4096,static_cast<BYTE*>(copy)+4096,v.m.stack_bytes-4096);
        std::vector<std::uint64_t> returns(v.layout.returns);
        for(unsigned i=0;i<returns.size();++i){
            const auto rsp=cpu.Rsp;demand(rsp>=v.m.stack+4096&&rsp+8<=v.m.stack+v.m.stack_bytes,"stack mirror bounds");mirror.step(cpu);
            const auto shadow=native::read_value<std::uint64_t>(v.p.process.value,ssp+8*i);returns[i]=cpu.Rip;
            log_.emit("call_chain","\"view\":"+json(v.name)+",\"phase\":"+json(phase)+",\"index\":"+std::to_string(i)+",\"frameRsp\":"+std::to_string(rsp)+",\"returnSlot\":"+std::to_string(cpu.Rsp-8)+",\"normalReturn\":"+std::to_string(cpu.Rip)+",\"shadowAddress\":"+std::to_string(ssp+8*i)+",\"shadowReturn\":"+std::to_string(shadow));
            demand(cpu.Rip==shadow,"target normal and protected returns match");
        }return returns;
    }
    void Execution::unwind(const Snapshot& s){
        ImageMirror mirror(s.image);auto buffer=VirtualAlloc(reinterpret_cast<void*>(s.meta.stack),s.meta.stack_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(buffer!=nullptr,"VirtualAlloc.captured_stack_mirror");
        struct Release{void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release{buffer};
        memcpy(static_cast<BYTE*>(buffer)+4096,s.stack.data(),s.stack.size());auto cpu=s.cpu;unsigned found=0;
        // Trap (leaf), then each declared C++ frame in innermost-first order.
        // Matching an unordered set would accept missing/duplicate/wrong frames.
        for(unsigned i=0;i<=s.layout.frames;++i){
            demand(cpu.Rsp>=s.meta.stack+4096&&cpu.Rsp+8<=s.meta.stack+s.meta.stack_bytes,"unwind stack bounds");
            const auto actual_rip=cpu.Rip;const auto entry=mirror.lookup(actual_rip);const auto start=cpu.Rsp;
            const auto expected=i?mirror.lookup(s.layout.frame[i-1].entry):nullptr;
            if(i&&(!entry||!expected||mirror.primary(entry)!=mirror.primary(expected)))throw Error("unwind.declared_frame_mismatch",ERROR_INVALID_DATA);
            mirror.step(cpu);
            if(i){const auto local=s.layout.frame[i-1].local;
                if(local<start||local+8>cpu.Rsp-8||local%8)throw Error("unwind.local_not_in_frame",ERROR_INVALID_DATA);
                ++found;
            }
            log_.emit("native_unwind_frame","\"index\":"+std::to_string(i)+",\"rip\":"+std::to_string(actual_rip)+",\"declaredEntry\":"+std::to_string(i?s.layout.frame[i-1].entry:0)+",\"rsp\":"+std::to_string(start)+",\"callerRsp\":"+std::to_string(cpu.Rsp)+",\"declaredFrames\":"+std::to_string(s.layout.frames)+",\"functionRva\":"+std::to_string(entry?entry->BeginAddress:0));
        }
        if(s.meta.abi_version==2){const auto actual=mirror.lookup(cpu.Rip);const auto caller=mirror.lookup(s.layout.caller_entry);
            if(!actual||!caller||mirror.primary(actual)!=mirror.primary(caller))throw Error("unwind.declared_depth_mismatch",ERROR_INVALID_DATA);
        }
        verify("source.native-unwind-sees-declared-CPP-frames",found==s.layout.frames,"ordered target PE unwind and per-frame local bounds verified");
    }
    Snapshot Execution::save(Child& v,const std::string& label){v.wait("checkpoint");log_.emit("raw_capture","\"label\":"+json(label)+",\"view\":"+json(v.name));Snapshot s;s.meta=v.metadata();s.layout=v.layout;s.cpu=v.context();s.cpu.Rip=s.meta.checkpoint+1;s.executable=identity_;s.image=capture_image(v.p.process.value,v.image,file_);
        demand(s.meta.frozen && !s.meta.in_dispatch && !s.meta.paint_depth && s.cpu.Rsp>=s.meta.stack+4096 && s.cpu.Rsp<s.meta.stack+s.meta.stack_bytes,"snapshot safe point bounds");
        s.memory.resize(s.meta.data_bytes);native::read_memory(v.p.process.value,s.meta.root,s.memory.data(),s.memory.size());
        s.stack.resize(s.meta.stack_bytes-4096);native::read_memory(v.p.process.value,s.meta.stack+4096,s.stack.data(),s.stack.size());
        s.cookie=native::read_value<std::uint64_t>(v.p.process.value,s.meta.cookie_address);s.complement=native::read_value<std::uint64_t>(v.p.process.value,s.meta.complement_address);
        s.returns=chain(v,"source-checkpoint");unwind(s);return s;
    }
    void Execution::restore(Child& v,const Snapshot& s,const std::string& fault){
        demand(v.pending && v.halt=="checkpoint" && !v.m.published,"restore private structurally rebuilt View only");
        if(strcmp(v.m.build,s.meta.build) || s.executable!=identity_ || v.image!=s.meta.image)throw Error("restore.build_or_image_identity",ERROR_REVISION_MISMATCH);
        const auto boot_cpu=v.context();const auto rebuilt=chain(v,"target-before-restore");
        Snapshot target;target.meta=v.m;target.layout=v.layout;target.cpu=boot_cpu;target.cpu.Rip=v.m.checkpoint+1;target.image=capture_image(v.p.process.value,v.image,file_);target.stack.resize(v.m.stack_bytes-4096);native::read_memory(v.p.process.value,v.m.stack+4096,target.stack.data(),target.stack.size());unwind(target);
        const bool matched=rebuilt==s.returns && boot_cpu.Rsp==s.cpu.Rsp && reinterpret_cast<std::uint64_t>(v.event.u.Exception.ExceptionRecord.ExceptionAddress)==s.meta.checkpoint && same_structure(v.layout,s.layout);
        log_.emit("chain_match","\"view\":"+json(v.name)+",\"matched\":"+std::string(matched?"true":"false")+",\"sourceSsp\":"+std::to_string(s.cpu.R11)+",\"targetSsp\":"+std::to_string(boot_cpu.R11)+",\"sourceRsp\":"+std::to_string(s.cpu.Rsp)+",\"targetRsp\":"+std::to_string(boot_cpu.Rsp)+",\"initializations\":"+std::to_string(v.initialized)+",\"businessPrefixes\":"+std::to_string(v.prefix_count()));
        if(!matched)throw Error("restore.call_chain_mismatch",ERROR_INVALID_DATA);
        demand(v.initialized==0 && v.prefix_count()==0,"no source business replay");
        std::vector<BYTE> before_data(s.meta.data_bytes);native::read_memory(v.p.process.value,s.meta.root,before_data.data(),s.meta.data_bytes);
        std::vector<BYTE> before_stack(s.stack.size());native::read_memory(v.p.process.value,s.meta.stack+4096,before_stack.data(),before_stack.size());
        write_bytes(out_/v.name/"target-before-restore-data.bin",before_data.data(),before_data.size());
        const auto active=static_cast<size_t>(boot_cpu.Rsp-s.meta.stack-4096);write_bytes(out_/v.name/"target-before-restore-stack.bin",before_stack.data()+active,before_stack.size()-active);write_bytes(out_/v.name/"target-before-restore-context.bin",&boot_cpu,sizeof(boot_cpu));
        if(v.m.gui){
        demand(v.m.window_pid==v.p.pid && v.m.window_tid==v.p.tid && v.m.window_user_data==s.meta.root &&
            !wcscmp(v.m.window_class,s.meta.window_class) && v.m.window_style==s.meta.window_style && v.m.window_ex_style==s.meta.window_ex_style &&
            !memcmp(&v.m.window_rect,&s.meta.window_rect,sizeof(RECT)),"native window descriptor/ownership matches supported fixed resource");
        log_.emit("window_resource_verified","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.m.window_pid)+",\"tid\":"+std::to_string(v.m.window_tid)+",\"class\":"+json(v.m.window_class)+",\"style\":"+std::to_string(v.m.window_style)+",\"exStyle\":"+std::to_string(v.m.window_ex_style)+",\"userData\":"+std::to_string(v.m.window_user_data)+",\"left\":"+std::to_string(v.m.window_rect.left)+",\"top\":"+std::to_string(v.m.window_rect.top)+",\"right\":"+std::to_string(v.m.window_rect.right)+",\"bottom\":"+std::to_string(v.m.window_rect.bottom));
        demand(v.m.active_stack_low==s.meta.active_stack_low && v.m.active_stack_high==s.meta.active_stack_high,"stack bounds match");
        if((v.m.shadow_policy&1) && boot_cpu.R11 && s.cpu.R11){
            const auto target_return=native::read_value<std::uint64_t>(v.p.process.value,boot_cpu.R11);
            const auto expected_return=*reinterpret_cast<const std::uint64_t*>(s.stack.data()+static_cast<size_t>(s.cpu.Rsp-s.meta.stack-4096));
            log_.emit("shadow_stack_diagnostic","\"view\":"+json(v.name)+",\"sourceSsp\":"+std::to_string(s.cpu.R11)+",\"targetBootSsp\":"+std::to_string(boot_cpu.R11)+",\"targetShadowReturn\":"+std::to_string(target_return)+",\"savedNormalReturn\":"+std::to_string(expected_return)+",\"policy\":"+std::to_string(v.m.shadow_policy));
        }
        }
        log_.emit("reference_registry","\"view\":"+json(v.name)+",\"sourceTracked\":"+std::to_string(s.meta.ref_count)+",\"sourceRequired\":"+std::to_string(s.meta.required_count)+",\"targetTracked\":"+std::to_string(v.m.ref_count)+",\"targetRequired\":"+std::to_string(v.m.required_count));
        write_bytes(out_/v.name/"target-metadata-before-restore.bin",&v.m,sizeof(v.m));
        if(s.meta.ref_count==0 || s.meta.ref_count>8 || s.meta.ref_count!=s.meta.required_count || v.m.ref_count!=s.meta.ref_count || v.m.required_count!=s.meta.required_count || memcmp(s.meta.refs,s.meta.required_refs,s.meta.ref_count*sizeof(Reference)) || memcmp(v.m.refs,v.m.required_refs,s.meta.ref_count*sizeof(Reference)) || memcmp(s.meta.refs,v.m.refs,s.meta.ref_count*sizeof(Reference)))throw Error("restore.unregistered_live_HWND",ERROR_INVALID_DATA);
        auto old_handle=[&](std::uint64_t logical){return logical==1?s.meta.window:logical==2?s.meta.edit:logical==3?s.meta.button:0;};
        auto new_handle=[&](std::uint64_t logical){return logical==1?v.m.window:logical==2?v.m.edit:logical==3?v.m.button:0;};
        auto refs=std::vector<Reference>(s.meta.refs,s.meta.refs+s.meta.ref_count);
        for(auto ref:refs){const bool data=ref.address>=s.meta.root && ref.address+sizeof(HWND)<=s.meta.root+s.meta.data_bytes;const bool stack=ref.address>=s.cpu.Rsp && ref.address+sizeof(HWND)<=s.meta.stack+s.meta.stack_bytes;
            if(!old_handle(ref.logical) || !(data||stack))throw Error("restore.unresolved_reference",ERROR_INVALID_DATA);
            const auto offset=static_cast<size_t>(ref.address-(data?s.meta.root:s.meta.stack+4096));std::uint64_t old=0;memcpy(&old,(data?s.memory:s.stack).data()+offset,8);if(old!=old_handle(ref.logical))throw Error("restore.reference_preimage",ERROR_INVALID_DATA);
        }
        if(v.m.gui)demand(v.m.window!=s.meta.window && v.m.window!=0,"new native window identity");
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        experimental::source_writes_started.insert(v.p.pid);
        log_.emit("restore_source_write_begin","\"view\":"+json(v.name)+",\"pid\":"+std::to_string(v.p.pid)+",\"cleanupMayDispatch\":false");
#endif
        native::write_memory(v.p.process.value,s.meta.root,s.memory.data(),s.memory.size());native::write_memory(v.p.process.value,s.meta.stack+4096,s.stack.data(),s.stack.size());
        for(auto ref:refs){const auto handle=new_handle(ref.logical);native::write_memory(v.p.process.value,ref.address,&handle,8);log_.emit("resource_rebind","\"view\":"+json(v.name)+",\"logicalWindow\":"+std::to_string(ref.logical)+",\"address\":"+std::to_string(ref.address)+",\"oldHwnd\":"+std::to_string(old_handle(ref.logical))+",\"newHwnd\":"+std::to_string(handle));}
        if(fault!="cookie"){native::write_memory(v.p.process.value,v.m.cookie_address,&s.cookie,8);native::write_memory(v.p.process.value,v.m.complement_address,&s.complement,8);}
        auto memory=s.memory;for(const auto& ref:refs)if(ref.address>=s.meta.root&&ref.address<s.meta.root+s.meta.data_bytes){const auto handle=new_handle(ref.logical);memcpy(memory.data()+static_cast<size_t>(ref.address-s.meta.root),&handle,8);}std::vector<std::uint8_t> readback(s.meta.data_bytes);native::read_memory(v.p.process.value,s.meta.root,readback.data(),s.meta.data_bytes);
        verify(v.name+".raw-memory-restored",readback==memory,"entire data page read back equals S0 except declared HWND relocation");
        auto expected_stack=s.stack;for(auto ref:refs)if(ref.address>=s.meta.stack+4096){const auto handle=new_handle(ref.logical);memcpy(expected_stack.data()+static_cast<size_t>(ref.address-s.meta.stack-4096),&handle,8);}
        std::vector<std::uint8_t> stack_readback(s.stack.size());native::read_memory(v.p.process.value,s.meta.stack+4096,stack_readback.data(),stack_readback.size());
        verify(v.name+".raw-stack-restored",stack_readback==expected_stack,"entire managed stack read back equals S0 except explicitly registered live HWND references");
        if(fault.empty()){write_bytes(out_/v.name/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-s.meta.stack-4096);write_bytes(out_/v.name/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);}
        if(fault=="stack"){const auto offset=static_cast<size_t>(s.layout.frame[0].local-s.meta.stack-4096);native::write_memory(v.p.process.value,s.layout.frame[0].local,before_stack.data()+offset,8);}
        if(fault=="stack-last"){const auto address=s.layout.frame[s.layout.frames-1].local;const auto offset=static_cast<size_t>(address-s.meta.stack-4096);native::write_memory(v.p.process.value,address,before_stack.data()+offset,8);}
        if(fault=="memory")native::write_memory(v.p.process.value,s.meta.root,before_data.data(),8);
        auto cpu=s.cpu;cpu.R11=boot_cpu.R11;if(fault=="register")cpu.R10=boot_cpu.R10;
        if(fault=="context"){Handle denied(OpenThread(THREAD_GET_CONTEXT,FALSE,v.p.tid));check(denied.value!=nullptr,"OpenThread.without_SET_CONTEXT");if(!SetThreadContext(denied.value,&cpu))throw Error("SetThreadContext.restore.denied",GetLastError());throw Error("context_fault_not_rejected",ERROR_INVALID_STATE);}
        check(SetThreadContext(v.p.thread.value,&cpu),"SetThreadContext.restore");
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
        if(experimental::reject_after_context){experimental::reject_after_context=false;log_.emit("after_context_fault","\"view\":"+json(v.name)+",\"sourceCpuWritten\":true,\"deliberateExperimentFault\":true");throw Error("cleanup.injected_after_context",ERROR_OPERATION_ABORTED);}
#endif
        const LONG rebuilt_done=0;native::write_memory(v.p.process.value,v.m.rebuild_address,&rebuilt_done,sizeof(rebuilt_done));
        chain(v,"after-ordinary-context-restore");
        const auto got=v.context();verify(v.name+".CPU-context-readback",got.Rip==cpu.Rip && got.Rsp==cpu.Rsp && got.R10==cpu.R10 && got.MxCsr==cpu.MxCsr,"stopped target RIP/RSP/register and floating point control read back after SetThreadContext");
        write_bytes(out_/v.name/"target-context-before-continue.bin",&got,sizeof(got));
        native::read_memory(v.p.process.value,s.meta.root,readback.data(),readback.size());native::read_memory(v.p.process.value,s.meta.stack+4096,stack_readback.data(),stack_readback.size());
        write_bytes(out_/v.name/"target-data-before-continue.bin",readback.data(),readback.size());const auto offset=static_cast<size_t>(s.cpu.Rsp-s.meta.stack-4096);write_bytes(out_/v.name/"target-active-stack-before-continue.bin",stack_readback.data()+offset,stack_readback.size()-offset);
        log_.emit("restored_context","\"view\":"+json(v.name)+",\"rip\":"+std::to_string(got.Rip)+",\"rsp\":"+std::to_string(got.Rsp)+",\"r10\":"+std::to_string(got.R10)+",\"published\":false");
        v.continue_event();v.wait("continued");xstate(v,"after-CPP-returns");
        verify(v.name+".declared-continuation-reached",v.initialized==0&&v.checkpoints==1&&v.prefix_count()==0,"new target reaches declared continuation with no initialization/prefix replay");
    }
}
