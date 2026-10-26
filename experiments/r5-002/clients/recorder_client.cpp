#include "recorder_model.hpp"
extern "C" __declspec(dllexport) unsigned ClientVersion(){return 1;}
extern "C" __declspec(dllexport) int RunClient(const wchar_t* m,const wchar_t* a,const wchar_t* o,const wchar_t* s){try{r305::Experiment<RecorderClient> e(m,a,o);return e.run(s);}catch(const std::exception& e){std::cerr<<"CLIENT_FAILURE "<<e.what()<<'\n';return 1;}}
