#include "channel.hpp"

namespace r302 {
int input_main(const Args& args) {
    Mapping mapping(args.get(L"--channel"), sizeof(Channel), false);
    auto& c = *static_cast<Channel*>(mapping.view);
    std::ofstream log(std::filesystem::path(args.get(L"--log")), std::ios::binary);
    HMODULE module = nullptr; HHOOK hook = nullptr;
    unsigned written = 0;
    auto emit = [&](const char* stage, DWORD error) {
        log << "{\"stage\":" << json(stage) << ",\"tickMs\":" << GetTickCount64() << ",\"pid\":" << GetCurrentProcessId()
            << ",\"targetPid\":" << c.pid << ",\"targetTid\":" << c.tid << ",\"error\":" << error
            << ",\"health\":" << c.health << ",\"consumed\":" << c.consumed << "}\n"; log.flush();
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
                << ",\"leftShift\":" << unsigned(t.left_shift) << ",\"rightShift\":" << unsigned(t.right_shift) << "}\n";
        }
        log.flush();
    };
    try {
        demand(log.good() && c.signature == channel_magic, "input log/channel");
        DWORD pid = 0;
        demand(GetWindowThreadProcessId(reinterpret_cast<HWND>(c.edit), &pid) == c.tid && pid == c.pid, "input target ownership");
        demand(GetWindowThreadProcessId(reinterpret_cast<HWND>(c.top), &pid) == c.tid && pid == c.pid, "input top ownership");
        demand(thread_desktop() == user_object_name(GetThreadDesktop(c.tid)), "input same desktop");
        module = LoadLibraryW(args.get(L"--dll").c_str()); check(module != nullptr, "LoadLibraryW.runtime");
        const auto proc = reinterpret_cast<HOOKPROC>(GetProcAddress(module, "ManagedGetMessage"));
        check(proc != nullptr, "GetProcAddress.ManagedGetMessage");
        hook = SetWindowsHookExW(WH_GETMESSAGE, proc, module, c.tid); check(hook != nullptr, "SetWindowsHookExW.thread_only");
        InterlockedExchange(&c.health, 1); InterlockedExchange(&c.ready, 1); emit("hook_installed", 0);
        LONG last = 0;
        while (true) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            const auto request = c.request;
            if (request == last) { Sleep(2); continue; }
            MemoryBarrier(); last = request;
            demand(c.count > 0 && c.count <= capacity && c.health == 1, "input request/channel health");
            for (unsigned i = 0; i < c.count; ++i) {
                const auto& p = c.packets[i];
                demand(p.kind >= key && p.kind <= release && (p.kind != key || supported(p.vk)), "unsupported packet");
            }
            for (unsigned i = 0; i < c.count; ++i) {
                const auto& p = c.packets[i];
                check(PostMessageW(reinterpret_cast<HWND>(c.top), envelope, static_cast<WPARAM>(c.token),
                    static_cast<LPARAM>(p.sequence)), "PostMessageW.managed_envelope");
                log << "{\"stage\":\"accepted\",\"sequence\":" << p.sequence << ",\"kind\":" << p.kind
                    << ",\"vk\":" << p.vk << ",\"down\":" << p.down << ",\"skipState\":" << p.skip_state << "}\n";
            }
            log.flush();
            const auto end = c.packets[c.count - 1].sequence;
            const auto deadline = GetTickCount64() + 2000;
            while (static_cast<std::uint64_t>(c.consumed) < end && c.health == 1 && GetTickCount64() < deadline) {
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
            InterlockedExchange(&c.command_done, last);
            if (c.command == 2) break;
        }
    } catch (const Error& e) {
        if (c.health != -1) fault(c, e.code, e.api.c_str());
        emit(c.error_api, e.code);
    }
    catch (...) { fault(c, ERROR_UNHANDLED_EXCEPTION); emit("bridge_exception", ERROR_UNHANDLED_EXCEPTION); }
    if (hook && !UnhookWindowsHookEx(hook)) { fault(c, GetLastError()); emit("UnhookWindowsHookEx", static_cast<DWORD>(c.error)); }
    if (module && !FreeLibrary(module)) { fault(c, GetLastError()); emit("FreeLibrary", static_cast<DWORD>(c.error)); }
    if (c.health == 1) InterlockedExchange(&c.health, 0);
    emit(c.health == -1 ? "channel_invalid" : "channel_closed", static_cast<DWORD>(c.error));
    InterlockedExchange(&c.ready, 2);
    return c.health == -1 ? 2 : 0;
}
}
