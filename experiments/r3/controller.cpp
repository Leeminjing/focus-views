#include "protocol.hpp"
#include "../common/native.hpp" // unchanged experimental hashing utilities, this TU only
#include <atomic>
#include <thread>

namespace r3 {
namespace {
struct Log {
    std::ofstream file;
    explicit Log(const std::filesystem::path& path) : file(path, std::ios::binary) { demand(file.good(), "controller log unavailable"); }
    void emit(const char* type, const std::string& fields) {
        file << "{\"type\":" << json(type) << ",\"tickMs\":" << GetTickCount64() << ',' << fields << "}\n";
        file.flush();
    }
    void result(const std::string& name, const std::string& status, const std::string& detail) {
        emit("capability", "\"name\":" + json(name) + ",\"status\":" + json(status) + ",\"detail\":" + json(detail));
        std::cout << name << ": " << status << " (" << detail << ")\n";
    }
};
std::wstring quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
struct Process {
    Handle process, thread;
    DWORD pid = 0, tid = 0;
    std::string label;
    bool exited = false;
};
class Processes {
    Handle job_;
    std::vector<std::unique_ptr<Process>> children_;
    Log& log_;
public:
    explicit Processes(Log& log) : log_(log) {
        job_.value = CreateJobObjectW(nullptr, nullptr); check(job_.value != nullptr, "CreateJobObjectW");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{}; info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(SetInformationJobObject(job_.value, JobObjectExtendedLimitInformation, &info, sizeof(info)), "SetInformationJobObject");
    }
    ~Processes() {
        try { stop_all(); } catch (const std::exception& e) { log_.emit("cleanup_error", "\"error\":" + json(e.what())); }
    }
    Process& spawn(const std::filesystem::path& exe, const std::wstring& desktop,
        const std::vector<std::wstring>& arguments, const std::string& label) {
        std::wstring command = quote(exe.wstring());
        for (const auto& argument : arguments) command += L" " + quote(argument);
        STARTUPINFOW startup{sizeof(startup)};
        auto desktop_string = desktop;
        startup.lpDesktop = desktop_string.data();
        PROCESS_INFORMATION info{};
        check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, exe.parent_path().c_str(), &startup, &info), "CreateProcessW");
        auto child = std::make_unique<Process>();
        child->process.value = info.hProcess; child->thread.value = info.hThread;
        child->pid = info.dwProcessId; child->tid = info.dwThreadId; child->label = label;
        auto& stored = *children_.emplace_back(std::move(child));
        log_.emit("process_created", "\"label\":" + json(label) + ",\"pid\":" + std::to_string(stored.pid) + ",\"desktop\":" + json(desktop.c_str()));
        check(AssignProcessToJobObject(job_.value, stored.process.value), "AssignProcessToJobObject");
        if (ResumeThread(stored.thread.value) == DWORD(-1)) throw Error("ResumeThread", GetLastError());
        return stored;
    }
    void confirm(Process& child) {
        if (child.exited) return;
        const auto wait = WaitForSingleObject(child.process.value, 5000);
        if (wait != WAIT_OBJECT_0) throw Error("WaitForSingleObject.process", wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT, child.label);
        DWORD code = 0; check(GetExitCodeProcess(child.process.value, &code), "GetExitCodeProcess");
        child.exited = true;
        log_.emit("process_exited", "\"label\":" + json(child.label) + ",\"pid\":" + std::to_string(child.pid) + ",\"exitCode\":" + std::to_string(code));
    }
    void terminate(Process& child) {
        if (child.exited) return;
        const auto wait = WaitForSingleObject(child.process.value, 0);
        if (wait == WAIT_FAILED) throw Error("WaitForSingleObject", GetLastError());
        if (wait == WAIT_TIMEOUT) check(TerminateProcess(child.process.value, 0xE0430001), "TerminateProcess.owned_child");
        confirm(child);
    }
    void stop_all() { for (auto& child : children_) terminate(*child); }
};
struct Desktop {
    HDESK handle = nullptr;
    std::wstring name, full;
    Log& log;
    Desktop(const std::wstring& value, Log& recorder) : name(value), log(recorder) {
        handle = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
            DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_ENUMERATE | DESKTOP_HOOKCONTROL, nullptr);
        check(handle != nullptr, "CreateDesktopW");
        full = user_object_name(GetProcessWindowStation()) + L"\\" + name;
        log.emit("desktop_created", "\"desktop\":" + json(full.c_str()));
    }
    ~Desktop() {
        if (handle) {
            const auto okay = CloseDesktop(handle);
            const auto error = okay ? 0 : GetLastError();
            log.emit("desktop_released", "\"desktop\":" + json(full.c_str()) + ",\"closed\":" + (okay ? "true" : "false") + ",\"error\":" + std::to_string(error));
        }
    }
};
struct HostSample {
    std::uint64_t foreground = 0;
    POINT cursor{};
    DWORD last_input = 0;
    std::wstring desktop;
};
HostSample host_sample() {
    HostSample result;
    result.foreground = reinterpret_cast<std::uint64_t>(GetForegroundWindow());
    check(GetCursorPos(&result.cursor), "GetCursorPos.host");
    LASTINPUTINFO input{sizeof(input)}; check(GetLastInputInfo(&input), "GetLastInputInfo"); result.last_input = input.dwTime;
    const auto desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    check(desk != nullptr, "OpenInputDesktop");
    try { result.desktop = user_object_name(desk); } catch (...) { CloseDesktop(desk); throw; }
    check(CloseDesktop(desk), "CloseDesktop.host_sample");
    return result;
}
class HostMonitor {
    std::atomic<bool> running_{true};
    std::thread worker_;
public:
    unsigned quiet_changes = 0, external_intervals = 0, errors = 0, samples = 0;
    explicit HostMonitor(const std::filesystem::path& output) {
        worker_ = std::thread([this, output] {
            std::ofstream trace(output, std::ios::binary);
            HostSample previous; bool have_previous = false;
            while (running_) {
                try {
                    const auto s = host_sample();
                    const bool changed = have_previous && (s.foreground != previous.foreground || s.desktop != previous.desktop ||
                        s.cursor.x != previous.cursor.x || s.cursor.y != previous.cursor.y);
                    const bool external = have_previous && s.last_input != previous.last_input;
                    if (external) ++external_intervals;
                    if (changed && !external) ++quiet_changes;
                    ++samples;
                    trace << "{\"tickMs\":" << GetTickCount64() << ",\"foreground\":" << s.foreground << ",\"inputDesktop\":" << json(s.desktop.c_str())
                        << ",\"cursorX\":" << s.cursor.x << ",\"cursorY\":" << s.cursor.y << ",\"lastInput\":" << s.last_input
                        << ",\"changed\":" << (changed ? "true" : "false") << ",\"externalInputInterval\":" << (external ? "true" : "false") << "}\n";
                    previous = s; have_previous = true;
                } catch (const std::exception& e) { ++errors; trace << "{\"error\":" << json(e.what()) << "}\n"; }
                trace.flush(); Sleep(50);
            }
        });
    }
    void stop() { running_ = false; if (worker_.joinable()) worker_.join(); }
    ~HostMonitor() { stop(); }
};
struct Instance {
    std::wstring role, state_name, ready_name;
    std::unique_ptr<Desktop> desktop;
    std::unique_ptr<Mapping> state;
    Handle ready;
    Process* process = nullptr;
};
class Experiment {
    std::filesystem::path exe_, output_;
    std::uint64_t serial_ = 0;
    std::wstring prefix_;
    Log log_;
    // Declared before processes: process cleanup happens before desktops close.
    std::vector<std::unique_ptr<Instance>> instances_;
    Processes processes_;
    HostMonitor monitor_;
    std::wstring unique(const std::wstring& suffix) { return prefix_ + L"_" + std::to_wstring(++serial_) + L"_" + suffix; }
public:
    Experiment(const std::filesystem::path& exe, const std::filesystem::path& output)
        : exe_(exe), output_(output), prefix_(L"fv_r3_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64())),
          log_(output / "controller.jsonl"), processes_(log_), monitor_(output / "host.jsonl") {}
    Telemetry observe(Instance& view, const char* label) {
        const auto response = bridge(view, L"observe");
        demand(response.done && !response.error && response.api_result, "read-only target observation failed");
        const auto t = read_shared(*static_cast<Shared*>(view.state->view));
        log_.emit("observation", "\"name\":" + json(label) + ",\"role\":" + json(view.role.c_str()) + ",\"state\":" + telemetry_json(t));
        return t;
    }
    Instance& start(const std::wstring& name, bool fail_process = false) {
        auto instance = std::make_unique<Instance>();
        instance->role = name;
        instance->desktop = std::make_unique<Desktop>(unique(name), log_);
        instance->state_name = L"Local\\" + unique(name + L"_state");
        instance->ready_name = L"Local\\" + unique(name + L"_ready");
        instance->state = std::make_unique<Mapping>(instance->state_name, sizeof(Shared), true);
        new(instance->state->view) Shared{};
        instance->ready.value = CreateEventW(nullptr, TRUE, FALSE, instance->ready_name.c_str());
        check(instance->ready.value != nullptr, "CreateEventW");
        auto& view = *instances_.emplace_back(std::move(instance));
        const auto program = fail_process ? exe_.parent_path() / "deliberately_missing_fixture.exe" : exe_;
        try {
            view.process = &processes_.spawn(program, view.desktop->full, {L"--role", L"fixture", L"--name", name,
                L"--state", view.state_name, L"--ready", view.ready_name,
                L"--log", (output_ / (utf8(name) + "-events.jsonl")).wstring()}, utf8(name) + "-fixture");
            HANDLE waits[]{view.ready.value, view.process->process.value};
            const auto wait = WaitForMultipleObjects(2, waits, FALSE, 10000);
            if (wait != WAIT_OBJECT_0) throw Error("fixture_ready", wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT, "fixture exited or did not signal readiness");
            const auto t = read_shared(*static_cast<Shared*>(view.state->view));
            demand(t.signature == magic && t.pid == view.process->pid && t.tid == view.process->tid && t.desktop == view.desktop->name, "fixture identity/desktop mismatch");
            log_.emit("instance_ready", "\"role\":" + json(name.c_str()) + ",\"state\":" + telemetry_json(t));
            return view;
        } catch (...) {
            if (view.process) processes_.terminate(*view.process);
            instances_.pop_back(); // mapping, readiness event and HDESK owned by this failed instance
            log_.emit("instance_creation_rollback", "\"role\":" + json(name.c_str()) + ",\"ready\":false,\"ownedResourcesReleased\":true");
            throw;
        }
    }
    Reply bridge(Instance& view, const std::wstring& op, const std::wstring& target = L"top", UINT message = 0,
        WPARAM wp = 0, LPARAM lp = 0, const std::filesystem::path& path = {}, DWORD deadline = 4000) {
        const auto reply_name = L"Local\\" + unique(L"reply");
        Mapping response(reply_name, sizeof(Reply), true);
        new(response.view) Reply{};
        const auto reply = static_cast<Reply*>(response.view);
        auto& worker = processes_.spawn(exe_, view.desktop->full, {L"--role",L"bridge",L"--state",view.state_name,L"--reply",reply_name,
            L"--op",op,L"--target",target,L"--message",std::to_wstring(message),L"--wparam",std::to_wstring(wp),
            L"--lparam",std::to_wstring(static_cast<std::uint64_t>(lp)),L"--path",path.wstring()}, utf8(view.role) + "-bridge-" + std::to_string(serial_));
        const auto wait = WaitForSingleObject(worker.process.value, deadline);
        const bool timed_out = wait == WAIT_TIMEOUT;
        if (wait == WAIT_FAILED) throw Error("WaitForSingleObject.bridge", GetLastError());
        if (timed_out) processes_.terminate(worker); else processes_.confirm(worker);
        Reply result;
        std::memcpy(&result, reply, sizeof(result));
        if (timed_out) { result.error = ERROR_TIMEOUT; result.api_result = 0; }
        if (!result.done && !timed_out) { result.error = ERROR_GEN_FAILURE; result.api_result = 0; }
        if (result.error && !path.empty()) {
            // Own fresh output only. Never return or retain a failed frame.
            std::error_code error;
            std::filesystem::remove(path.wstring() + L".partial", error);
            std::filesystem::remove(path, error);
        }
        log_.emit("bridge", "\"role\":" + json(view.role.c_str()) + ",\"operation\":" + json(op.c_str()) + ",\"target\":" + json(target.c_str())
            + ",\"message\":" + std::to_string(message) + ",\"wParam\":" + std::to_string(wp) + ",\"lParam\":" + std::to_string(lp)
            + ",\"api\":" + json(result.api) + ",\"error\":" + std::to_string(result.error) + ",\"apiResult\":" + std::to_string(result.api_result)
            + ",\"timeout\":" + (timed_out ? "true" : "false") + ",\"printCalled\":" + std::to_string(result.print_called)
            + ",\"bridgeDesktop\":" + json(result.desktop));
        return result;
    }
    bool capture(Instance& view, const std::string& name, DWORD deadline = 4000) {
        const auto path = output_ / (name + ".png");
        const auto response = bridge(view, L"capture", L"top", 0, 0, 0, path, deadline);
        const bool usable = response.done && !response.error && response.api_result && std::filesystem::exists(path);
        std::string fields = "\"name\":" + json(name) + ",\"role\":" + json(view.role.c_str()) + ",\"usable\":" + (usable ? "true" : "false")
            + ",\"error\":" + std::to_string(response.error) + ",\"api\":" + json(response.api) + ",\"printCalled\":" + std::to_string(response.print_called)
            + ",\"beforeUpdate\":" + std::to_string(response.before_update) + ",\"afterUpdate\":" + std::to_string(response.after_update)
            + ",\"printUpdate\":" + std::to_string(response.print_update) + ",\"beforePrints\":" + std::to_string(response.before_prints)
            + ",\"afterPrints\":" + std::to_string(response.after_prints) + ",\"width\":" + std::to_string(response.width) + ",\"height\":" + std::to_string(response.height);
        fields += ",\"beforeDraws\":" + std::to_string(response.before_draws) + ",\"afterDraws\":" + std::to_string(response.after_draws)
            + ",\"apiBeginMs\":" + std::to_string(response.api_begin_ms) + ",\"apiEndMs\":" + std::to_string(response.api_end_ms)
            + ",\"clientDx\":" + std::to_string(response.client_dx) + ",\"clientDy\":" + std::to_string(response.client_dy);
        if (usable) fields += ",\"path\":" + json(path.filename().string()) + ",\"sha256\":" + json(native::digest_text(native::digest(native::read_file(path))));
        log_.emit("capture", fields);
        return usable;
    }
    void message(Instance& view, const std::wstring& target, UINT message, WPARAM wp = 0, LPARAM lp = 0, bool send = false) {
        const auto response = bridge(view, send ? L"send" : L"post", target, message, wp, lp);
        demand(response.done && !response.error && response.api_result, "input message delivery failed");
    }
    void key(Instance& view, UINT key, bool down, bool send = false) {
        const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        auto bits = std::uint64_t(1) | (std::uint64_t(scan) << 16);
        if (key == VK_HOME || key == VK_END) bits |= 1ULL << 24;
        if (!down) bits |= 3ULL << 30;
        message(view, L"edit", down ? WM_KEYDOWN : WM_KEYUP, key, static_cast<LPARAM>(bits), send);
    }
    void text(Instance& view, const std::wstring& input) { for (const auto c : input) message(view, L"edit", WM_CHAR, c, 1); }
    void click(Instance& view, const std::wstring& target, int x, int y) {
        message(view, target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x,y));
        message(view, target, WM_LBUTTONUP, 0, MAKELPARAM(x,y));
    }
    void run(DWORD hold_ms, const std::wstring& scenario) {
        auto& a = start(L"A");
        if (scenario == L"capture") {
            Sleep(250);
            observe(a, "capture-probe-before");
            const auto usable = capture(a, "capture-probe");
            observe(a, "capture-probe-after");
            message(a, L"top", WM_CLOSE); processes_.confirm(*a.process);
            monitor_.stop();
            if (!usable) throw Error("capture_probe", ERROR_INVALID_DATA, "PrintWindow evidence did not validate");
            log_.emit("probe_complete", "\"scenario\":\"capture\",\"completed\":true");
            return;
        }
        auto& b = start(L"B");
        if (scenario == L"modifier") {
            const auto before = observe(a, "modifier-before");
            key(a, VK_CONTROL, true);
            const auto down = observe(a, "modifier-down-A"), control = observe(b, "modifier-down-B");
            const bool arrived = down.ctrl_down == before.ctrl_down + 1;
            const bool native_down = (down.key_ctrl & 0x8000) && down.keyboard_ok && (down.keyboard[VK_CONTROL] & 0x80);
            log_.result("posted_ctrl_queue_state", arrived && native_down ? "pass" : "fail", "minimal repro: message arrival versus GUI-thread native Ctrl queries");
            log_.result("B_ctrl_unaffected", control.ctrl_down == 0 && !(control.key_ctrl & 0x8000) ? "pass" : "fail", "minimal repro control instance");
            key(a, VK_CONTROL, false);
            message(a,L"top",WM_CLOSE); message(b,L"top",WM_CLOSE);
            processes_.confirm(*a.process); processes_.confirm(*b.process);
            monitor_.stop();
            log_.emit("probe_complete", "\"scenario\":\"modifier\",\"completed\":true");
            return;
        }
        if (scenario == L"capture-recovery") {
            Sleep(250);
            capture(a,"before-hang");
            if (SuspendThread(a.process->thread.value) == DWORD(-1)) throw Error("SuspendThread.owned_GUI", GetLastError());
            bool resumed = false;
            try {
                capture(a,"hung-no-frame",1500); capture(b,"B-while-A-hung");
                if (ResumeThread(a.process->thread.value) == DWORD(-1)) throw Error("ResumeThread.owned_GUI", GetLastError());
                resumed = true;
            } catch (...) { if (!resumed) ResumeThread(a.process->thread.value); throw; }
            Sleep(300); observe(a,"after-resume");
            const auto accepted = capture(a,"after-resume");
            log_.result("A_capture_after_capture_timeout", accepted ? "pass" : "fail", "minimal capture-cancellation/recovery path; retains strict paint/tick validation");
            message(a,L"top",WM_CLOSE); message(b,L"top",WM_CLOSE);
            processes_.confirm(*a.process); processes_.confirm(*b.process); monitor_.stop();
            log_.emit("probe_complete", "\"scenario\":\"capture-recovery\",\"completed\":true");
            return;
        }
        log_.result("background_windows", "pass", "two native GUI processes on distinct non-input desktops; cold-start only");
        Sleep(500);
        const auto a0 = observe(a, "no-preview-start-A"), b0 = observe(b, "no-preview-start-B");
        const bool first_a = capture(a, "A-initial"), first_b = capture(b, "B-initial");
        Sleep(650);
        const auto a1 = observe(a, "no-preview-later-A"), b1 = observe(b, "no-preview-later-B");
        const bool later_a = capture(a, "A-later"), later_b = capture(b, "B-later");
        const bool progress = a1.update > a0.update && b1.update > b0.update;
        log_.result("no_preview_execution", progress ? "pass" : "fail", "target GUI timers advanced without any preview");
        log_.result("gdi_printwindow", first_a && first_b && later_a && later_b && progress ? "pass" : "fail", "requires PNG visual tick plus matching target paint/update observations; not a DWM/GPU capability claim");

        click(a, L"edit", 10, 12); text(a, L"Alpha");
        const auto typed_a = observe(a, "typed-A"), unchanged_b = observe(b, "B-after-A-text");
        text(b, L"beta");
        const auto typed_b = observe(b, "typed-B");
        log_.result("directed_char_messages", std::wstring(typed_a.text) == L"Alpha" && std::wstring(unchanged_b.text).empty() && std::wstring(typed_b.text) == L"beta" ? "pass" : "fail",
            "WM_CHAR to native EDIT only; not a full keyboard-input capability");
        const auto clicks_before = typed_a.clicks;
        click(a, L"button", 40, 16);
        const auto clicked = observe(a, "button-A");
        log_.result("directed_button_messages", clicked.clicks == clicks_before + 1 ? "pass" : "fail", "native button generated BN_CLICKED from directed mouse messages");
        click(a, L"edit", 10, 12);
        const auto before_ctrl = observe(a, "before-Ctrl");
        key(a, VK_CONTROL, true);
        const auto ctrl_a = observe(a, "Ctrl-down-A"), ctrl_b = observe(b, "Ctrl-down-B-control");
        const bool ctrl_delivered = ctrl_a.ctrl_down == before_ctrl.ctrl_down + 1;
        const bool queue_down = (ctrl_a.key_ctrl & 0x8000) && ctrl_a.keyboard_ok && (ctrl_a.keyboard[VK_CONTROL] & 0x80);
        log_.result("posted_ctrl_queue_state", ctrl_delivered && queue_down ? "pass" : "fail", "WM_KEYDOWN arrival compared with target GetKeyState/GetKeyboardState high bits");
        log_.result("background_async_ctrl", ctrl_delivered && (ctrl_a.async_ctrl & 0x8000) ? "pass" : "fail", "inactive-desktop GetAsyncKeyState limitation; zero alone does not distinguish key-up from API restriction");
        log_.result("B_ctrl_unaffected", ctrl_b.ctrl_down == 0 && !(ctrl_b.key_ctrl & 0x8000) && !(ctrl_b.keyboard[VK_CONTROL] & 0x80) ? "pass" : "fail", "B did not receive A's directed modifier message");
        key(a, VK_CONTROL, false);
        key(a, VK_CONTROL, true, true); observe(a, "SendMessage-Ctrl-down-A"); key(a, VK_CONTROL, false, true);

        key(a, VK_HOME, true); key(a, VK_HOME, false);
        key(a, VK_SHIFT, true); key(a, VK_END, true); key(a, VK_END, false); key(a, VK_SHIFT, false);
        const auto selection = observe(a, "Shift-End-selection-A");
        log_.result("keyboard_shift_selection", selection.selection_start != selection.selection_end ? "pass" : "fail", "native EDIT selection after posted Home/Shift/End messages");
        message(a, L"edit", WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5,12));
        message(a, L"edit", WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(45,12));
        message(a, L"edit", WM_LBUTTONUP, 0, MAKELPARAM(45,12));
        const auto mouse_selection = observe(a, "directed-mouse-selection-A");
        log_.result("directed_mouse_selection", mouse_selection.selection_start != mouse_selection.selection_end ? "pass" : "fail", "explicitly addressed native EDIT mouse-message path only");

        message(a, L"drag", WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40,40));
        message(a, L"drag", WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100,60));
        const auto drag_before_b = observe(a, "drag-A-held");
        click(b, L"button", 40,16); text(b,L"X");
        const auto drag_after_b = observe(a, "drag-A-after-B"), b_after = observe(b,"B-during-A-drag");
        const bool capture_held = drag_before_b.capture == drag_before_b.drag && drag_after_b.capture == drag_after_b.drag;
        const bool stable = drag_after_b.drag_x == drag_before_b.drag_x && drag_after_b.drag_y == drag_before_b.drag_y;
        log_.result("capture_query_across_B_messages", capture_held && stable && b_after.clicks > typed_b.clicks ? "pass" : "fail", "GetCapture observed in A GUI thread before and after B interaction; routing unproven");
        log_.result("focus_query_across_B_messages", drag_before_b.focus == drag_after_b.focus && b_after.focus != 0 ? "pass" : "fail", "target GUI-thread GetFocus values; no controller-thread focus inference");
        log_.result("native_mouse_button_state", (drag_before_b.key_left & 0x8000) && (drag_before_b.keyboard[VK_LBUTTON] & 0x80) &&
            (drag_before_b.async_left & 0x8000) ? "pass" : "fail", "MK_LBUTTON message flag compared with target native button-state queries");
        message(a, L"drag", WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(700,220));
        observe(a, "explicitly-addressed-outside-drag");
        log_.result("native_cross_boundary_capture_routing", "not_verified", "outside move was explicitly sent to drag HWND; it cannot prove OS capture routing without real isolated input");
        message(a, L"drag", WM_LBUTTONUP, 0, MAKELPARAM(700,220));
        observe(a, "drag-released-A");
        capture(a, "A-after-input"); capture(b, "B-after-input");

        // B creation failure is injected in a third temporary instance. The two
        // existing instances must keep updating. Only owned resources are touched.
        const auto before_failure = observe(b, "before-create-failure-B");
        bool creation_rejected = false;
        try { start(L"failed-instance", true); }
        catch (const Error& e) {
            creation_rejected = e.api == "CreateProcessW" && e.code == ERROR_FILE_NOT_FOUND;
            log_.emit("expected_creation_failure", "\"api\":" + json(e.api) + ",\"error\":" + std::to_string(e.code));
            if (!creation_rejected) throw;
        }
        Sleep(200);
        const auto after_failure = observe(b, "after-create-failure-B");
        log_.result("creation_failure_isolation", creation_rejected && after_failure.update > before_failure.update && capture(b,"B-after-create-failure") ? "pass" : "fail",
            "failed new process was never published; B retained native execution and drawing");

        const auto old_a = observe(a,"before-hang-A");
        const auto previous_suspend = SuspendThread(a.process->thread.value);
        if (previous_suspend == DWORD(-1)) throw Error("SuspendThread.owned_GUI", GetLastError());
        bool resumed = false;
        try {
            const bool bad_frame = capture(a,"A-hung-must-not-exist",1500);
            const auto during_hang_b = observe(b,"B-while-A-hung");
            const bool b_capture = capture(b,"B-while-A-hung");
            log_.result("hung_target_failure", !bad_frame ? "pass" : "fail", "blocked/failed PrintWindow must publish no image; auxiliary process bounded by deadline");
            log_.result("hung_A_does_not_block_B", during_hang_b.update > after_failure.update && b_capture ? "pass" : "fail", "B observed and captured while A GUI thread was suspended");
            if (ResumeThread(a.process->thread.value) == DWORD(-1)) throw Error("ResumeThread.owned_GUI", GetLastError());
            resumed = true;
        } catch (...) { if (!resumed) ResumeThread(a.process->thread.value); throw; }
        Sleep(250);
        const auto recovered = observe(a,"after-resume-A");
        log_.result("A_execution_after_capture_timeout", recovered.update > old_a.update ? "pass" : "fail", "target GUI execution resumes after auxiliary process cleanup");
        log_.result("A_capture_after_capture_timeout", capture(a,"A-after-resume") ? "pass" : "fail", "requires fresh paint during capture as well as matching visual tick; an unconfirmed frame is rejected");

        log_.result("manual_host_interaction", hold_ms ? "not_verified" : "not_executed", "automated run does not substitute for a person's host-side judgment; see README procedure");
        if (hold_ms) {
            std::cout << "Holding background instances; directed inputs continue while host remains available...\n";
            const auto until = GetTickCount64() + hold_ms;
            while (GetTickCount64() < until) {
                text(b,L".");
                message(a,L"drag",WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,40));
                message(a,L"drag",WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(100,60));
                message(a,L"drag",WM_LBUTTONUP,0,MAKELPARAM(100,60));
                Sleep(250);
            }
        }
        message(a,L"top",WM_CLOSE);
        processes_.confirm(*a.process);
        const bool stale = capture(a,"A-exited-must-not-exist");
        log_.result("exited_target_failure", !stale ? "pass" : "fail", "stale HWND rejected; no previous image returned");
        const auto last_b = observe(b,"B-after-A-exit");
        log_.result("exit_A_does_not_stop_B", last_b.update > b0.update && capture(b,"B-after-A-exit") ? "pass" : "fail", "B remains independently observable and drawable");
        message(b,L"top",WM_CLOSE); processes_.confirm(*b.process);
        processes_.stop_all();
        monitor_.stop();
        log_.emit("host_result", "\"samples\":" + std::to_string(monitor_.samples) + ",\"quietChanges\":" + std::to_string(monitor_.quiet_changes)
            + ",\"externalInputIntervals\":" + std::to_string(monitor_.external_intervals) + ",\"errors\":" + std::to_string(monitor_.errors));
        const auto host_status = monitor_.errors || monitor_.quiet_changes ? "inconclusive" : monitor_.external_intervals ? "inconclusive" : "pass";
        log_.result("static_host_observation", host_status, "50ms foreground/input-desktop/cursor samples; external input intervals are not counted as runtime interference");
        log_.emit("diagnostic_complete", "\"completed\":true,\"guiSnapshotRestore\":false,\"R3Complete\":false");
        std::cout << "R3_001_DIAGNOSTIC_COMPLETED (capability failures remain failures)\n";
    }
};
}
int controller_main(const Args& args) {
    const auto output = std::filesystem::absolute(args.get(L"--output"));
    demand(!args.get(L"--output").empty() && !std::filesystem::exists(output), "output must be a fresh directory");
    std::filesystem::create_directories(output);
    wchar_t module[32768]{};
    const auto length = GetModuleFileNameW(nullptr, module, 32768);
    demand(length > 0 && length < 32768, "executable path unavailable");
    Experiment experiment(module, output);
    const auto hold = args.number(L"--hold-ms");
    demand(hold <= 300000, "hold time limited to 5 minutes");
    const auto scenario = args.number(L"--capture-probe") ? L"capture" : args.get(L"--scenario", L"all");
    demand(scenario == L"all" || scenario == L"capture" || scenario == L"modifier" || scenario == L"capture-recovery", "unknown scenario");
    experiment.run(static_cast<DWORD>(hold), scenario);
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    try {
        const r3::Args args(argc,argv);
        const auto role = args.get(L"--role", L"controller");
        if (role == L"fixture") return r3::fixture_main(args);
        if (role == L"bridge") return r3::bridge_main(args);
        r3::demand(role == L"controller", "unknown process role");
        return r3::controller_main(args);
    } catch (const r3::Error& e) {
        std::cout << "R3_FAILURE api=" << e.api << " code=" << e.code << " detail=" << e.what() << '\n'; return 1;
    } catch (const std::exception& e) {
        std::cout << "R3_FAILURE api=cpp_exception code=0 detail=" << e.what() << '\n'; return 1;
    }
}
