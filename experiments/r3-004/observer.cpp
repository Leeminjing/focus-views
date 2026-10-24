#include "channel.hpp"

// Read-only diagnostic extension, identical in both modes. The original GUI
// fixture, controls, input handling, and GetAsyncKeyState calls are unchanged.
extern "C" __declspec(dllexport) void WINAPI R304ReadOnlyProbe(r304::Probe* p) {
    p->pid = GetCurrentProcessId(); p->tid = GetCurrentThreadId();
    p->async_ctrl = GetAsyncKeyState(VK_CONTROL);
    p->repeated_ctrl = GetAsyncKeyState(VK_CONTROL);
    p->async_shift = GetAsyncKeyState(VK_SHIFT);
    p->left_ctrl = GetAsyncKeyState(VK_LCONTROL); p->right_ctrl = GetAsyncKeyState(VK_RCONTROL);
    p->left_shift = GetAsyncKeyState(VK_LSHIFT); p->right_shift = GetAsyncKeyState(VK_RSHIFT);
    p->async_x = GetAsyncKeyState('X');
    p->queue_ctrl = GetKeyState(VK_CONTROL); p->queue_shift = GetKeyState(VK_SHIFT);
    BYTE table[256]{}; p->keyboard_ok = GetKeyboardState(table);
    p->keyboard_ctrl = table[VK_CONTROL]; p->keyboard_shift = table[VK_SHIFT];
    // Deliberately uncovered dynamic resolution: a real native control query,
    // labelled separately, never substituted for the application's IAT call.
    const auto native = reinterpret_cast<SHORT(WINAPI*)(int)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetAsyncKeyState"));
    p->native_dynamic_ctrl = native ? native(VK_CONTROL) : 0;
    p->async_left = GetAsyncKeyState(VK_LBUTTON); p->queue_left = GetKeyState(VK_LBUTTON);
    p->keyboard_left = table[VK_LBUTTON];
    SetLastError(0); p->cursor_ok = GetCursorPos(&p->cursor); p->cursor_error = p->cursor_ok ? 0 : GetLastError();
    const auto cursor = reinterpret_cast<BOOL(WINAPI*)(LPPOINT)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetCursorPos"));
    p->native_cursor_ok = cursor ? cursor(&p->native_cursor) : FALSE;
    p->capture = reinterpret_cast<std::uint64_t>(GetCapture()); p->focus = reinterpret_cast<std::uint64_t>(GetFocus());
}
