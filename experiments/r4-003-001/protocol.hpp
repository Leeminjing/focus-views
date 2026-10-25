#pragma once
#include "undo.hpp"
#include "../r3-004/channel.hpp"
#include <bit>
namespace r305 {
using namespace r3;
constexpr std::uint64_t signature = 0x6135303352564646;
constexpr std::uint64_t data_address = 0x000001B100000000;
constexpr std::uint64_t stack_address = data_address+0x10000;
constexpr size_t data_size = 4096, stack_size = 2*1024*1024;
constexpr DWORD cpu_flags = CONTEXT_CONTROL|CONTEXT_INTEGER|CONTEXT_FLOATING_POINT;
constexpr UINT freeze_message = WM_APP+110, checkpoint_message = WM_APP+111;
constexpr UINT prepare_stop_message = WM_APP+112;
constexpr UINT resource_message = WM_APP+113;
constexpr UINT capture_message = WM_APP+114;
constexpr unsigned frame_limit=2048;
struct NativeFrame {
    DWORD pid=0,tid=0;std::uint64_t token=0;
    volatile LONG done=0;DWORD error=0;char api[96]{};
    int width=0,height=0,client_dx=0,client_dy=0;
    std::uint64_t begin_ms=0,end_ms=0;
    BYTE pixels[frame_limit*frame_limit*4]{};
};
inline std::wstring frame_name(DWORD pid,DWORD tid){return L"Local\\fv_r3005c_frame_"+std::to_wstring(pid)+L"_"+std::to_wstring(tid);}
struct WindowNode {
    std::uint64_t hwnd=0;
    unsigned logical=0,parent=0;
    int id=0;
    DWORD style=0,ex_style=0,pid=0,tid=0;
    RECT rect{};
    wchar_t klass[64]{},caption[128]{};
};
struct Resources {
    UndoState undo{};
    WindowNode nodes[3]{};
    DWORD selection_start=0,selection_end=0;
    BOOL modified=FALSE,can_undo=FALSE;
    unsigned focus=0,descendants=0;
    LRESULT first_visible=0;
    POINT caret{};
    LONG caret_at_selection_end=0;
    DWORD query_tid=0;
    unsigned ime_active=0;
    LONG ime_bytes=0;
};
struct ResourceService {
    unsigned operation=0,sequence=0,done=0,fault=0;
    LRESULT native_result=0;
    DWORD diagnostic_error=0;
    DWORD error=0;
    char api[96]{};
    Resources payload{},observed{};
};
enum FileOperation : unsigned { save_document=1,process_documents=2 };
struct FileRequest {
    std::uint64_t action=0,done=0;
    unsigned operation=0;
    wchar_t path[3][512]{},evidence[512]{};
    DWORD error=0,pid=0,tid=0;
    char api[96]{};
    unsigned live_handles=0,reads=0,writes=0,flushes=0,closes=0;
    unsigned read_size[2]{},written_size=0;
    char read_bytes[2][2048]{},written_bytes[4096]{},sha256[65]{};
};
struct Work {
    std::uint64_t seed = 0, counter = 0, receipt = 0, returned = 0, clicks = 0;
    HWND window = nullptr;
    wchar_t text[128]{};
    unsigned length = 0;
    LONG point_x = 40, point_y = 40;
    HWND edit=nullptr,button=nullptr;
    std::uint64_t document_reads=0,document_writes=0,processed_bytes=0;
};
struct Reference { std::uint64_t address = 0, logical = 1; };
struct Metadata {
    std::uint64_t magic = signature;
    char build[65]{};
    std::uint64_t image = 0, ready = 0, init = 0, checkpoint = 0, continued = 0, rejected = 0;
    std::uint64_t window = 0, root = data_address, stack = stack_address, stack_bytes = stack_size;
    std::uint64_t cookie_address = 0, complement_address = 0;
    std::uint64_t original_stack_low = 0, original_stack_high = 0, active_stack_low = 0, active_stack_high = 0;
    std::uint64_t reported_stack_low = 0, reported_stack_high = 0;
    std::uint64_t local[3]{};
    std::uint64_t frame_entry[3]{};
    Reference refs[8]{};
    unsigned ref_count = 0, frozen = 0, checkpoint_requested = 0, published = 0, rejected_error = 0;
    unsigned in_dispatch = 0, paint_depth = 0;
    unsigned freeze_after_resume = 0;
    unsigned prepare_pumping = 0, prepare_stop = 0;
    DWORD window_style = 0, window_ex_style = 0, window_pid = 0, window_tid = 0;
    RECT window_rect{};
    std::uint64_t window_user_data = 0;
    wchar_t window_class[64]{};
    DWORD pid = 0, tid = 0, shadow_policy = 0;
    unsigned gui = 0, wrong_chain = 0;
    std::uint64_t alt_checkpoint = 0, changed = 0, rebuild_address = 0;
    std::uint64_t business_sites[3]{};
    DWORD cfg_policy = 0;
    std::uint64_t edit=0,button=0;
    Reference required_refs[8]{};
    unsigned required_count=0,input_closed=0,resources_ready=0,ime_active=0;
    unsigned omit_ref=0,unknown_child=0,bad_parent=0;
    ResourceService service{};
    FileRequest file{};
};
inline std::uint64_t receipt(std::uint64_t reg,std::uint64_t leaf,std::uint64_t middle,std::uint64_t outer) {
    return std::rotl((reg^leaf)+middle,13)^outer;
}
inline std::string work_json(const Work& w) {
    std::ostringstream s; s << "{\"seed\":" << w.seed << ",\"counter\":" << w.counter << ",\"receipt\":" << w.receipt << ",\"returned\":" << w.returned
      << ",\"clicks\":" << w.clicks << ",\"window\":" << reinterpret_cast<std::uint64_t>(w.window) << ",\"text\":" << json(w.text)
      << ",\"length\":" << w.length << ",\"pointX\":" << w.point_x << ",\"pointY\":" << w.point_y << ",\"documentReads\":" << w.document_reads << ",\"documentWrites\":" << w.document_writes << ",\"processedBytes\":" << w.processed_bytes << '}'; return s.str();
}
int fixture_main(const Args& args);
void application_files(Metadata&,Work&);
std::string file_effect_json(const FileRequest&);
void capture_owned_window(Metadata& metadata,HWND top);
void resource_service(Metadata& metadata,HWND top,HWND edit,HWND button);
std::string resources_json(const Resources& resources);
}
