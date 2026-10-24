#pragma once
#include "../r3/protocol.hpp"
#include <memory>
#include <algorithm>

namespace r303 {
using namespace r3;
constexpr UINT envelope = WM_APP + 77;
constexpr std::uint64_t channel_magic = 0x3330303352564646;
constexpr UINT control_message = WM_APP + 78;
enum Control : LPARAM { install_iat = 1, probe_api = 2, remove_iat = 3 };
constexpr unsigned capacity = 128, trace_capacity = 4096;
enum Kind : unsigned { key = 1, character, mouse, barrier, release };
struct Packet {
    std::uint64_t sequence = 0;
    unsigned kind = 0, vk = 0, down = 0, skip_state = 0;
    UINT message = 0;
    WPARAM wp = 0;
    LPARAM lp = 0;
};
struct Trace {
    std::uint64_t sequence = 0, tick = 0;
    DWORD pid = 0, tid = 0, error = 0;
    unsigned removed = 0, applied = 0, kind = 0, vk = 0, down = 0;
    SHORT ctrl = 0, shift = 0, async_ctrl = 0;
    BYTE keyboard_ctrl = 0, keyboard_shift = 0;
    BYTE left_ctrl = 0, right_ctrl = 0, left_shift = 0, right_shift = 0;
};
struct AsyncState {
    std::uint64_t version = 0, input_sequence = 0;
    BYTE down[256]{};
};
struct Probe {
    std::uint64_t id = 0;
    DWORD pid = 0, tid = 0;
    SHORT async_ctrl = 0, repeated_ctrl = 0, async_shift = 0;
    SHORT left_ctrl = 0, right_ctrl = 0, left_shift = 0, right_shift = 0, async_x = 0;
    SHORT queue_ctrl = 0, queue_shift = 0, native_dynamic_ctrl = 0;
    BYTE keyboard_ctrl = 0, keyboard_shift = 0;
    BOOL keyboard_ok = FALSE;
};
struct Query {
    std::uint64_t tick = 0, caller = 0, module = 0, sequence = 0, version = 0, probe = 0;
    DWORD pid = 0, tid = 0;
    int vk = 0;
    SHORT result = 0;
    unsigned supported = 0, valid = 0;
};
constexpr unsigned query_capacity = 8192;
// One producer (controller), bridge owns posting, one target GUI thread consumes.
// Packet storage is immutable until command_done acknowledges the whole batch.
struct Channel {
    std::uint64_t signature = channel_magic, token = 0;
    DWORD pid = 0, tid = 0;
    std::uint64_t edit = 0, top = 0;
    unsigned enabled = 0, peek_probe = 1, count = 0, command = 0;
    unsigned virtualize = 0, fail_install = 0, fail_restore = 0;
    unsigned delivery_count = 0;
    volatile LONG ready = 0, health = 0, request = 0, command_done = 0;
    volatile LONG error = 0, trace_count = 0, fault_claimed = 0;
    char error_api[96]{};
    volatile LONG64 consumed = 0;
    volatile LONG state_lock = 0, query_count = 0, iat_installed = 0, pinned = 0, target_gone = 0;
    std::uint64_t image = 0, image_size = 0, slot = 0, native = 0, replacement = 0, slot_after = 0, runtime_image = 0;
    DWORD page_before = 0, page_after = 0;
    unsigned initialized = 0, restored = 0;
    wchar_t module_path[512]{}, native_path[512]{}, runtime_path[512]{};
    AsyncState async_state{};
    Probe probe{};
    Packet packets[capacity]{};
    Packet delivery[capacity]{};
    Trace traces[trace_capacity]{};
    Query queries[query_capacity]{};
};
inline std::wstring channel_name(DWORD pid, DWORD tid) {
    return L"Local\\fv_r3003_" + std::to_wstring(pid) + L"_" + std::to_wstring(tid);
}
inline bool supported(unsigned vk) {
    return vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_HOME || vk == VK_END || (vk >= 'A' && vk <= 'Z');
}
inline bool async_supported(int vk) {
    return vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_LSHIFT || vk == VK_RSHIFT || vk == 'X';
}
inline AsyncState read_async(Channel& c) {
    for (unsigned i = 0; i < 10000; ++i) {
        const auto before = InterlockedCompareExchange(&c.state_lock, 0, 0);
        if (before & 1) continue;
        MemoryBarrier(); const auto result = c.async_state; MemoryBarrier();
        if (before == InterlockedCompareExchange(&c.state_lock, 0, 0)) return result;
    }
    throw Error("async_state.seqlock", ERROR_RETRY);
}
inline void fault(Channel& c, DWORD error, const char* api = "runtime_validation") {
    // Preserve the primary failure. Cleanup diagnostics are logged separately;
    // retiring a hook after target exit may itself return failure with code zero.
    if (InterlockedCompareExchange(&c.fault_claimed,1,0) != 0) return;
    strncpy_s(c.error_api, api, _TRUNCATE);
    InterlockedExchange(&c.error, static_cast<LONG>(error));
    InterlockedExchange(&c.health, -1);
}
int input_main(const Args& args);
}
