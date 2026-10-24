#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace r3 {
constexpr std::uint64_t magic = 0x3152335749564646;
constexpr UINT observe_message = WM_APP + 17; // READ-ONLY diagnostic request
struct Telemetry {
    std::uint64_t signature = magic;
    DWORD pid = 0, tid = 0;
    std::uint64_t top = 0, edit = 0, button = 0, drag = 0;
    std::uint64_t update = 0, draws = 0, prints = 0, last_print_update = 0;
    std::uint64_t last_draw_update = 0, last_draw_ms = 0;
    std::uint64_t events = 0, ctrl_down = 0, ctrl_up = 0, characters = 0, clicks = 0, drag_moves = 0, capture_changes = 0;
    std::uint64_t focus = 0, capture = 0;
    SHORT key_ctrl = 0, key_shift = 0, key_left = 0, async_ctrl = 0, async_shift = 0, async_left = 0;
    BOOL keyboard_ok = FALSE;
    DWORD keyboard_error = 0;
    BYTE keyboard[256]{};
    DWORD selection_start = 0, selection_end = 0;
    int drag_x = 40, drag_y = 40;
    wchar_t text[128]{}, desktop[128]{};
};
struct Shared { volatile LONG sequence = 0; Telemetry data; };
struct Reply {
    volatile LONG done = 0;
    DWORD error = 0, api_result = 0, print_called = 0;
    std::uint64_t before_update = 0, after_update = 0, before_prints = 0, after_prints = 0, print_update = 0;
    std::uint64_t duration_ms = 0;
    std::uint64_t before_draws = 0, after_draws = 0, api_begin_ms = 0, api_end_ms = 0;
    int width = 0, height = 0;
    int client_dx = 0, client_dy = 0;
    wchar_t desktop[128]{};
    char api[64]{};
};
struct Error : std::runtime_error {
    DWORD code;
    std::string api;
    Error(const std::string& name, DWORD value, const std::string& detail = "")
        : std::runtime_error(name + " code=" + std::to_string(value) + " " + detail), code(value), api(name) {}
};
inline void check(BOOL okay, const char* api) { if (!okay) throw Error(api, GetLastError()); }
inline void demand(bool okay, const char* detail) { if (!okay) throw Error("validate", ERROR_INVALID_DATA, detail); }
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value = std::exchange(other.value, nullptr); }
        return *this;
    }
    void close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = nullptr; }
    ~Handle() { close(); }
};
inline std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw Error("WideCharToMultiByte", GetLastError());
    std::string result(length, '\0');
    check(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr), "WideCharToMultiByte");
    return result;
}
inline std::string json(const std::string& text) {
    std::string result = "\"";
    for (const unsigned char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { const char digits[] = "0123456789abcdef"; result += "\\u00"; result += digits[c >> 4]; result += digits[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
inline std::string json(const wchar_t* text) { return json(utf8(text)); }
inline std::wstring user_object_name(HANDLE object) {
    wchar_t name[256]{};
    DWORD actual = 0;
    check(GetUserObjectInformationW(object, UOI_NAME, name, sizeof(name), &actual), "GetUserObjectInformationW");
    return name;
}
inline std::wstring thread_desktop() { return user_object_name(GetThreadDesktop(GetCurrentThreadId())); }
struct Mapping {
    Handle handle;
    void* view = nullptr;
    Mapping(const std::wstring& name, std::size_t size, bool create) {
        if (create) handle.value = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(size), name.c_str());
        else handle.value = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!handle.value) throw Error(create ? "CreateFileMappingW" : "OpenFileMappingW", GetLastError());
        view = MapViewOfFile(handle.value, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!view) throw Error("MapViewOfFile", GetLastError());
    }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    ~Mapping() { if (view) UnmapViewOfFile(view); }
};
inline Telemetry read_shared(const Shared& shared) {
    for (int retry = 0; retry < 1000; ++retry) {
        const auto before = shared.sequence;
        if (before & 1) { Sleep(1); continue; }
        MemoryBarrier();
        Telemetry result;
        std::memcpy(&result, &shared.data, sizeof(result));
        MemoryBarrier();
        if (before == shared.sequence) return result;
    }
    throw Error("read_shared", ERROR_TIMEOUT, "seqlock did not settle");
}
inline std::string telemetry_json(const Telemetry& t) {
    std::ostringstream out;
    out << "{\"pid\":" << t.pid << ",\"tid\":" << t.tid << ",\"desktop\":" << json(t.desktop)
        << ",\"top\":" << t.top << ",\"edit\":" << t.edit << ",\"button\":" << t.button << ",\"drag\":" << t.drag
        << ",\"update\":" << t.update << ",\"draws\":" << t.draws << ",\"prints\":" << t.prints << ",\"printUpdate\":" << t.last_print_update
        << ",\"lastDrawUpdate\":" << t.last_draw_update << ",\"lastDrawMs\":" << t.last_draw_ms
        << ",\"events\":" << t.events << ",\"ctrlDownMessages\":" << t.ctrl_down << ",\"ctrlUpMessages\":" << t.ctrl_up
        << ",\"charMessages\":" << t.characters << ",\"buttonClicks\":" << t.clicks << ",\"dragMoves\":" << t.drag_moves
        << ",\"captureChanges\":" << t.capture_changes << ",\"focus\":" << t.focus << ",\"capture\":" << t.capture
        << ",\"GetKeyStateCtrl\":" << t.key_ctrl << ",\"GetKeyStateShift\":" << t.key_shift << ",\"GetKeyStateLeft\":" << t.key_left
        << ",\"GetAsyncKeyStateCtrl\":" << t.async_ctrl << ",\"GetAsyncKeyStateShift\":" << t.async_shift
        << ",\"GetAsyncKeyStateLeft\":" << t.async_left << ",\"GetKeyboardStateOk\":" << (t.keyboard_ok ? "true" : "false")
        << ",\"keyboardError\":" << t.keyboard_error << ",\"keyboardCtrl\":" << unsigned(t.keyboard[VK_CONTROL])
        << ",\"keyboardShift\":" << unsigned(t.keyboard[VK_SHIFT]) << ",\"keyboardLeft\":" << unsigned(t.keyboard[VK_LBUTTON])
        << ",\"selectionStart\":" << t.selection_start << ",\"selectionEnd\":" << t.selection_end
        << ",\"dragX\":" << t.drag_x << ",\"dragY\":" << t.drag_y << ",\"text\":" << json(t.text) << '}';
    return out.str();
}
struct Args {
    std::map<std::wstring, std::wstring> values;
    Args(int argc, wchar_t** argv) {
        for (int i = 1; i < argc; i += 2) {
            demand(i + 1 < argc && std::wstring(argv[i]).starts_with(L"--"), "options must be --name value pairs");
            demand(values.emplace(argv[i], argv[i+1]).second, "duplicate option");
        }
    }
    std::wstring get(const wchar_t* name, const std::wstring& fallback = L"") const {
        const auto it = values.find(name); return it == values.end() ? fallback : it->second;
    }
    std::uint64_t number(const wchar_t* name, std::uint64_t fallback = 0) const {
        const auto text = get(name); return text.empty() ? fallback : std::stoull(text, nullptr, 0);
    }
};
int fixture_main(const Args& args);
int bridge_main(const Args& args);
}
