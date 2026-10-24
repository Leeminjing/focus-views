#include "debugger.hpp"
#include "files.hpp"

namespace {
using namespace r2;
State state(Branch& b) {
    phase = b.role + "/observe";
    return read_value<State>(b.process.value, data_base);
}
void assert_test(bool okay, const std::string& detail) {
    if (!okay) fail("experiment_assertion", ERROR_INVALID_DATA, detail, 2);
}
std::string fingerprint(const Snapshot& snapshot) {
    std::vector<std::uint8_t> bytes;
    for (const auto& region : snapshot.regions) bytes.insert(bytes.end(), region.bytes.begin(), region.bytes.end());
    const auto cpu = reinterpret_cast<const std::uint8_t*>(&snapshot.cpu);
    bytes.insert(bytes.end(), cpu, cpu + sizeof(snapshot.cpu));
    return digest_text(digest(bytes));
}
void issue(Debugger& debug, Branch& b, DWORD rva, Operation op, std::uint64_t argument = 0,
    std::uint64_t logical = 0, std::uint64_t parameter = 0, const std::string& input = "", bool wait = true) {
    require(b.published && b.halted == Halt::result, "input requires published idle branch");
    phase = b.role + "/input";
    // Only the declared input range is written. Results/value/pointers/stack are
    // never filled by the controller; their changes come from assembly execution.
    struct Input { std::uint64_t command, argument, logical, parameter; std::uint8_t bytes[16]{}; };
    static_assert(sizeof(Input) == 48);
    Input request{static_cast<std::uint64_t>(op), argument, logical, parameter};
    require(input.size() <= sizeof(request.bytes), "input too large");
    std::memcpy(request.bytes, input.data(), input.size());
    write_memory(b.process.value, data_base + offsetof(State, command), &request, sizeof(request));
    b.stage = "operation";
    std::cout << "INPUT role=" << b.role << " op=" << request.command << " arg=" << argument << " logical=" << logical << '\n';
    debug.resume(b, Halt::result);
    if (wait) debug.wait(b, Halt::result, rva);
}
void verify_value(Branch& b, std::uint64_t expected, std::uint64_t steps) {
    const auto observed = state(b);
    assert_test(observed.value == expected && observed.alias1 == data_base && observed.alias2 == data_base &&
        observed.alias_observed == expected && observed.steps == steps, "MEMORY_OR_ALIAS_MISMATCH role=" + b.role);
    std::cout << "MEMORY_VERIFIED role=" << b.role << " value=" << hex(observed.value) << " steps=" << observed.steps
        << " alias1=" << hex(observed.alias1) << " alias2=" << hex(observed.alias2) << '\n';
}

int experiment(const std::filesystem::path& fixture, const std::string& mode, const std::filesystem::path& workdir) {
    const bool with_files = mode != "memory";
    const auto binary = read_file(fixture);
    const auto hash = digest(binary);
    const auto rva = metadata_rva(binary, "R2Metadata");
    std::cout << "FIXTURE_SHA256 " << digest_text(hash) << '\n';
    std::map<Branch*, std::unique_ptr<Files>> resources;
    Debugger debug(rva);
    debug.file_service = [&](Branch& b) {
        const auto found = resources.find(&b);
        require(found != resources.end(), "no resource table for service PID");
        auto request = read_value<Request>(b.process.value, data_base + offsetof(State, request));
        found->second->service(request);
        // Service ABI returns raw OS results, not business-state reconstruction.
        write_memory(b.process.value, data_base + offsetof(State, request), &request, sizeof(request));
    };
    auto& source = debug.spawn(fixture, "source");
    debug.wait(source, Halt::ready, rva);
    if (with_files) {
        resources.emplace(&source, std::make_unique<Files>(workdir / "source", "source"));
        resources.at(&source)->seed();
    }
    std::uint64_t seed = 0;
    const auto status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) fail("BCryptGenRandom", static_cast<DWORD>(status), "NTSTATUS");
    const auto baseline = std::rotl(seed, 17) ^ 0xA17C9E3D52B468F1ULL;
    const auto stack = seed ^ 0x6E24B59A0173D8CFULL;
    const auto reg = std::rotl(seed, 31) + 0x13579BDF2468ACE1ULL;
    const auto receipt = std::rotl((baseline + reg + 1) ^ stack, 9);
    std::cout << "ORACLE seed=" << hex(seed) << " baseline=" << hex(baseline) << " receipt=" << hex(receipt) << '\n';
    std::vector<std::uint8_t> code(static_cast<std::size_t>(source.metadata.code_size));
    read_memory(source.process.value, source.metadata.template_address, code.data(), code.size());
    for (const auto offset : {std::uint64_t(0), source.metadata.checkpoint_offset, source.metadata.result_offset, source.metadata.service_offset})
        require(code[static_cast<std::size_t>(offset)] == 0xCC, "not a one-byte INT3 label");
    allocate(source, code_base, page_size);
    allocate(source, data_base, page_size);
    allocate(source, stack_base, stack_size);
    write_memory(source.process.value, code_base, code.data(), code.size());
    protect(source, Region{code_base, PAGE_EXECUTE_READ, std::vector<std::uint8_t>(page_size)});
    auto cpu = debug.context(source);
    cpu.Rip = code_base; cpu.Rsp = stack_base + stack_size - 0x10;
    cpu.Rcx = seed; cpu.Rdx = data_base; cpu.R8 = with_files ? 1 : 0;
    debug.set_context(source, cpu);
    debug.resume(source, Halt::init, false);
    debug.wait(source, Halt::init, rva);
    debug.resume(source, Halt::checkpoint);
    debug.wait(source, Halt::checkpoint, rva);
    const auto snapshot = capture(debug, source, hash);
    const FileSnapshot file_snapshot = with_files ? resources.at(&source)->capture() : FileSnapshot{};
    assert_test(state(source).value == baseline && snapshot.cpu.R15 == reg &&
        read_value<std::uint64_t>(source.process.value, snapshot.cpu.Rsp + 0x20) == stack, "source oracle mismatch");
    const auto fixed = fingerprint(snapshot);
    const auto files_fixed = files_fingerprint(file_snapshot);
    std::cout << "S0_FIXED sha256=" << fixed << '\n';
    debug.stop(source);
    if (with_files) resources.at(&source)->close_all();
    std::cout << "SOURCE_EXIT_CONFIRMED_BEFORE_RESTORE\n";

    Branch* failed_b = nullptr;
    Branch* existing_a = nullptr;
    auto unchanged = [&] {
        assert_test(fingerprint(snapshot) == fixed && files_fingerprint(file_snapshot) == files_fixed, "S0_MUTATED");
        std::cout << "S0_UNCHANGED sha256=" << fixed << " file_graph=" << files_fixed << '\n';
    };
    auto close_files = [&] {
        for (auto& [branch, files] : resources) {
            files->close_all();
            std::cout << "FILE_HANDLES_CLOSED role=" << branch->role << " count=" << files->handle_count() << '\n';
        }
    };
    auto create = [&](const std::string& role) -> Branch& {
        auto& b = debug.spawn(fixture, role);
        try {
            debug.wait(b, Halt::ready, rva);
            // Deterministic routing barrier: A's pending computation must be
            // observed while B exists but is still unpublished, regardless of
            // which PID the OS schedules first.
            if (role == "B" && existing_a) debug.wait(*existing_a, Halt::result, rva);
            restore_cpu(debug, b, snapshot, digest(read_file(fixture)));
            if (with_files) {
                auto files = std::make_unique<Files>(workdir / role, role);
                auto& table = *resources.emplace(&b, std::move(files)).first->second;
                std::string fault;
                if (role == "A" && (mode == "split-alias" || mode == "omit-position" || mode == "omit-content")) fault = mode;
                if (role == "B" && (mode == "shared-open" || mode == "partial-create")) fault = mode;
                auto image = file_snapshot;
                if (role == "B" && mode == "unsupported-resource") image.supported = false;
                Files* a_files = nullptr;
                for (auto& [branch, stored] : resources) if (branch->role == "A") a_files = stored.get();
                table.restore(image, fault, a_files);
            }
            debug.resume(b, Halt::result, false);
            debug.wait(b, Halt::result, rva);
            const auto initial = state(b);
            assert_test(initial.receipt == receipt && initial.stack_observed == stack && initial.register_observed == reg,
                "restored initial execution receipt mismatch");
            verify_value(b, baseline, 0);
            b.published = true;
            std::cout << "VIEW_PUBLISHED role=" << role << " pid=" << b.pid << '\n';
            return b;
        } catch (...) {
            const auto original = std::current_exception();
            if (role == "B") failed_b = &b;
            debug.stop(b);
            if (resources.contains(&b)) resources.at(&b)->rollback();
            require(!b.published && !b.alive, "failed branch was published or still alive");
            std::cout << "VIEW_ROLLBACK role=" << role << " published=false alive=false\n";
            std::rethrow_exception(original);
        }
    };
    auto& a = create("A");
    existing_a = &a;
    // Deliberately leave A running while B starts: the central pump must route
    // A's result even while create(B) is waiting for B's loader/ready events.
    issue(debug, a, rva, Operation::memory, 11, 0, 0, "", false);
    if (mode == "partial-create" || mode == "unsupported-resource") {
        bool rejected = false;
        try { create("B"); }
        catch (const Failure& error) {
            std::cout << "CREATION_REJECTED " << error.what() << '\n';
            rejected = error.exit_code == 3 && ((mode == "partial-create" &&
                std::string(error.what()).find("injected_create_failure") != std::string::npos) ||
                (mode == "unsupported-resource" && std::string(error.what()).find("unsupported snapshot resource") != std::string::npos));
            if (!rejected) throw;
        }
        assert_test(rejected && failed_b && !failed_b->alive && !failed_b->published, "half-created B escaped rollback");
        debug.wait(a, Halt::result, rva); verify_value(a, baseline + 11, 1);
        issue(debug, a, rva, Operation::memory, 7); verify_value(a, baseline + 18, 2);
        unchanged();
        auto& c = create("C");
        debug.alive({&a, &c});
        issue(debug, a, rva, Operation::memory, 1); issue(debug, c, rva, Operation::memory, 3);
        verify_value(a, baseline + 19, 3); verify_value(c, baseline + 3, 1);
        assert_test(resources.at(&a)->content() == file_snapshot.files[0].content &&
            resources.at(&c)->content() == file_snapshot.files[0].content, "rollback damaged file versions");
        issue(debug, a, rva, Operation::read, 0, state(a).h1, 2);
        assert_test(state(a).response == 2 && state(a).file_bytes[0] == '2' && state(a).file_bytes[1] == '3', "A file unusable after rollback");
        issue(debug, a, rva, Operation::tell, 0, state(a).h2);
        assert_test(state(a).response == 4, "A aliases damaged by B failure");
        issue(debug, c, rva, Operation::tell, 0, state(c).h1);
        assert_test(state(c).response == 2, "C did not restore S0 file position after B failure");
        unchanged(); debug.alive({&a, &c});
        std::cout << "NO_HALF_PUBLISHED_B A_CONTINUES C_FROM_S0\n";
        debug.stop_all(); close_files();
        std::cout << "REJECTION_PATH_VERIFIED case=" << mode << '\n';
        return 3; // B was rejected, never count it as a successful computation.
    }
    auto& b = create("B");
    debug.wait(a, Halt::result, rva);
    verify_value(a, baseline + 11, 1);
    verify_value(b, baseline, 0);
    debug.alive({&a, &b});
    issue(debug, b, rva, Operation::memory, 101);
    verify_value(a, baseline + 11, 1);
    verify_value(b, baseline + 101, 1);
    issue(debug, a, rva, Operation::memory, 7, 0, 0, "", false);
    issue(debug, b, rva, Operation::memory, 17, 0, 0, "", false);
    debug.wait(b, Halt::result, rva);
    debug.wait(a, Halt::result, rva);
    verify_value(a, baseline + 18, 2);
    verify_value(b, baseline + 118, 2);
    std::map<Branch*, std::uint64_t> file_commands;
    auto file_command = [&](Branch& view, Operation op, std::uint64_t handle, std::uint64_t arg = 0,
        std::uint64_t length = 0, const std::string& input = "") -> State {
        issue(debug, view, rva, op, arg, handle, length, input);
        ++file_commands[&view];
        const auto observed = state(view);
        const auto memory_steps = view.role == "C" ? 0ULL : 2ULL;
        assert_test(observed.steps == memory_steps + file_commands[&view] && observed.error == 0,
            "service did not return through executing workload");
        return observed;
    };
    auto tell = [&](Branch& view, std::uint64_t handle, std::uint64_t expected) {
        const auto actual = file_command(view, Operation::tell, handle).response;
        std::cout << "POSITION role=" << view.role << " logical=" << handle << " actual=" << actual << " expected=" << expected << '\n';
        assert_test(actual == expected, "FILE_POSITION_OR_ALIAS_MISMATCH role=" + view.role + " expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
    };
    auto read = [&](Branch& view, std::uint64_t handle, const std::string& expected) {
        const auto result = file_command(view, Operation::read, handle, 0, expected.size());
        assert_test(result.response == expected.size() && std::memcmp(result.file_bytes, expected.data(), expected.size()) == 0,
            "FILE_CONTENT_MISMATCH role=" + view.role);
        std::cout << "READ_VERIFIED role=" << view.role << " bytes=" << expected << '\n';
    };
    auto content = [&](Branch& view, const std::string& expected) {
        const auto actual = resources.at(&view)->content();
        assert_test(std::string(actual.begin(), actual.end()) == expected, "FILE_VERSION_ISOLATION_MISMATCH role=" + view.role);
        std::cout << "CONTENT_VERIFIED role=" << view.role << " bytes=" << expected << '\n';
    };
    if (with_files) {
        const std::map<std::string, Operation> unsupported{{"reject-async", Operation::asynchronous}, {"reject-map", Operation::mapping},
            {"reject-lock", Operation::locking}, {"reject-device", Operation::device}, {"reject-unknown", Operation::unknown}};
        if (unsupported.contains(mode)) file_command(a, unsupported.at(mode), state(a).h1);
        tell(a, state(a).h1, 2); tell(a, state(a).h2, 2); tell(a, state(a).h3, 6);
        tell(b, state(b).h1, 2); tell(b, state(b).h2, 2); tell(b, state(b).h3, 6);
        read(a, state(a).h1, "23"); tell(a, state(a).h2, 4); tell(a, state(a).h3, 6);
        tell(b, state(b).h2, 2);
        file_command(a, Operation::write, state(a).h2, 0, 2, "AA");
        content(a, "0123AA6789"); content(b, "0123456789");
        file_command(b, Operation::seek, state(b).h2, 7);
        file_command(b, Operation::write, state(b).h2, 0, 2, "BB");
        content(a, "0123AA6789"); content(b, "0123456BB9");
        tell(a, state(a).h2, 6); tell(b, state(b).h1, 9);
        file_command(a, Operation::close, state(a).h1);
        read(a, state(a).h2, "6"); tell(b, state(b).h2, 9);
        assert_test(resources.at(&a)->identity(state(a).h2) != resources.at(&b)->identity(state(b).h2), "branches share native file identity");
        std::cout << "DISTINCT_NATIVE_FILES A=" << hex(resources.at(&a)->identity(state(a).h2))
            << " B=" << hex(resources.at(&b)->identity(state(b).h2)) << '\n';
    }
    unchanged();
    auto& c = create("C");
    debug.alive({&a, &b, &c});
    verify_value(a, baseline + 18, 2 + file_commands[&a]); verify_value(b, baseline + 118, 2 + file_commands[&b]); verify_value(c, baseline, 0);
    if (with_files) {
        content(c, "0123456789");
        tell(c, state(c).h1, 2); tell(c, state(c).h2, 2); tell(c, state(c).h3, 6);
        read(c, state(c).h1, "23"); tell(c, state(c).h2, 4);
        file_command(c, Operation::write, state(c).h2, 0, 2, "CC");
        content(c, "0123CC6789"); content(a, "0123AA6789"); content(b, "0123456BB9");
        tell(a, state(a).h2, 7); tell(b, state(b).h2, 9);
        assert_test(resources.at(&c)->identity(state(c).h2) != resources.at(&a)->identity(state(a).h2) &&
            resources.at(&c)->identity(state(c).h2) != resources.at(&b)->identity(state(b).h2), "C shares native file identity");
    }
    issue(debug, a, rva, Operation::memory, 1, 0, 0, "", false);
    issue(debug, b, rva, Operation::memory, 2, 0, 0, "", false);
    issue(debug, c, rva, Operation::memory, 3, 0, 0, "", false);
    debug.wait(c, Halt::result, rva); debug.wait(a, Halt::result, rva); debug.wait(b, Halt::result, rva);
    verify_value(a, baseline + 19, 3 + file_commands[&a]); verify_value(b, baseline + 120, 3 + file_commands[&b]);
    verify_value(c, baseline + 3, 1 + file_commands[&c]);
    unchanged();
    debug.alive({&a, &b, &c});
    std::cout << (with_files ? "R2_002_FILE_PASS\n" : "R2_001_MEMORY_PASS\n");
    debug.stop_all();
    close_files();
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    try {
        std::filesystem::path fixture, workdir;
        std::string mode = "memory";
        for (int i = 1; i < argc; ++i) {
            const std::wstring option = argv[i];
            require(i + 1 < argc, "missing option value");
            if (option == L"--fixture") fixture = std::filesystem::absolute(argv[++i]);
            else if (option == L"--workdir") workdir = std::filesystem::absolute(argv[++i]);
            else if (option == L"--case") {
                mode.clear();
                for (const auto letter : std::wstring(argv[++i])) {
                    require(letter > 0 && letter < 128, "case name must be ASCII");
                    mode.push_back(static_cast<char>(letter));
                }
            } else require(false, "unknown option");
        }
        const std::vector<std::string> modes{"memory","files","shared-open","split-alias","omit-position","omit-content",
            "partial-create","unsupported-resource","reject-async","reject-map","reject-lock","reject-device","reject-unknown"};
        require(!fixture.empty() && std::find(modes.begin(), modes.end(), mode) != modes.end(), "invalid fixture or case");
        require(mode == "memory" || !workdir.empty(), "file cases require --workdir");
        std::cout << "CASE " << mode << '\n';
        return experiment(fixture, mode, workdir);
    } catch (const native::Failure& error) {
        std::cout << "EXPERIMENT_FAILED " << error.what() << '\n';
        return error.exit_code;
    } catch (const std::exception& error) {
        std::cout << "EXPERIMENT_FAILED phase=" << native::phase << " api=cpp_exception code=0 detail=" << error.what() << '\n';
        return 1;
    }
}
