#include "snapshot.hpp"
#include "build_id.hpp"
#include <cstring>

extern "C" void FixtureReady();
extern "C" unsigned char WorkloadStart, WorkloadCheckpoint, WorkloadResult, WorkloadEnd;
extern "C" {
__declspec(dllexport) r1::Metadata R1Metadata;
}

int main(int argc, char** argv) {
    // Preparation only: the fixture never saves, restores or invokes the task.
    // These two fault modes test controller timeouts and exception cleanup.
    if (argc == 2 && std::strcmp(argv[1], "--hang-ready") == 0) Sleep(INFINITE);
    if (argc == 2 && std::strcmp(argv[1], "--fault-ready") == 0)
        RaiseException(0xE0421001, EXCEPTION_NONCONTINUABLE, 0, nullptr);

    R1Metadata.ready = reinterpret_cast<std::uint64_t>(&FixtureReady);
    const auto start = reinterpret_cast<std::uint64_t>(&WorkloadStart);
    R1Metadata.template_address = start;
    R1Metadata.code_size = reinterpret_cast<std::uint64_t>(&WorkloadEnd) - start;
    R1Metadata.init_offset = 0;
    R1Metadata.checkpoint_offset = reinterpret_cast<std::uint64_t>(&WorkloadCheckpoint) - start;
    R1Metadata.result_offset = reinterpret_cast<std::uint64_t>(&WorkloadResult) - start;
    std::memcpy(R1Metadata.build, R1_BUILD_ID, sizeof(R1Metadata.build));
    FixtureReady();
    return 90; // must never execute: controlled work terminates under debugger
}
