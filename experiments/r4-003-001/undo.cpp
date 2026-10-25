#include "undo.hpp"
#include <cstring>
namespace r305 {
namespace {
thread_local UndoState journal{};
thread_local HWND owned=nullptr;
thread_local bool armed=false,suspended=false;
void require(bool value,const char* api){if(!value)throw r3::Error(api,ERROR_NOT_SUPPORTED);}
void invalidate(const char* reason){journal.valid=0;strncpy_s(journal.reason,reason,_TRUNCATE);}
}
EditState edit_state(HWND window){
    EditState s{};
    const auto length=SendMessageW(window,WM_GETTEXTLENGTH,0,0);
    require(length>=0&&length<128,"undo.text_scope");
    require(SendMessageW(window,WM_GETTEXT,128,reinterpret_cast<LPARAM>(s.text))==length,"undo.text_read");
    SendMessageW(window,EM_GETSEL,reinterpret_cast<WPARAM>(&s.start),reinterpret_cast<LPARAM>(&s.end));
    s.modified=static_cast<BOOL>(SendMessageW(window,EM_GETMODIFY,0,0));
    s.can_undo=static_cast<BOOL>(SendMessageW(window,EM_CANUNDO,0,0));return s;
}
bool edit_equal(const EditState& a,const EditState& b){return !wcscmp(a.text,b.text)&&a.start==b.start&&a.end==b.end&&a.modified==b.modified&&a.can_undo==b.can_undo;}
std::string edit_json(const EditState& s){return "{\"text\":"+r3::json(s.text)+",\"start\":"+std::to_string(s.start)+",\"end\":"+std::to_string(s.end)+",\"modified\":"+std::to_string(s.modified)+",\"canUndo\":"+std::to_string(s.can_undo)+"}";}
std::string undo_json(const UndoState& s){return "{\"version\":"+std::to_string(s.version)+",\"operations\":"+std::to_string(s.operations)+",\"valid\":"+std::to_string(s.valid)+",\"ownerTid\":"+std::to_string(s.owner_tid)+",\"message\":"+std::to_string(s.message)+",\"character\":"+std::to_string(s.character)+",\"reason\":"+r3::json(s.reason)+",\"before\":"+edit_json(s.before)+",\"after\":"+edit_json(s.after)+"}";}
void undo_arm(HWND window,const UndoState* restored){owned=window;armed=true;suspended=false;journal=restored?*restored:UndoState{};journal.owner_tid=GetCurrentThreadId();}
void undo_suspend(bool value){suspended=value;}
UndoState undo_observed(){return journal;}
UndoState undo_snapshot(HWND window){
    require(window==owned&&armed&&!suspended,"undo.observer_not_armed");
    const auto current=edit_state(window);
    if(current.can_undo){require(journal.valid&&journal.operations==1,"undo.unsupported_or_multiple_edits");require(edit_equal(current,journal.after),"undo.unobserved_state_change");}
    else require(journal.operations==0&&journal.reason[0]==0,"undo.history_disappeared_or_unknown_mutation");
    return journal;
}
LRESULT undo_dispatch(HWND window,WNDPROC original,UINT message,WPARAM wp,LPARAM lp){
    const bool active=armed&&!suspended&&window==owned;
    const bool character=active&&message==WM_CHAR&&wp>=32&&wp<127;
    EditState before{};
    if(character)before=edit_state(window);
    if(active&&(message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==EM_UNDO||message==EM_REPLACESEL||message==WM_SETTEXT||message==EM_EMPTYUNDOBUFFER||message==EM_SETHANDLE||message==EM_SETMODIFY||message==WM_IME_STARTCOMPOSITION||message==WM_IME_COMPOSITION||(message==WM_KEYDOWN&&(wp==VK_DELETE||wp==VK_BACK))||(message==WM_CHAR&&!character)))invalidate("unsupported_mutation_or_IME");
    const auto result=CallWindowProcW(original,window,message,wp,lp);
    if(character){
        const auto after=edit_state(window);const bool first=journal.operations==0&&journal.reason[0]==0;
        ++journal.operations;
        if(first){journal.before=before;journal.after=after;journal.message=message;journal.character=wp;
            journal.valid=!before.can_undo&&before.start==0&&before.end==wcslen(before.text)&&before.end>0&&after.text[0]==static_cast<wchar_t>(wp)&&after.text[1]==0&&after.start==1&&after.end==1&&after.modified&&after.can_undo;
            if(!journal.valid)invalidate("not_single_full_selection_ASCII_replacement");
        }else invalidate("multiple_or_merged_edits");
    }
    return result;
}
void undo_rebuild(HWND window,const UndoState& saved,unsigned fault){
    require(saved.version==1&&saved.valid&&saved.operations==1&&saved.message==WM_CHAR&&saved.character>=32&&saved.character<127,"undo.restore.unsupported_journal");
    require(!saved.before.can_undo&&saved.before.start==0&&saved.before.end==wcslen(saved.before.text)&&saved.before.end>0,"undo.restore.preimage");
    suspended=true;
    struct Reset {~Reset(){suspended=false;}} reset;
    r3::check(SendMessageW(window,WM_SETTEXT,0,reinterpret_cast<LPARAM>(saved.before.text))!=0,"WM_SETTEXT.undo_preimage");
    SendMessageW(window,EM_SETSEL,saved.before.start,saved.before.end);
    wchar_t replacement[2]{fault==6?L'!':static_cast<wchar_t>(saved.character),0};
    SendMessageW(window,EM_REPLACESEL,fault==5?FALSE:TRUE,reinterpret_cast<LPARAM>(replacement));
    SendMessageW(window,EM_SETSEL,saved.after.start,saved.after.end);
    SendMessageW(window,EM_SETMODIFY,saved.after.modified,0);
    require(edit_equal(edit_state(window),saved.after),"undo.restore.native_readback");
}
}
