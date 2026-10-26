#include "contract.hpp"
#include "build_id.hpp"
#include <bcrypt.h>
#include <windowsx.h>
extern "C" {
    void EnterManagedStack(void*);void ReadyTrap();void InitTrap();std::uint64_t CheckpointTrap();std::uint64_t AlternateCheckpointTrap();
    void FourthWorkTrap();void OuterWorkTrap();void MiddleWorkTrap();void LeafWorkTrap();void ContinuationTrap();void RejectTrap();void ChangedTrap();
    extern unsigned char ReadySite,CheckpointSite,AlternateCheckpointSite;
    extern uintptr_t __security_cookie,__security_cookie_complement;
    __declspec(dllexport) r305::MetadataV2 R305Metadata;
    __declspec(dllexport) LONG R305Rebuild=0;
    __declspec(dllexport) recorder::Contract RecorderContract;
    __declspec(dllexport) r305::ClientCommand RecorderMailbox;
}
namespace recorder {
using namespace r305;
namespace {
State* const data=reinterpret_cast<State*>(data_base);
unsigned checkpoint_probe=0;bool checkpoint_probe_sent=false,quit_probe_sent=false;
std::unique_ptr<Mapping> state_map;Shared* shared=nullptr;std::ofstream events;Telemetry telemetry;WNDPROC edit_proc=nullptr;
[[noreturn]] void reject(DWORD code){R305Metadata.base.rejected_error=code;RejectTrap();__assume(0);}
void observe(const char* name,UINT message=0){
    telemetry.pid=GetCurrentProcessId();telemetry.tid=GetCurrentThreadId();telemetry.top=reinterpret_cast<std::uint64_t>(data->top);telemetry.edit=reinterpret_cast<std::uint64_t>(data->editor);telemetry.button=reinterpret_cast<std::uint64_t>(data->export_button);telemetry.drag=telemetry.top;telemetry.update=data->ticks;telemetry.clicks=data->clicks;
    telemetry.focus=reinterpret_cast<std::uint64_t>(GetFocus());telemetry.capture=reinterpret_cast<std::uint64_t>(GetCapture());telemetry.key_ctrl=GetKeyState(VK_CONTROL);telemetry.key_shift=GetKeyState(VK_SHIFT);telemetry.key_left=GetKeyState(VK_LBUTTON);telemetry.async_ctrl=GetAsyncKeyState(VK_CONTROL);telemetry.async_shift=GetAsyncKeyState(VK_SHIFT);telemetry.async_left=GetAsyncKeyState(VK_LBUTTON);
    BYTE keys[256]{};telemetry.keyboard_ok=GetKeyboardState(keys);memcpy(telemetry.keyboard,keys,sizeof(keys));
    GetWindowTextW(data->editor,telemetry.text,128);DWORD start=0,end=0;SendMessageW(data->editor,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));telemetry.selection_start=start;telemetry.selection_end=end;
    POINT cursor{};SetLastError(0);const auto okay=GetCursorPos(&cursor);const auto error=okay?0:GetLastError();++telemetry.events;
    InterlockedIncrement(&shared->sequence);MemoryBarrier();shared->data=telemetry;MemoryBarrier();InterlockedIncrement(&shared->sequence);
    events<<"{\"event\":"<<json(name)<<",\"tickMs\":"<<GetTickCount64()<<",\"queryThread\":"<<GetCurrentThreadId()<<",\"message\":"<<message<<",\"cursorQueryOk\":"<<okay<<",\"cursorQueryError\":"<<error<<",\"state\":"<<telemetry_json(telemetry)<<",\"recorder\":"<<state_json(*data)<<"}\n";events.flush();
}
LRESULT CALLBACK editor_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_IME_STARTCOMPOSITION)R305Metadata.base.ime_active=1;if(message==WM_IME_ENDCOMPOSITION)R305Metadata.base.ime_active=0;
    const auto value=undo_dispatch(window,edit_proc,message,wp,lp);
    if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_CHAR)observe("native_edit_processed",message);return value;
}
void paint(HDC dc){
    RECT bounds{};GetClientRect(data->top,&bounds);FillRect(dc,&bounds,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));SetBkMode(dc,TRANSPARENT);
    const auto title=L"Data recorder / run "+std::to_wstring(data->run_id)+L" / PID "+std::to_wstring(GetCurrentProcessId());TextOutW(dc,24,24,title.c_str(),static_cast<int>(title.size()));
    const auto count=L"Ticks "+std::to_wstring(data->ticks)+L" / exports "+std::to_wstring(data->exports)+L" / imports "+std::to_wstring(data->imports);TextOutW(dc,24,116,count.c_str(),static_cast<int>(count.size()));
    for(unsigned i=0;i<4;++i){const auto row=L"row "+std::to_wstring(i)+L": "+std::to_wstring(data->samples[i].value)+L"   quality "+std::to_wstring(data->samples[i].quality);TextOutW(dc,24,212+20*static_cast<int>(i),row.c_str(),static_cast<int>(row.size()));}
    TextOutW(dc,24,300,data->note,static_cast<int>(data->note_length));
    TextOutW(dc,24,334,L"Recorder clock bits:",20);for(unsigned bit=0;bit<16;++bit){RECT r{24+static_cast<LONG>(bit)*10,350,32+static_cast<LONG>(bit)*10,362};FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject((data->ticks&(1ULL<<bit))?WHITE_BRUSH:BLACK_BRUSH)));}
    ++telemetry.draws;telemetry.last_print_update=data->ticks&65535;telemetry.last_draw_update=data->ticks&65535;telemetry.last_draw_ms=GetTickCount64();
}
LRESULT CALLBACK window_proc(HWND w,UINT message,WPARAM wp,LPARAM lp){
    auto& m=R305Metadata.base;
    if(message==freeze_message){m.frozen=1;return 0;}if(message==checkpoint_message){m.checkpoint_requested=1;return 0;}if(message==prepare_stop_message){m.prepare_stop=1;return 0;}
    if(message==resource_message){resource_service(m,w,data->editor,data->export_button);return 0;}if(message==capture_message){capture_owned_window(m,w);return 0;}if(message==observe_message){observe("requested_observation",message);return 0;}
    if(message==WM_PAINT){++m.paint_depth;PAINTSTRUCT ps{};const auto dc=BeginPaint(w,&ps);paint(dc);EndPaint(w,&ps);--m.paint_depth;observe("wm_paint_end",message);return 0;}
    if(message==WM_PRINTCLIENT){++telemetry.prints;++m.paint_depth;paint(reinterpret_cast<HDC>(wp));--m.paint_depth;observe("wm_printclient",message);return 0;}
    if(!m.published)return DefWindowProcW(w,message,wp,lp);
    if(message==WM_COMMAND&&LOWORD(wp)==202&&HIWORD(wp)==BN_CLICKED&&!m.frozen){++data->clicks;export_or_process(RecorderMailbox,*data);observe("native_button_clicked",message);InvalidateRect(w,nullptr,FALSE);return 0;}
    if(message==WM_CHAR&&!m.frozen&&wp>=32&&wp<127&&data->note_length+1<96){data->note[data->note_length++]=static_cast<wchar_t>(wp);data->note[data->note_length]=0;data->samples[0].value+=static_cast<std::uint64_t>(wp)*17;InvalidateRect(w,nullptr,FALSE);observe("recorder_note_input",message);return 0;}
    return DefWindowProcW(w,message,wp,lp);
}
void reference(volatile HWND* address,unsigned logical=1){auto& m=R305Metadata.base;demand(m.ref_count<8&&m.required_count<8,"recorder reference capacity");Reference r{reinterpret_cast<std::uint64_t>(address),logical};m.required_refs[m.required_count++]=r;m.refs[m.ref_count++]=r;}
void describe(){auto& m=R305Metadata.base;m.window=reinterpret_cast<std::uint64_t>(data->top);m.edit=reinterpret_cast<std::uint64_t>(data->editor);m.button=reinterpret_cast<std::uint64_t>(data->export_button);m.window_tid=GetWindowThreadProcessId(data->top,&m.window_pid);m.window_style=static_cast<DWORD>(GetWindowLongPtrW(data->top,GWL_STYLE));m.window_ex_style=static_cast<DWORD>(GetWindowLongPtrW(data->top,GWL_EXSTYLE));GetWindowRect(data->top,&m.window_rect);GetClassNameW(data->top,m.window_class,64);m.window_user_data=static_cast<std::uint64_t>(GetWindowLongPtrW(data->top,GWLP_USERDATA));}
__declspec(noinline) std::uint64_t sample(){
    volatile std::uint64_t values[4]{};volatile HWND window=nullptr,editor=nullptr;
    if(!R305Rebuild){values[0]=data->samples[2].value+data->run_id;window=data->top;editor=data->editor;LeafWorkTrap();}
    reference(&window);reference(&editor,2);R305Metadata.base.local[0]=reinterpret_cast<std::uint64_t>(&values[0]);
    for(unsigned i=0;i<3;++i)R305Metadata.execution.frame[i].local=R305Metadata.base.local[i];
    const auto reg=R305Metadata.base.wrong_chain?AlternateCheckpointTrap():CheckpointTrap();
    if(window!=data->top||editor!=data->editor||GetParent(editor)!=window)reject(ERROR_INVALID_WINDOW_HANDLE);data->return_mask|=8;return reg+values[0];
}
__declspec(noinline) std::uint64_t fold(){
    volatile std::uint64_t values[4]{};volatile HWND window=nullptr,button=nullptr;
    if(!R305Rebuild){std::uint64_t aggregate=0;for(unsigned i=0;i<8;++i)aggregate+=data->samples[i].value*(i+1);values[0]=aggregate;window=data->top;button=data->export_button;MiddleWorkTrap();}
    reference(&window);reference(&button,3);R305Metadata.base.local[1]=reinterpret_cast<std::uint64_t>(&values[0]);const auto value=sample();
    if(window!=data->top||button!=data->export_button||GetParent(button)!=window)reject(ERROR_INVALID_WINDOW_HANDLE);data->return_mask|=16;return (value^values[0])*1099511628211ULL;
}
__declspec(noinline) std::uint64_t emit(){volatile std::uint64_t values[4]{};if(!R305Rebuild){values[0]=data->samples[0].tick^(data->descriptor<<3);OuterWorkTrap();}R305Metadata.base.local[2]=reinterpret_cast<std::uint64_t>(&values[0]);const auto value=fold();data->return_mask|=32;return std::rotl(value,7)+values[0];}
__declspec(noinline) std::uint64_t record(){
    volatile std::uint64_t values[4]{};
    if(!R305Rebuild){values[0]=data->samples[5].value^data->run_id^0x35192A;FourthWorkTrap();}
    R305Metadata.execution.frame[3].local=reinterpret_cast<std::uint64_t>(&values[0]);
    const auto value=emit();data->return_mask|=64;return (value^values[0])+0x10591;
}
__declspec(noinline) void application(){
    auto& m=R305Metadata.base;
    if(!R305Rebuild){InitTrap();check(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&data->run_id),sizeof(data->run_id),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"BCryptGenRandom.recorder");data->descriptor=(2ULL<<32)|8;for(unsigned i=0;i<8;++i){data->samples[i].tick=i+1;data->samples[i].value=((data->run_id>>(i*5))&1023)+10+i;data->samples[i].quality=10+i;}swprintf_s(data->note,L"ROW-%08x",static_cast<unsigned>(data->run_id));data->note_length=static_cast<unsigned>(wcslen(data->note));data->ticks=27;SendMessageW(data->editor,WM_SETTEXT,0,reinterpret_cast<LPARAM>(data->note));SendMessageW(data->editor,EM_SETMODIFY,TRUE,0);SetFocus(data->editor);undo_arm(data->editor);m.published=1;observe("recorder_initialized");}
    if(R305Rebuild){m.frozen=1;m.checkpoint_requested=1;}
    for(;;){MSG message{};while(!R305Rebuild&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){++m.in_dispatch;TranslateMessage(&message);DispatchMessageW(&message);--m.in_dispatch;}
        if(m.checkpoint_requested){if(!R305Rebuild)UpdateWindow(data->top);if(!R305Rebuild&&checkpoint_probe&&!checkpoint_probe_sent){checkpoint_probe_sent=true;check(PostMessageW(data->top,checkpoint_probe==2?WM_APP+499:WM_PAINT,0,0),"PostMessageW.checkpoint_probe");}
            MSG pending{};const auto capture=GetCapture();const bool queued=!R305Rebuild&&PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE);
            events<<"{\"event\":\"checkpoint_guard\",\"tickMs\":"<<GetTickCount64()<<",\"frozen\":"<<m.frozen<<",\"dispatch\":"<<m.in_dispatch<<",\"paintDepth\":"<<m.paint_depth<<",\"resourcesReady\":"<<m.resources_ready<<",\"capture\":"<<reinterpret_cast<std::uint64_t>(capture)<<",\"queued\":"<<(queued?"true":"false")<<",\"message\":"<<pending.message<<",\"window\":"<<reinterpret_cast<std::uint64_t>(pending.hwnd)<<"}\n";events.flush();
            if(!m.frozen||m.in_dispatch||m.paint_depth||!m.resources_ready||(!R305Rebuild&&capture))reject(ERROR_BUSY);
            if(!R305Rebuild){
                // Resource-safe owner paint only. Never discard or replay an input
                // or restart a failed Snapshot; all other queued messages refuse.
                const auto deadline=GetTickCount64()+1000;
                while(PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE)){
                    if(pending.message!=WM_PAINT||(pending.hwnd!=data->top&&pending.hwnd!=data->editor&&pending.hwnd!=data->export_button)){
                        events<<"{\"event\":\"checkpoint_queue_rejected\",\"message\":"<<pending.message<<",\"window\":"<<reinterpret_cast<std::uint64_t>(pending.hwnd)<<"}\n";events.flush();reject(ERROR_BUSY);
                    }
                    if(GetTickCount64()>=deadline)reject(ERROR_TIMEOUT);
                    if(checkpoint_probe==3&&!quit_probe_sent){MSG withdrawn{};check(PeekMessageW(&withdrawn,pending.hwnd,WM_PAINT,WM_PAINT,PM_REMOVE),"PeekMessageW.quit_probe_withdraw");PostQuitMessage(0);quit_probe_sent=true;}
                    MSG paint{};if(!PeekMessageW(&paint,pending.hwnd,WM_PAINT,WM_PAINT,PM_REMOVE))reject(ERROR_BUSY);
                    if(paint.message!=WM_PAINT||paint.hwnd!=pending.hwnd){events<<"{\"event\":\"checkpoint_removed_rejected\",\"message\":"<<paint.message<<",\"window\":"<<reinterpret_cast<std::uint64_t>(paint.hwnd)<<"}\n";events.flush();reject(ERROR_BUSY);}
                    ++m.in_dispatch;TranslateMessage(&paint);DispatchMessageW(&paint);--m.in_dispatch;
                    events<<"{\"event\":\"checkpoint_paint_drained\",\"message\":"<<paint.message<<",\"window\":"<<reinterpret_cast<std::uint64_t>(paint.hwnd)<<"}\n";events.flush();
                }
                const auto remaining=PeekMessageW(&pending,nullptr,0,0,PM_NOREMOVE);
                events<<"{\"event\":\"checkpoint_guard_final\",\"queued\":"<<(remaining?"true":"false")<<",\"message\":"<<pending.message<<",\"inputClosed\":"<<m.input_closed<<"}\n";events.flush();
                if(remaining||GetCapture()||m.in_dispatch||m.paint_depth||!m.input_closed||!m.frozen||!m.resources_ready)reject(ERROR_BUSY);
            }describe();m.ref_count=0;m.required_count=0;reference(&data->top);reference(&data->editor,2);reference(&data->export_button,3);data->return_mask=0;data->outcome=record()^data->descriptor^data->samples[7].value;ContinuationTrap();m.checkpoint_requested=0;m.frozen=m.freeze_after_resume;observe("recorder_continued");}
        if(!m.frozen){data->ticks+=3;InvalidateRect(data->top,nullptr,FALSE);}MsgWaitForMultipleObjectsEx(0,nullptr,40,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
}
void managed(){auto& m=R305Metadata.base;GetCurrentThreadStackLimits(&m.reported_stack_low,&m.reported_stack_high);const auto tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());m.active_stack_low=reinterpret_cast<std::uint64_t>(tib->StackLimit);m.active_stack_high=reinterpret_cast<std::uint64_t>(tib->StackBase);ReadyTrap();if(R305Rebuild)while(!m.prepare_stop){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}MsgWaitForMultipleObjectsEx(0,nullptr,25,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}application();}
}
}
extern "C" void R305ManagedEntry(){recorder::managed();}
int wmain(int argc,wchar_t** argv){try{
    using namespace recorder;using namespace r305;const Args args(argc,argv);auto& m=R305Metadata.base;R305Rebuild=args.get(L"--mode")==L"source"?0:1;m.freeze_after_resume=R305Rebuild?1:0;
    checkpoint_probe=static_cast<unsigned>(args.number(L"--checkpoint-probe"));m.abi_version=2;m.abi_bytes=sizeof(MetadataV2);m.root=data_base;m.data_bytes=data_bytes;m.stack=stack_base;m.stack_bytes=stack_bytes;m.gui=1;m.pid=GetCurrentProcessId();m.tid=GetCurrentThreadId();m.image=reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));memcpy(m.build,R305_BUILD_ID,sizeof(m.build));
    if(args.number(L"--bad-abi"))m.abi_version=99;if(args.number(L"--unknown-child"))m.resource_kinds|=(1ULL<<63);
    state_map=std::make_unique<Mapping>(args.get(L"--state"),sizeof(Shared),false);shared=static_cast<Shared*>(state_map->view);events.open(std::filesystem::path(args.get(L"--log")),std::ios::binary);demand(events.good(),"recorder events");
    m.ready=reinterpret_cast<std::uint64_t>(&ReadySite);m.init=reinterpret_cast<std::uint64_t>(&InitTrap);m.checkpoint=reinterpret_cast<std::uint64_t>(&CheckpointSite);m.continued=reinterpret_cast<std::uint64_t>(&ContinuationTrap);m.rejected=reinterpret_cast<std::uint64_t>(&RejectTrap);m.alt_checkpoint=reinterpret_cast<std::uint64_t>(&AlternateCheckpointSite);m.changed=reinterpret_cast<std::uint64_t>(&ChangedTrap);m.rebuild_address=reinterpret_cast<std::uint64_t>(&R305Rebuild);m.cookie_address=reinterpret_cast<std::uint64_t>(&__security_cookie);m.complement_address=reinterpret_cast<std::uint64_t>(&__security_cookie_complement);m.frame_entry[0]=reinterpret_cast<std::uint64_t>(&sample);m.frame_entry[1]=reinterpret_cast<std::uint64_t>(&fold);m.frame_entry[2]=reinterpret_cast<std::uint64_t>(&emit);m.business_sites[0]=reinterpret_cast<std::uint64_t>(&OuterWorkTrap);m.business_sites[1]=reinterpret_cast<std::uint64_t>(&MiddleWorkTrap);m.business_sites[2]=reinterpret_cast<std::uint64_t>(&LeafWorkTrap);m.wrong_chain=static_cast<unsigned>(args.number(L"--wrong-chain"));
    auto& layout=R305Metadata.execution;layout.frames=4;layout.returns=6;layout.caller_entry=reinterpret_cast<std::uint64_t>(&application);
    for(unsigned i=0;i<3;++i)layout.frame[i]={m.frame_entry[i],0,m.business_sites[2-i]};
    layout.frame[3]={reinterpret_cast<std::uint64_t>(&record),0,reinterpret_cast<std::uint64_t>(&FourthWorkTrap)};
    if(args.number(L"--bad-layout")){layout.frames=3;layout.returns=5;}
    if(args.number(L"--missing-frame"))layout.frame[3].entry=0;
    if(args.number(L"--bad-unwind"))layout.frame[3].entry=reinterpret_cast<std::uint64_t>(&application);

    check(GetProcessMitigationPolicy(GetCurrentProcess(),static_cast<PROCESS_MITIGATION_POLICY>(15),&m.shadow_policy,sizeof(m.shadow_policy)),"GetProcessMitigationPolicy.CET");check(GetProcessMitigationPolicy(GetCurrentProcess(),static_cast<PROCESS_MITIGATION_POLICY>(7),&m.cfg_policy,sizeof(m.cfg_policy)),"GetProcessMitigationPolicy.CFG");
    demand(VirtualAlloc(reinterpret_cast<void*>(data_base),data_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reinterpret_cast<void*>(data_base),"recorder data allocation");demand(VirtualAlloc(reinterpret_cast<void*>(stack_base),stack_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==reinterpret_cast<void*>(stack_base),"recorder stack allocation");DWORD old=0;check(VirtualProtect(reinterpret_cast<void*>(stack_base),4096,PAGE_NOACCESS,&old),"VirtualProtect.recorder_guard");
    WNDCLASSW cls{};cls.lpfnWndProc=window_proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"FocusViewsNativeRecorderV2";check(RegisterClassW(&cls)!=0,"RegisterClassW.recorder");
    data->top=CreateWindowExW(0,cls.lpszClassName,L"Independent native data recorder",WS_OVERLAPPEDWINDOW,40,40,640,420,nullptr,nullptr,cls.hInstance,data);check(data->top!=nullptr,"CreateWindowExW.recorder");SetWindowLongPtrW(data->top,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(data));
    data->editor=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_LEFT|ES_NOHIDESEL,24,72,540,32,data->top,reinterpret_cast<HMENU>(201),cls.hInstance,nullptr);data->export_button=CreateWindowExW(0,L"BUTTON",L"Export CSV",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,24,150,166,45,data->top,reinterpret_cast<HMENU>(202),cls.hInstance,nullptr);check(data->editor&&data->export_button,"CreateWindowExW.recorder_controls");
    SendMessageW(data->editor,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);SendMessageW(data->export_button,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);edit_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(data->editor,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(editor_proc)));check(edit_proc!=nullptr,"SetWindowLongPtrW.recorder_observer");
    if(args.number(L"--unknown-child"))check(CreateWindowExW(0,L"STATIC",L"unregistered",WS_CHILD|WS_VISIBLE,320,170,100,20,data->top,nullptr,cls.hInstance,nullptr)!=nullptr,"CreateWindowExW.unknown_resource");
    describe();wcscpy_s(telemetry.desktop,thread_desktop().c_str());ShowWindow(data->top,SW_SHOWNOACTIVATE);UpdateWindow(data->top);observe("recorder_prepared");GetCurrentThreadStackLimits(&m.original_stack_low,&m.original_stack_high);auto tib=reinterpret_cast<NT_TIB*>(NtCurrentTeb());tib->StackBase=reinterpret_cast<void*>(stack_base+stack_bytes);tib->StackLimit=reinterpret_cast<void*>(stack_base+4096);EnterManagedStack(tib->StackBase);__assume(0);
}catch(const std::exception& e){std::cerr<<"RECORDER_FAILURE "<<e.what()<<'\n';return 1;}}
