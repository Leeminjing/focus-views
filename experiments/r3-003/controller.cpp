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

using namespace r303;
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
    unsigned failures_ = 0;
public:
    Experiment(const std::filesystem::path& exe, const std::filesystem::path& output)
        : exe_(exe), output_(output), prefix_(L"fv_r303_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64())),
          log_(output / "controller.jsonl"), processes_(log_), monitor_(output / "host.jsonl") {}
    void verify(const std::string& name, bool okay, const std::string& detail) {
        if (!okay) ++failures_;
        log_.emit("assertion", "\"name\":" + json(name) + ",\"passed\":" + (okay ? "true" : "false") + ",\"detail\":" + json(detail));
        std::cout << "assertion " << name << ": " << (okay ? "pass" : "fail") << " (" << detail << ")\n";
    }
    View& start(const std::wstring& name, bool enabled, unsigned fail_install = 0) {
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
        auto& c = v.channel(); c.pid = t.pid; c.tid = t.tid; c.edit = t.edit; c.top = t.top; c.enabled = 1;
        c.virtualize = enabled; c.fail_install = fail_install;
        c.token = (GetTickCount64() << 16) ^ GetCurrentProcessId() ^ t.pid;
        v.bridge = &processes_.spawn(exe_, v.desktop->full, {L"--role",L"input",L"--channel",channel_name(t.pid,t.tid),
            L"--dll",(exe_.parent_path() / L"runtime303.dll").wstring(),L"--log",(output_ / (utf8(name) + "-input.jsonl")).wstring()}, utf8(name) + "-input");
        const auto deadline = GetTickCount64() + 5000;
        while (!c.ready && GetTickCount64() < deadline && WaitForSingleObject(v.bridge->process.value,0) == WAIT_TIMEOUT) Sleep(2);
        if (!fail_install) demand(c.ready == 1 && c.health == 1 && _wcsicmp(c.module_path,exe_.c_str()) == 0, "persistent input bridge initialization and EXE identity");
        else demand(c.ready == 2 && c.health == -1,"failed IAT install must not publish ready channel");
        log_.emit(fail_install ? "instance_rejected" : "instance_ready", "\"name\":" + json(name.c_str()) + ",\"enabled\":" + (enabled ? "true" : "false") +
            ",\"health\":" + std::to_string(c.health) + ",\"error\":" + std::to_string(c.error) + ",\"state\":" + telemetry_json(t));
        return v;
    }
    bool submit(View& v, std::vector<Packet> packets, unsigned command = 1, bool expected_failure = false) {
        auto& c = v.channel();
        demand(c.health == 1 && c.command_done == v.request && packets.size() + 1 <= capacity, "submit idle healthy channel");
        if (command != 4 && command != 5) { Packet end{}; end.kind = barrier; packets.push_back(end); }
        c.count = static_cast<unsigned>(packets.size()); c.command = command;
        for (unsigned i = 0; i < c.count; ++i) { packets[i].sequence = ++v.sequence; c.packets[i] = packets[i]; }
        MemoryBarrier(); InterlockedExchange(&c.request, ++v.request);
        const auto deadline = GetTickCount64() + 6000;
        while (c.command_done != v.request && c.health == 1 && GetTickCount64() < deadline) Sleep(2);
        const bool okay = c.command_done == v.request && c.health != -1;
        log_.emit("batch", "\"name\":" + json(v.name.c_str()) + ",\"request\":" + std::to_string(v.request) +
            ",\"firstSequence\":" + std::to_string(packets.empty() ? 0 : packets.front().sequence) + ",\"count\":" + std::to_string(c.count) +
            ",\"lastSequence\":" + std::to_string(v.sequence) + ",\"processed\":" + (okay ? "true" : "false") +
            ",\"command\":" + std::to_string(command) + ",\"consumed\":" + std::to_string(c.consumed) +
            ",\"version\":" + std::to_string(c.async_state.version) + ",\"health\":" + std::to_string(c.health) + ",\"error\":" + std::to_string(c.error) + ",\"errorApi\":" + json(c.error_api));
        if (!okay && !expected_failure) throw Error("input_batch", c.error ? static_cast<DWORD>(c.error) : ERROR_TIMEOUT);
        return okay;
    }
    Telemetry observe(View& v, const char* name) {
        submit(v, {},5);
        const auto t = v.state_now();
        log_.emit("observation", "\"name\":" + json(name) + ",\"view\":" + json(v.name.c_str()) + ",\"state\":" + telemetry_json(t));
        return t;
    }
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
            verify(utf8(v.name) + ".closed-cleanly", v.channel().ready == 2 && v.channel().health == 0 && !v.channel().error && clear(v.state_now()) &&
                !v.channel().iat_installed && !v.channel().pinned && v.channel().restored, "queue bits clear, original IAT/protection restored, self reference released, bridge exited");
        }
    }
    std::uint64_t read_slot(View& v, const char* name) {
        std::uint64_t address = 0; SIZE_T read = 0;
        check(ReadProcessMemory(v.fixture->process.value,reinterpret_cast<void*>(v.channel().slot),&address,sizeof(address),&read),"ReadProcessMemory.owned_EXE_IAT");
        demand(read == sizeof(address),"short IAT read");
        log_.emit("iat_readback","\"name\":" + json(name) + ",\"view\":" + json(v.name.c_str()) + ",\"slot\":" + std::to_string(v.channel().slot) +
            ",\"actual\":" + std::to_string(address) + ",\"native\":" + std::to_string(v.channel().native) + ",\"replacement\":" + std::to_string(v.channel().replacement));
        return address;
    }
    Probe inspect(View& v, const char* name) {
        const auto telemetry = observe(v,name);
        const auto p = v.channel().probe; const auto state = read_async(v.channel());
        std::ostringstream fields;
        fields << "\"name\":" << json(name) << ",\"view\":" << json(v.name.c_str()) << ",\"probe\":" << p.id << ",\"pid\":" << p.pid << ",\"tid\":" << p.tid
            << ",\"version\":" << state.version << ",\"sequence\":" << state.input_sequence << ",\"consumed\":" << v.channel().consumed
            << ",\"asyncCtrl\":" << p.async_ctrl << ",\"repeatCtrl\":" << p.repeated_ctrl << ",\"asyncShift\":" << p.async_shift
            << ",\"leftCtrl\":" << p.left_ctrl << ",\"rightCtrl\":" << p.right_ctrl << ",\"leftShift\":" << p.left_shift << ",\"rightShift\":" << p.right_shift
            << ",\"asyncX\":" << p.async_x << ",\"queueCtrl\":" << p.queue_ctrl << ",\"queueShift\":" << p.queue_shift
            << ",\"keyboardCtrl\":" << unsigned(p.keyboard_ctrl) << ",\"keyboardShift\":" << unsigned(p.keyboard_shift)
            << ",\"keyboardOk\":" << (p.keyboard_ok ? "true" : "false") << ",\"nativeDynamicCtrl\":" << p.native_dynamic_ctrl
            << ",\"fixtureAsyncCtrl\":" << telemetry.async_ctrl << ",\"fixtureAsyncShift\":" << telemetry.async_shift;
        log_.emit("probe",fields.str());
        verify(utf8(v.name) + ".probe-is-real-EXE-GUI-thread",p.pid == v.fixture->pid && p.tid == v.fixture->tid && p.keyboard_ok &&
            p.async_ctrl == telemetry.async_ctrl && p.async_shift == telemetry.async_shift && p.async_ctrl == p.repeated_ctrl,
            "read-only EXE extension and unchanged fixture call the same real import; repeated query is non-consuming");
        return p;
    }
    static bool high(SHORT value) { return (value & 0x8000) != 0; }
    void verify_call_sites(View& v) {
        std::map<std::uint64_t,bool> callers;
        for (LONG i=0;i<v.channel().query_count;++i) {
            const auto& q = v.channel().queries[i];
            if (q.valid) callers.emplace(q.caller,false);
        }
        bool okay = !callers.empty();
        for (auto& [caller,matched] : callers) {
            BYTE instruction[6]{}; SIZE_T read = 0;
            check(ReadProcessMemory(v.fixture->process.value,reinterpret_cast<void*>(caller-6),instruction,sizeof(instruction),&read),"ReadProcessMemory.EXE_callsite");
            std::int32_t displacement = 0; std::memcpy(&displacement,instruction+2,sizeof(displacement));
            const auto referenced_slot = caller + displacement;
            matched = read == sizeof(instruction) && instruction[0] == 0xFF && instruction[1] == 0x15 && referenced_slot == v.channel().slot;
            okay = okay && matched;
            log_.emit("callsite", "\"view\":" + json(v.name.c_str()) + ",\"caller\":" + std::to_string(caller) +
                ",\"opcode0\":" + std::to_string(instruction[0]) + ",\"opcode1\":" + std::to_string(instruction[1]) +
                ",\"referencedSlot\":" + std::to_string(referenced_slot) + ",\"expectedSlot\":" + std::to_string(v.channel().slot) +
                ",\"matched\":" + (matched ? "true" : "false"));
        }
        verify(utf8(v.name) + ".actual-call-instructions-target-IAT",okay,"controller reads live EXE call instructions; RIP-relative import calls resolve to the recorded slot");
    }
    void exit_case() {
        auto& exited = start(L"exit-pending",true);
        submit(exited,{press(VK_LCONTROL,true)},3);
        const auto pending = inspect(exited,"before-owned-target-exit");
        verify("exit-case-really-held-async",high(pending.async_ctrl) && !high(pending.queue_ctrl),"pending accepted input exists before target exits");
        processes_.terminate(*exited.fixture); processes_.confirm(*exited.bridge);
        verify("negative.target-exit-invalidates",exited.channel().health == -1 && exited.channel().error == ERROR_PROCESS_ABORTED &&
            exited.channel().target_gone && !exited.channel().iat_installed && !exited.channel().pinned &&
            !read_async(exited.channel()).down[VK_CONTROL],"target process confirmed exited; preserve primary exit error and clear private async state");
    }
    int finish() {
        processes_.stop_all(); monitor_.stop();
        log_.emit("host_result","\"samples\":" + std::to_string(monitor_.samples) + ",\"quietChanges\":" + std::to_string(monitor_.quiet_changes) +
            ",\"externalInputIntervals\":" + std::to_string(monitor_.external_intervals) + ",\"errors\":" + std::to_string(monitor_.errors));
        log_.result("static_host_observation",monitor_.errors || monitor_.quiet_changes || monitor_.external_intervals ? "inconclusive" : "pass","real host native foreground/desktop/cursor/async modifiers; external activity not attributed to runtime");
        log_.result("manual_host_interaction","not_executed","no human verdict generated; existing R3-002 manual procedure retained");
        log_.emit("experiment_complete","\"failedAssertions\":" + std::to_string(failures_) + ",\"R3Complete\":false,\"guiRestore\":false");
        return failures_ ? 2 : 0;
    }
    int run(const std::wstring& scenario) {
        if (scenario == L"exit") { exit_case(); return finish(); }
        auto& native = start(L"native",false);
        submit(native,{press(VK_LCONTROL,true)},3);
        const auto n0 = inspect(native,"native-before-dequeue");
        submit(native,{},4);
        const auto n1 = inspect(native,"native-after-dequeue");
        verify("native.baseline-failure-retained",!high(n0.async_ctrl) && !high(n1.async_ctrl) && high(n1.queue_ctrl),"queue mechanism enabled, EXE IAT native: async remains native zero");
        log_.result("native.async_private_Ctrl",high(n1.async_ctrl) ? "pass" : "fail","actual unpatched import; expected native background limitation");
        verify("native.IAT-unchanged",read_slot(native,"native-import") == native.channel().native,"no IAT replacement in native control");
        release_keys(native,true);

        auto& a = start(L"A",true); auto& b = start(L"B",true);
        verify("A.IAT-installed",read_slot(a,"installed") == a.channel().replacement && a.channel().pinned,"only owned EXE import replaced; runtime pinned until restore");
        const auto consumed_before = a.channel().consumed;
        submit(a,{press(VK_LCONTROL,true)},3);
        const auto down = inspect(a,"accepted-down-before-message-dequeue");
        const auto separate = inspect(b,"B-while-A-held");
        verify("async.before-dequeue",high(down.async_ctrl) && !high(down.queue_ctrl) && a.channel().consumed == consumed_before,"accepted async input is effective before queued keyboard messages are delivered");
        verify("async.A-B-independent",!high(separate.async_ctrl) && !high(separate.queue_ctrl),"B reads its own accepted input state");
        verify("async.native-dynamic-path-remains-native",!high(down.native_dynamic_ctrl),"GetProcAddress USER32 call is intentionally not intercepted");
        log_.result("managed.EXE_IAT_async_Ctrl",high(down.async_ctrl) ? "pass" : "fail","actual EXE GetAsyncKeyState call, high bit virtualized");
        if (scenario == L"minimal") {
            verify_call_sites(a); verify_call_sites(b);
            submit(a,{},4); release_keys(a,true); release_keys(b,true);
        } else {
            // A key-up is accepted while the preceding key-down is still pending.
            // During subsequent down dispatch, queue Ctrl is down but async Ctrl
            // is already up. This deliberately disproves synchronized-table fakes.
            submit(a,{press(VK_LCONTROL,false)},3);
            const auto up = inspect(a,"accepted-up-before-both-messages");
            verify("async.up-before-dequeue",!high(up.async_ctrl) && !high(up.queue_ctrl) && a.channel().consumed == consumed_before,"up visible immediately, neither key event consumed");
            const auto trace_begin = a.channel().trace_count;
            submit(a,{},4);
            bool divergent = false;
            for (LONG i=trace_begin;i<a.channel().trace_count;++i) {
                const auto& t = a.channel().traces[i];
                if (t.removed && t.kind == key && t.down && high(t.ctrl)) divergent = true;
            }
            const auto after = inspect(a,"after-down-up-dequeued");
            verify("queue-and-async-different-time-semantics",divergent && !high(after.async_ctrl) && !high(after.queue_ctrl),"fixture per-message trace independently checks async up while queue down");
            submit(a,{press(VK_LCONTROL,true),press(VK_LCONTROL,false),press(VK_LCONTROL,true),press(VK_LCONTROL,false)});
            const auto burst = inspect(a,"burst-final-up");
            verify("async.burst-and-repeated-query",!high(burst.async_ctrl) && burst.async_ctrl == burst.repeated_ctrl,"four accepted transitions; repeat reads do not consume low-bit history");

            submit(a,{press(VK_LCONTROL,true),press(VK_RCONTROL,true),press(VK_LCONTROL,false)},3);
            const auto right = inspect(a,"only-right-Ctrl-held-before-dequeue");
            verify("async.Ctrl-sides-OR",high(right.async_ctrl) && !high(right.left_ctrl) && high(right.right_ctrl),"generic Ctrl is OR of independent side states");
            submit(a,{},4);
            submit(a,{press(VK_RCONTROL,false),press(VK_LSHIFT,true),press(VK_RSHIFT,true),press(VK_LSHIFT,false),press('X',true)},3);
            const auto sx = inspect(a,"right-Shift-and-X-held");
            verify("async.Shift-and-X",!high(sx.async_ctrl) && high(sx.async_shift) && !high(sx.left_shift) && high(sx.right_shift) && high(sx.async_x),"supported ordinary key and side-specific modifier states");
            submit(b,{press(VK_LCONTROL,true)});
            const auto bh = inspect(b,"B-own-Ctrl-held"); const auto ah = inspect(a,"A-unaffected-by-B");
            verify("async.interleaved-inputs",high(bh.async_ctrl) && !high(bh.async_shift) && !high(ah.async_ctrl) && high(ah.async_shift),"A and B states differ while both process API calls");
            submit(a,{},4);
            Packet release_packet{}; release_packet.kind = release;
            submit(a,{release_packet},3);
            const auto cancelled = inspect(a,"cancel-effective-before-queue-release");
            verify("async.cancel-linearization",!high(cancelled.async_ctrl) && !high(cancelled.async_shift) && !high(cancelled.async_x) && high(cancelled.queue_shift),"cancel clears accepted async state before queue release is consumed");
            submit(a,{},4);
            verify("async.cancel-queue-completes",!high(inspect(a,"cancel-dequeued").queue_shift),"queue state clears when its release packet is consumed");

            auto& missing = start(L"missing-import",true,1);
            processes_.confirm(*missing.bridge);
            verify("negative.missing-import-rejected",missing.channel().error == ERROR_PROC_NOT_FOUND && !missing.channel().iat_installed && !missing.channel().pinned,"actual missing-name lookup; no ready input channel");
            auto& rollback = start(L"install-rollback",true,2);
            processes_.confirm(*rollback.bridge);
            verify("negative.partial-install-rollback",rollback.channel().error == ERROR_CANCELLED && !rollback.channel().iat_installed && !rollback.channel().pinned &&
                read_slot(rollback,"rolled-back-install") == rollback.channel().native && rollback.channel().page_before == rollback.channel().page_after,"injected error after real exchange; original slot/protection restored");

            auto& failed = start(L"unload-failure",true);
            submit(failed,{press(VK_LCONTROL,true)});
            failed.channel().fail_restore = 1;
            bool refused = false;
            try { release_keys(failed,true); } catch (const Error&) { refused = true; }
            processes_.confirm(*failed.bridge);
            verify("negative.unload-refusal-invalidates",refused && failed.channel().health == -1 && failed.channel().error == ERROR_ACCESS_DENIED &&
                failed.channel().iat_installed && failed.channel().pinned && read_slot(failed,"failed-unload-still-pinned") == failed.channel().replacement,"injected restoration refusal; no dangling pointer into unloaded DLL, no healthy result");
            log_.result("injected_restore_refusal.healthy_unload","fail","IAT still points at pinned runtime; channel rejected; target is later terminated");
            bool rejected = false;
            try { submit(failed,{press(VK_LCONTROL,true)}); } catch (const Error&) { rejected = true; }
            verify("negative.invalid-channel-rejects-input",rejected,"failed cleanup cannot accept future commands");
            processes_.terminate(*failed.fixture);

            exit_case();
            submit(b,{press(VK_LCONTROL,false)});
            verify("B-continues-after-other-failures",!high(inspect(b,"B-after-other-channel-failures").async_ctrl),"existing B remains operational");
            // Integration regression in the new runtime, in addition to the full
            // untouched R3-002 suite: actual EDIT still uses queue modifier state.
            Packet click{}; click.kind = mouse; click.message = WM_LBUTTONDOWN; click.wp = MK_LBUTTON; click.lp = MAKELPARAM(12,12);
            auto click_up = click; click_up.message = WM_LBUTTONUP; click_up.wp = 0;
            std::vector<Packet> text{click,click_up};
            for (const auto ch : std::wstring(L"alpha")) { Packet p{}; p.kind = character; p.wp = ch; text.push_back(p); }
            submit(b,text);
            submit(b,{press(VK_HOME,true),press(VK_HOME,false)});
            submit(b,{press(VK_LSHIFT,true),press(VK_END,true),press(VK_END,false),press(VK_LSHIFT,false)});
            const auto selected = observe(b,"R303-native-EDIT-Shift-End");
            verify("B.native-EDIT-queue-regression",std::wstring(selected.text) == L"alpha" && selected.selection_start == 0 && selected.selection_end == 5,
                "native EDIT selects through real queue state even when accepted async Shift is already up");
            verify_call_sites(a); verify_call_sites(b);
            release_keys(a,true);
            verify("A.IAT-restored",read_slot(a,"after-normal-unload") == a.channel().native,"controller reads actual original IAT address after bridge exits");
            // Existing fixture still calls its original API after IAT restoration.
            const auto reply_name = L"Local\\" + prefix_ + L"_unpatched_observe";
            Mapping reply(reply_name,sizeof(Reply),true); new(reply.view) Reply{};
            auto& helper = processes_.spawn(exe_,a.desktop->full,{L"--role",L"bridge",L"--state",a.state_name,L"--reply",reply_name,L"--op",L"observe",L"--target",L"top"},"A-native-after-unload");
            processes_.confirm(helper);
            const auto final_native = a.state_now();
            verify("A.native-after-unload",!high(final_native.async_ctrl) && static_cast<Reply*>(reply.view)->done && !static_cast<Reply*>(reply.view)->error,"unchanged fixture continues through restored native import");
            release_keys(b,true);
        }
        return finish();
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
        if (role == L"input") return r303::input_main(args);
        r3::demand(role == L"controller","unknown role");
        const auto output = std::filesystem::absolute(args.get(L"--output"));
        r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output),"output must be new");
        std::filesystem::create_directories(output);
        wchar_t path[32768]{}; r3::check(GetModuleFileNameW(nullptr,path,32768) != 0,"GetModuleFileNameW");
        const auto scenario = args.get(L"--scenario",L"all"); r3::demand(scenario == L"all" || scenario == L"minimal" || scenario == L"exit","unknown scenario");
        r3::Experiment experiment(path,output); return experiment.run(scenario);
    } catch (const std::exception& e) { std::cout << "R3_003_FAILURE " << e.what() << '\n'; return 1; }
}
