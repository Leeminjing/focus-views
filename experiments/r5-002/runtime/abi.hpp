#pragma once
#include "../../r5-001a/runtime/abi.hpp"
namespace r305 {
// The v1 prefix is frozen. v2 adds bounded execution declarations only;
// GUI/resource ABI and the app's managed data remain separate.
constexpr unsigned execution_capacity=8;
struct ExecutionFrame { std::uint64_t entry=0,local=0,business=0; };
struct ExecutionLayout {
    unsigned version=1,bytes=sizeof(ExecutionLayout),frames=0,returns=0;
    std::uint64_t caller_entry=0;
    ExecutionFrame frame[execution_capacity]{};
};
struct MetadataV2 { Metadata base{}; ExecutionLayout execution{}; };
static_assert(offsetof(MetadataV2,execution)==sizeof(Metadata));
inline bool same_structure(const ExecutionLayout& a,const ExecutionLayout& b){
    if(a.version!=b.version||a.bytes!=b.bytes||a.frames!=b.frames||a.returns!=b.returns||a.caller_entry!=b.caller_entry)return false;
    for(unsigned i=0;i<a.frames;++i)if(a.frame[i].entry!=b.frame[i].entry||a.frame[i].local!=b.frame[i].local||a.frame[i].business!=b.frame[i].business)return false;
    return true;
}
}
