#pragma once
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
struct Work {
    std::uint64_t seed = 0, counter = 0, receipt = 0, returned = 0, clicks = 0;
    HWND window = nullptr;
    wchar_t text[128]{};
    unsigned length = 0;
    LONG point_x = 40, point_y = 40;
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
};
inline std::uint64_t receipt(std::uint64_t reg,std::uint64_t leaf,std::uint64_t middle,std::uint64_t outer) {
    return std::rotl((reg^leaf)+middle,13)^outer;
}
inline std::string work_json(const Work& w) {
    std::ostringstream s; s << "{\"seed\":" << w.seed << ",\"counter\":" << w.counter << ",\"receipt\":" << w.receipt << ",\"returned\":" << w.returned
      << ",\"clicks\":" << w.clicks << ",\"window\":" << reinterpret_cast<std::uint64_t>(w.window) << ",\"text\":" << json(w.text)
      << ",\"length\":" << w.length << ",\"pointX\":" << w.point_x << ",\"pointY\":" << w.point_y << '}'; return s.str();
}
int fixture_main(const Args& args);
}
