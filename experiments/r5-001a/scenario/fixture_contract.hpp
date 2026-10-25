#pragma once
#include "../runtime/abi.hpp"
namespace r305 {
constexpr std::uint64_t data_address=0x000001B100000000,stack_address=data_address+0x10000;
constexpr size_t data_size=4096,stack_size=2*1024*1024;
enum FileOperation : unsigned {save_document=1,process_documents=2};
struct FileRequest : FileCompletion {unsigned operation=0;wchar_t path[3][512]{},evidence[512]{};};
static_assert(std::is_trivially_copyable_v<FileRequest>);
struct Work {
    std::uint64_t seed = 0, counter = 0, receipt = 0, returned = 0, clicks = 0;
    HWND window = nullptr;
    wchar_t text[128]{};
    unsigned length = 0;
    LONG point_x = 40, point_y = 40;
    HWND edit=nullptr,button=nullptr;
    std::uint64_t document_reads=0,document_writes=0,processed_bytes=0;
};
struct FixtureContract {std::uint64_t magic=0x31544E4346585646ULL;std::uint32_t version=1,work_bytes=sizeof(Work),mailbox_bytes=sizeof(FileRequest);};
#ifdef FV_TARGET_EXPORT
extern "C" {extern __declspec(dllexport) const FixtureContract R5FixtureContract;}
#else
extern "C" {extern const FixtureContract R5FixtureContract;}
#endif
inline std::string work_json(const Work& w) {
    std::ostringstream s; s << "{\"seed\":" << w.seed << ",\"counter\":" << w.counter << ",\"receipt\":" << w.receipt << ",\"returned\":" << w.returned
      << ",\"clicks\":" << w.clicks << ",\"window\":" << reinterpret_cast<std::uint64_t>(w.window) << ",\"text\":" << json(w.text)
      << ",\"length\":" << w.length << ",\"pointX\":" << w.point_x << ",\"pointY\":" << w.point_y << ",\"documentReads\":" << w.document_reads << ",\"documentWrites\":" << w.document_writes << ",\"processedBytes\":" << w.processed_bytes << '}'; return s.str();
}
int fixture_main(const Args&);
void application_files(Metadata&,Work&);
#ifdef FV_TARGET_EXPORT
extern "C" {extern __declspec(dllexport) FileRequest R5FixtureMailbox;}
#else
extern "C" {extern FileRequest R5FixtureMailbox;}
#endif
inline std::string file_effect_json(const FileRequest& r){
    return "{\"action\":"+std::to_string(r.action)+",\"operation\":"+std::to_string(r.operation)+",\"pid\":"+std::to_string(r.pid)+",\"tid\":"+std::to_string(r.tid)+",\"error\":"+std::to_string(r.error)+",\"api\":"+json(r.api)+",\"liveHandles\":"+std::to_string(r.live_handles)+",\"reads\":"+std::to_string(r.reads)+",\"writes\":"+std::to_string(r.writes)+",\"flushes\":"+std::to_string(r.flushes)+",\"closes\":"+std::to_string(r.closes)+",\"readA\":"+json(r.read_bytes[0])+",\"readB\":"+json(r.read_bytes[1])+",\"written\":"+json(r.written_bytes)+",\"writtenSize\":"+std::to_string(r.written_size)+",\"sha256\":"+json(r.sha256)+"}";
}
}
