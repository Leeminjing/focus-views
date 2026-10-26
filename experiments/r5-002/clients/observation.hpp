#pragma once
#include "command.hpp"
#include "../runtime/execution.hpp"
namespace r305 {
// Read-only test DTO. Never copied into either target; it is not a memory ABI.
struct Observation {
    std::uint64_t seed=0,counter=0,receipt=0,returned=0,clicks=0;
    HWND window=nullptr;
    wchar_t text[128]{};unsigned length=0;
    LONG point_x=0,point_y=0;
    std::uint64_t document_reads=0,document_writes=0,processed_bytes=0;
    std::string details="{}";
};
inline std::string work_json(const Observation& w){return "{\"seed\":"+std::to_string(w.seed)+",\"counter\":"+std::to_string(w.counter)+",\"receipt\":"+std::to_string(w.receipt)+",\"returned\":"+std::to_string(w.returned)+",\"clicks\":"+std::to_string(w.clicks)+",\"window\":"+std::to_string(reinterpret_cast<std::uint64_t>(w.window))+",\"text\":"+json(w.text)+",\"length\":"+std::to_string(w.length)+",\"pointX\":"+std::to_string(w.point_x)+",\"pointY\":"+std::to_string(w.point_y)+",\"documentReads\":"+std::to_string(w.document_reads)+",\"documentWrites\":"+std::to_string(w.document_writes)+",\"processedBytes\":"+std::to_string(w.processed_bytes)+",\"application\":"+w.details+"}";}
inline std::uint64_t local_value(const Snapshot& s,unsigned index){std::uint64_t v=0;const auto offset=s.layout.frame[index].local-s.meta.stack-4096;demand(offset+8<=s.stack.size(),"oracle local bounds");memcpy(&v,s.stack.data()+offset,8);return v;}
template<class T>T raw_state(const Snapshot& s){demand(sizeof(T)<=s.memory.size(),"oracle data bounds");T value{};memcpy(&value,s.memory.data(),sizeof(value));return value;}
}
