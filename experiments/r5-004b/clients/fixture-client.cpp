#include "../../r5-002/clients/fixture_model.hpp"
#include "../../r5-004a/clients/run.hpp"
#include "../faults.hpp"
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 4;}
extern "C" __declspec(dllexport) int RunCrashClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario,const wchar_t* gate,const wchar_t* previous){
    const auto label=r3::utf8(scenario);if(label.starts_with("relocation-"))r5004b::relocation_fault=label;
    return r5004a::run<FixtureClient>(m,a,o,mode,store,label.starts_with("relocation-")?L"positive":scenario,gate,previous);
}
