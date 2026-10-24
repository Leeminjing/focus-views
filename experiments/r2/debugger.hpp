#pragma once
#include "snapshot.hpp"
#include "../common/native.hpp"
#include <functional>
#include <map>
#include <memory>

namespace r2 {
using namespace native;
enum class Halt { none, ready, init, checkpoint, result };
struct Branch {
    std::string role, stage = "prepare";
    Handle process, thread;
    DWORD pid = 0, tid = 0;
    std::uint64_t image = 0, ntdll_begin = 0, ntdll_end = 0;
    Metadata metadata;
    DEBUG_EVENT event{};
    Halt expected = Halt::ready, halted = Halt::none;
    bool pending = false, alive = true, running = true, startup_seen = false;
    bool published = false, stopping = false, in_service = false;
    ULONGLONG deadline = 0;
    unsigned init_count = 0, checkpoint_count = 0, result_count = 0, service_count = 0;
};

// One creating/debugging thread owns ALL events. No per-child WaitForDebugEvent.
class Debugger {
    std::map<DWORD, std::unique_ptr<Branch>> branches_;
    DWORD owner_ = GetCurrentThreadId(), timeout_, metadata_offset_;
    bool emergency_ = false;
    void owner() { require(owner_ == GetCurrentThreadId(), "debugger used on non-owner thread"); }
    void continue_event(Branch& b) {
        require(b.pending, "no per-PID pending event");
        checked(ContinueDebugEvent(b.pid, b.event.dwThreadId, DBG_CONTINUE), "ContinueDebugEvent");
        b.pending = false;
        b.halted = Halt::none;
    }
public:
    std::function<void(Branch&)> file_service;
    explicit Debugger(DWORD metadata_offset, DWORD timeout = 10000) : timeout_(timeout), metadata_offset_(metadata_offset) {}
    ~Debugger() {
        emergency_ = true;
        for (auto& [pid, b] : branches_) {
            (void)pid;
            if (b->alive) {
                try { stop(*b); }
                catch (const std::exception& error) { std::cout << "CLEANUP_FAILED " << error.what() << '\n'; }
            }
        }
    }
    Branch& spawn(const std::filesystem::path& fixture, const std::string& role) {
        owner();
        auto b = std::make_unique<Branch>();
        b->role = role;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION info{};
        std::wstring command = L"\"" + fixture.wstring() + L"\"";
        checked(CreateProcessW(fixture.c_str(), command.data(), nullptr, nullptr, FALSE,
            DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr, fixture.parent_path().c_str(), &startup, &info), "CreateProcessW");
        b->process.value = info.hProcess;
        b->thread.value = info.hThread;
        b->pid = info.dwProcessId;
        b->tid = info.dwThreadId;
        b->deadline = GetTickCount64() + timeout_;
        const auto pid = b->pid;
        require(!branches_.contains(pid), "PID reuse within experiment");
        std::cout << "PROCESS_CREATED role=" << role << " pid=" << pid << " tid=" << b->tid << '\n';
        return *branches_.emplace(pid, std::move(b)).first->second;
    }
    CONTEXT context(Branch& b) {
        require(b.pending, "context requires stopped PID");
        CONTEXT cpu{};
        cpu.ContextFlags = context_flags;
        checked(GetThreadContext(b.thread.value, &cpu), "GetThreadContext");
        return cpu;
    }
    void set_context(Branch& b, const CONTEXT& cpu) {
        require(b.pending, "set context requires stopped PID");
        checked(SetThreadContext(b.thread.value, &cpu), "SetThreadContext");
    }
    void resume(Branch& b, Halt expected, bool advance_trap = true) {
        owner();
        require(b.alive && b.pending && b.event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT, "resume requires own exception event");
        if (advance_trap) {
            auto cpu = context(b);
            const auto trap = reinterpret_cast<std::uint64_t>(b.event.u.Exception.ExceptionRecord.ExceptionAddress);
            require(cpu.Rip == trap || cpu.Rip == trap + 1, "unexpected INT3 convention");
            cpu.Rip = trap + 1;
            set_context(b, cpu);
        }
        b.expected = expected;
        b.running = true;
        b.deadline = GetTickCount64() + timeout_;
        continue_event(b);
    }
    void pump(DWORD metadata_offset) {
        owner();
        for (const auto& [pid, b] : branches_) {
            if (!emergency_ && b->alive && b->running && !b->stopping && GetTickCount64() >= b->deadline) {
                phase = b->role + "/" + b->stage;
                fail("WaitForDebugEvent", ERROR_TIMEOUT, "per-branch deadline pid=" + std::to_string(pid), 4);
            }
        }
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 50)) {
            const auto error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT) return;
            fail("WaitForDebugEvent", error, "central event pump");
        }
        const auto found = branches_.find(event.dwProcessId);
        require(found != branches_.end(), "event from unknown PID");
        auto& b = *found->second;
        require(!b.pending, "second event while PID has uncontinued event");
        b.event = event;
        b.pending = true;
        phase = b.role + "/" + b.stage;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            b.image = reinterpret_cast<std::uint64_t>(event.u.CreateProcessInfo.lpBaseOfImage);
            if (event.u.CreateProcessInfo.hFile) checked(CloseHandle(event.u.CreateProcessInfo.hFile), "CloseHandle.debug_image");
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            Handle file(event.u.LoadDll.hFile);
            if (file.value) {
                wchar_t path[32768]{};
                const auto length = GetFinalPathNameByHandleW(file.value, path, 32768, FILE_NAME_NORMALIZED);
                if (!length) fail("GetFinalPathNameByHandleW", GetLastError(), "debug DLL");
                require(length < 32768, "DLL path too long");
                if (_wcsicmp(std::filesystem::path(path).filename().c_str(), L"ntdll.dll") == 0) {
                    b.ntdll_begin = reinterpret_cast<std::uint64_t>(event.u.LoadDll.lpBaseOfDll);
                    const auto dos = read_value<IMAGE_DOS_HEADER>(b.process.value, b.ntdll_begin);
                    b.ntdll_end = b.ntdll_begin + read_value<IMAGE_NT_HEADERS64>(b.process.value,
                        b.ntdll_begin + dos.e_lfanew).OptionalHeader.SizeOfImage;
                }
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            const auto code = event.u.ExitProcess.dwExitCode;
            continue_event(b);
            const auto wait = WaitForSingleObject(b.process.value, 1000);
            if (wait != WAIT_OBJECT_0) fail("WaitForSingleObject", wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT, "exit handle");
            b.alive = b.running = b.published = false;
            std::cout << "PROCESS_TERMINATED_CONFIRMED role=" << b.role << " pid=" << b.pid << " code=" << hex(code) << '\n';
            if (!b.stopping && !emergency_) fail("debug_exit", code, "unexpected exit");
            return;
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const auto& exception = event.u.Exception;
            if (emergency_ || b.stopping) { b.running = false; return; }
            const auto trap = reinterpret_cast<std::uint64_t>(exception.ExceptionRecord.ExceptionAddress);
            if (exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT || !exception.dwFirstChance)
                fail("debug_exception", exception.ExceptionRecord.ExceptionCode, "pid=" + std::to_string(b.pid) + " address=" + hex(trap));
            if (!b.startup_seen && trap >= b.ntdll_begin && trap < b.ntdll_end) {
                b.startup_seen = true;
                std::cout << "SYSTEM_STARTUP_BREAKPOINT role=" << b.role << " pid=" << b.pid << '\n';
            } else {
                require(event.dwThreadId == b.tid, "breakpoint from non-managed TID");
                Halt actual = Halt::none;
                if (b.expected == Halt::ready) {
                    b.metadata = read_value<Metadata>(b.process.value, b.image + metadata_offset);
                    require(b.metadata.signature == r1::magic && b.metadata.version == 2 && b.metadata.ready == trap,
                        "not fixture ready breakpoint");
                    require(b.metadata.code_size > 0 && b.metadata.code_size <= page_size &&
                        b.metadata.checkpoint_offset < b.metadata.result_offset && b.metadata.result_offset < b.metadata.code_size &&
                        b.metadata.service_offset < b.metadata.code_size && b.metadata.build[64] == 0, "invalid metadata layout");
                    actual = Halt::ready;
                    DWORD shadow_flags = 0;
                    if (GetProcessMitigationPolicy(b.process.value, static_cast<PROCESS_MITIGATION_POLICY>(15), &shadow_flags, sizeof(shadow_flags)))
                        std::cout << "MITIGATION role=" << b.role << " shadow_stack_flags=" << hex(shadow_flags) << '\n';
                    else std::cout << "MITIGATION_QUERY_UNAVAILABLE role=" << b.role << " api=GetProcessMitigationPolicy code=" << hex(GetLastError()) << '\n';
                    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamic{};
                    checked(GetProcessMitigationPolicy(b.process.value, ProcessDynamicCodePolicy, &dynamic, sizeof(dynamic)), "GetProcessMitigationPolicy");
                    std::cout << "MITIGATION role=" << b.role << " dynamic_code_flags=" << hex(dynamic.Flags) << '\n';
                } else if (trap == code_base) {
                    ++b.init_count;
                    std::cout << "TASK_INIT role=" << b.role << " pid=" << b.pid << '\n';
                    require(b.role == "source", "restored branch replayed initialization");
                    actual = Halt::init;
                } else if (trap == code_base + b.metadata.checkpoint_offset) {
                    ++b.checkpoint_count;
                    std::cout << "CHECKPOINT role=" << b.role << " pid=" << b.pid << '\n';
                    require(b.role == "source", "restored branch replayed original checkpoint");
                    actual = Halt::checkpoint;
                } else if (trap == code_base + b.metadata.result_offset) {
                    ++b.result_count;
                    actual = Halt::result;
                } else if (trap == code_base + b.metadata.service_offset) {
                    require(static_cast<bool>(file_service), "file service not installed");
                    b.in_service = true;
                    try { file_service(b); } catch (...) { b.in_service = false; throw; }
                    b.in_service = false;
                    ++b.service_count;
                    resume(b, b.expected);
                    return;
                }
                require(actual != Halt::none && actual == b.expected, "unexpected per-branch breakpoint or state");
                b.halted = actual;
                b.running = false;
                std::cout << "HALT role=" << b.role << " pid=" << b.pid << " kind=" << static_cast<int>(actual)
                    << " init=" << b.init_count << " checkpoint=" << b.checkpoint_count << " results=" << b.result_count << '\n';
                return;
            }
        } else if (event.dwDebugEventCode == RIP_EVENT) {
            fail("RIP_EVENT", event.u.RipInfo.dwError, "debugger error");
        }
        continue_event(b);
    }
    void wait(Branch& b, Halt target, DWORD metadata_offset) {
        while (b.alive && (b.running || b.halted != target)) pump(metadata_offset);
        require(b.alive && b.halted == target, "wait target exited");
    }
    void stop(Branch& b) {
        owner();
        if (!b.alive) return;
        b.stopping = true;
        if (!TerminateProcess(b.process.value, 0xE0420000))
            fail("TerminateProcess", GetLastError(), "own branch " + b.role);
        if (b.pending) continue_event(b);
        const auto deadline = GetTickCount64() + 5000;
        while (b.alive && GetTickCount64() < deadline) {
            pump(metadata_offset_);
        }
        if (b.alive) fail("cleanup", ERROR_TIMEOUT, "PID-specific exit not confirmed", 4);
        std::cout << "EVENT_COUNTS role=" << b.role << " init=" << b.init_count << " checkpoint=" << b.checkpoint_count
            << " results=" << b.result_count << " services=" << b.service_count << '\n';
    }
    void stop_all() { for (auto& [pid, b] : branches_) { (void)pid; stop(*b); } }
    void alive(const std::vector<Branch*>& views) {
        std::cout << "SIMULTANEOUSLY_ALIVE";
        for (auto b : views) {
            require(b->alive && b->published && WaitForSingleObject(b->process.value, 0) == WAIT_TIMEOUT, "branch not alive/published");
            std::cout << ' ' << b->role << '=' << b->pid;
        }
        std::cout << '\n';
    }
};

inline void allocate(Branch& b, std::uint64_t address, std::size_t size) {
    auto memory = VirtualAllocEx(b.process.value, reinterpret_cast<void*>(address), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) fail("VirtualAllocEx", GetLastError(), "exact address " + hex(address), 3);
    require(reinterpret_cast<std::uint64_t>(memory) == address, "allocation moved");
}
inline void protect(Branch& b, const Region& region) {
    DWORD old = 0;
    checked(VirtualProtectEx(b.process.value, reinterpret_cast<void*>(region.address), region.bytes.size(), region.protection, &old), "VirtualProtectEx");
    if (region.protection == PAGE_EXECUTE_READ)
        checked(FlushInstructionCache(b.process.value, reinterpret_cast<void*>(region.address), region.bytes.size()), "FlushInstructionCache");
}
inline Snapshot capture(Debugger& debug, Branch& b, const std::array<std::uint8_t, 32>& hash) {
    require(b.halted == Halt::checkpoint && !b.in_service, "snapshot requires completed services at safe checkpoint");
    Snapshot snapshot;
    snapshot.metadata = b.metadata;
    snapshot.executable_hash = hash;
    snapshot.cpu = debug.context(b);
    const auto trap = code_base + b.metadata.checkpoint_offset;
    require(snapshot.cpu.Rip == trap || snapshot.cpu.Rip == trap + 1, "checkpoint RIP mismatch");
    snapshot.cpu.Rip = trap + 1;
    for (const auto [address, size, protection] : {
        std::tuple{code_base, page_size, DWORD(PAGE_EXECUTE_READ)},
        std::tuple{data_base, page_size, DWORD(PAGE_READWRITE)},
        std::tuple{stack_base, stack_size, DWORD(PAGE_READWRITE)}}) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(b.process.value, reinterpret_cast<void*>(address), &info, sizeof(info)))
            fail("VirtualQueryEx", GetLastError(), "capture region");
        require(info.State == MEM_COMMIT && info.Protect == protection && info.RegionSize >= size, "region boundary/protection mismatch");
        Region region{address, protection, std::vector<std::uint8_t>(size)};
        read_memory(b.process.value, address, region.bytes.data(), size);
        std::cout << "CAPTURE_REGION address=" << hex(address) << " size=" << size << " sha256=" << digest_text(digest(region.bytes)) << '\n';
        snapshot.regions.push_back(std::move(region));
    }
    std::cout << "CAPTURE_CONTEXT rip=" << hex(snapshot.cpu.Rip) << " rsp=" << hex(snapshot.cpu.Rsp)
        << " r15=" << hex(snapshot.cpu.R15) << '\n';
    return snapshot;
}
inline void restore_cpu(Debugger& debug, Branch& b, const Snapshot& snapshot, const std::array<std::uint8_t,32>& hash) {
    phase = b.role + "/restore";
    if (snapshot.executable_hash != hash || std::memcmp(snapshot.metadata.build, b.metadata.build, 65) != 0)
        fail("validate_snapshot", ERROR_REVISION_MISMATCH, "build mismatch", 3);
    require(snapshot.metadata.code_size == b.metadata.code_size && snapshot.metadata.checkpoint_offset == b.metadata.checkpoint_offset &&
        snapshot.metadata.service_offset == b.metadata.service_offset && snapshot.metadata.result_offset == b.metadata.result_offset, "layout mismatch");
    for (const auto& region : snapshot.regions) {
        allocate(b, region.address, region.bytes.size());
        write_memory(b.process.value, region.address, region.bytes.data(), region.bytes.size());
        protect(b, region);
        std::vector<std::uint8_t> readback(region.bytes.size());
        read_memory(b.process.value, region.address, readback.data(), readback.size());
        require(readback == region.bytes, "raw restore readback mismatch");
    }
    debug.set_context(b, snapshot.cpu);
    const auto actual = debug.context(b);
    require(actual.Rip == snapshot.cpu.Rip && actual.Rsp == snapshot.cpu.Rsp && actual.R15 == snapshot.cpu.R15 &&
        actual.R12 == snapshot.cpu.R12 && (actual.EFlags & 1) == (snapshot.cpu.EFlags & 1), "context restore mismatch");
    std::cout << "RESTORED_CONTEXT role=" << b.role << " pid=" << b.pid << " rip=" << hex(actual.Rip)
        << " rsp=" << hex(actual.Rsp) << " r15=" << hex(actual.R15) << '\n';
}
}
