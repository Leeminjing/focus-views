#define FV_TARGET_EXPORT
#include "../scenario/fixture_contract.hpp"
#include "build_id.hpp"
#include <bcrypt.h>
#include <windowsx.h>
#include <intrin.h>
extern "C" {
    void EnterManagedStack(void*);
    void OuterWorkTrap(); void MiddleWorkTrap(); void LeafWorkTrap(); void ChangedTrap();
    std::uint64_t AlternateCheckpointTrap(); extern unsigned char AlternateCheckpointSite;
    void ReadyTrap(); void InitTrap(); std::uint64_t CheckpointTrap(); void ContinuationTrap(); void RejectTrap();
    extern unsigned char CheckpointSite, ReadySite;
    extern uintptr_t __security_cookie, __security_cookie_complement;
    __declspec(dllexport) r305::Metadata R305Metadata;
    __declspec(dllexport) LONG R305Rebuild = 0;
}
namespace r305 {
extern "C" __declspec(dllexport) FileRequest R5FixtureMailbox{};
extern "C" __declspec(dllexport) const FixtureContract R5FixtureContract{};
namespace {
std::uint64_t receipt(std::uint64_t reg,std::uint64_t leaf,std::uint64_t middle,std::uint64_t outer){return std::rotl((reg^leaf)+middle,13)^outer;}

Work* const work = reinterpret_cast<Work*>(data_address);
Shared* shared = nullptr;
std::unique_ptr<Mapping> shared_mapping;
std::ofstream trace;
bool source_mode = false;
Telemetry telemetry;
std::uint64_t event_count = 0;
WNDPROC native_edit_proc=nullptr;
[[noreturn]] void reject(DWORD error) {
    R305Metadata.rejected_error = error; R305Metadata.published = 0; RejectTrap(); __assume(0);
}
void observe(const char* event,UINT message = 0) {
    POINT cursor{};SetLastError(0);const BOOL cursor_ok=GetCursorPos(&cursor);const DWORD cursor_error=cursor_ok?0:GetLastError();
    telemetry.pid = GetCurrentProcessId(); telemetry.tid = GetCurrentThreadId();
    telemetry.top = reinterpret_cast<std::uint64_t>(work->window); telemetry.edit = reinterpret_cast<std::uint64_t>(work->edit); telemetry.button = reinterpret_cast<std::uint64_t>(work->button); telemetry.drag = telemetry.top;
    telemetry.update = work->counter; telemetry.clicks = work->clicks; telemetry.events = ++event_count;
    telemetry.focus = reinterpret_cast<std::uint64_t>(GetFocus()); telemetry.capture = reinterpret_cast<std::uint64_t>(GetCapture());
    telemetry.key_ctrl = GetKeyState(VK_CONTROL); telemetry.key_shift = GetKeyState(VK_SHIFT); telemetry.key_left = GetKeyState(VK_LBUTTON);
    telemetry.async_ctrl = GetAsyncKeyState(VK_CONTROL); telemetry.async_shift = GetAsyncKeyState(VK_SHIFT); telemetry.async_left = GetAsyncKeyState(VK_LBUTTON);
    telemetry.keyboard_ok = GetKeyboardState(telemetry.keyboard); telemetry.keyboard_error = telemetry.keyboard_ok ? 0 : GetLastError();
    if(work->edit){GetWindowTextW(work->edit,telemetry.text,128);SendMessageW(work->edit,EM_GETSEL,reinterpret_cast<WPARAM>(&telemetry.selection_start),reinterpret_cast<LPARAM>(&telemetry.selection_end));}
    else wcscpy_s(telemetry.text,work->text); telemetry.drag_x = work->point_x; telemetry.drag_y = work->point_y;
    InterlockedIncrement(&shared->sequence); MemoryBarrier(); shared->data = telemetry; MemoryBarrier(); InterlockedIncrement(&shared->sequence);
    trace << "{\"event\":" << json(event) << ",\"tickMs\":" << GetTickCount64() << ",\"cursorQueryOk\":"<<cursor_ok<<",\"cursorQueryError\":"<<cursor_error<<",\"cursorQueryX\":"<<cursor.x<<",\"cursorQueryY\":"<<cursor.y<<",\"queryThread\":" << GetCurrentThreadId()
          << ",\"message\":" << message << ",\"published\":" << R305Metadata.published << ",\"state\":" << telemetry_json(telemetry)
          << ",\"work\":" << work_json(*work) << "}\n"; trace.flush();
}
LRESULT CALLBACK edit_observer(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_IME_STARTCOMPOSITION)R305Metadata.ime_active=1;
    if(message==WM_IME_ENDCOMPOSITION)R305Metadata.ime_active=0;
    const auto result=undo_dispatch(window,native_edit_proc,message,wp,lp);
    if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_CHAR)observe("native_edit_processed",message);
    return result;
}
void draw(HDC dc) {
    RECT area{}; GetClientRect(work->window,&area);
    const auto brush = CreateSolidBrush(RGB(239,243,248)); FillRect(dc,&area,brush); DeleteObject(brush);
    SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT)); SetBkMode(dc,TRANSPARENT);
    const auto title = L"R3-005a real GUI / PID "+std::to_wstring(GetCurrentProcessId());
    TextOutW(dc,24,20,title.c_str(),static_cast<int>(title.size()));
    const auto label = R305Metadata.published ? std::wstring(work->text) : L"runtime preparation (unpublished)";
    TextOutW(dc,24,260,label.c_str(),static_cast<int>(label.size()));
    const auto files=L"Files read "+std::to_wstring(work->document_reads)+L" / written "+std::to_wstring(work->document_writes);
    TextOutW(dc,24,290,files.c_str(),static_cast<int>(files.size()));
    const auto counters = L"Counter "+std::to_wstring(work->counter)+L"  Clicks "+std::to_wstring(work->clicks);
    TextOutW(dc,24,115,counters.c_str(),static_cast<int>(counters.size()));
    const auto dot = CreateSolidBrush(RGB(38,106,188)); auto old = SelectObject(dc,dot);
    Ellipse(dc,work->point_x-8,work->point_y+220-8,work->point_x+8,work->point_y+220+8); SelectObject(dc,old); DeleteObject(dot);
    TextOutW(dc,24,334,L"Actual GDI counter bits:",24);
    for (unsigned bit=0;bit<16;++bit) {
        RECT r{24+static_cast<LONG>(10*bit),352,32+static_cast<LONG>(10*bit),362};
        FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject((work->counter&(1ULL<<bit)) ? WHITE_BRUSH : BLACK_BRUSH)));
    }
    check(GdiFlush(),"GdiFlush.target-draw");
    ++telemetry.draws; telemetry.last_draw_update = work->counter; telemetry.last_draw_ms = GetTickCount64();
}
LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if (message == WM_NCCREATE) { SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)); return TRUE; }
    if (!GetWindowLongPtrW(window,GWLP_USERDATA)) return DefWindowProcW(window,message,wp,lp);
    if (message == freeze_message) { R305Metadata.frozen = 1; observe("runtime_freeze"); return 0; }
    if (message == checkpoint_message) { R305Metadata.checkpoint_requested = 1; return 0; }
    if (message == prepare_stop_message) { R305Metadata.prepare_stop = 1; return 0; }
    if (message == resource_message) { resource_service(R305Metadata,work->window,work->edit,work->button);observe("runtime_resource_service",message);return 0; }
    if (message == capture_message) { capture_owned_window(R305Metadata,window);observe("runtime_capture_end",message);return 0; }
    if (message == observe_message) { observe("read_only_observe",message); return 0; }
    if (message == WM_PAINT) {
        ++R305Metadata.paint_depth; PAINTSTRUCT ps{}; auto dc=BeginPaint(window,&ps); draw(dc); EndPaint(window,&ps); --R305Metadata.paint_depth;
        observe("wm_paint_end",message); return 0;
    }
    if (message == WM_PRINTCLIENT) { ++R305Metadata.paint_depth; draw(reinterpret_cast<HDC>(wp)); --R305Metadata.paint_depth; observe("wm_printclient",message); return 0; }
    if (!R305Metadata.published) return DefWindowProcW(window,message,wp,lp);
    if(message==WM_COMMAND && LOWORD(wp)==102 && HIWORD(wp)==BN_CLICKED && !R305Metadata.frozen){++work->clicks;application_files(R305Metadata,*work);observe("native_button_clicked",message);InvalidateRect(window,nullptr,FALSE);return 0;}
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
void reference(volatile HWND* value,unsigned logical=1) {
    auto& m=R305Metadata; if (m.ref_count >= 8) reject(ERROR_BUFFER_OVERFLOW);
    const Reference ref{reinterpret_cast<std::uint64_t>(value),logical};
    if(m.required_count>=8)reject(ERROR_BUFFER_OVERFLOW);m.required_refs[m.required_count++]=ref;
    if(!(m.omit_ref && logical==2 && value!=&work->edit))m.refs[m.ref_count++]=ref;
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
    volatile std::uint64_t values[4]{};
    volatile HWND window=nullptr;
    volatile HWND edit=nullptr;
    if(!R305Rebuild) {
        values[0]=work->seed^work->counter;values[1]=work->seed+0x11731;values[2]=0x1234;values[3]=work->counter+17;
        window=work->window;edit=work->edit;LeafWorkTrap();
    }
    if(R305Metadata.gui){reference(&window);reference(&edit,2);}
    R305Metadata.local[0]=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto reg=R305Metadata.wrong_chain ? AlternateCheckpointTrap() : CheckpointTrap();
    if (R305Metadata.gui && (window != work->window || !IsWindow(window) || GetWindowLongPtrW(window,GWLP_USERDATA) != reinterpret_cast<LONG_PTR>(work))) reject(ERROR_INVALID_WINDOW_HANDLE);
    if(edit!=work->edit||GetParent(edit)!=window)reject(ERROR_INVALID_WINDOW_HANDLE);
    work->returned|=1; return reg^values[0];
}
__declspec(noinline) std::uint64_t middle() {
    volatile std::uint64_t values[4]{};
    volatile HWND window=nullptr;
    volatile HWND button=nullptr;
    if(!R305Rebuild) {
        values[0]=std::rotl(work->seed,19)+work->counter;values[1]=11;values[2]=12;values[3]=13;
        window=work->window;button=work->button;MiddleWorkTrap();
    }
    if(R305Metadata.gui){reference(&window);reference(&button,3);} R305Metadata.local[1]=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto result=leaf(); if (window != work->window) reject(ERROR_INVALID_WINDOW_HANDLE);
    if(button!=work->button||GetParent(button)!=window)reject(ERROR_INVALID_WINDOW_HANDLE);
    work->returned|=2; return std::rotl(result+values[0],13);
}
__declspec(noinline) std::uint64_t outer() {
    volatile std::uint64_t values[4]{};
    if(!R305Rebuild) {values[0]=work->seed^0xBADA55117733ULL;values[1]=21;values[2]=22;values[3]=23;OuterWorkTrap();}
    R305Metadata.local[2]=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto result=middle(); work->returned|=4; return result^values[0];
}
__declspec(noinline) void application() {
    if(!R305Rebuild) {
    InitTrap(); // controller counts this independently, target must never reach it
    const auto status=BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&work->seed),sizeof(work->seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) reject(static_cast<DWORD>(status));
    swprintf_s(work->text,L"BASE-%016llx",static_cast<unsigned long long>(work->seed)); work->length=static_cast<unsigned>(wcslen(work->text));
    check(SendMessageW(work->edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(work->text))!=0,"WM_SETTEXT.source_business_initialization");
    SendMessageW(work->edit,EM_SETMODIFY,TRUE,0);SetFocus(work->edit);
    work->counter=17+(work->seed%100); R305Metadata.published=1;
    undo_arm(work->edit);
    observe("business_initialized"); if(R305Metadata.gui)InvalidateRect(work->window,nullptr,FALSE);
    }
    if(!R305Metadata.gui || R305Rebuild){R305Metadata.frozen=1;R305Metadata.checkpoint_requested=1;}
    for (;;) {
        MSG message{};
        while (R305Metadata.gui && !R305Rebuild && PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            ++R305Metadata.in_dispatch; TranslateMessage(&message); DispatchMessageW(&message); --R305Metadata.in_dispatch;
        }
        if (R305Metadata.checkpoint_requested) {
            if(R305Metadata.gui && !R305Rebuild)UpdateWindow(work->window);
            MSG pending{};
            if (!R305Metadata.frozen || R305Metadata.in_dispatch || R305Metadata.paint_depth || (!R305Rebuild && R305Metadata.gui && (GetCapture() || PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE)))) reject(ERROR_BUSY);
            if(R305Metadata.gui)describe_window();
            auto& m=R305Metadata;if(!m.resources_ready)reject(ERROR_INVALID_STATE);
            m.ref_count=0;m.required_count=0;reference(&work->window);reference(&work->edit,2);reference(&work->button,3);
            work->returned=0;work->receipt=outer()^work->seed;
            ContinuationTrap(); // reached only after several actual C++ returns
            m.checkpoint_requested=0; m.frozen=m.freeze_after_resume;
            observe("application_continued"); if(m.gui)InvalidateRect(work->window,nullptr,FALSE);
            if(!m.gui) {work->text[work->length++]=L'!';work->text[work->length]=0;++work->counter;ChangedTrap();Sleep(INFINITE);}
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
    ReadyTrap();
    if(R305Rebuild){
        // Owner-thread resource preparation remains responsive before any
        // structural CALL chain or CPU restore is attempted.
        while(!R305Metadata.prepare_stop){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjectsEx(0,nullptr,25,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    }
    // Same application/outer/middle/leaf CALL sites in both processes.
    // Rebuild mode only enters structural frames and skips business prefixes.
    application();
}
}
int fixture_main(const Args& args) {
    source_mode=args.get(L"--mode")==L"source";
    R305Rebuild=source_mode?0:1;
    R305Metadata.freeze_after_resume=source_mode?0:1;
    shared_mapping=std::make_unique<Mapping>(args.get(L"--state"),sizeof(Shared),false); shared=static_cast<Shared*>(shared_mapping->view);
    trace.open(std::filesystem::path(args.get(L"--log")),std::ios::binary); demand(trace.good(),"fixture trace");
    auto& m=R305Metadata;m.abi_bytes=sizeof(m);m.root=data_address;m.data_bytes=data_size;m.stack=stack_address;m.stack_bytes=stack_size;
    if(args.number(L"--bad-abi"))m.abi_version=99;
    if(args.number(L"--unknown-child"))m.resource_kinds|=(1ULL<<63);
    memcpy(m.build,R305_BUILD_ID,sizeof(m.build)); m.pid=GetCurrentProcessId(); m.tid=GetCurrentThreadId(); m.image=reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
    m.gui=static_cast<unsigned>(args.number(L"--gui"));m.wrong_chain=static_cast<unsigned>(args.number(L"--wrong-chain"));
    m.omit_ref=static_cast<unsigned>(args.number(L"--omit-ref"));m.unknown_child=static_cast<unsigned>(args.number(L"--unknown-child"));m.bad_parent=static_cast<unsigned>(args.number(L"--bad-parent"));
    m.alt_checkpoint=reinterpret_cast<std::uint64_t>(&AlternateCheckpointSite);m.changed=reinterpret_cast<std::uint64_t>(&ChangedTrap);m.rebuild_address=reinterpret_cast<std::uint64_t>(&R305Rebuild);
    m.business_sites[0]=reinterpret_cast<std::uint64_t>(&OuterWorkTrap);m.business_sites[1]=reinterpret_cast<std::uint64_t>(&MiddleWorkTrap);m.business_sites[2]=reinterpret_cast<std::uint64_t>(&LeafWorkTrap);
    m.ready=reinterpret_cast<std::uint64_t>(&ReadySite); m.init=reinterpret_cast<std::uint64_t>(&InitTrap);
    m.checkpoint=reinterpret_cast<std::uint64_t>(&CheckpointSite);
    m.continued=reinterpret_cast<std::uint64_t>(&ContinuationTrap); m.rejected=reinterpret_cast<std::uint64_t>(&RejectTrap);
    m.cookie_address=reinterpret_cast<std::uint64_t>(&__security_cookie); m.complement_address=reinterpret_cast<std::uint64_t>(&__security_cookie_complement);
    m.frame_entry[0]=reinterpret_cast<std::uint64_t>(&leaf); m.frame_entry[1]=reinterpret_cast<std::uint64_t>(&middle); m.frame_entry[2]=reinterpret_cast<std::uint64_t>(&outer);
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),static_cast<PROCESS_MITIGATION_POLICY>(15),&m.shadow_policy,sizeof(m.shadow_policy))) reject(GetLastError());
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),static_cast<PROCESS_MITIGATION_POLICY>(7),&m.cfg_policy,sizeof(m.cfg_policy))) reject(GetLastError());
    // Observe the current policy without modifying it. A real restore attempt,
    // not a speculative policy gate, determines whether Windows permits resume.
    if (VirtualAlloc(reinterpret_cast<void*>(data_address),data_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE) != reinterpret_cast<void*>(data_address)) reject(GetLastError());
    if (VirtualAlloc(reinterpret_cast<void*>(stack_address),stack_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE) != reinterpret_cast<void*>(stack_address)) reject(GetLastError());
    DWORD old=0; check(VirtualProtect(reinterpret_cast<void*>(stack_address),4096,PAGE_NOACCESS,&old),"VirtualProtect.stack_guard");
    if(m.gui) {
    WNDCLASSW cls{}; cls.lpfnWndProc=window_proc; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"FocusViewsR305SelfDraw";
    check(RegisterClassW(&cls)!=0,"RegisterClassW");
    const auto klass=args.number(L"--fail-window") ? L"FocusViewsR305MissingClass" : cls.lpszClassName;
    work->window=CreateWindowExW(0,klass,L"focus-views R3-005a owned self-drawn window",WS_OVERLAPPEDWINDOW,40,40,640,420,nullptr,nullptr,cls.hInstance,work);
    if (!work->window) reject(GetLastError());
    m.window=reinterpret_cast<std::uint64_t>(work->window);
    work->edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_LEFT|ES_NOHIDESEL,24,72,540,32,work->window,reinterpret_cast<HMENU>(101),cls.hInstance,nullptr);check(work->edit!=nullptr,"CreateWindowExW.EDIT");
    work->button=CreateWindowExW(0,L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,24,150,166,45,m.bad_parent?work->edit:work->window,reinterpret_cast<HMENU>(102),cls.hInstance,nullptr);check(work->button!=nullptr,"CreateWindowExW.BUTTON");
    SendMessageW(work->edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);SendMessageW(work->button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
    native_edit_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(work->edit,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(edit_observer)));check(native_edit_proc!=nullptr,"SetWindowLongPtrW.read_only_EDIT_observer");
    m.edit=reinterpret_cast<std::uint64_t>(work->edit);m.button=reinterpret_cast<std::uint64_t>(work->button);
    if(m.unknown_child)check(CreateWindowExW(0,L"STATIC",L"unsupported",WS_CHILD|WS_VISIBLE,220,160,80,25,work->window,reinterpret_cast<HMENU>(103),cls.hInstance,nullptr)!=nullptr,"CreateWindowExW.unknown_child");
    wcscpy_s(telemetry.desktop,thread_desktop().c_str()); ShowWindow(work->window,SW_SHOWNOACTIVATE); UpdateWindow(work->window); observe("runtime_prepared");
    describe_window();
    }
    GetCurrentThreadStackLimits(&m.original_stack_low,&m.original_stack_high);
    // Explicitly managed NT_TIB stack bounds, not a copy of source TEB/PEB/TLS.
    // This experimental ABI boundary is verified and disclosed, not a supported
    // Windows stack-switching API or arbitrary startup-stack restoration.
    auto tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    tib->StackBase=reinterpret_cast<void*>(stack_address+stack_size); tib->StackLimit=reinterpret_cast<void*>(stack_address+4096);
    m.active_stack_low=stack_address+4096; m.active_stack_high=stack_address+stack_size;
    EnterManagedStack(reinterpret_cast<void*>(m.active_stack_high)); __assume(0);
}
}
extern "C" void R305ManagedEntry(){r305::managed_entry();}
