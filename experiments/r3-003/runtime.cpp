#include "channel.hpp"
#include "iat.hpp"
#include <intrin.h>

namespace {
using namespace r303;
std::unique_ptr<Mapping> mapping;
Channel* channel = nullptr;
BYTE held[256]{}, owned[256]{};
BYTE delivered[256]{};
bool probing = false;
std::unique_ptr<IatPatch> patch;
void connect() {
    if (!mapping) {
        mapping = std::make_unique<Mapping>(channel_name(GetCurrentProcessId(),GetCurrentThreadId()),sizeof(Channel),false);
        channel = static_cast<Channel*>(mapping->view);
    }
    demand(channel->signature == channel_magic && channel->pid == GetCurrentProcessId() && channel->tid == GetCurrentThreadId(),"runtime channel identity");
}

void record(const Packet& p, bool removed, bool applied, DWORD error) {
    const auto index = channel->trace_count;
    if (index >= static_cast<LONG>(trace_capacity)) { fault(*channel, ERROR_BUFFER_OVERFLOW); return; }
    Trace t{};
    t.sequence = p.sequence; t.tick = GetTickCount64(); t.pid = GetCurrentProcessId(); t.tid = GetCurrentThreadId();
    t.kind = p.kind; t.vk = p.vk; t.down = p.down; t.removed = removed; t.applied = applied; t.error = error;
    t.ctrl = GetKeyState(VK_CONTROL); t.shift = GetKeyState(VK_SHIFT); t.async_ctrl = GetAsyncKeyState(VK_CONTROL);
    BYTE table[256]{};
    if (!GetKeyboardState(table)) { t.error = GetLastError(); fault(*channel, t.error); }
    t.keyboard_ctrl = table[VK_CONTROL]; t.keyboard_shift = table[VK_SHIFT];
    t.left_ctrl = table[VK_LCONTROL]; t.right_ctrl = table[VK_RCONTROL];
    t.left_shift = table[VK_LSHIFT]; t.right_shift = table[VK_RSHIFT];
    channel->traces[index] = t;
    MemoryBarrier(); InterlockedIncrement(&channel->trace_count);
}
void apply(const Packet& p) {
    BYTE table[256]{};
    check(GetKeyboardState(table), "GetKeyboardState.runtime");
    if (p.kind == release) {
        for (unsigned i = 0; i < 256; ++i) if (owned[i]) held[i] = 0;
    } else { owned[p.vk] = 1; held[p.vk] = p.down ? 0x80 : 0; }
    for (unsigned i = 0; i < 256; ++i) if (owned[i]) table[i] = (table[i] & 1) | held[i];
    if (owned[VK_LCONTROL] || owned[VK_RCONTROL]) {
        table[VK_CONTROL] = (table[VK_CONTROL] & 1) | held[VK_LCONTROL] | held[VK_RCONTROL];
    }
    if (owned[VK_LSHIFT] || owned[VK_RSHIFT]) {
        table[VK_SHIFT] = (table[VK_SHIFT] & 1) | held[VK_LSHIFT] | held[VK_RSHIFT];
    }
    check(SetKeyboardState(table), "SetKeyboardState.target_GUI_thread");
}
}

extern "C" __declspec(dllexport) SHORT WINAPI ViewGetAsyncKeyState(int vk) {
    using namespace r303;
    const auto caller = reinterpret_cast<std::uint64_t>(_ReturnAddress());
    SHORT result = 0;
    AsyncState state{};
    const bool supported_key = async_supported(vk);
    bool valid = channel && channel->health == 1;
    try {
        if (valid) { state = read_async(*channel); if (supported_key && state.down[vk]) result = static_cast<SHORT>(0x8000); }
    } catch (const Error& e) { valid = false; fault(*channel,e.code,e.api.c_str()); }
    if (channel) {
        const auto index = channel->query_count;
        if (index >= static_cast<LONG>(query_capacity)) { fault(*channel,ERROR_BUFFER_OVERFLOW,"async.query_log_capacity"); return 0; }
        Query q{}; q.tick = GetTickCount64(); q.caller = caller;
        q.module = caller >= channel->image && caller-channel->image < channel->image_size ? channel->image : 0;
        q.pid = GetCurrentProcessId(); q.tid = GetCurrentThreadId(); q.sequence = state.input_sequence; q.version = state.version;
        q.probe = channel->probe.id; q.vk = vk; q.result = result; q.supported = supported_key; q.valid = valid;
        channel->queries[index] = q; MemoryBarrier(); InterlockedIncrement(&channel->query_count);
    }
    return result; // high bit only; unsupported keys return zero, never host passthrough
}

extern "C" __declspec(dllexport) LRESULT CALLBACK ManagedCallWindow(int code, WPARAM wp, LPARAM lp) {
    using namespace r303;
    if (code < 0) return CallNextHookEx(nullptr,code,wp,lp);
    const auto& message = *reinterpret_cast<CWPSTRUCT*>(lp);
    if (message.message != control_message) return CallNextHookEx(nullptr,code,wp,lp);
    try {
        connect();
        if (message.wParam != channel->token || reinterpret_cast<std::uint64_t>(message.hwnd) != channel->top) return CallNextHookEx(nullptr,code,wp,lp);
        if (message.lParam == install_iat) {
            demand(!patch,"IAT already initialized"); patch = std::make_unique<IatPatch>(*channel);
            patch->install(reinterpret_cast<void*>(&ViewGetAsyncKeyState));
        } else if (message.lParam == probe_api) {
            const auto observer = reinterpret_cast<void(WINAPI*)(Probe*)>(GetProcAddress(GetModuleHandleW(nullptr),"R303ReadOnlyProbe"));
            check(observer != nullptr,"GetProcAddress.EXE_read_only_probe");
            ++channel->probe.id; observer(&channel->probe);
        } else if (message.lParam == remove_iat) {
            demand(patch != nullptr,"missing IAT transaction"); patch->remove();
        } else throw Error("runtime.control",ERROR_NOT_SUPPORTED);
    } catch (const Error& e) { if (channel) fault(*channel,e.code,e.api.c_str()); }
    catch (...) { if (channel) fault(*channel,ERROR_UNHANDLED_EXCEPTION,"runtime.control_exception"); }
    return CallNextHookEx(nullptr,code,wp,lp);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK ManagedGetMessage(int code, WPARAM wp, LPARAM lp) {
    using namespace r303;
    if (code < 0 || code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    auto& message = *reinterpret_cast<MSG*>(lp);
    if (message.message != envelope) return CallNextHookEx(nullptr, code, wp, lp);
    try {
        connect();
        if (message.wParam != channel->token) return CallNextHookEx(nullptr, code, wp, lp);
        const auto sequence = static_cast<std::uint64_t>(message.lParam);
        const Packet* packet = nullptr;
        demand(channel->delivery_count <= capacity, "packet capacity");
        for (unsigned i = 0; i < channel->delivery_count; ++i) if (channel->delivery[i].sequence == sequence) packet = &channel->delivery[i];
        demand(packet != nullptr, "unknown managed sequence");
        const auto p = *packet;
        demand(p.kind >= key && p.kind <= release && (p.kind != key || supported(p.vk)), "unsupported managed packet");
        if (wp != PM_REMOVE) {
            record(p, false, false, 0); // never transform a peek or advance the table
            return CallNextHookEx(nullptr, code, wp, lp);
        }
        demand(sequence == static_cast<std::uint64_t>(channel->consumed) + 1, "duplicate or out-of-order consumption");
        if (channel->peek_probe && !probing) {
            probing = true;
            MSG peek{};
            // Diagnostic only: view the next queued envelope twice. Nested hook calls
            // must not apply that future event. The original fixture stays unchanged.
            PeekMessageW(&peek, nullptr, envelope, envelope, PM_NOREMOVE);
            PeekMessageW(&peek, nullptr, envelope, envelope, PM_NOREMOVE);
            probing = false;
        }
        const bool was_down = p.kind == key && ((p.vk == VK_LCONTROL || p.vk == VK_RCONTROL) ?
            (delivered[VK_LCONTROL] || delivered[VK_RCONTROL]) : (p.vk == VK_LSHIFT || p.vk == VK_RSHIFT) ?
            (delivered[VK_LSHIFT] || delivered[VK_RSHIFT]) : delivered[p.vk]);
        if (p.kind == key) delivered[p.vk] = p.down ? 1 : 0;
        if (p.kind == release) std::memset(delivered, 0, sizeof(delivered));
        bool applied = false;
        if ((p.kind == key || p.kind == release) && channel->enabled && !p.skip_state) { apply(p); applied = true; }
        message.hwnd = reinterpret_cast<HWND>(channel->edit);
        if (p.kind == key) {
            const auto generic = (p.vk == VK_LCONTROL || p.vk == VK_RCONTROL) ? VK_CONTROL :
                (p.vk == VK_LSHIFT || p.vk == VK_RSHIFT) ? VK_SHIFT : p.vk;
            const auto scan = MapVirtualKeyW(p.vk, MAPVK_VK_TO_VSC);
            const bool extended = p.vk == VK_RCONTROL || p.vk == VK_HOME || p.vk == VK_END;
            message.message = p.down ? WM_KEYDOWN : WM_KEYUP; message.wParam = generic;
            message.lParam = 1 | (static_cast<LPARAM>(scan & 0xff) << 16) | (extended ? (1LL << 24) : 0) |
                (!p.down ? (3LL << 30) : was_down ? (1LL << 30) : 0);
        } else if (p.kind == character) { message.message = WM_CHAR; message.wParam = p.wp; message.lParam = 1; }
        else if (p.kind == mouse) { message.message = p.message; message.wParam = p.wp; message.lParam = p.lp; }
        else { message.hwnd = reinterpret_cast<HWND>(channel->top); message.message = observe_message; message.wParam = 0; message.lParam = 0; }
        record(p, true, applied, 0);
        InterlockedExchange64(&channel->consumed, static_cast<LONG64>(sequence));
    } catch (const Error& e) {
        if (channel) fault(*channel, e.code, e.api.c_str());
        message.message = WM_NULL;
    } catch (...) {
        if (channel) fault(*channel, ERROR_UNHANDLED_EXCEPTION);
        message.message = WM_NULL;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
