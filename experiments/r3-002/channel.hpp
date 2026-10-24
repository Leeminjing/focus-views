#pragma once
#include "../r3/protocol.hpp"
#include <memory>

namespace r302 {
using namespace r3;
constexpr UINT envelope = WM_APP + 77;
constexpr std::uint64_t channel_magic = 0x3230303352564646;
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
// One producer (controller), bridge owns posting, one target GUI thread consumes.
// Packet storage is immutable until command_done acknowledges the whole batch.
struct Channel {
    std::uint64_t signature = channel_magic, token = 0;
    DWORD pid = 0, tid = 0;
    std::uint64_t edit = 0, top = 0;
    unsigned enabled = 0, peek_probe = 1, count = 0, command = 0;
    volatile LONG ready = 0, health = 0, request = 0, command_done = 0;
    volatile LONG error = 0, trace_count = 0;
    char error_api[96]{};
    volatile LONG64 consumed = 0;
    Packet packets[capacity]{};
    Trace traces[trace_capacity]{};
};
inline std::wstring channel_name(DWORD pid, DWORD tid) {
    return L"Local\\fv_r3002_" + std::to_wstring(pid) + L"_" + std::to_wstring(tid);
}
inline bool supported(unsigned vk) {
    return vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_HOME || vk == VK_END || (vk >= 'A' && vk <= 'Z');
}
inline void fault(Channel& c, DWORD error, const char* api = "runtime_validation") {
    strncpy_s(c.error_api, api, _TRUNCATE);
    InterlockedExchange(&c.error, static_cast<LONG>(error));
    InterlockedExchange(&c.health, -1);
}
int input_main(const Args& args);
}
