#include "protocol.hpp"
#include <imm.h>

namespace r305 {
namespace {
void require(bool okay,const char* api,DWORD code=ERROR_NOT_SUPPORTED){if(!okay)throw Error(api,code);}
unsigned logical(HWND value,HWND top,HWND edit,HWND button){return value==top?1u:value==edit?2u:value==button?3u:0u;}
WindowNode node(HWND window,unsigned identity,HWND top,HWND edit,HWND button){
    WindowNode n{};n.hwnd=reinterpret_cast<std::uint64_t>(window);n.logical=identity;
    n.parent=identity==1?0:logical(GetParent(window),top,edit,button);n.id=GetDlgCtrlID(window);
    n.tid=GetWindowThreadProcessId(window,&n.pid);n.style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));n.ex_style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
    check(GetClassNameW(window,n.klass,64)!=0,"GetClassNameW.resource");check(GetWindowRect(window,&n.rect),"GetWindowRect.resource");
    if(identity!=1){POINT points[2]{{n.rect.left,n.rect.top},{n.rect.right,n.rect.bottom}};SetLastError(0);const auto moved=MapWindowPoints(nullptr,top,points,2);check(moved!=0||GetLastError()==0,"MapWindowPoints.resource");n.rect={points[0].x,points[0].y,points[1].x,points[1].y};}
    const auto length=SendMessageW(window,WM_GETTEXTLENGTH,0,0);require(length>=0&&length<128,"WM_GETTEXTLENGTH.resource");
    require(SendMessageW(window,WM_GETTEXT,128,reinterpret_cast<LPARAM>(n.caption))==length,"WM_GETTEXT.resource",ERROR_INVALID_DATA);
    return n;
}
BOOL CALLBACK count_child(HWND,LPARAM data){++*reinterpret_cast<unsigned*>(data);return TRUE;}
Resources inspect(Metadata& m,HWND top,HWND edit,HWND button){
    Resources r{};r.undo=undo_observed();r.query_tid=GetCurrentThreadId();r.nodes[0]=node(top,1,top,edit,button);r.nodes[1]=node(edit,2,top,edit,button);r.nodes[2]=node(button,3,top,edit,button);
    check(EnumChildWindows(top,count_child,reinterpret_cast<LPARAM>(&r.descendants)),"EnumChildWindows.resource");
    SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&r.selection_start),reinterpret_cast<LPARAM>(&r.selection_end));
    r.modified=static_cast<BOOL>(SendMessageW(edit,EM_GETMODIFY,0,0));r.can_undo=static_cast<BOOL>(SendMessageW(edit,EM_CANUNDO,0,0));
    r.focus=logical(GetFocus(),top,edit,button);r.first_visible=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);
    if(r.focus==2)check(GetCaretPos(&r.caret),"GetCaretPos.resource");else r.caret={-1,-1};
    r.caret_at_selection_end=static_cast<LONG>(SendMessageW(edit,EM_POSFROMCHAR,r.selection_end,0));
    const auto length=wcslen(r.nodes[1].caption);
    if(r.caret_at_selection_end==-1 && length>0 && r.selection_end==length){
        const auto last=static_cast<LONG>(SendMessageW(edit,EM_POSFROMCHAR,length-1,0));
        require(last!=-1,"resource.EDIT.end_caret_position");
        const auto dc=GetDC(edit);check(dc!=nullptr,"GetDC.EDIT_caret");
        const auto font=reinterpret_cast<HFONT>(SendMessageW(edit,WM_GETFONT,0,0));const auto old=SelectObject(dc,font);SIZE width{};
        const auto okay=GetTextExtentPoint32W(dc,r.nodes[1].caption+length-1,1,&width);SelectObject(dc,old);ReleaseDC(edit,dc);check(okay,"GetTextExtentPoint32W.EDIT_caret");
        r.caret_at_selection_end=MAKELONG(static_cast<short>(LOWORD(last))+width.cx,HIWORD(last));
    }
    r.ime_active=m.ime_active;
    const auto context=ImmGetContext(edit);
    if(context){r.ime_bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);check(ImmReleaseContext(edit,context),"ImmReleaseContext.resource");}
    return r;
}
void validate(const Resources& r){
    require(r.descendants==2,"resource.unknown_descendant");
    for(unsigned i=0;i<3;++i){const auto& n=r.nodes[i];require(n.logical==i+1 && n.pid==GetCurrentProcessId() && n.tid==GetCurrentThreadId(),"resource.thread_or_identity");
        require(n.parent==(i?1u:0u),"resource.parent_relation");}
    require(!wcscmp(r.nodes[0].klass,L"FocusViewsR305SelfDraw")&&!_wcsicmp(r.nodes[1].klass,L"EDIT")&&!_wcsicmp(r.nodes[2].klass,L"BUTTON"),"resource.control_class");
    require(r.nodes[1].id==101&&r.nodes[2].id==102,"resource.control_id");
    require(!r.can_undo||(r.undo.valid&&r.undo.operations==1),"resource.EDIT.unobserved_undo_history");
    require(!r.ime_active&&r.ime_bytes<=0&&r.ime_bytes>=-1,"resource.EDIT.IME_composition");
    require(r.first_visible==0,"resource.EDIT.horizontal_scroll");
    require(r.focus==2,"resource.focus_scope");
    const auto length=wcslen(r.nodes[1].caption);
    require(r.selection_start<=r.selection_end&&r.selection_end<=length&&length>0,"resource.EDIT.selection_scope");
    for(const auto c:std::wstring(r.nodes[1].caption))require(c>=32&&c<127,"resource.EDIT.ASCII_scope");
    RECT format{};SendMessageW(reinterpret_cast<HWND>(r.nodes[1].hwnd),EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    // Single-line EM_POSFROMCHAR can report y=0 while the GUI caret is at
    // the formatting rectangle's top (observed y=2). Use x for direction,
    // validate actual caret y against native formatting bounds, and compare
    // the full saved caret POINT during reconstruction readback below.
    require(r.caret_at_selection_end!=-1&&static_cast<short>(LOWORD(r.caret_at_selection_end))==r.caret.x&&r.caret.y>=format.top&&r.caret.y<format.bottom,"resource.EDIT.reverse_or_unknown_caret");
    const auto font=reinterpret_cast<HFONT>(SendMessageW(reinterpret_cast<HWND>(r.nodes[1].hwnd),WM_GETFONT,0,0));
    require(font==GetStockObject(DEFAULT_GUI_FONT),"resource.EDIT.font_scope");
}
void restore(const Resources& saved,unsigned fault,Metadata& m,HWND top,HWND edit,HWND button){
    // Capture/restore runs only on the responsive owner GUI thread. The target
    // has not entered the business task or structural checkpoint yet.
    require((!saved.can_undo||(saved.undo.valid&&saved.undo.operations==1)) && saved.descendants==2 && saved.focus==2,"resource.restore.unsupported_snapshot");
    require(saved.selection_start<=saved.selection_end && saved.selection_end<=wcslen(saved.nodes[1].caption),"resource.restore.selection",ERROR_INVALID_DATA);
    const auto current=inspect(m,top,edit,button);
    require(current.descendants==2,"resource.restore.unknown_descendant");
    for(unsigned i=0;i<3;++i){const auto& a=saved.nodes[i];const auto& b=current.nodes[i];
        require(a.logical==i+1&&a.parent==(i?1u:0u)&&b.parent==a.parent,"resource.restore.parent_relation",ERROR_INVALID_DATA);
        require(!_wcsicmp(a.klass,b.klass)&&a.id==b.id&&a.style==b.style&&a.ex_style==b.ex_style&&!memcmp(&a.rect,&b.rect,sizeof(RECT)),"resource.restore.window_descriptor",ERROR_INVALID_DATA);
    }
    undo_suspend(true);
    if(saved.can_undo)undo_rebuild(edit,saved.undo,fault);
    else if(fault!=1)check(SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(saved.nodes[1].caption))!=0,"WM_SETTEXT.restore");
    undo_suspend(true);
    if(fault!=2)SendMessageW(edit,EM_SETSEL,saved.selection_start,saved.selection_end);
    if(fault!=3)SendMessageW(edit,EM_SETMODIFY,saved.modified,0);
    if(fault!=4){SetFocus(edit);require(GetFocus()==edit,"SetFocus.restore",ERROR_INVALID_STATE);}
    undo_arm(edit,saved.can_undo?&saved.undo:nullptr);
    m.resources_ready=1;
}
}
std::string resources_json(const Resources& r){
    std::ostringstream s;s<<"{\"undo\":"<<undo_json(r.undo)<<",\"queryTid\":"<<r.query_tid<<",\"descendants\":"<<r.descendants<<",\"focus\":"<<r.focus<<",\"selectionStart\":"<<r.selection_start<<",\"selectionEnd\":"<<r.selection_end<<",\"modified\":"<<r.modified<<",\"canUndo\":"<<r.can_undo<<",\"firstVisible\":"<<r.first_visible<<",\"caretX\":"<<r.caret.x<<",\"caretY\":"<<r.caret.y<<",\"imeActive\":"<<r.ime_active<<",\"imeBytes\":"<<r.ime_bytes<<",\"nodes\":[";
    for(unsigned i=0;i<3;++i){if(i)s<<',';const auto& n=r.nodes[i];s<<"{\"logical\":"<<n.logical<<",\"hwnd\":"<<n.hwnd<<",\"parent\":"<<n.parent<<",\"id\":"<<n.id<<",\"pid\":"<<n.pid<<",\"tid\":"<<n.tid<<",\"class\":"<<json(n.klass)<<",\"text\":"<<json(n.caption)<<",\"style\":"<<n.style<<",\"exStyle\":"<<n.ex_style<<",\"left\":"<<n.rect.left<<",\"top\":"<<n.rect.top<<",\"right\":"<<n.rect.right<<",\"bottom\":"<<n.rect.bottom<<'}';}s<<"]}";return s.str();
}
void resource_service(Metadata& m,HWND top,HWND edit,HWND button){
    auto& service=m.service;service.error=0;service.api[0]=0;
    try{
        require(GetCurrentThreadId()==m.tid,"resource.owner_thread",ERROR_INVALID_THREAD_ID);
        if(service.operation==1){
            require(m.frozen&&m.input_closed&&!m.paint_depth&&!GetCapture(),"resource.capture.not_quiescent",ERROR_BUSY);
            service.observed=inspect(m,top,edit,button);service.observed.undo=undo_snapshot(edit);validate(service.observed);service.payload=service.observed;m.resources_ready=1;
        }else if(service.operation==2){
            service.observed=inspect(m,top,edit,button);
            restore(service.payload,service.fault,m,top,edit,button);service.observed=inspect(m,top,edit,button);
            const auto& a=service.payload;const auto& b=service.observed;
            require(!wcscmp(a.nodes[1].caption,b.nodes[1].caption)&&a.selection_start==b.selection_start&&a.selection_end==b.selection_end&&a.modified==b.modified&&a.focus==b.focus&&!memcmp(&a.caret,&b.caret,sizeof(POINT))&&a.can_undo==b.can_undo,"resource.restore.readback",ERROR_CRC);
            validate(service.observed);
        }else if(service.operation==3){service.observed=inspect(m,top,edit,button);}
        else if(service.operation==4){service.native_result=SendMessageW(edit,EM_UNDO,0,0);service.observed=inspect(m,top,edit,button);}
        else if(service.operation==5){SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);service.observed=inspect(m,top,edit,button);}
        else if(service.operation==6){SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"?"));service.observed=inspect(m,top,edit,button);}
        else throw Error("resource.operation",ERROR_INVALID_PARAMETER);
    }catch(const Error& e){
        service.error=e.code;strncpy_s(service.api,e.api.c_str(),_TRUNCATE);m.resources_ready=0;
        // Preserve the primary failure and independently read the actual failed
        // native state. A diagnostic read failure must not overwrite it.
        try{service.observed=inspect(m,top,edit,button);}catch(const Error& diagnostic){service.diagnostic_error=diagnostic.code;}
    }
    MemoryBarrier();InterlockedExchange(reinterpret_cast<volatile LONG*>(&service.done),static_cast<LONG>(service.sequence));
}
}
