#pragma once
#include "../../r5-004/crash.hpp"
#include "../../r5-003/clients/restart.hpp"
#include "abort-host.hpp"
namespace r5004a {
using namespace r5004;
struct RejectCheckpoint {
    experimental::Observer before=experimental::observer;void* state=experimental::observer_state;Log& log;bool reject;bool image;std::string target;
    RejectCheckpoint(Log& value,bool enabled,bool guard,bool second=false):log(value),reject(enabled),image(guard),target(second?"RB":"RA"){experimental::observer_state=this;experimental::observer=[](void* p,const char* stage,const std::filesystem::path& identity){auto& self=*static_cast<RejectCheckpoint*>(p);if((self.reject||self.image)&&!strcmp(stage,"checkpoint-before-restore")&&identity==self.target){self.log.emit("deterministic_checkpoint_rejection","\"view\":"+json(self.target)+",\"stage\":\"before-source-CPU-write\",\"snapshotCpuApplied\":false");if(self.image){experimental::reject_image=true;return;}throw Error("restore.build_or_image_identity",ERROR_REVISION_MISMATCH);}if(self.before)self.before(self.state,stage,identity);};}
    ~RejectCheckpoint(){experimental::observer=before;experimental::observer_state=state;}
};
template<class Client>int run(const wchar_t* m,const wchar_t* app,const wchar_t* output,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario,const wchar_t* gate,const wchar_t* previous){Log log(std::filesystem::path(output)/"recovery.jsonl");try{
    Gate synchronization(gate,output);
    experimental::reject_after_context=!wcscmp(scenario,L"checkpoint-postwrite-reject");
    experimental::detach_cleanup=!wcscmp(scenario,L"checkpoint-detach-reject");
    experimental::suspend_cleanup=!wcscmp(scenario,L"checkpoint-suspend-reject");
    RejectCheckpoint failure(log,!wcscmp(scenario,L"checkpoint-reject"),!wcscmp(scenario,L"checkpoint-image-reject")||!wcscmp(scenario,L"checkpoint-detach-reject")||!wcscmp(scenario,L"checkpoint-abort-reject")||!wcscmp(scenario,L"checkpoint-abort-RB-reject")||experimental::suspend_cleanup,!wcscmp(scenario,L"checkpoint-abort-RB-reject"));
    const bool native_abort=wcscmp(scenario,L"checkpoint-image-reject")&&wcscmp(scenario,L"checkpoint-reject")&&!experimental::detach_cleanup&&!experimental::suspend_cleanup;
    const auto abort_fault=!wcscmp(scenario,L"abort-wrong-thread")?AbortFault::wrong_thread:!wcscmp(scenario,L"abort-mapping-missing")?AbortFault::missing_mapping:AbortFault::none;
    AbortRegistry cleanup(std::filesystem::path(m).parent_path(),output,log,native_abort&&wcscmp(mode,L"save"),abort_fault);
    if(wcscmp(mode,L"save")){if(!*previous)throw Error("recovery.owner_certificate_required",ERROR_INVALID_DATA);verify_previous(previous,store,log);if(!std::filesystem::exists(std::filesystem::path(store)/"session.fvs"))throw Error("recovery.session_uncommitted",ERROR_INVALID_STATE);}
    r5003::Restart<Client> experiment(m,app,output,store);if(!wcscmp(mode,L"save"))return experiment.save_phase();demand(!wcscmp(mode,L"restore"),"known crash experiment mode");return experiment.restore_phase(scenario);
}catch(const Error& e){log.emit("recovery_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"message\":"+json(e.what()));std::cerr<<e.what()<<'\n';return 2;}catch(const std::exception& e){log.emit("recovery_failure","\"api\":\"unexpected\",\"message\":"+json(e.what()));return 2;}}
}
