#pragma once
#include "../r1/snapshot.hpp"
#include <cstddef>

namespace r2 {
using r1::code_base;
using r1::data_base;
using r1::stack_base;
using r1::page_size;
using r1::stack_size;
using r1::context_flags;
using r1::Region;
struct Metadata {
    std::uint64_t signature = r1::magic;
    std::uint64_t version = 2;
    std::uint64_t ready = 0;
    std::uint64_t template_address = 0;
    std::uint64_t code_size = 0;
    std::uint64_t checkpoint_offset = 0;
    std::uint64_t service_offset = 0;
    std::uint64_t result_offset = 0;
    char build[65]{};
};
enum class Operation : std::uint64_t {
    memory = 1, open = 10, read = 11, write = 12, seek = 13,
    duplicate = 14, close = 15, tell = 16,
    asynchronous = 20, mapping = 21, locking = 22, device = 23, unknown = 24
};
struct Request {
    std::uint64_t op, handle, argument, length;
    std::uint8_t bytes[16];
    std::uint64_t response, error;
};
struct State {
    std::uint64_t value, alias1, alias2, steps, receipt;
    // Only these input fields may be written by the controller after restore.
    std::uint64_t command, argument, logical, parameter;
    std::uint8_t input_bytes[16];
    std::uint64_t response, alias_observed, stack_observed, register_observed;
    std::uint8_t file_bytes[16];
    std::uint64_t error, h1, h2, h3;
    std::uint8_t padding[88];
    Request request;
};
static_assert(offsetof(State, command) == 40 && offsetof(State, request) == 256);
static_assert(offsetof(State, h1) == 144 && sizeof(Request) == 64);
struct Snapshot {
    std::array<std::uint8_t, 32> executable_hash{};
    Metadata metadata;
    CONTEXT cpu{};
    std::vector<Region> regions;
};
}
