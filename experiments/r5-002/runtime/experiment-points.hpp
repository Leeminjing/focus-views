#pragma once
#include <filesystem>
// Opt-in experiment instrumentation. No callbacks or changes to target ABI.
namespace r305::experimental {
using Observer=void(*)(void*,const char*,const std::filesystem::path&);
inline thread_local Observer observer=nullptr;
inline thread_local void* observer_state=nullptr;
inline void point(const char* stage,const std::filesystem::path& identity={}){
    if(observer)observer(observer_state,stage,identity);
}
}
