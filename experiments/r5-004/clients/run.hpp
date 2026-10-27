#pragma once
#include "../crash.hpp"
#include "../../r5-003/clients/restart.hpp"
namespace r5004 {
template<class Client>int run(const wchar_t* m,const wchar_t* app,const wchar_t* output,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario,const wchar_t* gate,const wchar_t* previous){Log log(std::filesystem::path(output)/"recovery.jsonl");try{
    Gate synchronization(gate,output);
    if(wcscmp(mode,L"save")){if(!*previous)throw Error("recovery.owner_certificate_required",ERROR_INVALID_DATA);verify_previous(previous,store,log);if(!std::filesystem::exists(std::filesystem::path(store)/"session.fvs"))throw Error("recovery.session_uncommitted",ERROR_INVALID_STATE);}
    r5003::Restart<Client> experiment(m,app,output,store);if(!wcscmp(mode,L"save"))return experiment.save_phase();demand(!wcscmp(mode,L"restore"),"known crash experiment mode");return experiment.restore_phase(scenario);
}catch(const Error& e){log.emit("recovery_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"message\":"+json(e.what()));std::cerr<<e.what()<<'\n';return 2;}catch(const std::exception& e){log.emit("recovery_failure","\"api\":\"unexpected\",\"message\":"+json(e.what()));return 2;}}
}
