#pragma once
#include <filesystem>
#include <set>
namespace r305 {class Child;}
// Opt-in experiment instrumentation. No callbacks or changes to target ABI.
namespace r305::experimental {
using Observer=void(*)(void*,const char*,const std::filesystem::path&);
inline thread_local Observer observer=nullptr;
inline thread_local void* observer_state=nullptr;
inline thread_local std::wstring generation;
#ifdef FOCUS_VIEWS_STOPPOINT_EXPERIMENT
inline thread_local bool reject_image=false;
inline thread_local bool detach_cleanup=false;
inline thread_local bool suspend_cleanup=false;
inline thread_local bool reject_after_context=false;
inline thread_local std::set<unsigned long> source_writes_started;
using PrepareCleanup=void(*)(void*,Child&,void*);
using StopCleanup=void(*)(void*,Child&);
inline thread_local void* cleanup_state=nullptr;
inline thread_local PrepareCleanup prepare_cleanup=nullptr;
inline thread_local StopCleanup stop_cleanup=nullptr,release_cleanup=nullptr;
#endif
inline void point(const char* stage,const std::filesystem::path& identity={}){
    if(observer)observer(observer_state,stage,identity);
}
}
