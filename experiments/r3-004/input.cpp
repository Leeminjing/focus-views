#include "channel.hpp"

namespace r304 {
int input_main(const Args& args) {
    Mapping mapping(args.get(L"--channel"), sizeof(Channel), false);
    auto& c = *static_cast<Channel*>(mapping.view);
    std::ofstream log(std::filesystem::path(args.get(L"--log")), std::ios::binary);
    HMODULE module = nullptr; HHOOK hook = nullptr, controls = nullptr;
    unsigned written = 0, queried = 0;
    std::vector<Packet> pending;
    std::uint64_t lease_deadline = 0;
    auto emit = [&](const char* stage, DWORD error) {
        log << "{\"stage\":" << json(stage) << ",\"tickMs\":" << GetTickCount64() << ",\"pid\":" << GetCurrentProcessId()
            << ",\"targetPid\":" << c.pid << ",\"targetTid\":" << c.tid << ",\"error\":" << error
            << ",\"health\":" << c.health << ",\"consumed\":" << c.consumed << ",\"stateVersion\":" << c.async_state.version
            << ",\"inputSequence\":" << c.async_state.input_sequence << ",\"iatInstalled\":" << c.iat_installed << ",\"pinned\":" << c.pinned
            << ",\"image\":" << c.image << ",\"imageSize\":" << c.image_size << ",\"slot\":" << c.slot << ",\"native\":" << c.native
            << ",\"primaryError\":" << c.error << ",\"primaryApi\":" << json(c.error_api) << ",\"targetGone\":" << c.target_gone
            << ",\"replacement\":" << c.replacement << ",\"slotAfter\":" << c.slot_after << ",\"pageBefore\":" << c.page_before
            << ",\"pageAfter\":" << c.page_after << ",\"module\":" << json(c.module_path) << ",\"nativeModule\":" << json(c.native_path)
            << ",\"cursorSlot\":" << c.cursor_slot << ",\"cursorNative\":" << c.cursor_native << ",\"cursorReplacement\":" << c.cursor_replacement
            << ",\"cursorAfter\":" << c.cursor_after << ",\"cursorPageBefore\":" << c.cursor_page_before << ",\"cursorPageAfter\":" << c.cursor_page_after
            << ",\"cleanupDone\":" << c.cleanup_done
            << ",\"runtimeImage\":" << c.runtime_image << ",\"replacementModule\":" << json(c.runtime_path) << "}\n"; log.flush();
    };
    auto flush = [&] {
        const auto count = static_cast<unsigned>(c.trace_count); MemoryBarrier();
        while (written < count && written < trace_capacity) {
            const auto& t = c.traces[written++];
            log << "{\"stage\":\"hook\",\"sequence\":" << t.sequence << ",\"tickMs\":" << t.tick
                << ",\"pid\":" << t.pid << ",\"tid\":" << t.tid << ",\"removed\":" << t.removed << ",\"applied\":" << t.applied
                << ",\"kind\":" << t.kind << ",\"vk\":" << t.vk << ",\"down\":" << t.down << ",\"error\":" << t.error
                << ",\"ctrl\":" << t.ctrl << ",\"shift\":" << t.shift << ",\"asyncCtrl\":" << t.async_ctrl
                << ",\"keyboardCtrl\":" << unsigned(t.keyboard_ctrl) << ",\"keyboardShift\":" << unsigned(t.keyboard_shift)
                << ",\"leftCtrl\":" << unsigned(t.left_ctrl) << ",\"rightCtrl\":" << unsigned(t.right_ctrl)
                << ",\"leftShift\":" << unsigned(t.left_shift) << ",\"rightShift\":" << unsigned(t.right_shift)
                << ",\"action\":" << t.action << ",\"target\":" << t.target << ",\"capture\":" << t.capture << ",\"focus\":" << t.focus << ",\"captured\":" << t.captured << ",\"depth\":" << t.depth
                << ",\"x\":" << t.x << ",\"y\":" << t.y << ",\"clientX\":" << t.client_x << ",\"clientY\":" << t.client_y
                << ",\"queueLeft\":" << t.left << ",\"keyboardLeft\":" << unsigned(t.keyboard_left) << "}\n";
        }
        const auto count_queries = static_cast<unsigned>(c.query_count); MemoryBarrier();
        while (queried < count_queries && queried < query_capacity) {
            const auto& q = c.queries[queried++];
            log << "{\"stage\":\"virtual_query\",\"index\":" << queried << ",\"tickMs\":" << q.tick << ",\"pid\":" << q.pid << ",\"tid\":" << q.tid
                << ",\"api\":" << json(q.api ? "GetCursorPos" : "GetAsyncKeyState") << ",\"cursorX\":" << q.cursor.x << ",\"cursorY\":" << q.cursor.y << ",\"caller\":" << q.caller << ",\"module\":" << q.module << ",\"sequence\":" << q.sequence
                << ",\"version\":" << q.version << ",\"probe\":" << q.probe << ",\"vk\":" << q.vk << ",\"result\":" << q.result
                << ",\"supported\":" << q.supported << ",\"valid\":" << q.valid << "}\n";
        }
        log.flush();
    };
    auto control = [&](Control operation) {
        DWORD_PTR result = 0;
        check(SendMessageTimeoutW(reinterpret_cast<HWND>(c.top),control_message,static_cast<WPARAM>(c.token),operation,
            SMTO_BLOCK | SMTO_ABORTIFHUNG,2000,&result) != 0,"SendMessageTimeoutW.runtime_control");
        if (c.health == -1) throw Error(c.error_api,static_cast<DWORD>(c.error));
    };
    auto publish = [&](const Packet& p, const char* reason) {
        if (p.kind != key && p.kind != release && p.kind != mouse) return;
        auto state = read_async(c);
        if (p.kind == release) std::memset(state.down,0,sizeof(state.down));
        else if (p.kind == mouse) {
            state.logical = {p.x,p.y}; state.cursor = state.logical;
            check(ClientToScreen(reinterpret_cast<HWND>(c.top),&state.cursor),"ClientToScreen.accepted_cursor"); state.cursor_valid = TRUE;
            if (p.action != move) state.down[VK_LBUTTON] = p.action == left_down ? 1 : 0;
        } else state.down[p.vk] = p.down ? 1 : 0;
        lease_deadline = state.down[VK_LBUTTON] ? GetTickCount64()+c.lease_ms : 0;
        state.down[VK_CONTROL] = state.down[VK_LCONTROL] | state.down[VK_RCONTROL];
        state.down[VK_SHIFT] = state.down[VK_LSHIFT] | state.down[VK_RSHIFT];
        ++state.version; state.input_sequence = p.sequence;
        InterlockedIncrement(&c.state_lock); MemoryBarrier(); c.async_state = state; MemoryBarrier();
        // Linearization point: even epoch publishes the entire event state.
        InterlockedIncrement(&c.state_lock);
        log << "{\"stage\":\"effective\",\"reason\":" << json(reason) << ",\"sequence\":" << p.sequence << ",\"version\":" << state.version
            << ",\"vk\":" << p.vk << ",\"down\":" << p.down << ",\"kind\":" << p.kind << ",\"ctrl\":" << unsigned(state.down[VK_CONTROL])
            << ",\"shift\":" << unsigned(state.down[VK_SHIFT]) << ",\"leftCtrl\":" << unsigned(state.down[VK_LCONTROL])
            << ",\"rightCtrl\":" << unsigned(state.down[VK_RCONTROL]) << ",\"leftShift\":" << unsigned(state.down[VK_LSHIFT])
            << ",\"rightShift\":" << unsigned(state.down[VK_RSHIFT]) << ",\"x\":" << unsigned(state.down['X'])
            << ",\"action\":" << p.action << ",\"left\":" << unsigned(state.down[VK_LBUTTON]) << ",\"cursorX\":" << state.cursor.x << ",\"cursorY\":" << state.cursor.y
            << ",\"logicalX\":" << state.logical.x << ",\"logicalY\":" << state.logical.y << "}\n"; log.flush();
    };
    try {
        demand(log.good() && c.signature == channel_magic, "input log/channel");
        DWORD pid = 0;
        demand(GetWindowThreadProcessId(reinterpret_cast<HWND>(c.edit), &pid) == c.tid && pid == c.pid, "input target ownership");
        demand(GetWindowThreadProcessId(reinterpret_cast<HWND>(c.top), &pid) == c.tid && pid == c.pid, "input top ownership");
        demand(thread_desktop() == user_object_name(GetThreadDesktop(c.tid)), "input same desktop");
        Handle target(OpenProcess(SYNCHRONIZE,FALSE,c.pid)); check(target.value != nullptr,"OpenProcess.owned_target_liveness");
        c.bridge_pid = GetCurrentProcessId();
        module = LoadLibraryW(args.get(L"--dll").c_str()); check(module != nullptr, "LoadLibraryW.runtime");
        const auto proc = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "ManagedGetMessage"));
        check(proc != nullptr, "GetProcAddress.ManagedGetMessage");
        hook = SetWindowsHookExW(WH_GETMESSAGE, proc, module, c.tid); check(hook != nullptr, "SetWindowsHookExW.thread_only");
        const auto control_proc = reinterpret_cast<HOOKPROC>(GetProcAddress(module,"ManagedCallWindow"));
        check(control_proc != nullptr,"GetProcAddress.ManagedCallWindow");
        controls = SetWindowsHookExW(WH_CALLWNDPROC,control_proc,module,c.tid); check(controls != nullptr,"SetWindowsHookExW.thread_control");
        control(install_iat);
        demand(c.initialized && (!c.virtualize || c.iat_installed),"IAT install attestation");
        InterlockedExchange(&c.health, 1); InterlockedExchange(&c.ready, 1); emit("hook_installed", 0);
        LONG last = 0;
        while (true) {
            if (c.health == -1) throw Error(c.error_api,static_cast<DWORD>(c.error));
            if (lease_deadline && GetTickCount64() > lease_deadline) throw Error("mouse.button_up.lease_expired",ERROR_TIMEOUT);
            if (WaitForSingleObject(target.value,0) == WAIT_OBJECT_0) throw Error("owned_target.exit",ERROR_PROCESS_ABORTED);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            const auto request = c.request;
            if (request == last) { Sleep(2); continue; }
            MemoryBarrier(); last = request;
            demand(c.count <= capacity && c.health == 1 && c.command >= 1 && c.command <= 6, "input request/channel health");
            if (c.command == 6) { control(destroy_capture); flush(); InterlockedExchange(&c.command_done,last); continue; }
            if (c.command == 5) {
                control(probe_api);
                DWORD_PTR result = 0;
                check(SendMessageTimeoutW(reinterpret_cast<HWND>(c.top),observe_message,0,0,SMTO_BLOCK | SMTO_ABORTIFHUNG,1000,&result) != 0,"SendMessageTimeoutW.fixture_native_observe");
                flush(); emit("probe_completed",0); InterlockedExchange(&c.command_done,last); continue;
            }
            demand(pending.size()+c.count <= capacity,"pending managed queue capacity");
            for (unsigned i = 0; i < c.count; ++i) {
                const auto& p = c.packets[i];
                demand(p.kind >= key && p.kind <= release && (p.kind != key || supported(p.vk)), "unsupported packet");
            }
            for (unsigned i = 0; i < c.count; ++i) {
                const auto& p = c.packets[i];
                pending.push_back(p); publish(p,"accepted_input");
                log << "{\"stage\":\"accepted\",\"sequence\":" << p.sequence << ",\"kind\":" << p.kind
                    << ",\"vk\":" << p.vk << ",\"down\":" << p.down << ",\"skipState\":" << p.skip_state
                    << ",\"action\":" << p.action << ",\"x\":" << p.x << ",\"y\":" << p.y << "}\n";
            }
            log.flush();
            if (c.command == 3) { emit("staged_not_posted",0); InterlockedExchange(&c.command_done,last); continue; }
            demand(!pending.empty(),"nothing pending for delivery");
            c.delivery_count = static_cast<unsigned>(pending.size());
            std::copy(pending.begin(),pending.end(),c.delivery); MemoryBarrier();
            for (const auto& p : pending) {
                check(PostMessageW(reinterpret_cast<HWND>(c.top),envelope,static_cast<WPARAM>(c.token),static_cast<LPARAM>(p.sequence)),"PostMessageW.managed_envelope");
            }
            const auto end = pending.back().sequence;
            const auto deadline = GetTickCount64() + 2000;
            while (static_cast<std::uint64_t>(c.consumed) < end && c.health == 1 && GetTickCount64() < deadline) {
                if (WaitForSingleObject(target.value,0) == WAIT_OBJECT_0) throw Error("owned_target.exit",ERROR_PROCESS_ABORTED);
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                Sleep(2);
            }
            flush();
            if (c.health != 1) throw Error("target_hook", static_cast<DWORD>(c.error));
            if (static_cast<std::uint64_t>(c.consumed) != end) throw Error("target_dequeue_ack", ERROR_TIMEOUT);
            // The final barrier can only be dequeued after prior key dispatch returns.
            // This read-only query then observes original control processing, not just hook execution.
            DWORD_PTR result = 0;
            check(SendMessageTimeoutW(reinterpret_cast<HWND>(c.top), observe_message, 0, 0,
                SMTO_BLOCK | SMTO_ABORTIFHUNG, 1000, &result) != 0, "SendMessageTimeoutW.processed_observation");
            emit("processed", 0);
            pending.clear();
            if (c.command == 2) { control(remove_iat); emit("iat_restored",0); }
            InterlockedExchange(&c.command_done, last);
            if (c.command == 2) break;
        }
    } catch (const Error& e) {
        if (e.api == "owned_target.exit") {
            c.target_gone = 1; c.iat_installed = 0; c.pinned = 0; c.slot_after = 0;
        }
        if (c.health != -1) fault(c, e.code, e.api.c_str());
        emit(c.error_api, e.code);
    }
    catch (...) { fault(c, ERROR_UNHANDLED_EXCEPTION); emit("bridge_exception", ERROR_UNHANDLED_EXCEPTION); }
    if (c.health == -1) {
        Packet clear{}; clear.kind = release; clear.sequence = c.async_state.input_sequence;
        publish(clear,"channel_invalidation");
        if (!c.target_gone && c.initialized) {
            DWORD_PTR result = 0;
            SetLastError(0);
            const auto okay = SendMessageTimeoutW(reinterpret_cast<HWND>(c.top),control_message,static_cast<WPARAM>(c.token),remove_iat,SMTO_BLOCK|SMTO_ABORTIFHUNG,1000,&result);
            emit(okay && c.restored ? "invalid_channel_cleanup_confirmed" : "invalid_channel_cleanup_unconfirmed",okay ? 0 : GetLastError());
        }
    }
    flush();
    if (controls && !UnhookWindowsHookEx(controls)) {
        const auto error = GetLastError(); fault(c,error,"UnhookWindowsHookEx.control"); emit("UnhookWindowsHookEx.control.failed",error);
    }
    if (hook && !UnhookWindowsHookEx(hook)) {
        const auto error = GetLastError(); fault(c,error,"UnhookWindowsHookEx.queue"); emit("UnhookWindowsHookEx.queue.failed",error);
    }
    if (module && !FreeLibrary(module)) {
        const auto error = GetLastError(); fault(c,error,"FreeLibrary.bridge_module"); emit("FreeLibrary.bridge_module.failed",error);
    }
    if (c.health == 1) InterlockedExchange(&c.health, 0);
    emit(c.health == -1 ? "channel_invalid" : "channel_closed", static_cast<DWORD>(c.error));
    InterlockedExchange(&c.ready, 2);
    return c.health == -1 ? 2 : 0;
}
}
