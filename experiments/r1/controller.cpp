#include "../common/native.hpp"

namespace {
using namespace native;
class Child {
    DEBUG_EVENT event_{};
    bool pending_ = false;
    bool exited_ = false;
    bool startup_break_seen_ = false;
    std::uint64_t ntdll_begin_ = 0, ntdll_end_ = 0;
    ULONGLONG deadline_;
public:
    Handle process, thread;
    DWORD pid = 0, tid = 0;
    std::uint64_t image = 0;
    r1::Metadata metadata{};
    explicit Child(const std::filesystem::path& fixture, DWORD timeout, const std::wstring& argument = L"")
        : deadline_(GetTickCount64() + timeout) {
        std::wstring command = L"\"" + fixture.wstring() + L"\" " + argument;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        checked(CreateProcessW(fixture.c_str(), command.data(), nullptr, nullptr, FALSE,
            DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr, fixture.parent_path().c_str(), &startup, &info), "CreateProcessW");
        process.value = info.hProcess;
        thread.value = info.hThread;
        pid = info.dwProcessId;
        tid = info.dwThreadId;
        std::cout << "PROCESS_CREATED pid=" << pid << " tid=" << tid << '\n';
    }
    ~Child() {
        if (!exited_) {
            try { stop(); }
            catch (const std::exception& error) { std::cout << "CLEANUP_FAILED " << error.what() << '\n'; }
        }
    }
    CONTEXT context() {
        CONTEXT cpu{};
        cpu.ContextFlags = r1::context_flags;
        checked(GetThreadContext(thread.value, &cpu), "GetThreadContext");
        return cpu;
    }
    void set_context(const CONTEXT& cpu) {
        require(pending_, "thread must be debug-stopped");
        checked(SetThreadContext(thread.value, &cpu), "SetThreadContext");
    }
    void resume() {
        require(pending_, "no pending debug event");
        checked(ContinueDebugEvent(event_.dwProcessId, event_.dwThreadId, DBG_CONTINUE), "ContinueDebugEvent");
        pending_ = false;
    }
    void normalize_trap(CONTEXT& cpu, std::uint64_t trap) {
        require(cpu.Rip == trap || cpu.Rip == trap + 1, "unexpected INT3 RIP convention");
        std::cout << "TRAP_CONTEXT raw_rip=" << hex(cpu.Rip) << " resume_rip=" << hex(trap + 1) << '\n';
        cpu.Rip = trap + 1;
    }
    // Returns only breakpoint exceptions. Everything else is either handled as
    // lifecycle metadata or rejected. A random breakpoint can never become S0.
    std::uint64_t next_break() {
        require(!pending_, "previous event not continued");
        while (true) {
            if (GetTickCount64() >= deadline_)
                fail("WaitForDebugEvent", ERROR_TIMEOUT, "experiment deadline", 4);
            if (!WaitForDebugEvent(&event_, 50)) {
                const auto error = GetLastError();
                if (error == ERROR_SEM_TIMEOUT) continue;
                fail("WaitForDebugEvent", error, "debug wait failed");
            }
            pending_ = true;
            if (event_.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
                image = reinterpret_cast<std::uint64_t>(event_.u.CreateProcessInfo.lpBaseOfImage);
                if (event_.u.CreateProcessInfo.hFile) CloseHandle(event_.u.CreateProcessInfo.hFile);
            } else if (event_.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
                const auto& dll = event_.u.LoadDll;
                Handle file(dll.hFile);
                if (file.value) {
                    wchar_t path[32768]{};
                    const auto length = GetFinalPathNameByHandleW(file.value, path, 32768, FILE_NAME_NORMALIZED);
                    if (length == 0) fail("GetFinalPathNameByHandleW", GetLastError(), "debug DLL file");
                    require(length < 32768, "DLL path too long");
                    if (_wcsicmp(std::filesystem::path(path).filename().c_str(), L"ntdll.dll") == 0) {
                        ntdll_begin_ = reinterpret_cast<std::uint64_t>(dll.lpBaseOfDll);
                        const auto dos = read_value<IMAGE_DOS_HEADER>(process.value, ntdll_begin_);
                        const auto pe = read_value<IMAGE_NT_HEADERS64>(process.value, ntdll_begin_ + dos.e_lfanew);
                        ntdll_end_ = ntdll_begin_ + pe.OptionalHeader.SizeOfImage;
                    }
                }
            } else if (event_.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                const auto& exception = event_.u.Exception;
                const auto address = reinterpret_cast<std::uint64_t>(exception.ExceptionRecord.ExceptionAddress);
                if (exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT && exception.dwFirstChance) {
                    if (!startup_break_seen_ && address >= ntdll_begin_ && address < ntdll_end_) {
                        startup_break_seen_ = true;
                        std::cout << "SYSTEM_STARTUP_BREAKPOINT pid=" << pid << " address=" << hex(address) << '\n';
                    } else {
                        require(event_.dwThreadId == tid, "trap on non-managed thread");
                        return address;
                    }
                } else {
                    fail("debug_exception", exception.ExceptionRecord.ExceptionCode,
                        "pid=" + std::to_string(pid) + " address=" + hex(address));
                }
            } else if (event_.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
                const auto code = event_.u.ExitProcess.dwExitCode;
                resume();
                exited_ = true;
                fail("debug_exit", code, "unexpected process exit");
            } else if (event_.dwDebugEventCode == RIP_EVENT) {
                fail("debug_RIP_EVENT", event_.u.RipInfo.dwError, "debugger error");
            }
            resume();
        }
    }
    void ready(DWORD metadata_offset) {
        const auto trap = next_break();
        metadata = read_value<r1::Metadata>(process.value, image + metadata_offset);
        require(metadata.signature == r1::magic && metadata.version == 1 &&
            metadata.machine == IMAGE_FILE_MACHINE_AMD64 && metadata.ready == trap, "not fixture ready breakpoint");
        require(metadata.code_size > 0 && metadata.code_size <= r1::page_size &&
            metadata.init_offset == 0 && metadata.checkpoint_offset < metadata.result_offset &&
            metadata.result_offset < metadata.code_size && metadata.build[64] == 0, "invalid managed layout");
        std::cout << "FIXTURE_READY pid=" << pid << " build=" << metadata.build << '\n';
        // SDK 18362 predates the named CET enum. Windows ABI policy index 15 is
        // ProcessUserShadowStackPolicy; its Flags field is one DWORD. Query only.
        DWORD shadow_flags = 0;
        if (GetProcessMitigationPolicy(process.value, static_cast<PROCESS_MITIGATION_POLICY>(15),
            &shadow_flags, sizeof(shadow_flags))) {
            std::cout << "MITIGATION shadow_stack_flags=" << hex(shadow_flags) << '\n';
        } else {
            std::cout << "MITIGATION_QUERY_UNAVAILABLE api=GetProcessMitigationPolicy code=" << hex(GetLastError()) << '\n';
        }
        PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic{};
        checked(GetProcessMitigationPolicy(process.value, ProcessDynamicCodePolicy, &dynamic, sizeof(dynamic)), "GetProcessMitigationPolicy");
        std::cout << "MITIGATION dynamic_code_flags=" << hex(dynamic.Flags) << '\n';
    }
    void allocate(std::uint64_t address, std::size_t size) {
        auto memory = VirtualAllocEx(process.value, reinterpret_cast<void*>(address), size,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!memory) fail("VirtualAllocEx", GetLastError(), "exact_address=" + hex(address), 3);
        require(reinterpret_cast<std::uint64_t>(memory) == address, "allocation moved unexpectedly");
    }
    void protect(std::uint64_t address, std::size_t size, DWORD protection) {
        DWORD old = 0;
        checked(VirtualProtectEx(process.value, reinterpret_cast<void*>(address), size, protection, &old), "VirtualProtectEx");
        if (protection == PAGE_EXECUTE_READ)
            checked(FlushInstructionCache(process.value, reinterpret_cast<void*>(address), size), "FlushInstructionCache");
    }
    void stop() {
        if (exited_) return;
        // Only handles obtained from this instance's CreateProcess are used.
        if (!TerminateProcess(process.value, 0xE0420000)) {
            const auto error = GetLastError();
            if (WaitForSingleObject(process.value, 0) != WAIT_OBJECT_0)
                fail("TerminateProcess", error, "cleanup own child");
        }
        if (pending_) resume();
        const auto cleanup_deadline = GetTickCount64() + 5000;
        while (GetTickCount64() < cleanup_deadline) {
            DEBUG_EVENT cleanup{};
            if (!WaitForDebugEvent(&cleanup, 50)) {
                if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
                fail("WaitForDebugEvent", GetLastError(), "cleanup");
            }
            if (cleanup.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && cleanup.u.LoadDll.hFile)
                CloseHandle(cleanup.u.LoadDll.hFile);
            if (cleanup.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && cleanup.u.CreateProcessInfo.hFile)
                CloseHandle(cleanup.u.CreateProcessInfo.hFile);
            checked(ContinueDebugEvent(cleanup.dwProcessId, cleanup.dwThreadId, DBG_CONTINUE), "ContinueDebugEvent.cleanup");
            if (cleanup.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
                const auto wait = WaitForSingleObject(process.value, 1000);
                if (wait != WAIT_OBJECT_0)
                    fail("WaitForSingleObject.cleanup", wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT,
                        "terminated process handle did not signal", 4);
                exited_ = true;
                std::cout << "PROCESS_TERMINATED_CONFIRMED pid=" << pid << " exit_event=" << hex(cleanup.u.ExitProcess.dwExitCode) << '\n';
                return;
            }
        }
        fail("cleanup", ERROR_TIMEOUT, "child exit not confirmed", 4);
    }
};

void source_layout(Child& child) {
    const auto& meta = child.metadata;
    std::vector<std::uint8_t> code(static_cast<std::size_t>(meta.code_size));
    read_memory(child.process.value, meta.template_address, code.data(), code.size());
    for (const auto offset : {meta.init_offset, meta.checkpoint_offset, meta.result_offset})
        require(code[static_cast<std::size_t>(offset)] == 0xCC, "expected one-byte INT3 label");
    child.allocate(r1::code_base, r1::page_size);
    child.allocate(r1::data_base, r1::page_size);
    child.allocate(r1::stack_base, r1::stack_size);
    write_memory(child.process.value, r1::code_base, code.data(), code.size());
    child.protect(r1::code_base, r1::page_size, PAGE_EXECUTE_READ);
}
r1::Snapshot capture(Child& child, const std::array<std::uint8_t, 32>& hash) {
    r1::Snapshot snapshot;
    snapshot.metadata = child.metadata;
    snapshot.executable_hash = hash;
    snapshot.cpu = child.context();
    child.normalize_trap(snapshot.cpu, r1::code_base + child.metadata.checkpoint_offset);
    for (const auto [address, size, protection] : {
        std::tuple{r1::code_base, r1::page_size, DWORD(PAGE_EXECUTE_READ)},
        std::tuple{r1::data_base, r1::page_size, DWORD(PAGE_READWRITE)},
        std::tuple{r1::stack_base, r1::stack_size, DWORD(PAGE_READWRITE)}}) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(child.process.value, reinterpret_cast<void*>(address), &info, sizeof(info)))
            fail("VirtualQueryEx", GetLastError(), "capture region");
        require(info.State == MEM_COMMIT && info.Protect == protection && info.RegionSize >= size,
            "capture region protection or extent mismatch");
        r1::Region region{address, protection, std::vector<std::uint8_t>(size)};
        read_memory(child.process.value, address, region.bytes.data(), size);
        std::cout << "CAPTURE_REGION address=" << hex(address) << " size=" << size << " protection=" << hex(protection)
            << " sha256=" << digest_text(digest(region.bytes)) << '\n';
        snapshot.regions.push_back(std::move(region));
    }
    return snapshot;
}
std::uint64_t word(const r1::Region& region, std::size_t offset) {
    require(offset <= region.bytes.size() && sizeof(std::uint64_t) <= region.bytes.size() - offset, "snapshot word out of range");
    std::uint64_t value;
    std::memcpy(&value, region.bytes.data() + offset, sizeof(value));
    return value;
}
void restore(Child& child, const r1::Snapshot& snapshot, const std::array<std::uint8_t, 32>& actual_hash,
    const std::string& mode) {
    phase = "restore_preflight";
    if (snapshot.machine != IMAGE_FILE_MACHINE_AMD64 || snapshot.executable_hash != actual_hash ||
        std::memcmp(snapshot.metadata.build, child.metadata.build, 65) != 0)
        fail("validate_snapshot", ERROR_REVISION_MISMATCH, "BUILD_MISMATCH restore rejected before writes", 3);
    require(child.metadata.code_size == snapshot.metadata.code_size &&
        child.metadata.checkpoint_offset == snapshot.metadata.checkpoint_offset &&
        child.metadata.result_offset == snapshot.metadata.result_offset, "build layout mismatch");
    std::vector<std::uint8_t> template_bytes(static_cast<std::size_t>(child.metadata.code_size));
    read_memory(child.process.value, child.metadata.template_address, template_bytes.data(), template_bytes.size());
    require(std::equal(template_bytes.begin(), template_bytes.end(), snapshot.regions[0].bytes.begin()), "loaded code differs from snapshot");
    if (mode == "address-conflict") {
        child.allocate(r1::data_base, r1::page_size);
        std::cout << "INJECTED_ADDRESS_CONFLICT address=" << hex(r1::data_base) << '\n';
    }
    phase = "restore_regions";
    for (const auto& region : snapshot.regions) {
        child.allocate(region.address, region.bytes.size());
        const bool omit = (mode == "omit-memory" && region.address == r1::data_base) ||
                          (mode == "omit-stack" && region.address == r1::stack_base);
        if (!omit) write_memory(child.process.value, region.address, region.bytes.data(), region.bytes.size());
        else std::cout << "INJECTED_OMISSION region=" << hex(region.address) << '\n';
        child.protect(region.address, region.bytes.size(), region.protection);
        std::vector<std::uint8_t> readback(region.bytes.size());
        read_memory(child.process.value, region.address, readback.data(), readback.size());
        require(omit ? std::all_of(readback.begin(), readback.end(), [](auto byte) { return byte == 0; }) :
            readback == region.bytes, "raw region restore readback mismatch");
    }
    phase = "restore_context";
    auto cpu = snapshot.cpu;
    if (mode == "omit-register") cpu.R15 = 0;
    if (mode == "omit-flags") cpu.EFlags &= ~DWORD(1);
    child.set_context(cpu);
    const auto observed = child.context();
    require(observed.Rip == cpu.Rip && observed.Rsp == cpu.Rsp && observed.R15 == cpu.R15 &&
        observed.R12 == cpu.R12 && (observed.EFlags & 1) == (cpu.EFlags & 1), "context readback mismatch");
    std::cout << "RESTORED_CONTEXT pid=" << child.pid << " rip=" << hex(observed.Rip)
        << " rsp=" << hex(observed.Rsp) << " r15=" << hex(observed.R15) << " eflags=" << hex(observed.EFlags) << '\n';
}

int experiment(const std::filesystem::path& fixture, const std::string& mode, DWORD timeout) {
    const auto binary = read_file(fixture);
    const auto hash = digest(binary);
    const auto export_offset = metadata_rva(binary);
    std::cout << "FIXTURE_SHA256 " << digest_text(hash) << '\n';
    std::uint64_t seed = 0;
    const auto random_status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (random_status < 0) fail("BCryptGenRandom", static_cast<DWORD>(random_status), "NTSTATUS");
    const auto mem_expected = std::rotl(seed, 17) ^ 0xA17C9E3D52B468F1ULL;
    const auto stack_expected = seed ^ 0x6E24B59A0173D8CFULL;
    const auto reg_expected = std::rotl(seed, 31) + 0x13579BDF2468ACE1ULL;
    require(mem_expected != 0 && stack_expected != 0 && reg_expected != 0, "degenerate nonce; rerun");
    const auto expected = std::rotl((mem_expected + reg_expected + 1) ^ stack_expected, 9);
    std::cout << "ORACLE seed=" << hex(seed) << " memory=" << hex(mem_expected) << " stack=" << hex(stack_expected)
        << " r15=" << hex(reg_expected) << " expected=" << hex(expected) << '\n';
    phase = "source_ready";
    Child source(fixture, timeout, mode == "timeout" ? L"--hang-ready" : mode == "unexpected-exception" ? L"--fault-ready" : L"");
    source.ready(export_offset);
    source_layout(source);
    auto initial = source.context();
    initial.Rip = r1::code_base;
    initial.Rsp = r1::stack_base + r1::stack_size - 0x10;
    initial.Rcx = seed;
    initial.Rdx = r1::data_base;
    source.set_context(initial);
    source.resume();
    phase = "source_initialization";
    require(source.next_break() == r1::code_base, "expected task initialization trap");
    std::cout << "TASK_INIT role=source pid=" << source.pid << " count=1\n";
    auto init = source.context();
    source.normalize_trap(init, r1::code_base);
    source.set_context(init);
    source.resume();
    phase = "capture_checkpoint";
    require(source.next_break() == r1::code_base + source.metadata.checkpoint_offset, "expected named checkpoint");
    std::cout << "CHECKPOINT role=source pid=" << source.pid << " count=1\n";
    auto snapshot = capture(source, hash);
    require(snapshot.cpu.Rsp >= r1::stack_base && snapshot.cpu.Rsp + 0x28 <= r1::stack_base + r1::stack_size,
        "managed stack pointer out of range");
    require(word(snapshot.regions[1], 0) == mem_expected &&
        word(snapshot.regions[2], static_cast<std::size_t>(snapshot.cpu.Rsp - r1::stack_base + 0x20)) == stack_expected &&
        snapshot.cpu.R15 == reg_expected && (snapshot.cpu.EFlags & 1), "captured state disagrees with independent oracle");
    std::cout << "CAPTURE_CONTEXT rip=" << hex(snapshot.cpu.Rip) << " rsp=" << hex(snapshot.cpu.Rsp)
        << " r15=" << hex(snapshot.cpu.R15) << " eflags=" << hex(snapshot.cpu.EFlags) << '\n';
    phase = "source_termination";
    source.stop();
    std::cout << "SOURCE_EXIT_CONFIRMED_BEFORE_RESTORE pid=" << source.pid << '\n';
    if (mode == "build-mismatch") snapshot.executable_hash[0] ^= 1;

    const int restores = mode == "positive" ? 2 : 1;
    for (int i = 0; i < restores; ++i) {
        phase = "target_ready";
        Child target(fixture, timeout);
        require(target.pid != source.pid, "target PID must differ from source");
        target.ready(export_offset);
        restore(target, snapshot, digest(read_file(fixture)), mode);
        phase = "resume_computation";
        target.resume();
        const auto trap = target.next_break();
        if (trap == r1::code_base + target.metadata.init_offset)
            fail("task_event", ERROR_INVALID_DATA, "TASK_INIT role=restore forbidden replay");
        if (trap == r1::code_base + target.metadata.checkpoint_offset)
            fail("task_event", ERROR_INVALID_DATA, "CHECKPOINT role=restore forbidden prelude replay");
        require(trap == r1::code_base + target.metadata.result_offset, "unexpected restored breakpoint");
        const auto result = read_value<std::uint64_t>(target.process.value, r1::data_base + 8);
        const auto actual_reg = read_value<std::uint64_t>(target.process.value, r1::data_base + 16);
        const auto actual_stack = read_value<std::uint64_t>(target.process.value, r1::data_base + 24);
        std::cout << "RESULT restore=" << i + 1 << " pid=" << target.pid << " actual=" << hex(result)
            << " expected=" << hex(expected) << " observed_memory="
            << hex(read_value<std::uint64_t>(target.process.value, r1::data_base))
            << " observed_r15=" << hex(actual_reg) << " observed_stack=" << hex(actual_stack) << '\n';
        std::cout << "EVENT_COUNTS role=restore init=0 checkpoint=0 result=1\n";
        phase = "target_termination";
        target.stop();
        phase = "validate_result";
        if (result != expected || actual_reg != reg_expected || actual_stack != stack_expected)
            fail("result_oracle", ERROR_INVALID_DATA, "RESULT_MISMATCH restored execution failed validation", 2);
    }
    std::cout << "R1_CONTROLLED_EXPERIMENT_PASS restores=" << restores << " source_init=1 source_checkpoint=1 restored_init=0\n";
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    try {
        std::filesystem::path fixture;
        std::string mode = "positive";
        DWORD timeout = 10000;
        for (int i = 1; i < argc; ++i) {
            const std::wstring option = argv[i];
            if (i + 1 == argc) fail("arguments", ERROR_BAD_ARGUMENTS, "missing argument value");
            if (option == L"--fixture") fixture = std::filesystem::absolute(argv[++i]);
            else if (option == L"--case") {
                const std::wstring text = argv[++i];
                mode.clear();
                for (const auto letter : text) {
                    require(letter > 0 && letter < 128, "case name must be ASCII");
                    mode.push_back(static_cast<char>(letter));
                }
            }
            else if (option == L"--timeout-ms") {
                const auto value = std::stoul(argv[++i]);
                require(value >= 100 && value <= 60000, "timeout must be 100..60000 ms");
                timeout = value;
            } else fail("arguments", ERROR_BAD_ARGUMENTS, "unknown option");
        }
        require(!fixture.empty(), "--fixture required");
        const std::vector<std::string> cases{"positive", "omit-memory", "omit-stack", "omit-register", "omit-flags",
            "address-conflict", "build-mismatch", "timeout", "unexpected-exception"};
        require(std::find(cases.begin(), cases.end(), mode) != cases.end(), "unknown test case");
        std::cout << "CASE " << mode << " timeout_ms=" << timeout << " pointer_bits=" << sizeof(void*) * 8 << '\n';
        return experiment(fixture, mode, timeout);
    } catch (const Failure& failure) {
        std::cout << "EXPERIMENT_FAILED " << failure.what() << '\n';
        return failure.exit_code;
    } catch (const std::exception& failure) {
        std::cout << "EXPERIMENT_FAILED phase=" << phase << " api=cpp_exception code=0 detail=" << failure.what() << '\n';
        return 1;
    }
}
