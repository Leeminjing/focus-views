#include "channel.hpp"

// Read-only diagnostic extension, identical in both modes. The original GUI
// fixture, controls, input handling, and GetAsyncKeyState calls are unchanged.
extern "C" __declspec(dllexport) void WINAPI R303ReadOnlyProbe(r303::Probe* p) {
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
}
