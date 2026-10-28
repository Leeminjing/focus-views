#include "../r5-002/clients/fixture_model.hpp"
#include "run.hpp"
#include "../r5-004b/faults.hpp"
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 4;}
extern "C" __declspec(dllexport) int RunCrashClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario,const wchar_t* gate,const wchar_t* previous){
    std::wstring selected(scenario);r5004a::synthetic_block=selected.starts_with(L"blocked:");if(r5004a::synthetic_block)selected.erase(0,8);r5004a::inline_control=selected.starts_with(L"inline:");
    if(selected.starts_with(L"inline:"))selected.erase(0,7);else if(selected.starts_with(L"deferred:"))selected.erase(0,9);
    const auto label=r3::utf8(selected);if(label.starts_with("relocation-"))r5004b::relocation_fault=label;
    return r5004a::run<FixtureClient>(m,a,o,mode,store,label.starts_with("relocation-")?L"positive":selected.c_str(),gate,previous);
}
