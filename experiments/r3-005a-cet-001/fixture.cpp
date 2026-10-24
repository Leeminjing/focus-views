#include "protocol.hpp"
#include "build_id.hpp"
#include <bcrypt.h>
#include <windowsx.h>
#include <intrin.h>
extern "C" {
    void EnterManagedStack(void(*)(),void*);
    void ReadyTrap(); void InitTrap(); std::uint64_t CheckpointTrap(); void ContinuationTrap(); void RejectTrap();
    extern unsigned char CheckpointSite, ReadySite;
    void ApiTrap(); void DiagnosticTrap();
    extern unsigned char ApiSite, DiagnosticSite;
    extern uintptr_t __security_cookie, __security_cookie_complement;
    __declspec(dllexport) r305::Metadata R305Metadata;
}
namespace r305 {
namespace {
Work* const work = reinterpret_cast<Work*>(data_address);
Shared* shared = nullptr;
std::unique_ptr<Mapping> shared_mapping;
std::ofstream trace;
bool source_mode = false;
std::wstring diagnostic_mode;
Telemetry telemetry;
std::uint64_t event_count = 0;
void restore_entry() {
    ApiTrap();
    RtlRestoreContext(&R305Metadata.staged_cpu,nullptr);
}
// Independent native-stack controls. These never enter application() and are
// explicitly not a reconstruction of the source business call chain.
__declspec(noinline) void diagnostic_restore(CONTEXT* cpu) {
    ++R305Metadata.diagnostic_restore_calls;
    ApiTrap();
    RtlRestoreContext(cpu,nullptr);
}
__declspec(noinline) std::uint64_t diagnostic_leaf(std::uint64_t seed) {
    volatile std::uint64_t values[4]{seed^0x11335577ULL,2,3,4};
    DiagnosticTrap();
    if(diagnostic_mode==L"rtl-local") {
        CONTEXT local{};
        RtlCaptureContext(&local);
        if(!R305Metadata.diagnostic_phase) {
            R305Metadata.diagnostic_phase=1;
            trace << "{\"event\":\"local_capture\",\"api\":\"RtlCaptureContext\",\"rip\":" << local.Rip << ",\"rsp\":" << local.Rsp << ",\"flags\":" << local.ContextFlags << "}\n";trace.flush();
            diagnostic_restore(&local);
        }
    }
    ++R305Metadata.diagnostic_returns;
    return values[0]+19;
}
__declspec(noinline) std::uint64_t diagnostic_seh_leaf(std::uint64_t seed) {
    volatile std::uint64_t values[4]{seed^0x11335577ULL,2,3,4};
    DiagnosticTrap();
    __try { RaiseException(0xE305C001,0,0,nullptr); }
    __except(GetExceptionCode()==0xE305C001 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        R305Metadata.diagnostic_phase=1;
    }
    ++R305Metadata.diagnostic_returns;
    return values[0]+19;
}
__declspec(noinline) std::uint64_t diagnostic_middle(std::uint64_t seed) {
    volatile std::uint64_t values[4]{seed+31,5,6,7};
    const auto value=diagnostic_mode==L"native-seh" ? diagnostic_seh_leaf(seed) : diagnostic_leaf(seed);
    ++R305Metadata.diagnostic_returns;
    return value^values[0];
}
__declspec(noinline) std::uint64_t diagnostic_outer(std::uint64_t seed) {
    volatile std::uint64_t values[4]{seed+43,8,9,10};
    const auto value=diagnostic_middle(seed);
    ++R305Metadata.diagnostic_returns;
    return value+values[0];
}
[[noreturn]] void reject(DWORD error) {
    R305Metadata.rejected_error = error; R305Metadata.published = 0; RejectTrap(); __assume(0);
}
void observe(const char* event,UINT message = 0) {
    telemetry.pid = GetCurrentProcessId(); telemetry.tid = GetCurrentThreadId();
    telemetry.top = reinterpret_cast<std::uint64_t>(work->window); telemetry.edit = telemetry.top; telemetry.button = telemetry.top; telemetry.drag = telemetry.top;
    telemetry.update = work->counter; telemetry.clicks = work->clicks; telemetry.events = ++event_count;
    telemetry.focus = reinterpret_cast<std::uint64_t>(GetFocus()); telemetry.capture = reinterpret_cast<std::uint64_t>(GetCapture());
    telemetry.key_ctrl = GetKeyState(VK_CONTROL); telemetry.key_shift = GetKeyState(VK_SHIFT); telemetry.key_left = GetKeyState(VK_LBUTTON);
    telemetry.async_ctrl = GetAsyncKeyState(VK_CONTROL); telemetry.async_shift = GetAsyncKeyState(VK_SHIFT); telemetry.async_left = GetAsyncKeyState(VK_LBUTTON);
    telemetry.keyboard_ok = GetKeyboardState(telemetry.keyboard); telemetry.keyboard_error = telemetry.keyboard_ok ? 0 : GetLastError();
    wcscpy_s(telemetry.text,work->text); telemetry.drag_x = work->point_x; telemetry.drag_y = work->point_y;
    InterlockedIncrement(&shared->sequence); MemoryBarrier(); shared->data = telemetry; MemoryBarrier(); InterlockedIncrement(&shared->sequence);
    trace << "{\"event\":" << json(event) << ",\"tickMs\":" << GetTickCount64() << ",\"queryThread\":" << GetCurrentThreadId()
          << ",\"message\":" << message << ",\"published\":" << R305Metadata.published << ",\"state\":" << telemetry_json(telemetry)
          << ",\"work\":" << work_json(*work) << "}\n"; trace.flush();
}
void draw(HDC dc) {
    RECT area{}; GetClientRect(work->window,&area);
    const auto brush = CreateSolidBrush(RGB(239,243,248)); FillRect(dc,&area,brush); DeleteObject(brush);
    SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT)); SetBkMode(dc,TRANSPARENT);
    const auto title = L"R3-005a real GUI / PID "+std::to_wstring(GetCurrentProcessId());
    TextOutW(dc,24,20,title.c_str(),static_cast<int>(title.size()));
    const auto label = R305Metadata.published ? std::wstring(work->text) : L"runtime preparation (unpublished)";
    TextOutW(dc,24,80,label.c_str(),static_cast<int>(label.size()));
    const auto counters = L"Counter "+std::to_wstring(work->counter)+L"  Clicks "+std::to_wstring(work->clicks);
    TextOutW(dc,24,115,counters.c_str(),static_cast<int>(counters.size()));
    RECT button{24,150,190,195}; Rectangle(dc,button.left,button.top,button.right,button.bottom); TextOutW(dc,34,165,L"click / type here",17);
    const auto dot = CreateSolidBrush(RGB(38,106,188)); auto old = SelectObject(dc,dot);
    Ellipse(dc,work->point_x-8,work->point_y+220-8,work->point_x+8,work->point_y+220+8); SelectObject(dc,old); DeleteObject(dot);
    TextOutW(dc,24,334,L"Actual GDI counter bits:",24);
    for (unsigned bit=0;bit<16;++bit) {
        RECT r{24+static_cast<LONG>(10*bit),352,32+static_cast<LONG>(10*bit),362};
        FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject((work->counter&(1ULL<<bit)) ? WHITE_BRUSH : BLACK_BRUSH)));
    }
    ++telemetry.draws; telemetry.last_draw_update = work->counter; telemetry.last_draw_ms = GetTickCount64();
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if (message == WM_NCCREATE) { SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)); return TRUE; }
    if (!GetWindowLongPtrW(window,GWLP_USERDATA)) return DefWindowProcW(window,message,wp,lp);
    if (message == freeze_message) { R305Metadata.frozen = 1; observe("runtime_freeze"); return 0; }
    if (message == checkpoint_message) { R305Metadata.checkpoint_requested = 1; return 0; }
    if (message == prepare_stop_message) { R305Metadata.prepare_stop = 1; return 0; }
    if (message == observe_message) { observe("read_only_observe",message); return 0; }
    if (message == WM_PAINT) {
        ++R305Metadata.paint_depth; PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps); draw(dc); EndPaint(window,&ps); --R305Metadata.paint_depth;
        observe("wm_paint_end",message); return 0;
    }
    if (message == WM_PRINTCLIENT) { ++R305Metadata.paint_depth; draw(reinterpret_cast<HDC>(wp)); --R305Metadata.paint_depth; observe("wm_printclient",message); return 0; }
    if (!R305Metadata.published) return DefWindowProcW(window,message,wp,lp);
    if (message == WM_CHAR && !R305Metadata.frozen && wp >= 32 && wp < 127 && work->length+1 < 128) {
        work->text[work->length++]=static_cast<wchar_t>(wp); work->text[work->length]=0; InvalidateRect(window,nullptr,FALSE); observe("character",message); return 0;
    }
    if (message == WM_LBUTTONDOWN && !R305Metadata.frozen) { SetCapture(window); observe("mouse_down",message); return 0; }
    if (message == WM_MOUSEMOVE && GetCapture() == window && (wp&MK_LBUTTON) && !R305Metadata.frozen) {
        work->point_x=GET_X_LPARAM(lp); work->point_y=GET_Y_LPARAM(lp)-220; InvalidateRect(window,nullptr,FALSE); observe("drag_move",message); return 0;
    }
    if (message == WM_LBUTTONUP) {
        if (GetCapture() == window) { if (!R305Metadata.frozen) ++work->clicks; ReleaseCapture(); }
        InvalidateRect(window,nullptr,FALSE); observe("mouse_up",message); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void reference(volatile HWND* value) {
    auto& m=R305Metadata; if (m.ref_count >= 8) reject(ERROR_BUFFER_OVERFLOW);
    m.refs[m.ref_count++]={reinterpret_cast<std::uint64_t>(value),1};
}
void describe_window() {
    auto& m=R305Metadata;
    m.window_tid=GetWindowThreadProcessId(work->window,&m.window_pid);
    m.window_style=static_cast<DWORD>(GetWindowLongPtrW(work->window,GWL_STYLE));
    m.window_ex_style=static_cast<DWORD>(GetWindowLongPtrW(work->window,GWL_EXSTYLE));
    m.window_user_data=static_cast<std::uint64_t>(GetWindowLongPtrW(work->window,GWLP_USERDATA));
    check(GetWindowRect(work->window,&m.window_rect),"GetWindowRect.resource");
    check(GetClassNameW(work->window,m.window_class,64)!=0,"GetClassNameW.resource");
}
__declspec(noinline) std::uint64_t leaf() {
    volatile std::uint64_t values[4]{work->seed^work->counter,work->seed+0x11731,0x1234,work->counter+17};
    volatile HWND window=work->window; reference(&window);
    R305Metadata.local[0]=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto reg=CheckpointTrap();
    if (window != work->window || !IsWindow(window) || GetWindowLongPtrW(window,GWLP_USERDATA) != reinterpret_cast<LONG_PTR>(work)) reject(ERROR_INVALID_WINDOW_HANDLE);
    work->returned|=1; return reg^values[0];
}
__declspec(noinline) std::uint64_t middle() {
    volatile std::uint64_t values[4]{std::rotl(work->seed,19)+work->counter,11,12,13};
    volatile HWND window=work->window; reference(&window); R305Metadata.local[1]=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto result=leaf(); if (window != work->window) reject(ERROR_INVALID_WINDOW_HANDLE);
    work->returned|=2; return std::rotl(result+values[0],13);
}
__declspec(noinline) std::uint64_t outer() {
    volatile std::uint64_t values[4]{work->seed^0xBADA55117733ULL,21,22,23};
    R305Metadata.local[2]=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto result=middle(); work->returned|=4; return result^values[0];
}
__declspec(noinline) void application() {
    InitTrap(); // controller counts this independently, target must never reach it
    const auto status=BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&work->seed),sizeof(work->seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) reject(static_cast<DWORD>(status));
    swprintf_s(work->text,L"BASE-%016llx",static_cast<unsigned long long>(work->seed)); work->length=static_cast<unsigned>(wcslen(work->text));
    work->counter=17+(work->seed%100); R305Metadata.published=1;
    observe("business_initialized"); InvalidateRect(work->window,nullptr,FALSE);
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            ++R305Metadata.in_dispatch; TranslateMessage(&message); DispatchMessageW(&message); --R305Metadata.in_dispatch;
        }
        if (R305Metadata.checkpoint_requested) {
            UpdateWindow(work->window);
            MSG pending{};
            if (!R305Metadata.frozen || R305Metadata.in_dispatch || R305Metadata.paint_depth || GetCapture() || PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE)) reject(ERROR_BUSY);
            describe_window();
            auto& m=R305Metadata; m.refs[0]={data_address+offsetof(Work,window),1}; m.ref_count=1;
            work->receipt=outer();
            ContinuationTrap(); // reached only after several actual C++ returns
            m.checkpoint_requested=0; m.frozen=m.freeze_after_resume;
            observe("application_continued"); InvalidateRect(work->window,nullptr,FALSE);
        }
        if (!R305Metadata.frozen) { ++work->counter; InvalidateRect(work->window,nullptr,FALSE); }
        MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
}
void managed_entry() {
    GetCurrentThreadStackLimits(&R305Metadata.reported_stack_low,&R305Metadata.reported_stack_high);
    const auto tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    R305Metadata.active_stack_low=reinterpret_cast<std::uint64_t>(tib->StackLimit);
    R305Metadata.active_stack_high=reinterpret_cast<std::uint64_t>(tib->StackBase);
    ReadyTrap(); if (source_mode) application();
    // Private runtime service only: permits real preparation-window capture.
    // No application entry or business initialization runs in this carrier.
    R305Metadata.prepare_pumping=1;
    for (;;) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        if(R305Metadata.prepare_stop){R305Metadata.prepare_stop=0;ReadyTrap();}
        MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
}
}
int fixture_main(const Args& args) {
    source_mode=args.get(L"--mode")==L"source";
    diagnostic_mode=args.get(L"--mode");
    R305Metadata.freeze_after_resume=source_mode?0:1;
    shared_mapping=std::make_unique<Mapping>(args.get(L"--state"),sizeof(Shared),false); shared=static_cast<Shared*>(shared_mapping->view);
    trace.open(std::filesystem::path(args.get(L"--log")),std::ios::binary); demand(trace.good(),"fixture trace");
    auto& m=R305Metadata; memcpy(m.build,R305_BUILD_ID,sizeof(m.build)); m.pid=GetCurrentProcessId(); m.tid=GetCurrentThreadId(); m.image=reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
    m.ready=reinterpret_cast<std::uint64_t>(&ReadySite); m.init=reinterpret_cast<std::uint64_t>(&InitTrap);
    m.checkpoint=reinterpret_cast<std::uint64_t>(&CheckpointSite);
    m.continued=reinterpret_cast<std::uint64_t>(&ContinuationTrap); m.rejected=reinterpret_cast<std::uint64_t>(&RejectTrap);
    m.api_site=reinterpret_cast<std::uint64_t>(&ApiSite); m.diagnostic_site=reinterpret_cast<std::uint64_t>(&DiagnosticSite);
    m.cookie_address=reinterpret_cast<std::uint64_t>(&__security_cookie); m.complement_address=reinterpret_cast<std::uint64_t>(&__security_cookie_complement);
    m.frame_entry[0]=reinterpret_cast<std::uint64_t>(&leaf); m.frame_entry[1]=reinterpret_cast<std::uint64_t>(&middle); m.frame_entry[2]=reinterpret_cast<std::uint64_t>(&outer);
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),static_cast<PROCESS_MITIGATION_POLICY>(15),&m.shadow_policy,sizeof(m.shadow_policy))) reject(GetLastError());
    // Observe the current policy without modifying it. A real restore attempt,
    // not a speculative policy gate, determines whether Windows permits resume.
    if (VirtualAlloc(reinterpret_cast<void*>(data_address),data_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE) != reinterpret_cast<void*>(data_address)) reject(GetLastError());
    if (VirtualAlloc(reinterpret_cast<void*>(stack_address),stack_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE) != reinterpret_cast<void*>(stack_address)) reject(GetLastError());
    DWORD old=0; check(VirtualProtect(reinterpret_cast<void*>(stack_address),4096,PAGE_NOACCESS,&old),"VirtualProtect.stack_guard");
    WNDCLASSW cls{}; cls.lpfnWndProc=window_proc; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"FocusViewsR305SelfDraw";
    check(RegisterClassW(&cls)!=0,"RegisterClassW");
    const auto klass=args.number(L"--fail-window") ? L"FocusViewsR305MissingClass" : cls.lpszClassName;
    work->window=CreateWindowExW(0,klass,L"focus-views R3-005a owned self-drawn window",WS_OVERLAPPEDWINDOW,40,40,640,420,nullptr,nullptr,cls.hInstance,work);
    if (!work->window) reject(GetLastError());
    m.window=reinterpret_cast<std::uint64_t>(work->window);
    wcscpy_s(telemetry.desktop,thread_desktop().c_str()); ShowWindow(work->window,SW_SHOWNOACTIVATE); UpdateWindow(work->window); observe("runtime_prepared");
    describe_window();
    GetCurrentThreadStackLimits(&m.original_stack_low,&m.original_stack_high);
    if(!source_mode) {
        // Carrier stays on its untouched native bootstrap stack. Raw source
        // stack bytes are restored into the separately allocated managed stack.
        m.active_stack_low=m.original_stack_low; m.active_stack_high=m.original_stack_high;
        m.reported_stack_low=m.original_stack_low; m.reported_stack_high=m.original_stack_high;
        ReadyTrap();
        if(diagnostic_mode==L"native-chain" || diagnostic_mode==L"native-seh" || diagnostic_mode==L"rtl-local") {
            std::uint64_t seed=0;
            const auto status=BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&seed),sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if(status<0)reject(static_cast<DWORD>(status));
            m.diagnostic_expected=(((seed^0x11335577ULL)+19)^(seed+31))+(seed+43);
            m.diagnostic_result=diagnostic_outer(seed);
            ContinuationTrap();
            ExitProcess(0);
        }
        if(m.restore_method==1) {
            // Retain the original R305 experimental stack ABI. A private
            // transfer frame is below all restored live source frames, so the
            // API can run without overwriting them. This does not manufacture
            // source shadow returns and is not a fix for OS native stack limits.
            if(m.staged_cpu.Rsp<=stack_address+stack_size/2)reject(ERROR_INVALID_ADDRESS);
            auto tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
            tib->StackBase=reinterpret_cast<void*>(stack_address+stack_size);
            tib->StackLimit=reinterpret_cast<void*>(stack_address+4096);
            EnterManagedStack(restore_entry,reinterpret_cast<void*>(stack_address+stack_size/2));__assume(0);
        }
        if(m.restore_method==2) {
            ApiTrap();
            RtlRestoreContext(&m.staged_cpu,nullptr);
        }
        reject(ERROR_INVALID_STATE);
    }
    // Explicitly managed NT_TIB stack bounds, not a copy of source TEB/PEB/TLS.
    // This experimental ABI boundary is verified and disclosed, not a supported
    // Windows stack-switching API or arbitrary startup-stack restoration.
    auto tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    tib->StackBase=reinterpret_cast<void*>(stack_address+stack_size); tib->StackLimit=reinterpret_cast<void*>(stack_address+4096);
    m.active_stack_low=stack_address+4096; m.active_stack_high=stack_address+stack_size;
    EnterManagedStack(managed_entry,reinterpret_cast<void*>(m.active_stack_high)); __assume(0);
}
}
