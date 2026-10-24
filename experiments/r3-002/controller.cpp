#include "channel.hpp"
// Lifecycle/host sampling helpers are a frozen local copy of R3-001. Existing
// experiment sources and shared tools remain untouched by this diagnostic.
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
    SHORT ctrl = 0, shift = 0;
    std::wstring desktop;
};
HostSample host_sample() {
    HostSample result;
    result.foreground = reinterpret_cast<std::uint64_t>(GetForegroundWindow());
    result.ctrl = GetAsyncKeyState(VK_CONTROL) & static_cast<SHORT>(0x8000);
    result.shift = GetAsyncKeyState(VK_SHIFT) & static_cast<SHORT>(0x8000);
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
                        s.cursor.x != previous.cursor.x || s.cursor.y != previous.cursor.y || s.ctrl != previous.ctrl || s.shift != previous.shift);
                    const bool external = have_previous && s.last_input != previous.last_input;
                    if (external) ++external_intervals;
                    if (changed && !external) ++quiet_changes;
                    ++samples;
                    trace << "{\"tickMs\":" << GetTickCount64() << ",\"foreground\":" << s.foreground << ",\"inputDesktop\":" << json(s.desktop.c_str())
                        << ",\"cursorX\":" << s.cursor.x << ",\"cursorY\":" << s.cursor.y << ",\"lastInput\":" << s.last_input
                        << ",\"ctrlHigh\":" << s.ctrl << ",\"shiftHigh\":" << s.shift
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

using namespace r302;
struct View {
    std::wstring name, state_name;
    std::unique_ptr<Desktop> desktop;
    std::unique_ptr<Mapping> state, input;
    Process* fixture = nullptr;
    Process* bridge = nullptr;
    std::uint64_t sequence = 0;
    LONG request = 0;
    Channel& channel() { return *static_cast<Channel*>(input->view); }
    Telemetry state_now() { return read_shared(*static_cast<Shared*>(state->view)); }
};
Packet press(unsigned vk, bool down, bool skip = false) {
    Packet p{}; p.kind = key; p.vk = vk; p.down = down; p.skip_state = skip; return p;
}
class Experiment {
    std::filesystem::path exe_, output_;
    std::wstring prefix_;
    Log log_;
    std::vector<std::unique_ptr<View>> views_;
    Processes processes_;
    HostMonitor monitor_;
    unsigned serial_ = 0, failures_ = 0;
public:
    Experiment(const std::filesystem::path& exe, const std::filesystem::path& output)
        : exe_(exe), output_(output), prefix_(L"fv_r302_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64())),
          log_(output / "controller.jsonl"), processes_(log_), monitor_(output / "host.jsonl") {}
    void verify(const std::string& name, bool okay, const std::string& detail) {
        if (!okay) ++failures_;
        log_.emit("assertion", "\"name\":" + json(name) + ",\"passed\":" + (okay ? "true" : "false") + ",\"detail\":" + json(detail));
        std::cout << "assertion " << name << ": " << (okay ? "pass" : "fail") << " (" << detail << ")\n";
    }
    View& start(const std::wstring& name, bool enabled) {
        auto item = std::make_unique<View>(); item->name = name;
        item->desktop = std::make_unique<Desktop>(prefix_ + L"_" + name, log_);
        item->state_name = L"Local\\" + prefix_ + L"_" + name;
        item->state = std::make_unique<Mapping>(item->state_name, sizeof(Shared), true);
        new(item->state->view) Shared{};
        const auto ready_name = item->state_name + L"_ready";
        Handle ready(CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str())); check(ready.value != nullptr, "CreateEventW.fixture_ready");
        auto& v = *views_.emplace_back(std::move(item));
        v.fixture = &processes_.spawn(exe_, v.desktop->full, {L"--role",L"fixture",L"--name",name,L"--state",v.state_name,
            L"--ready",ready_name,L"--log",(output_ / (utf8(name) + "-events.jsonl")).wstring()}, utf8(name) + "-fixture");
        HANDLE waits[]{ready.value, v.fixture->process.value};
        if (WaitForMultipleObjects(2, waits, FALSE, 10000) != WAIT_OBJECT_0) throw Error("fixture_ready", ERROR_TIMEOUT);
        const auto t = v.state_now();
        demand(t.pid == v.fixture->pid && t.tid == v.fixture->tid && t.desktop == v.desktop->name, "owned fixture identity");
        v.input = std::make_unique<Mapping>(channel_name(t.pid,t.tid), sizeof(Channel), true);
        new(v.input->view) Channel{};
        auto& c = v.channel(); c.pid = t.pid; c.tid = t.tid; c.edit = t.edit; c.top = t.top; c.enabled = enabled;
        c.token = (GetTickCount64() << 16) ^ GetCurrentProcessId() ^ t.pid;
        v.bridge = &processes_.spawn(exe_, v.desktop->full, {L"--role",L"input",L"--channel",channel_name(t.pid,t.tid),
            L"--dll",(exe_.parent_path() / L"runtime.dll").wstring(),L"--log",(output_ / (utf8(name) + "-input.jsonl")).wstring()}, utf8(name) + "-input");
        const auto deadline = GetTickCount64() + 5000;
        while (!c.ready && GetTickCount64() < deadline && WaitForSingleObject(v.bridge->process.value,0) == WAIT_TIMEOUT) Sleep(2);
        demand(c.ready == 1 && c.health == 1, "persistent input bridge initialization");
        log_.emit("instance_ready", "\"name\":" + json(name.c_str()) + ",\"enabled\":" + (enabled ? "true" : "false") + ",\"state\":" + telemetry_json(t));
        return v;
    }
    bool submit(View& v, std::vector<Packet> packets, unsigned command = 1, bool expected_failure = false) {
        auto& c = v.channel();
        demand(c.health == 1 && c.command_done == v.request && packets.size() + 1 <= capacity, "submit idle healthy channel");
        Packet end{}; end.kind = barrier; packets.push_back(end);
        c.count = static_cast<unsigned>(packets.size()); c.command = command;
        for (unsigned i = 0; i < c.count; ++i) { packets[i].sequence = ++v.sequence; c.packets[i] = packets[i]; }
        MemoryBarrier(); InterlockedExchange(&c.request, ++v.request);
        const auto deadline = GetTickCount64() + 6000;
        while (c.command_done != v.request && c.health == 1 && GetTickCount64() < deadline) Sleep(2);
        const bool okay = c.command_done == v.request && c.health != -1;
        log_.emit("batch", "\"name\":" + json(v.name.c_str()) + ",\"request\":" + std::to_string(v.request) +
            ",\"firstSequence\":" + std::to_string(packets.front().sequence) + ",\"count\":" + std::to_string(c.count) +
            ",\"lastSequence\":" + std::to_string(v.sequence) + ",\"processed\":" + (okay ? "true" : "false") +
            ",\"health\":" + std::to_string(c.health) + ",\"error\":" + std::to_string(c.error) + ",\"errorApi\":" + json(c.error_api));
        if (!okay && !expected_failure) throw Error("input_batch", c.error ? static_cast<DWORD>(c.error) : ERROR_TIMEOUT);
        return okay;
    }
    Telemetry observe(View& v, const char* name) {
        submit(v, {});
        const auto t = v.state_now();
        log_.emit("observation", "\"name\":" + json(name) + ",\"view\":" + json(v.name.c_str()) + ",\"state\":" + telemetry_json(t));
        return t;
    }
    void seed(View& v) {
        Packet down{}; down.kind = mouse; down.message = WM_LBUTTONDOWN; down.wp = MK_LBUTTON; down.lp = MAKELPARAM(12,12);
        auto up = down; up.message = WM_LBUTTONUP; up.wp = 0;
        std::vector<Packet> packets{down,up};
        for (const auto ch : std::wstring(L"alpha")) { Packet p{}; p.kind = character; p.wp = ch; packets.push_back(p); }
        submit(v, packets);
        const auto t = observe(v,"seeded-through-native-messages");
        verify(utf8(v.name) + ".native-edit-seeded", std::wstring(t.text) == L"alpha" && t.focus == t.edit, "WM_CHAR and native mouse focus; no text/selection setter");
    }
    static bool ctrl(const Telemetry& t) { return (t.key_ctrl & 0x8000) && t.keyboard_ok && (t.keyboard[VK_CONTROL] & 0x80); }
    static bool shift(const Telemetry& t) { return (t.key_shift & 0x8000) && t.keyboard_ok && (t.keyboard[VK_SHIFT] & 0x80); }
    static bool clear(const Telemetry& t) {
        return t.keyboard_ok && !(t.key_ctrl & 0x8000) && !(t.key_shift & 0x8000) &&
            !(t.keyboard[VK_CONTROL] & 0x80) && !(t.keyboard[VK_SHIFT] & 0x80) &&
            !(t.keyboard[VK_LCONTROL] & 0x80) && !(t.keyboard[VK_RCONTROL] & 0x80) &&
            !(t.keyboard[VK_LSHIFT] & 0x80) && !(t.keyboard[VK_RSHIFT] & 0x80);
    }
    void release_keys(View& v, bool stop = false) {
        Packet p{}; p.kind = release; submit(v,{p},stop ? 2 : 1);
        if (stop) {
            processes_.confirm(*v.bridge);
            verify(utf8(v.name) + ".closed-cleanly", v.channel().ready == 2 && v.channel().health == 0 && !v.channel().error && clear(v.state_now()), "native generic/side high bits clear before unhook; bridge process exited");
        }
    }
    void capture(View& v, const std::wstring& name) {
        const auto reply_name = L"Local\\" + prefix_ + L"_capture_" + std::to_wstring(++serial_);
        Mapping mapping(reply_name, sizeof(Reply), true); auto* reply = new(mapping.view) Reply{};
        const auto path = output_ / (name + L".png");
        auto& helper = processes_.spawn(exe_, v.desktop->full, {L"--role",L"bridge",L"--state",v.state_name,L"--reply",reply_name,
            L"--op",L"capture",L"--target",L"top",L"--path",path.wstring()}, utf8(name) + "-capture");
        if (WaitForSingleObject(helper.process.value,4000) != WAIT_OBJECT_0) { processes_.terminate(helper); throw Error("capture_timeout", ERROR_TIMEOUT); }
        processes_.confirm(helper);
        log_.emit("capture", "\"name\":" + json(name.c_str()) + ",\"error\":" + std::to_string(reply->error) +
            ",\"beforeDraws\":" + std::to_string(reply->before_draws) + ",\"afterDraws\":" + std::to_string(reply->after_draws) +
            ",\"apiBeginMs\":" + std::to_string(reply->api_begin_ms) + ",\"apiEndMs\":" + std::to_string(reply->api_end_ms) +
            ",\"tick\":" + std::to_string(reply->print_update) + ",\"clientDx\":" + std::to_string(reply->client_dx) + ",\"clientDy\":" + std::to_string(reply->client_dy));
        verify(utf8(name) + ".fresh-target-paint", reply->done && !reply->error && reply->after_draws > reply->before_draws && std::filesystem::exists(path), "unchanged R3-001 PrintWindow helper and pixel/paint validator");
    }
    void selection(View& v, bool expected) {
        submit(v,{press(VK_HOME,true),press(VK_HOME,false)});
        const auto home = observe(v,"Home");
        verify(utf8(v.name) + ".Home-at-start", home.selection_start == 0 && home.selection_end == 0, "native EDIT processed Home");
        submit(v,{press(VK_LSHIFT,true),press(VK_END,true),press(VK_END,false),press(VK_LSHIFT,false)});
        const auto t = observe(v,"Shift-End");
        const DWORD expected_length = v.name == L"B" ? 6 : 5;
        const bool selected = t.selection_start == 0 && t.selection_end == expected_length;
        log_.result(utf8(v.name) + ".native_shift_selection", selected ? "pass" : "fail", "actual native EDIT selection=" +
            std::to_string(t.selection_start) + ".." + std::to_string(t.selection_end));
        verify(utf8(v.name) + (expected ? ".native-Shift-End-selection" : ".baseline-selection-failure-retained"), expected ? selected : !selected,
            "actual native EDIT selection=" + std::to_string(t.selection_start) + ".." + std::to_string(t.selection_end));
        capture(v,v.name + L"-selection");
    }
    int run(const std::wstring& scenario, DWORD hold_ms) {
        auto& baseline = start(L"baseline",false); seed(baseline);
        submit(baseline,{press(VK_LCONTROL,true)});
        const auto off = observe(baseline,"baseline-Ctrl-down");
        log_.result("baseline.queue_ctrl_consistency", ctrl(off) ? "pass" : "fail", "state intervention disabled; same fixture and envelope transport");
        verify("baseline.message-without-queue-state", off.ctrl_down > 0 && !ctrl(off), "same fixture; same envelope transport; SetKeyboardState disabled");
        submit(baseline,{press(VK_LCONTROL,false)});
        if (scenario != L"modifier") selection(baseline,false);
        release_keys(baseline,true);

        // Fully unhooked control: the original R3-001 disposable bridge posts a
        // plain key message. Wait for target evidence, not a posting return value.
        const auto before_plain = baseline.state_now().ctrl_down;
        const auto plain_reply_name = L"Local\\" + prefix_ + L"_plain";
        Mapping plain_mapping(plain_reply_name, sizeof(Reply), true); auto* plain_reply = new(plain_mapping.view) Reply{};
        auto& plain = processes_.spawn(exe_, baseline.desktop->full, {L"--role",L"bridge",L"--state",baseline.state_name,
            L"--reply",plain_reply_name,L"--op",L"post",L"--target",L"edit",L"--message",std::to_wstring(WM_KEYDOWN),
            L"--wparam",std::to_wstring(VK_CONTROL),L"--lparam",std::to_wstring(1 | (0x1D << 16))}, "baseline-unhooked-post");
        processes_.confirm(plain);
        const auto plain_deadline = GetTickCount64() + 2000;
        while (baseline.state_now().ctrl_down == before_plain && GetTickCount64() < plain_deadline) Sleep(2);
        const auto unhooked = baseline.state_now();
        log_.result("unhooked.queue_ctrl_consistency", ctrl(unhooked) ? "pass" : "fail", "plain native PostMessage after runtime input hook removed");
        log_.emit("observation", "\"name\":\"fully-unhooked-Ctrl-down\",\"view\":\"baseline\",\"state\":" + telemetry_json(unhooked));
        verify("baseline.fully-unhooked-failure-retained", plain_reply->done && !plain_reply->error &&
            unhooked.ctrl_down == before_plain + 1 && !ctrl(unhooked), "plain R3-001 PostMessage after hook and input bridge removed");

        auto& a = start(L"A",true); auto& b = start(L"B",true); seed(a); seed(b);
        submit(a,{press(VK_LCONTROL,true)});
        const auto on = observe(a,"managed-Ctrl-down"); const auto b0 = observe(b,"B-while-A-Ctrl-held");
        log_.result("managed.queue_ctrl_consistency", ctrl(on) ? "pass" : "fail", "original GetKeyState and GetKeyboardState; target GUI thread");
        verify("managed.Ctrl-native-queries", ctrl(on) && on.ctrl_down > 0, "GetKeyState and GetKeyboardState read actual target thread table");
        verify("managed.B-isolated", !ctrl(b0) && b0.ctrl_down == 0 && std::wstring(b0.text) == L"alpha", "B has independent native table and EDIT");
        submit(b,{press(VK_HOME,true),press(VK_HOME,false),press(VK_END,true),press(VK_END,false)});
        const auto b_interleaved = observe(b,"B-operated-while-A-held");
        verify("managed.interleaved-B-does-not-release-A", !ctrl(b_interleaved) && b_interleaved.selection_start == 5 &&
            b_interleaved.selection_end == 5 && ctrl(observe(a,"A-still-held-after-B-input")), "B native navigation completes while A Ctrl remains held");
        submit(b,{press('X',true),press('X',false)});
        // TranslateMessage may append WM_CHAR after our posted barrier. The batch
        // ack confirms key dispatch; independently wait for the actual EDIT text.
        const auto character_deadline = GetTickCount64() + 2000;
        while (std::wstring(b.state_now().text) == L"alpha" && GetTickCount64() < character_deadline) Sleep(2);
        const auto typed = observe(b,"B-native-X-key-produced-character");
        verify("managed.normal-key-TranslateMessage", std::wstring(typed.text) == L"alphax" && !ctrl(typed) &&
            ctrl(observe(a,"A-still-held-after-B-character")), "B X down/up generated native WM_CHAR through unchanged TranslateMessage; A Ctrl still held");
        if (hold_ms) {
            std::cout << "MANUAL_HOLD: use your real desktop; A holds private Ctrl while B navigates. Human verdict must be recorded separately.\n";
            const auto hold_deadline = GetTickCount64() + hold_ms;
            while (GetTickCount64() < hold_deadline) {
                submit(b,{press(VK_HOME,true),press(VK_HOME,false),press(VK_END,true),press(VK_END,false)});
                Sleep(100);
            }
            verify("managed.hold-keeps-A-and-B-independent", ctrl(observe(a,"A-after-host-interaction-hold")) &&
                !ctrl(observe(b,"B-after-host-interaction-hold")), "automatic target-state check only; not a human host-interaction verdict");
        }
        submit(a,{press(VK_RCONTROL,true),press(VK_LCONTROL,false)});
        const auto right = observe(a,"right-Ctrl-still-held");
        verify("managed.Ctrl-side-OR", ctrl(right) && !(right.keyboard[VK_LCONTROL] & 0x80) && (right.keyboard[VK_RCONTROL] & 0x80), "left release preserves held right and generic Ctrl");
        submit(a,{press(VK_RCONTROL,false)});
        verify("managed.Ctrl-up-clears", clear(observe(a,"managed-Ctrl-up")), "both sides released");
        submit(a,{press(VK_LSHIFT,true),press(VK_RSHIFT,true),press(VK_LSHIFT,false)});
        const auto rs = observe(a,"right-Shift-still-held");
        verify("managed.Shift-side-OR", shift(rs) && !(rs.keyboard[VK_LSHIFT] & 0x80) && (rs.keyboard[VK_RSHIFT] & 0x80), "generic Shift remains held");
        submit(a,{press(VK_RSHIFT,false)});
        verify("managed.Shift-up-clears", clear(observe(a,"managed-Shift-up")), "no remaining Shift");

        submit(a,{press(VK_LCONTROL,true,true)});
        const auto skipped = observe(a,"skipped-Ctrl-state-update");
        log_.result("injected_missing_update.queue_ctrl_consistency", ctrl(skipped) ? "pass" : "fail", "intentionally omitted SetKeyboardState for this message");
        verify("negative.omitted-state-update-detected", !ctrl(skipped), "message alone retains original mismatch; this is an injected capability failure");
        release_keys(a);
        const auto trace_begin = a.channel().trace_count;
        submit(a,{press(VK_LCONTROL,true),press(VK_LCONTROL,false),press(VK_LCONTROL,true),press(VK_LCONTROL,false)});
        unsigned keys = 0, peeks = 0; bool correct = true;
        for (LONG i = trace_begin; i < a.channel().trace_count; ++i) {
            const auto& t = a.channel().traces[i];
            if (!t.removed) { ++peeks; correct = correct && !t.applied; }
            if (t.removed && t.kind == key) { ++keys; correct = correct && ((t.ctrl & 0x8000) != 0) == bool(t.down) && ((t.keyboard_ctrl & 0x80) != 0) == bool(t.down); }
        }
        verify("managed.burst-dequeue-state", keys == 4 && correct, "four events in one batch; each consumed query follows that event");
        verify("managed.repeated-peek-no-apply", peeks >= 2 && correct, "next envelope viewed twice using PM_NOREMOVE; not consumed or applied");
        if (scenario != L"modifier") { selection(a,true); selection(b,true); }
        submit(a,{press(VK_LCONTROL,true)}); release_keys(a);
        verify("managed.cancel-releases", clear(observe(a,"after-input-cancel")), "release packet runs on target thread; channel remains usable");
        submit(a,{press(VK_LCONTROL,true),press(VK_LCONTROL,false)});
        verify("managed.channel-continues-after-cancel", !ctrl(observe(a,"after-cancel-next-input")), "subsequent sequence consumed");

        // Deliberately prevent cleanup acknowledgement on A. Its input bridge must
        // invalidate, never call this a healthy unload. B must continue independently.
        if (scenario != L"modifier") {
            submit(a,{press(VK_LCONTROL,true)});
            const auto prior = SuspendThread(a.fixture->thread.value);
            if (prior == DWORD(-1)) throw Error("SuspendThread.owned_fixture",GetLastError());
            bool failed = false;
            try { Packet p{}; p.kind = release; failed = !submit(a,{p},2,true); }
            catch (...) { ResumeThread(a.fixture->thread.value); throw; }
            if (ResumeThread(a.fixture->thread.value) == DWORD(-1)) throw Error("ResumeThread.owned_fixture",GetLastError());
            processes_.confirm(*a.bridge);
            log_.result("injected_cleanup_timeout.release_confirmed", failed ? "fail" : "pass", "A GUI thread intentionally suspended; cleanup not acknowledged; channel invalid");
            verify("negative.unconfirmed-release-invalidates", failed && a.channel().health == -1 && a.channel().error == ERROR_TIMEOUT, "owned A suspended; no target ack; channel invalid instead of healthy");
            submit(b,{press(VK_LCONTROL,true),press(VK_LCONTROL,false)});
            verify("managed.B-continues-after-A-invalid", !ctrl(observe(b,"B-after-A-input-failure")), "B independently processes new input");
        } else release_keys(a,true);
        submit(b,{press(VK_LCONTROL,true),press(VK_LSHIFT,true)});
        const auto held_at_unload = observe(b,"B-held-before-normal-unload");
        verify("managed.unload-has-held-modifiers", ctrl(held_at_unload) && shift(held_at_unload), "Ctrl and Shift actually held before normal cleanup");
        release_keys(b,true);
        processes_.stop_all(); monitor_.stop();
        log_.emit("host_result", "\"samples\":" + std::to_string(monitor_.samples) + ",\"quietChanges\":" + std::to_string(monitor_.quiet_changes) +
            ",\"externalInputIntervals\":" + std::to_string(monitor_.external_intervals) + ",\"errors\":" + std::to_string(monitor_.errors));
        log_.result("static_host_observation", monitor_.errors || monitor_.quiet_changes || monitor_.external_intervals ? "inconclusive" : "pass", "50ms samples; external user activity is not runtime interference");
        log_.result("manual_host_interaction", hold_ms ? "not_verified" : "not_executed", "hold mode alone is not a human verdict; static samples do not replace it");
        log_.result("async_private_state", "not_supported", "original GetAsyncKeyState recorded without replacement");
        log_.emit("experiment_complete", "\"failedAssertions\":" + std::to_string(failures_) + ",\"R3Complete\":false,\"guiRestore\":false");
        return failures_ ? 2 : 0;
    }
};
}
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    try {
        const r3::Args args(argc,argv);
        const auto role = args.get(L"--role",L"controller");
        if (role == L"fixture") return r3::fixture_main(args);
        if (role == L"bridge") return r3::bridge_main(args);
        if (role == L"input") return r302::input_main(args);
        r3::demand(role == L"controller", "unknown role");
        const auto output = std::filesystem::absolute(args.get(L"--output"));
        r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output), "output must be new");
        std::filesystem::create_directories(output);
        wchar_t path[32768]{}; r3::check(GetModuleFileNameW(nullptr,path,32768) != 0, "GetModuleFileNameW");
        const auto scenario = args.get(L"--scenario",L"all");
        r3::demand(scenario == L"all" || scenario == L"modifier", "unknown scenario");
        const auto hold = args.number(L"--hold-ms");
        r3::demand(hold <= 30000, "hold time limited to 30 seconds by bounded diagnostic trace storage");
        r3::Experiment experiment(path,output); return experiment.run(scenario,static_cast<DWORD>(hold));
    } catch (const std::exception& e) { std::cout << "R3_002_FAILURE " << e.what() << '\n'; return 1; }
}
