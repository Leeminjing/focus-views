#include "../r3-004/channel.hpp"
#include "../r3-004/iat.hpp"
#include <intrin.h>
#ifdef FOCUS_VIEWS_QUERY_STREAM
#include "../r5-003/runtime/query-stream.hpp"
#endif

namespace {
using namespace r304;
std::unique_ptr<Mapping> mapping;
Channel* channel = nullptr;
BYTE held[256]{}, owned[256]{};
BYTE delivered[256]{};
bool probing = false;
std::unique_ptr<IatPatch> patch;
Handle bridge_liveness;
UINT_PTR watchdog = 0;
HWND remembered_capture = nullptr;
POINT event_cursor{};
bool record_query(const Query& q,const char* api){
#ifdef FOCUS_VIEWS_QUERY_STREAM
    (void)api;
    static std::unique_ptr<r5003query::Producer> producer;
    static std::uint64_t token=0;
    try{if(!producer||token!=channel->token){producer=std::make_unique<r5003query::Producer>(*channel);token=channel->token;}producer->write(*channel,q);return true;}
    catch(const Error& e){fault(*channel,e.code,e.api.c_str());return false;}
#else
    const auto index=channel->query_count;
    if(index>=static_cast<LONG>(query_capacity)){fault(*channel,ERROR_BUFFER_OVERFLOW,api);return false;}
    channel->queries[index]=q;MemoryBarrier();InterlockedIncrement(&channel->query_count);return true;
#endif
}
bool belongs(HWND w) {
    DWORD pid = 0;
    const auto top = reinterpret_cast<HWND>(channel->top);
    return w && GetWindowThreadProcessId(w,&pid) == channel->tid && pid == channel->pid && (w == top || IsChild(top,w));
}
void cleanup() {
    BYTE table[256]{}; check(GetKeyboardState(table),"GetKeyboardState.cleanup");
    for (unsigned i=0;i<256;++i) if (owned[i]) table[i] &= 1;
    table[VK_LBUTTON] &= 1; table[VK_CONTROL] &= 1; table[VK_SHIFT] &= 1;
    check(SetKeyboardState(table),"SetKeyboardState.cleanup");
    const auto capture = GetCapture();
    if (capture) {
        demand(belongs(capture),"cleanup capture ownership");
        POINT pt = event_cursor; check(ScreenToClient(capture,&pt),"ScreenToClient.cleanup");
        // Native window procedure receives a real release before the cleanup
        // fallback. This is runtime cancellation, never a business state setter.
        SendMessageW(capture,WM_LBUTTONUP,0,MAKELPARAM(static_cast<short>(pt.x),static_cast<short>(pt.y)));
        if (GetCapture() == capture) check(ReleaseCapture(),"ReleaseCapture.cleanup");
    }
    std::memset(held,0,sizeof(held)); std::memset(delivered,0,sizeof(delivered));
    remembered_capture = nullptr; InterlockedExchange(&channel->cleanup_done,1);
}
void CALLBACK orphan_watchdog(HWND, UINT, UINT_PTR timer, DWORD) {
    if (!channel || !bridge_liveness.value || WaitForSingleObject(bridge_liveness.value,0) != WAIT_OBJECT_0) return;
    KillTimer(nullptr,timer); watchdog = 0;
    fault(*channel,ERROR_BROKEN_PIPE,"owned_input_bridge.exit");
    try {
        // Sole writer handoff only after the bridge process is confirmed dead.
        auto state = channel->async_state; std::memset(state.down,0,sizeof(state.down)); ++state.version;
        auto epoch = InterlockedCompareExchange(&channel->state_lock,0,0);
        if (epoch & 1) ++epoch; // dead producer can have left an odd epoch
        InterlockedExchange(&channel->state_lock,epoch+1); MemoryBarrier(); channel->async_state = state; MemoryBarrier(); InterlockedExchange(&channel->state_lock,epoch+2);
        cleanup(); if (patch) patch->remove(false);
        InterlockedExchange(&channel->watchdog_done,1);
    } catch (...) { InterlockedExchange(&channel->watchdog_done,-1); }
}
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
    t.action = p.action;
    t.ctrl = GetKeyState(VK_CONTROL); t.shift = GetKeyState(VK_SHIFT); t.async_ctrl = GetAsyncKeyState(VK_CONTROL);
    BYTE table[256]{};
    if (!GetKeyboardState(table)) { t.error = GetLastError(); fault(*channel, t.error); }
    t.keyboard_ctrl = table[VK_CONTROL]; t.keyboard_shift = table[VK_SHIFT];
    t.left_ctrl = table[VK_LCONTROL]; t.right_ctrl = table[VK_RCONTROL];
    t.left_shift = table[VK_LSHIFT]; t.right_shift = table[VK_RSHIFT];
    t.left = GetKeyState(VK_LBUTTON); t.keyboard_left = table[VK_LBUTTON];
    t.capture = reinterpret_cast<std::uint64_t>(GetCapture()); t.focus = reinterpret_cast<std::uint64_t>(GetFocus());
    channel->traces[index] = t;
    MemoryBarrier(); InterlockedIncrement(&channel->trace_count);
}
void apply(const Packet& p) {
    BYTE table[256]{};
    check(GetKeyboardState(table), "GetKeyboardState.runtime");
    if (p.kind == release) {
        for (unsigned i = 0; i < 256; ++i) if (owned[i]) held[i] = 0;
    } else if (p.kind == mouse) {
        owned[VK_LBUTTON] = 1;
        if (p.action != move) held[VK_LBUTTON] = p.action == left_down ? 0x80 : 0;
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
    using namespace r304;
    const auto caller = reinterpret_cast<std::uint64_t>(_ReturnAddress());
    SHORT result = 0;
    AsyncState state{};
    const bool supported_key = async_supported(vk);
    bool valid = channel && channel->health == 1;
    try {
        if (valid) { state = read_async(*channel); if (supported_key && state.down[vk]) result = static_cast<SHORT>(0x8000); }
    } catch (const Error& e) { valid = false; fault(*channel,e.code,e.api.c_str()); }
    if (channel) {
        Query q{}; q.tick = GetTickCount64(); q.caller = caller;
        q.module = caller >= channel->image && caller-channel->image < channel->image_size ? channel->image : 0;
        q.pid = GetCurrentProcessId(); q.tid = GetCurrentThreadId(); q.sequence = state.input_sequence; q.version = state.version;
        q.probe = channel->probe.id; q.vk = vk; q.result = result; q.supported = supported_key; q.valid = valid;
        if(!record_query(q,"async.query_log_capacity"))return 0;
    }
    return result; // high bit only; unsupported keys return zero, never host passthrough
}

extern "C" __declspec(dllexport) BOOL WINAPI ViewGetCursorPos(LPPOINT point) {
    const auto caller = reinterpret_cast<std::uint64_t>(_ReturnAddress());
    bool valid = channel && channel->health == 1 && point;
    AsyncState state{};
    try { if (valid) { state = read_async(*channel); valid = state.cursor_valid != FALSE; } }
    catch (const Error& e) { valid = false; fault(*channel,e.code,e.api.c_str()); }
    if (valid) *point = state.cursor;
    if (channel) {
        Query q{}; q.api = 1; q.tick = GetTickCount64(); q.caller = caller;
        q.module = caller >= channel->image && caller-channel->image < channel->image_size ? channel->image : 0;
        q.pid = GetCurrentProcessId(); q.tid = GetCurrentThreadId(); q.sequence = state.input_sequence; q.version = state.version;
        q.probe = channel->probe.id; q.result = valid ? 1 : 0; q.valid = valid; q.supported = 1; q.cursor = state.cursor;
        if(!record_query(q,"cursor.query_log_capacity"))return FALSE;
    }
    if (!valid) SetLastError(ERROR_INVALID_STATE);
    return valid ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK ManagedCallWindow(int code, WPARAM wp, LPARAM lp) {
    using namespace r304;
    if (code < 0) return CallNextHookEx(nullptr,code,wp,lp);
    const auto& message = *reinterpret_cast<CWPSTRUCT*>(lp);
    if (message.message != control_message) return CallNextHookEx(nullptr,code,wp,lp);
    try {
        connect();
        if (message.wParam != channel->token || reinterpret_cast<std::uint64_t>(message.hwnd) != channel->top) return CallNextHookEx(nullptr,code,wp,lp);
        if (message.lParam == install_iat) {
            demand(!patch,"IAT already initialized"); patch = std::make_unique<IatPatch>(*channel);
            patch->install(reinterpret_cast<void*>(&ViewGetAsyncKeyState),reinterpret_cast<void*>(&ViewGetCursorPos));
            bridge_liveness.value = OpenProcess(SYNCHRONIZE,FALSE,channel->bridge_pid);
            check(bridge_liveness.value != nullptr,"OpenProcess.bridge_watchdog");
            watchdog = SetTimer(nullptr,0,100,orphan_watchdog); check(watchdog != 0,"SetTimer.bridge_watchdog");
        } else if (message.lParam == probe_api) {
            const auto observer = reinterpret_cast<void(WINAPI*)(Probe*)>(GetProcAddress(GetModuleHandleW(nullptr),"R304ReadOnlyProbe"));
            check(observer != nullptr,"GetProcAddress.EXE_read_only_probe");
            ++channel->probe.id; observer(&channel->probe);
        } else if (message.lParam == remove_iat) {
            cleanup(); demand(patch != nullptr,"missing IAT transaction"); patch->remove();
            if (watchdog) { check(KillTimer(nullptr,watchdog),"KillTimer.watchdog"); watchdog = 0; }
            // A normal confirmed close need not unload this DLL immediately.
            // Forget only the fully restored transaction; a future bridge on
            // this same GUI thread must build and attest a fresh transaction.
            demand(channel->restored&&!channel->iat_installed&&!channel->pinned,"confirmed transaction retirement");
            patch.reset();bridge_liveness.close();
        } else if (message.lParam == cleanup_input) {
            cleanup();
        } else if (message.lParam == destroy_capture) {
            const auto capture = GetCapture(); demand(capture && belongs(capture),"destroy owned capture");
            check(DestroyWindow(capture),"DestroyWindow.capture_negative");
        } else throw Error("runtime.control",ERROR_NOT_SUPPORTED);
    } catch (const Error& e) { if (channel) fault(*channel,e.code,e.api.c_str()); }
    catch (...) { if (channel) fault(*channel,ERROR_UNHANDLED_EXCEPTION,"runtime.control_exception"); }
    return CallNextHookEx(nullptr,code,wp,lp);
}

extern "C" __declspec(dllexport) LRESULT CALLBACK ManagedGetMessage(int code, WPARAM wp, LPARAM lp) {
    using namespace r304;
    if (code < 0 || code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    auto& message = *reinterpret_cast<MSG*>(lp);
    if (message.message != envelope) return CallNextHookEx(nullptr, code, wp, lp);
    try {
        connect();
        if (message.wParam != channel->token) return CallNextHookEx(nullptr, code, wp, lp);
        if (channel->health != 1) { message.message = WM_NULL; return CallNextHookEx(nullptr,code,wp,lp); }
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
        // Observe capture after the PREVIOUS native dispatch has returned (the
        // following barrier also reaches here). Never infer capture from a down
        // packet or from a fixture command. Keep the previous HWND long enough
        // to detect destruction instead of silently reverting to hit testing.
        if (remembered_capture && !IsWindow(remembered_capture)) throw Error("captured_window.destroyed",ERROR_INVALID_WINDOW_HANDLE);
        const auto actual_capture = GetCapture();
        if (actual_capture) demand(belongs(actual_capture),"observed capture ownership");
        remembered_capture = actual_capture;
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
        if ((p.kind == key || p.kind == release || p.kind == mouse) && channel->enabled && !p.skip_state) { apply(p); applied = true; }
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
        else if (p.kind == mouse) {
            demand(p.action >= move && p.action <= left_up,"mouse action subset");
            if (remembered_capture && !IsWindow(remembered_capture)) throw Error("captured_window.destroyed",ERROR_INVALID_WINDOW_HANDLE);
            const auto top = reinterpret_cast<HWND>(channel->top);
            POINT screen{p.x,p.y}; check(ClientToScreen(top,&screen),"ClientToScreen.mouse_event"); event_cursor = screen;
            const auto capture = GetCapture(); HWND target = capture;
            unsigned depth = 0;
            if (capture) demand(belongs(capture),"capture ownership");
            else {
                target = top;
                for (;depth<32;++depth) {
                    POINT local = screen; check(ScreenToClient(target,&local),"ScreenToClient.hit_test");
                    const auto child = ChildWindowFromPointEx(target,local,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED|CWP_SKIPTRANSPARENT);
                    if (!child) throw Error("mouse.hit_test.outside_owned_tree",ERROR_NOT_FOUND);
                    demand(belongs(child),"hit_test candidate ownership");
                    if (child == target) break;
                    target = child;
                }
                demand(depth < 32,"hit_test depth");
            }
            if (channel->foreign_candidate) target = reinterpret_cast<HWND>(channel->foreign_candidate);
            if (!belongs(target)) throw Error("mouse.route.ownership",ERROR_ACCESS_DENIED);
            POINT local = screen; check(ScreenToClient(target,&local),"ScreenToClient.routed_target");
            demand(local.x >= SHRT_MIN && local.x <= SHRT_MAX && local.y >= SHRT_MIN && local.y <= SHRT_MAX,"mouse LPARAM range");
            message.hwnd = target; message.pt = screen;
            message.message = p.action == move ? WM_MOUSEMOVE : p.action == left_down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
            message.wParam = (held[VK_LBUTTON] ? MK_LBUTTON : 0) | ((held[VK_LSHIFT] || held[VK_RSHIFT]) ? MK_SHIFT : 0) | ((held[VK_LCONTROL] || held[VK_RCONTROL]) ? MK_CONTROL : 0);
            message.lParam = MAKELPARAM(static_cast<short>(local.x),static_cast<short>(local.y));
            remembered_capture = capture;
            record(p,true,applied,0);
            auto& route = channel->traces[channel->trace_count-1]; route.target = reinterpret_cast<std::uint64_t>(target);
            route.x = p.x; route.y = p.y; route.client_x = local.x; route.client_y = local.y; route.captured = capture != nullptr; route.depth = depth;
        }
        else if (p.kind == release) { cleanup(); message.hwnd = reinterpret_cast<HWND>(channel->top); message.message = observe_message; message.wParam = 0; message.lParam = 0; }
        else { message.hwnd = reinterpret_cast<HWND>(channel->top); message.message = observe_message; message.wParam = 0; message.lParam = 0; }
        if (p.kind != mouse) record(p, true, applied, 0);
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
