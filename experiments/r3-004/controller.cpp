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
    SHORT ctrl = 0, shift = 0, left = 0;
    std::wstring desktop;
};
HostSample host_sample() {
    HostSample result;
    result.foreground = reinterpret_cast<std::uint64_t>(GetForegroundWindow());
    result.ctrl = GetAsyncKeyState(VK_CONTROL) & static_cast<SHORT>(0x8000);
    result.shift = GetAsyncKeyState(VK_SHIFT) & static_cast<SHORT>(0x8000);
    result.left = GetAsyncKeyState(VK_LBUTTON) & static_cast<SHORT>(0x8000);
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
                        s.cursor.x != previous.cursor.x || s.cursor.y != previous.cursor.y || s.ctrl != previous.ctrl || s.shift != previous.shift || s.left != previous.left);
                    const bool external = have_previous && s.last_input != previous.last_input;
                    if (external) ++external_intervals;
                    if (changed && !external) ++quiet_changes;
                    ++samples;
                    trace << "{\"tickMs\":" << GetTickCount64() << ",\"foreground\":" << s.foreground << ",\"inputDesktop\":" << json(s.desktop.c_str())
                        << ",\"cursorX\":" << s.cursor.x << ",\"cursorY\":" << s.cursor.y << ",\"lastInput\":" << s.last_input
                        << ",\"ctrlHigh\":" << s.ctrl << ",\"shiftHigh\":" << s.shift << ",\"leftHigh\":" << s.left
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

using namespace r304;
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
    bool manual_window_ = false;
public:
    Experiment(const std::filesystem::path& exe, const std::filesystem::path& output)
        : exe_(exe), output_(output), prefix_(L"fv_r304_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64())),
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
            L"--dll",(exe_.parent_path() / L"runtime304.dll").wstring(),L"--log",(output_ / (utf8(name) + "-input.jsonl")).wstring()}, utf8(name) + "-input");
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
        if (command != 4 && command != 5 && command != 6) { Packet end{}; end.kind = barrier; packets.push_back(end); }
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
    int finish() {
        processes_.stop_all(); monitor_.stop();
        log_.emit("host_result","\"samples\":" + std::to_string(monitor_.samples) + ",\"quietChanges\":" + std::to_string(monitor_.quiet_changes) +
            ",\"externalInputIntervals\":" + std::to_string(monitor_.external_intervals) + ",\"errors\":" + std::to_string(monitor_.errors));
        log_.result("static_host_observation",monitor_.errors || monitor_.quiet_changes || monitor_.external_intervals ? "inconclusive" : "pass","real host native foreground/desktop/cursor/async modifiers; external activity not attributed to runtime");
        log_.result("manual_host_interaction",manual_window_ ? "not_verified" : "not_executed","hold window alone is not a human verdict; follow R3-004 README procedure");
        log_.emit("experiment_complete","\"failedAssertions\":" + std::to_string(failures_) + ",\"R3Complete\":false,\"guiRestore\":false");
        return failures_ ? 2 : 0;
    }

    static Packet pointer(unsigned action, LONG x, LONG y) {
        Packet p{}; p.kind = mouse; p.action = action; p.x = x; p.y = y; return p;
    }
    static bool high(SHORT x) { return (x & 0x8000) != 0; }
    void click(View& v,LONG x,LONG y) { submit(v,{pointer(move,x,y),pointer(left_down,x,y),pointer(left_up,x,y)}); }
    Probe inspect(View& v,const char* name) {
        const auto t = observe(v,name); const auto p = v.channel().probe; const auto s = read_async(v.channel());
        std::ostringstream f;
        f << "\"name\":" << json(name) << ",\"view\":" << json(v.name.c_str()) << ",\"pid\":" << p.pid << ",\"tid\":" << p.tid
          << ",\"version\":" << s.version << ",\"sequence\":" << s.input_sequence << ",\"consumed\":" << v.channel().consumed
          << ",\"asyncLeft\":" << p.async_left << ",\"queueLeft\":" << p.queue_left << ",\"keyboardLeft\":" << unsigned(p.keyboard_left)
          << ",\"cursorOk\":" << p.cursor_ok << ",\"cursorError\":" << p.cursor_error << ",\"cursorX\":" << p.cursor.x << ",\"cursorY\":" << p.cursor.y
          << ",\"expectedX\":" << s.cursor.x << ",\"expectedY\":" << s.cursor.y << ",\"nativeCursorOk\":" << p.native_cursor_ok
          << ",\"nativeCursorX\":" << p.native_cursor.x << ",\"nativeCursorY\":" << p.native_cursor.y
          << ",\"capture\":" << p.capture << ",\"focus\":" << p.focus;
        log_.emit("probe",f.str());
        verify(utf8(v.name)+".EXE-query-on-GUI-thread",p.pid == v.fixture->pid && p.tid == v.fixture->tid && p.keyboard_ok &&
            p.async_left == t.async_left && p.capture == t.capture && p.focus == t.focus,"read-only EXE probe and unchanged fixture independently query native APIs");
        return p;
    }
    void slots(View& v,bool installed) {
        const auto& c = v.channel();
        for (unsigned n=0;n<2;++n) {
            const auto slot = n ? c.cursor_slot : c.slot; std::uint64_t actual = 0; SIZE_T read = 0;
            check(ReadProcessMemory(v.fixture->process.value,reinterpret_cast<void*>(slot),&actual,sizeof(actual),&read),"ReadProcessMemory.IAT");
            const auto expected = installed ? (n ? c.cursor_replacement : c.replacement) : (n ? c.cursor_native : c.native);
            log_.emit("iat_readback","\"view\":"+json(v.name.c_str())+",\"api\":"+json(n ? "GetCursorPos" : "GetAsyncKeyState")+",\"actual\":"+std::to_string(actual)+",\"expected\":"+std::to_string(expected));
            verify(utf8(v.name)+".IAT-"+(n ? "cursor" : "async"),read == sizeof(actual) && actual == expected,"controller reads actual remote import slot");
        }
    }
    void calls(View& v) {
        bool okay = true; unsigned cursor_calls = 0, left_calls = 0; std::map<std::uint64_t,unsigned> callers;
        auto& c = v.channel();
        for (LONG i=0;i<c.query_count;++i) {
            const auto q = c.queries[i];
            if (!q.valid) continue;
            callers.emplace(q.caller,q.api); if (q.api) ++cursor_calls; else if (q.vk == VK_LBUTTON) ++left_calls;
            okay = okay && q.module == c.image && q.pid == c.pid && q.tid == c.tid;
        }
        for (const auto& [caller,api] : callers) {
            BYTE bytes[6]{}; SIZE_T count = 0;
            check(ReadProcessMemory(v.fixture->process.value,reinterpret_cast<void*>(caller-6),bytes,6,&count),"ReadProcessMemory.callsite");
            std::int32_t offset = 0; std::memcpy(&offset,bytes+2,4);
            const auto expected = api ? c.cursor_slot : c.slot;
            const bool matched = count == 6 && bytes[0] == 0xff && bytes[1] == 0x15 && caller+offset == expected;
            okay = okay && matched;
            log_.emit("callsite","\"view\":"+json(v.name.c_str())+",\"caller\":"+std::to_string(caller)+",\"api\":"+json(api ? "GetCursorPos" : "GetAsyncKeyState")+",\"matched\":"+(matched ? "true" : "false"));
        }
        verify(utf8(v.name)+".actual-IAT-call-sites",okay && cursor_calls && left_calls,"actual EXE instructions and caller module/thread checked for both APIs");
    }
    void close(View& v) {
        Packet p{}; p.kind = release; submit(v,{p},2); processes_.confirm(*v.bridge);
        const auto& c = v.channel(); const auto t = v.state_now();
        verify(utf8(v.name)+".clean-close",c.health == 0 && c.ready == 2 && c.restored && !c.iat_installed && !c.pinned && c.cleanup_done &&
            !t.capture && !high(t.key_left) && !(t.keyboard[VK_LBUTTON]&0x80) && !read_async(v.channel()).down[VK_LBUTTON],"button/capture released, both imports and protections restored before bridge exits");
        slots(v,false);
    }
    void capture(View& v,const std::wstring& name) {
        const auto reply_name = L"Local\\"+prefix_+L"_image_"+name;
        Mapping mapping(reply_name,sizeof(Reply),true); auto* reply = new(mapping.view) Reply{};
        const auto path = output_/(name+L".png");
        auto& helper = processes_.spawn(exe_,v.desktop->full,{L"--role",L"bridge",L"--state",v.state_name,L"--reply",reply_name,L"--op",L"capture",L"--target",L"top",L"--path",path.wstring()},utf8(name)+"-capture");
        if (WaitForSingleObject(helper.process.value,4000) != WAIT_OBJECT_0) { processes_.terminate(helper); throw Error("capture_timeout",ERROR_TIMEOUT); }
        processes_.confirm(helper);
        log_.emit("capture","\"name\":"+json(name.c_str())+",\"view\":"+json(v.name.c_str())+",\"error\":"+std::to_string(reply->error)+
            ",\"beforeDraws\":"+std::to_string(reply->before_draws)+",\"afterDraws\":"+std::to_string(reply->after_draws)+
            ",\"apiBeginMs\":"+std::to_string(reply->api_begin_ms)+",\"apiEndMs\":"+std::to_string(reply->api_end_ms)+
            ",\"tick\":"+std::to_string(reply->print_update)+",\"clientDx\":"+std::to_string(reply->client_dx)+",\"clientDy\":"+std::to_string(reply->client_dy));
        verify(utf8(name)+".fresh-GDI",reply->done && !reply->error && reply->after_draws > reply->before_draws && std::filesystem::exists(path),"unchanged native PrintWindow helper checks real target paint and decoded Tick pixels");
    }
    void invalidated(View& v,DWORD error,const char* name) {
        processes_.confirm(*v.bridge);
        verify(name,v.channel().health == -1 && v.channel().error == static_cast<LONG>(error),std::string(v.channel().error_api)+" error="+std::to_string(v.channel().error));
        log_.result(name,"fail","input capability refused; negative-test assertion is separate");
    }
    void still_works(View& b) {
        const auto before = b.state_now().clicks; click(b,80,158);
        verify("B.survives-other-channel-failure",b.state_now().clicks == before+1 && b.channel().health == 1,"native button remains usable after another View fails");
    }
    int run(const std::wstring& scenario,DWORD hold_ms) {
        manual_window_ = hold_ms != 0;
        auto& native = start(L"native",false);
        submit(native,{pointer(left_down,80,230)},3);
        const auto native_pre = inspect(native,"native-accepted-before-dequeue");
        submit(native,{},4); const auto native_post = inspect(native,"native-after-dequeue");
        verify("native.async-failure-retained",!high(native_pre.async_left) && !high(native_post.async_left) && high(native_post.queue_left),"same EXE; native async left does not reflect managed button; queue table intervention still active");
        log_.result("native.private_async_mouse",high(native_post.async_left) ? "pass" : "fail","unpatched main EXE import; actual Windows result");
        log_.result("native.private_cursor",native_post.cursor_ok && native_post.cursor.x == read_async(native.channel()).cursor.x && native_post.cursor.y == read_async(native.channel()).cursor.y ? "observed_match" : "fail","unpatched main EXE cursor result, no forced expectation");
        slots(native,false); close(native);
        auto& a = start(L"A",true); auto& b = start(L"B",true);
        slots(a,true); slots(b,true);
        verify("A-B.same-initial-UI",std::wstring(a.state_now().text) == std::wstring(b.state_now().text) && a.state_now().clicks == 0 && b.state_now().clicks == 0 &&
            a.state_now().drag_x == b.state_now().drag_x && a.state_now().drag_y == b.state_now().drag_y,"cold-start GUI instances, no restoration claim");
        click(a,80,105); // normal native EDIT click establishes actual focus
        submit(b,{pointer(move,80,158)});
        const auto b0 = inspect(b,"B-initial-cursor");
        const auto consumed = a.channel().consumed;
        submit(a,{pointer(left_down,80,230)},3);
        const auto accepted = inspect(a,"A-down-accepted-not-dequeued");
        verify("mouse.dual-time-semantics",high(accepted.async_left) && !high(accepted.queue_left) && !accepted.capture && a.channel().consumed == consumed,
            "accepted left/cursor effective before dequeue; native queue and capture unchanged");
        verify("cursor.acceptance-visible",accepted.cursor_ok && accepted.cursor.x == read_async(a.channel()).cursor.x && accepted.cursor.y == read_async(a.channel()).cursor.y,"actual GetCursorPos IAT call returns accepted screen coordinates");
        submit(a,{},4);
        const auto down = inspect(a,"A-drag-down"); const auto td = a.state_now();
        verify("A.native-capture-and-button-state",down.capture == td.drag && high(down.queue_left) && (down.keyboard_left&0x80) && high(down.async_left),"unchanged native drag procedure called SetCapture; real queue query sees left held");
        submit(a,{pointer(move,650,350)});
        const auto outside = inspect(a,"A-outside-drag-and-top"); const auto ta = a.state_now();
        verify("A.capture-routes-outside",outside.capture == ta.drag && ta.drag_x == 626 && ta.drag_y == 146 && ta.drag_moves > td.drag_moves,"top-client (650,350) routes to captured drag client (626,146), beyond both bounds");
        const auto cursor_a = read_async(a.channel()).cursor;
        const auto focus_a = outside.focus;
        click(b,80,158); const auto clicked = inspect(b,"B-click-while-A-dragging");
        const auto preserved = inspect(a,"A-after-B-click");
        verify("B.native-button-click",b.state_now().clicks == 1 && !clicked.capture && !high(clicked.queue_left) && !high(clicked.async_left),"ordinary BUTTON received routed down/up and emitted BN_CLICKED");
        verify("A.unaffected-by-B",preserved.capture == ta.drag && preserved.focus == focus_a && high(preserved.async_left) && high(preserved.queue_left) &&
            preserved.cursor.x == cursor_a.x && preserved.cursor.y == cursor_a.y && a.state_now().drag_x == 626 && a.state_now().drag_y == 146,"actual capture/focus/drag plus accepted cursor and button remain A-private");
        verify("B.independent-cursor",clicked.cursor_ok && clicked.cursor.x == b0.cursor.x && clicked.cursor.y == b0.cursor.y && !high(clicked.async_left),"B position and button state separate from A");
        if (scenario != L"minimal") {
            capture(a,L"A-outside-held"); capture(b,L"B-clicked");
            const auto deadline = GetTickCount64()+hold_ms;
            if (hold_ms) { log_.emit("manual_window_started","\"durationMs\":"+std::to_string(hold_ms)); std::cout << "MANUAL_WINDOW_STARTED " << hold_ms << " ms; use host normally; no automatic human verdict\n"; }
            while (GetTickCount64()<deadline) { submit(a,{pointer(move,150,250)}); click(b,80,158); Sleep(100); }
            if (hold_ms) log_.emit("manual_window_finished","\"humanVerdict\":\"not_verified\"");
            submit(a,{pointer(move,180,260)}); capture(a,L"A-inside-held");
        }
        submit(a,{pointer(left_up,180,260)});
        const auto released = inspect(a,"A-up-releases-capture");
        verify("A.release-clears-native-capture",!released.capture && !high(released.queue_left) && !high(released.async_left),"native WM_LBUTTONUP calls ReleaseCapture");
        click(a,80,158); verify("A.route-resumes-hit-test",a.state_now().clicks == 1 && !a.state_now().capture,"after release, coordinates route to BUTTON instead of stale drag target");
        if (scenario != L"minimal") {
            capture(a,L"A-final"); capture(b,L"B-final");
            // A queued down/up burst: async already up while queue down executes.
            const auto begin = a.channel().trace_count;
            submit(a,{pointer(left_down,80,230),pointer(left_up,80,230)},3);
            const auto burst_pre = inspect(a,"A-burst-accepted"); submit(a,{},4);
            bool saw_down = false, peek = false;
            for (LONG i=begin;i<a.channel().trace_count;++i) { const auto& t = a.channel().traces[i]; if (t.kind == mouse && t.removed && high(t.left)) saw_down = true; if (!t.removed) peek = true; }
            verify("mouse.burst-peek-order",!high(burst_pre.async_left) && !high(burst_pre.queue_left) && saw_down && peek && !a.state_now().capture,"non-consuming peeks do not apply future up; actual per-event queue table changes");
            // Native EDIT keyboard behavior survives the new router and two-slot IAT.
            click(b,30,105); std::vector<Packet> chars;
            for (auto ch : std::wstring(L"alpha")) { Packet p{}; p.kind = character; p.wp = ch; chars.push_back(p); } submit(b,chars);
            submit(b,{press(VK_HOME,true),press(VK_HOME,false),press(VK_LSHIFT,true),press(VK_END,true),press(VK_END,false),press(VK_LSHIFT,false)});
            const auto selection = observe(b,"B-native-EDIT-keyboard-regression");
            verify("B.EDIT-Shift-End-regression",std::wstring(selection.text) == L"alpha" && selection.selection_start == 0 && selection.selection_end == 5,"unmodified EDIT uses native queue state with routed focus");
            auto& wrong = start(L"wrong-owner",true); wrong.channel().foreign_candidate = b.state_now().button;
            submit(wrong,{pointer(move,80,158)},1,true); invalidated(wrong,ERROR_ACCESS_DENIED,"negative.foreign-window-rejected"); still_works(b);
            auto& destroyed = start(L"destroyed-capture",true); submit(destroyed,{pointer(left_down,80,230)});
            submit(destroyed,{},6); submit(destroyed,{pointer(move,650,350)},1,true);
            invalidated(destroyed,ERROR_INVALID_WINDOW_HANDLE,"negative.destroyed-capture-invalidates"); still_works(b);
            auto& missing = start(L"omitted-up",true); missing.channel().lease_ms = 200;
            submit(missing,{pointer(left_down,80,230)}); Sleep(300);
            invalidated(missing,ERROR_TIMEOUT,"negative.omitted-up-invalidates");
            verify("negative.omitted-up-cleaned",missing.channel().cleanup_done && !missing.state_now().capture && !read_async(missing.channel()).down[VK_LBUTTON],"lease expiry sends native release and restores imports, channel remains invalid"); still_works(b);
            auto& closed = start(L"close-held",true); submit(closed,{pointer(left_down,80,230)}); close(closed); still_works(b);
            auto& orphan = start(L"bridge-exit",true); submit(orphan,{pointer(left_down,80,230)}); processes_.terminate(*orphan.bridge);
            auto deadline_orphan = GetTickCount64()+3000;
            while (!orphan.channel().watchdog_done && GetTickCount64()<deadline_orphan) Sleep(5);
            // Query unchanged fixture after orphan watchdog without using dead bridge.
            Sleep(120);
            verify("negative.bridge-exit-watchdog",orphan.channel().health == -1 && orphan.channel().error == ERROR_BROKEN_PIPE && orphan.channel().watchdog_done == 1 &&
                orphan.channel().cleanup_done && orphan.channel().restored && orphan.channel().pinned && !orphan.state_now().capture && !read_async(orphan.channel()).down[VK_LBUTTON],
                "GUI timer confirms bridge process death, clears capture/state, restores imports, retains self pin until target exits");
            slots(orphan,false); still_works(b);
            auto& hung = start(L"hung-thread",true); submit(hung,{pointer(left_down,80,230)});
            check(SuspendThread(hung.fixture->thread.value) != DWORD(-1),"SuspendThread.owned_GUI_negative");
            try { submit(hung,{pointer(move,650,350)},1,true); processes_.confirm(*hung.bridge); }
            catch (...) { ResumeThread(hung.fixture->thread.value); throw; }
            verify("negative.hung-thread-invalidates",hung.channel().health == -1 && hung.channel().error == ERROR_TIMEOUT && hung.channel().iat_installed && hung.channel().pinned && !hung.channel().cleanup_done,"dequeue and cleanup timeout; no healthy unload or dangling imports");
            check(ResumeThread(hung.fixture->thread.value) != DWORD(-1),"ResumeThread.owned_GUI_negative"); processes_.terminate(*hung.fixture); still_works(b);
            auto& refusal = start(L"uninstall-refusal",true); submit(refusal,{pointer(left_down,80,230)}); refusal.channel().fail_restore = 1;
            Packet cancel{}; cancel.kind = release; submit(refusal,{cancel},2,true);
            invalidated(refusal,ERROR_ACCESS_DENIED,"negative.uninstall-refused");
            verify("negative.uninstall-keeps-runtime-pinned",refusal.channel().iat_installed && refusal.channel().pinned && refusal.channel().cleanup_done && !refusal.state_now().capture,"input channel invalid; native release completed; two IAT references still backed by live DLL"); slots(refusal,true); still_works(b);
            auto& rollback = start(L"partial-install",true,2); processes_.confirm(*rollback.bridge);
            verify("negative.two-slot-install-rollback",rollback.channel().error == ERROR_CANCELLED && !rollback.channel().iat_installed && !rollback.channel().pinned && rollback.channel().restored,"second-slot transaction failure rolls back actual first slot"); slots(rollback,false);
        }
        calls(a); calls(b); close(a); close(b);
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
        if (role == L"input") return r304::input_main(args);
        r3::demand(role == L"controller","unknown role");
        const auto output = std::filesystem::absolute(args.get(L"--output"));
        r3::demand(!args.get(L"--output").empty() && !std::filesystem::exists(output),"output must be new");
        std::filesystem::create_directories(output);
        wchar_t path[32768]{}; r3::check(GetModuleFileNameW(nullptr,path,32768) != 0,"GetModuleFileNameW");
        const auto scenario = args.get(L"--scenario",L"all"); r3::demand(scenario == L"all" || scenario == L"minimal","unknown scenario");
        r3::Experiment experiment(path,output); return experiment.run(scenario,static_cast<DWORD>(args.number(L"--hold-seconds"))*1000);
    } catch (const std::exception& e) { std::cout << "R3_004_FAILURE " << e.what() << '\n'; return 1; }
}
