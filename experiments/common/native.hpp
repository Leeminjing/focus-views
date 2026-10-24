#pragma once
#include "../r1/snapshot.hpp"
#include <bcrypt.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace native {
inline std::string phase = "startup";
std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}
struct Failure : std::runtime_error {
    int exit_code;
    Failure(int result, const std::string& api, DWORD code, const std::string& detail)
        : std::runtime_error("phase=" + phase + " api=" + api + " code=" + hex(code) +
                             " detail=" + detail), exit_code(result) {}
};
[[noreturn]] void fail(const char* api, DWORD code, const std::string& detail, int result = 1) {
    throw Failure(result, api, code, detail);
}
void require(bool condition, const char* detail) {
    if (!condition) fail("validate", ERROR_INVALID_DATA, detail);
}
void checked(BOOL success, const char* api) {
    if (!success) fail(api, GetLastError(), "Windows API failed");
}
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) fail("ifstream", ERROR_FILE_NOT_FOUND, path.string());
    const auto size = input.tellg();
    require(size > 0 && size < 64 * 1024 * 1024, "fixture file size invalid");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
        fail("ifstream.read", ERROR_READ_FAULT, path.string());
    return bytes;
}
std::array<std::uint8_t, 32> digest(const std::vector<std::uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) fail("BCryptOpenAlgorithmProvider", static_cast<DWORD>(status), "NTSTATUS");
    std::array<std::uint8_t, 32> result{};
    status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), result.data(), static_cast<ULONG>(result.size()));
    const auto close_status = BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) fail("BCryptHash", static_cast<DWORD>(status), "NTSTATUS");
    if (close_status < 0) fail("BCryptCloseAlgorithmProvider", static_cast<DWORD>(close_status), "NTSTATUS");
    return result;
}
std::string digest_text(const std::array<std::uint8_t, 32>& bytes) {
    std::ostringstream out;
    for (auto byte : bytes) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return out.str();
}

// Resolve just the exported metadata RVA from the PE file; do not load or run it.
template<class T> T file_at(const std::vector<std::uint8_t>& file, std::size_t offset) {
    require(offset <= file.size() && sizeof(T) <= file.size() - offset, "PE range invalid");
    T result;
    std::memcpy(&result, file.data() + offset, sizeof(T));
    return result;
}
DWORD metadata_rva(const std::vector<std::uint8_t>& file, const char* wanted = "R1Metadata") {
    const auto dos = file_at<IMAGE_DOS_HEADER>(file, 0);
    require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0, "invalid DOS header");
    const auto pe = file_at<IMAGE_NT_HEADERS64>(file, dos.e_lfanew);
    require(pe.Signature == IMAGE_NT_SIGNATURE && pe.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        pe.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC, "fixture must be PE x64");
    const auto sections = static_cast<std::size_t>(dos.e_lfanew) + sizeof(DWORD) +
        sizeof(IMAGE_FILE_HEADER) + pe.FileHeader.SizeOfOptionalHeader;
    auto offset = [&](DWORD rva) -> std::size_t {
        if (rva < pe.OptionalHeader.SizeOfHeaders) return rva;
        for (WORD i = 0; i < pe.FileHeader.NumberOfSections; ++i) {
            const auto section = file_at<IMAGE_SECTION_HEADER>(file, sections + i * sizeof(IMAGE_SECTION_HEADER));
            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < section.SizeOfRawData)
                return static_cast<std::size_t>(section.PointerToRawData) + rva - section.VirtualAddress;
        }
        fail("parse_PE", ERROR_INVALID_DATA, "unmapped export RVA");
    };
    const auto directory = pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    require(directory.VirtualAddress != 0, "missing exports");
    const auto exports = file_at<IMAGE_EXPORT_DIRECTORY>(file, offset(directory.VirtualAddress));
    for (DWORD i = 0; i < exports.NumberOfNames; ++i) {
        const auto name_rva = file_at<DWORD>(file, offset(exports.AddressOfNames) + i * sizeof(DWORD));
        const auto name_offset = offset(name_rva);
        const auto wanted_size = std::strlen(wanted) + 1;
        if (name_offset + wanted_size > file.size() ||
            std::memcmp(file.data() + name_offset, wanted, wanted_size) != 0) continue;
        const auto ordinal = file_at<WORD>(file, offset(exports.AddressOfNameOrdinals) + i * sizeof(WORD));
        require(ordinal < exports.NumberOfFunctions, "invalid export ordinal");
        return file_at<DWORD>(file, offset(exports.AddressOfFunctions) + ordinal * sizeof(DWORD));
    }
    fail("parse_PE", ERROR_PROC_NOT_FOUND, "metadata export missing");
}

void read_memory(HANDLE process, std::uint64_t address, void* output, std::size_t size) {
    SIZE_T actual = 0;
    checked(ReadProcessMemory(process, reinterpret_cast<void*>(address), output, size, &actual), "ReadProcessMemory");
    require(actual == size, "partial memory read");
}
void write_memory(HANDLE process, std::uint64_t address, const void* input, std::size_t size) {
    SIZE_T actual = 0;
    checked(WriteProcessMemory(process, reinterpret_cast<void*>(address), input, size, &actual), "WriteProcessMemory");
    require(actual == size, "partial memory write");
}
template<class T> T read_value(HANDLE process, std::uint64_t address) {
    T result{};
    read_memory(process, address, &result, sizeof(result));
    return result;
}

}
