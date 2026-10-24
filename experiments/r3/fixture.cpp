#include "protocol.hpp"
#include <windowsx.h>

namespace r3 {
namespace {
Shared* shared = nullptr;
Telemetry current;
HWND top = nullptr, edit = nullptr, button = nullptr, drag = nullptr;
WNDPROC edit_original = nullptr, button_original = nullptr;
std::ofstream trace;
std::wstring role;

void observe() {
    // All calls execute on the actual GUI thread. No virtual key state table.
    current.focus = reinterpret_cast<std::uint64_t>(GetFocus());
    current.capture = reinterpret_cast<std::uint64_t>(GetCapture());
    current.key_ctrl = GetKeyState(VK_CONTROL);
    current.key_shift = GetKeyState(VK_SHIFT);
    current.key_left = GetKeyState(VK_LBUTTON);
    current.async_ctrl = GetAsyncKeyState(VK_CONTROL);
    current.async_shift = GetAsyncKeyState(VK_SHIFT);
    current.async_left = GetAsyncKeyState(VK_LBUTTON);
    current.keyboard_ok = GetKeyboardState(current.keyboard);
    current.keyboard_error = current.keyboard_ok ? 0 : GetLastError();
    if (edit && IsWindow(edit)) {
        GetWindowTextW(edit, current.text, 128);
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&current.selection_start), reinterpret_cast<LPARAM>(&current.selection_end));
    }
    InterlockedIncrement(&shared->sequence);
    std::memcpy(&shared->data, &current, sizeof(current));
    MemoryBarrier();
    InterlockedIncrement(&shared->sequence);
}
void record(const char* event, HWND window = nullptr, UINT message = 0, WPARAM wp = 0, LPARAM lp = 0) {
    ++current.events;
    observe();
    trace << "{\"event\":" << json(event) << ",\"tickMs\":" << GetTickCount64()
        << ",\"queryThread\":" << GetCurrentThreadId()
        << ",\"window\":" << reinterpret_cast<std::uint64_t>(window) << ",\"message\":" << message
        << ",\"wParam\":" << wp << ",\"lParam\":" << lp << ",\"state\":" << telemetry_json(current) << "}\n";
    trace.flush();
}
bool tracked(UINT message) {
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONUP || message == WM_MOUSEMOVE || message == WM_CAPTURECHANGED || message == WM_SETFOCUS || message == WM_KILLFOCUS;
}
LRESULT control_impl(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    const auto original = window == edit ? edit_original : button_original;
    if (tracked(message)) {
        if (message == WM_KEYDOWN && wp == VK_CONTROL) ++current.ctrl_down;
        if (message == WM_KEYUP && wp == VK_CONTROL) ++current.ctrl_up;
        if (message == WM_CHAR) ++current.characters;
        if (message == WM_CAPTURECHANGED) ++current.capture_changes;
        record("control_before", window, message, wp, lp);
    }
    const auto result = CallWindowProcW(original, window, message, wp, lp);
    if (tracked(message)) record("control_after", window, message, wp, lp);
    return result;
}
LRESULT CALLBACK control_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    try { return control_impl(window,message,wp,lp); }
    catch (const std::exception& e) { trace << "{\"event\":\"callback_failure\",\"error\":" << json(e.what()) << "}\n"; trace.flush(); TerminateProcess(GetCurrentProcess(), 72); return 0; }
}
void parent_draw(HDC dc) {
    ++current.draws;
    current.last_draw_update = current.update;
    current.last_draw_ms = GetTickCount64();
    RECT area{};
    GetClientRect(top, &area);
    HBRUSH background = CreateSolidBrush(RGB(239, 243, 248));
    FillRect(dc, &area, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, RGB(24, 36, 54));
    const auto title = L"Native Win32 / " + role + L"   Tick " + std::to_wstring(current.update);
    TextOutW(dc, 24, 20, title.c_str(), static_cast<int>(title.size()));
    const auto details = L"GUI thread " + std::to_wstring(current.tid) + L"   Button clicks " + std::to_wstring(current.clicks);
    TextOutW(dc, 24, 44, details.c_str(), static_cast<int>(details.size()));
    const wchar_t label[] = L"Native EDIT (message delivery is not proof of key-state consistency)";
    TextOutW(dc, 24, 68, label, static_cast<int>(std::size(label) - 1));
    RECT signal{480, 16, 592, 54};
    const auto intensity = static_cast<BYTE>(50 + (current.update % 20) * 8);
    HBRUSH pulse = CreateSolidBrush(role == L"A" ? RGB(intensity, 100, 210) : RGB(30, intensity, 100));
    FillRect(dc, &signal, pulse);
    DeleteObject(pulse);
    // Diagnostic visual counter, rendered by this application, not the bridge.
    // The bridge can read it back to connect the PNG to a real target paint.
    const wchar_t marker_label[] = L"Native paint tick bits:";
    TextOutW(dc, 24, 334, marker_label, static_cast<int>(std::size(marker_label) - 1));
    for (int bit = 0; bit < 16; ++bit) {
        RECT cell{24 + bit * 10, 352, 32 + bit * 10, 362};
        FillRect(dc, &cell, reinterpret_cast<HBRUSH>(GetStockObject((current.update & (1ULL << bit)) ? WHITE_BRUSH : BLACK_BRUSH)));
    }
}
void drag_draw(HWND window, HDC dc) {
    RECT area{};
    GetClientRect(window, &area);
    FillRect(dc, &area, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(dc, &area, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    const auto text = L"SetCapture / ReleaseCapture: point (" + std::to_wstring(current.drag_x) + L", " + std::to_wstring(current.drag_y) + L")";
    TextOutW(dc, 8, 8, text.c_str(), static_cast<int>(text.size()));
    HBRUSH point = CreateSolidBrush(RGB(38, 106, 188));
    const auto old = SelectObject(dc, point);
    Ellipse(dc, current.drag_x - 8, current.drag_y - 8, current.drag_x + 8, current.drag_y + 8);
    SelectObject(dc, old);
    DeleteObject(point);
}
LRESULT CALLBACK drag_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); drag_draw(window, dc); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { drag_draw(window, reinterpret_cast<HDC>(wp)); return 0; }
    if (message == WM_LBUTTONDOWN) {
        SetCapture(window); // ordinary native mouse capture; no replacement state
        current.drag_x = GET_X_LPARAM(lp); current.drag_y = GET_Y_LPARAM(lp);
        record("drag_down", window, message, wp, lp);
        InvalidateRect(window, nullptr, FALSE); return 0;
    }
    if (message == WM_MOUSEMOVE) {
        if (GetCapture() == window && (wp & MK_LBUTTON)) {
            current.drag_x = GET_X_LPARAM(lp); current.drag_y = GET_Y_LPARAM(lp);
            ++current.drag_moves;
            InvalidateRect(window, nullptr, FALSE);
        }
        record("drag_move", window, message, wp, lp); return 0;
    }
    if (message == WM_LBUTTONUP) {
        if (GetCapture() == window) check(ReleaseCapture(), "ReleaseCapture");
        record("drag_up", window, message, wp, lp); return 0;
    }
    if (message == WM_CAPTURECHANGED) { ++current.capture_changes; record("drag_capture_changed", window, message, wp, lp); }
    return DefWindowProcW(window, message, wp, lp);
}
LRESULT CALLBACK parent_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CREATE) {
        top = window;
        const auto instance = GetModuleHandleW(nullptr);
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            24, 92, 568, 32, window, reinterpret_cast<HMENU>(101), instance, nullptr);
        button = CreateWindowExW(0, L"BUTTON", L"Native button", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            24, 140, 160, 36, window, reinterpret_cast<HMENU>(102), instance, nullptr);
        drag = CreateWindowExW(0, L"FocusViewsR3Drag", L"", WS_CHILD | WS_VISIBLE,
            24, 204, 568, 126, window, reinterpret_cast<HMENU>(103), instance, nullptr);
        demand(edit && button && drag, "child window creation failed");
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        edit_original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(control_proc)));
        button_original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(button, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(control_proc)));
        demand(edit_original && button_original, "control subclass failed");
        current.top = reinterpret_cast<std::uint64_t>(top); current.edit = reinterpret_cast<std::uint64_t>(edit);
        current.button = reinterpret_cast<std::uint64_t>(button); current.drag = reinterpret_cast<std::uint64_t>(drag);
        check(SetTimer(window, 1, 100, nullptr) != 0, "SetTimer");
        return 0;
    }
    if (message == WM_TIMER) { ++current.update; InvalidateRect(window, nullptr, FALSE); observe(); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); parent_draw(dc); EndPaint(window, &paint);
        record("wm_paint_end", window, message, wp, lp); return 0;
    }
    if (message == WM_PRINT) {
        ++current.prints; current.last_print_update = current.update;
        record("wm_print_begin", window, message, wp, lp);
        const auto result = DefWindowProcW(window, message, wp, lp);
        record("wm_print_end", window, message, wp, lp); return result;
    }
    if (message == WM_PRINTCLIENT) {
        parent_draw(reinterpret_cast<HDC>(wp)); record("wm_printclient", window, message, wp, lp); return 0;
    }
    if (message == observe_message) { record("read_only_observe", window, message, wp, lp); return 1; }
    if (message == WM_COMMAND && HIWORD(wp) == BN_CLICKED && LOWORD(wp) == 102) {
        ++current.clicks; InvalidateRect(window, nullptr, FALSE); record("button_clicked", window, message, wp, lp); return 0;
    }
    if (message == WM_DESTROY) { record("destroy", window, message, wp, lp); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
// Exceptions must never cross a Win32 callback boundary.
LRESULT CALLBACK safe_parent(HWND w, UINT m, WPARAM p, LPARAM l) {
    try { return parent_proc(w,m,p,l); }
    catch (const std::exception& e) { trace << "{\"event\":\"callback_failure\",\"error\":" << json(e.what()) << "}\n"; trace.flush(); TerminateProcess(GetCurrentProcess(), 70); return 0; }
}
LRESULT CALLBACK safe_drag(HWND w, UINT m, WPARAM p, LPARAM l) {
    try { return drag_proc(w,m,p,l); }
    catch (const std::exception& e) { trace << "{\"event\":\"callback_failure\",\"error\":" << json(e.what()) << "}\n"; trace.flush(); TerminateProcess(GetCurrentProcess(), 71); return 0; }
}
}
int fixture_main(const Args& args) {
    Mapping map(args.get(L"--state"), sizeof(Shared), false);
    shared = static_cast<Shared*>(map.view);
    trace.open(std::filesystem::path(args.get(L"--log")), std::ios::binary);
    demand(trace.good(), "fixture trace unavailable");
    role = args.get(L"--name");
    current.pid = GetCurrentProcessId(); current.tid = GetCurrentThreadId();
    const auto desktop = thread_desktop();
    wcsncpy_s(current.desktop, desktop.c_str(), _TRUNCATE);
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.hInstance = instance; window_class.lpfnWndProc = safe_drag; window_class.lpszClassName = L"FocusViewsR3Drag";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    check(RegisterClassW(&window_class), "RegisterClassW.drag");
    window_class.lpfnWndProc = safe_parent; window_class.lpszClassName = L"FocusViewsR3Fixture";
    check(RegisterClassW(&window_class), "RegisterClassW.top");
    const auto title = L"focus-views R3 native baseline / " + role;
    const auto window = CreateWindowExW(0, window_class.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        40, 40, 640, 420, nullptr, nullptr, instance, nullptr);
    check(window != nullptr, "CreateWindowExW");
    ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
    record("ready", window);
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, args.get(L"--ready").c_str()));
    check(ready.value != nullptr, "OpenEventW"); check(SetEvent(ready.value), "SetEvent");
    MSG message{};
    while (true) {
        const auto status = GetMessageW(&message, nullptr, 0, 0);
        if (status == -1) throw Error("GetMessageW", GetLastError());
        if (status == 0) break;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    return 0;
}
}
