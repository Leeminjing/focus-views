#pragma once
#include "../../r5-001a/runtime/abi.hpp"
namespace r305 {
// Experiment client command ABI, not an application's managed business state.
enum ClientOperation : unsigned {client_save=1,client_process=2};
struct ClientCommand : FileCompletion {unsigned operation=0;wchar_t path[3][512]{},evidence[512]{};};
inline std::string file_effect_json(const ClientCommand& r){return "{\"action\":"+std::to_string(r.action)+",\"operation\":"+std::to_string(r.operation)+",\"pid\":"+std::to_string(r.pid)+",\"tid\":"+std::to_string(r.tid)+",\"error\":"+std::to_string(r.error)+",\"api\":"+json(r.api)+",\"liveHandles\":"+std::to_string(r.live_handles)+",\"reads\":"+std::to_string(r.reads)+",\"writes\":"+std::to_string(r.writes)+",\"flushes\":"+std::to_string(r.flushes)+",\"closes\":"+std::to_string(r.closes)+",\"readA\":"+json(r.read_bytes[0])+",\"readB\":"+json(r.read_bytes[1])+",\"written\":"+json(r.written_bytes)+",\"writtenSize\":"+std::to_string(r.written_size)+",\"sha256\":"+json(r.sha256)+"}";}
}
