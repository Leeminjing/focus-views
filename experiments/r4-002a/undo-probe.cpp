#include "undo.hpp"
#include <bcrypt.h>
#include <fstream>
#include <filesystem>
namespace r305 {
namespace {
WNDPROC original;
LRESULT CALLBACK observer(HWND w,UINT m,WPARAM p,LPARAM l){return undo_dispatch(w,original,m,p,l);}
void queued_character(HWND edit,wchar_t value){
    r3::check(PostMessageW(edit,WM_CHAR,value,1),"PostMessageW.probe_character");
    MSG msg{};const auto end=GetTickCount64()+1000;bool consumed=false;
    while(GetTickCount64()<end){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);if(msg.hwnd==edit&&msg.message==WM_CHAR)consumed=true;}if(consumed)return;Sleep(1);}
    throw r3::Error("probe.character_timeout",ERROR_TIMEOUT);
}
}
int undo_probe(const r3::Args& args){
    std::ofstream log(std::filesystem::path(args.get(L"--log")),std::ios::binary);r3::demand(log.good(),"probe log");
    auto emit=[&](const char* phase,const EditState& state){log<<"{\"phase\":"<<r3::json(phase)<<",\"pid\":"<<GetCurrentProcessId()<<",\"tid\":"<<GetCurrentThreadId()<<",\"state\":"<<edit_json(state)<<"}\n";log.flush();};
    const auto name=L"fv_undo_probe_"+std::to_wstring(GetCurrentProcessId());
    const auto desk=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);r3::check(desk!=nullptr,"CreateDesktopW.undo_probe");
    struct Desk {HDESK value;~Desk(){CloseDesktop(value);}} desk_guard{desk};r3::check(SetThreadDesktop(desk),"SetThreadDesktop.undo_probe");
    const auto top=CreateWindowExW(0,L"STATIC",L"native EDIT undo mechanism",WS_OVERLAPPEDWINDOW,30,30,640,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);r3::check(top!=nullptr,"CreateWindowExW.undo_top");
    struct Window {HWND value;~Window(){DestroyWindow(value);}} window_guard{top};
    HWND windows[3]{};for(unsigned i=0;i<3;++i){windows[i]=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NOHIDESEL,20,40+static_cast<int>(i)*50,540,32,top,reinterpret_cast<HMENU>(static_cast<UINT_PTR>(100+i)),GetModuleHandleW(nullptr),nullptr);r3::check(windows[i]!=nullptr,"CreateWindowExW.undo_edit");}
    const auto source=windows[0],reference=windows[1],target=windows[2];
    std::uint64_t seed=0;r3::demand(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&seed),sizeof(seed),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"random text");
    wchar_t base[64]{};swprintf_s(base,L"BASE-%016llx",static_cast<unsigned long long>(seed));
    for(const auto edit:{source,reference}){SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(base));SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(edit,EM_SETMODIFY,TRUE,0);}
    original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(source,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(observer)));
    r3::check(original!=nullptr,"SetWindowLongPtrW.undo_observer");undo_arm(source);
    emit("source-before",edit_state(source));queued_character(source,L'A');
    const auto fixed=edit_state(source);const auto saved=undo_snapshot(source);
    emit("source-after",fixed);log<<"{\"phase\":\"resource-journal\",\"journal\":"<<undo_json(saved)<<"}\n";log.flush();
    queued_character(reference,L'A');r3::demand(edit_equal(fixed,edit_state(reference)),"reference starts with same real edit");
    undo_rebuild(target,saved,0);emit("target-rebuilt",edit_state(target));
    bool passed=true;
    for(unsigned i=0;i<2;++i){const auto a=SendMessageW(reference,EM_UNDO,0,0),b=SendMessageW(target,EM_UNDO,0,0);const auto expected=edit_state(reference),actual=edit_state(target);emit(i?"reference-undo2":"reference-undo1",expected);emit(i?"target-undo2":"target-undo1",actual);
        const bool same=a==b&&edit_equal(expected,actual);passed=passed&&same;log<<"{\"phase\":\"undo-comparison\",\"index\":"<<i+1<<",\"referenceResult\":"<<a<<",\"targetResult\":"<<b<<",\"equal\":"<<(same?"true":"false")<<"}\n";log.flush();}
    queued_character(reference,L'Z');queued_character(target,L'Z');emit("reference-continued-input",edit_state(reference));emit("target-continued-input",edit_state(target));passed=passed&&edit_equal(edit_state(reference),edit_state(target))&&edit_equal(fixed,edit_state(source));
    log<<"{\"phase\":\"complete\",\"sourceUntouched\":"<<(edit_equal(fixed,edit_state(source))?"true":"false")<<",\"passed\":"<<(passed?"true":"false")<<"}\n";
    return passed?0:2;
}
}
