#include "snapshot.hpp"
#include "build_id.hpp"
#include <cstring>
extern "C" void FixtureReady();
extern "C" unsigned char WorkloadStart, WorkloadCheckpoint, WorkloadService, WorkloadResult, WorkloadEnd;
extern "C" { __declspec(dllexport) r2::Metadata R2Metadata; }
int main() {
    R2Metadata.ready = reinterpret_cast<std::uint64_t>(&FixtureReady);
    const auto start = reinterpret_cast<std::uint64_t>(&WorkloadStart);
    R2Metadata.template_address = start;
    R2Metadata.code_size = reinterpret_cast<std::uint64_t>(&WorkloadEnd) - start;
    R2Metadata.checkpoint_offset = reinterpret_cast<std::uint64_t>(&WorkloadCheckpoint) - start;
    R2Metadata.service_offset = reinterpret_cast<std::uint64_t>(&WorkloadService) - start;
    R2Metadata.result_offset = reinterpret_cast<std::uint64_t>(&WorkloadResult) - start;
    std::memcpy(R2Metadata.build, R1_BUILD_ID, sizeof(R2Metadata.build));
    FixtureReady();
    return 90;
}
