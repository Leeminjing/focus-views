#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <array>
#include <vector>

namespace r1 {
constexpr std::uint64_t magic = 0x31524B4345484346;
constexpr std::uint64_t code_base = 0x000001B000000000;
constexpr std::uint64_t data_base = code_base + 0x10000;
constexpr std::uint64_t stack_base = code_base + 0x20000;
constexpr std::size_t page_size = 0x1000;
constexpr std::size_t stack_size = 0x10000;
constexpr DWORD context_flags = CONTEXT_CONTROL | CONTEXT_INTEGER;

// In-process fixture metadata, not a public ABI or serialized snapshot format.
struct Metadata {
    std::uint64_t signature = magic;
    std::uint64_t version = 1;
    std::uint64_t machine = IMAGE_FILE_MACHINE_AMD64;
    std::uint64_t ready = 0;
    std::uint64_t template_address = 0;
    std::uint64_t code_size = 0;
    std::uint64_t init_offset = 0;
    std::uint64_t checkpoint_offset = 0;
    std::uint64_t result_offset = 0;
    char build[65]{};
};

struct Region {
    std::uint64_t address;
    DWORD protection;
    std::vector<std::uint8_t> bytes;
};

struct Snapshot {
    WORD machine = IMAGE_FILE_MACHINE_AMD64;
    std::array<std::uint8_t, 32> executable_hash{};
    Metadata metadata{};
    CONTEXT cpu{}; // CONTROL + INTEGER only. No SIMD/XSTATE/OS structures.
    std::vector<Region> regions;
};
}
