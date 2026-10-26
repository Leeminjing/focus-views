#pragma once
#include "../../r3-004/channel.hpp"
#include <climits>
// Diagnostic transport only. The keyboard/mouse Channel ABI and input semantics
// are unchanged. One GUI-thread producer, one resident-bridge consumer.
namespace r5003query {
using namespace r304;
constexpr std::uint64_t magic=0x3151525453564646;
struct Ring {
    std::uint64_t signature=magic,token=0;DWORD pid=0,tid=0;
    volatile LONG64 published=0,acknowledged=0;
    Query entries[query_capacity]{};
};
inline std::wstring name(const Channel& c){return L"Local\\fv_r5003_queries_"+std::to_wstring(c.pid)+L"_"+std::to_wstring(c.tid)+L"_"+std::to_wstring(c.token);}
inline std::uint64_t value(volatile LONG64& x){const auto n=InterlockedCompareExchange64(&x,0,0);if(n<0)throw Error("query_stream.negative_sequence",ERROR_INVALID_DATA);return static_cast<std::uint64_t>(n);}
class Consumer {
    Mapping mapping_;Ring& ring_;
public:
    explicit Consumer(const Channel& c):mapping_(name(c),sizeof(Ring),true),ring_(*new(mapping_.view)Ring{}){ring_.token=c.token;ring_.pid=c.pid;ring_.tid=c.tid;}
    unsigned count(){const auto n=value(ring_.published);if(n>LONG_MAX)throw Error("query_stream.sequence_limit",ERROR_BUFFER_OVERFLOW);MemoryBarrier();return static_cast<unsigned>(n);}
    Query at(unsigned index){const auto acknowledged=value(ring_.acknowledged),published=value(ring_.published);if(index<acknowledged||index>=published||published-acknowledged>query_capacity)throw Error("query_stream.read_bounds",ERROR_INVALID_DATA);MemoryBarrier();return ring_.entries[index%query_capacity];}
    void acknowledge(unsigned n){if(n<value(ring_.acknowledged)||n>value(ring_.published))throw Error("query_stream.ack_bounds",ERROR_INVALID_DATA);MemoryBarrier();InterlockedExchange64(&ring_.acknowledged,n);}
};
class Producer {
    Mapping mapping_;Ring& ring_;
public:
    explicit Producer(const Channel& c):mapping_(name(c),sizeof(Ring),false),ring_(*static_cast<Ring*>(mapping_.view)){
        if(ring_.signature!=magic||ring_.token!=c.token||ring_.pid!=c.pid||ring_.tid!=c.tid)throw Error("query_stream.identity",ERROR_INVALID_OWNER);
    }
    void write(Channel& c,const Query& q){
        if(GetCurrentThreadId()!=c.tid)throw Error("query_stream.producer_thread",ERROR_INVALID_THREAD_ID);
        const auto next=value(ring_.published),ack=value(ring_.acknowledged);
        if(next<ack||next-ack>=query_capacity||next>=LONG_MAX)throw Error("query_stream.backpressure",ERROR_BUFFER_OVERFLOW);
        ring_.entries[next%query_capacity]=q;MemoryBarrier();InterlockedExchange64(&ring_.published,static_cast<LONG64>(next+1));InterlockedExchange(&c.query_count,static_cast<LONG>(next+1));
    }
};
}
