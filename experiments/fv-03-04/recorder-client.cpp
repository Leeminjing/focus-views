#include "../r5-002/clients/recorder_model.hpp"
#include "../r5-004c/run.hpp"
#include "../r5-004b/faults.hpp"
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 4;}
extern "C" __declspec(dllexport) int RunCrashClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario,const wchar_t* gate,const wchar_t* previous){
    fv_live::set_mode(scenario);return r5004a::run<RecorderClient>(m,a,o,mode,store,L"positive",gate,previous);
}
