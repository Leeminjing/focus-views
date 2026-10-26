#include "../../r5-002/clients/recorder_model.hpp"
#include "restart.hpp"
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 2;}
extern "C" __declspec(dllexport) int RunRestartClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* mode,const wchar_t* store,const wchar_t* scenario){try{r5003::Restart<RecorderClient> e(m,a,o,store);if(!wcscmp(mode,L"save"))return e.save_phase();r3::demand(!wcscmp(mode,L"restore"),"known restart phase");return e.restore_phase(scenario);}catch(const std::exception& e){std::cerr<<"CLIENT_FAILURE "<<e.what()<<'\n';return 1;}}
