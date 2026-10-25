#pragma once
#include "../r3/protocol.hpp"
namespace r305 {
// Explicit resource journal, not a dump of Windows private EDIT storage.
struct EditState {
    wchar_t text[128]{};
    DWORD start=0,end=0;
    BOOL modified=FALSE,can_undo=FALSE;
};
struct UndoState {
    unsigned version=1,operations=0,valid=0;
    DWORD owner_tid=0;
    UINT message=0;
    WPARAM character=0;
    EditState before{},after{};
    char reason[96]{};
};
EditState edit_state(HWND);
bool edit_equal(const EditState&,const EditState&);
std::string edit_json(const EditState&);
std::string undo_json(const UndoState&);
void undo_arm(HWND,const UndoState* restored=nullptr);
void undo_suspend(bool);
UndoState undo_snapshot(HWND);
UndoState undo_observed();
LRESULT undo_dispatch(HWND,WNDPROC,UINT,WPARAM,LPARAM);
void undo_rebuild(HWND,const UndoState&,unsigned fault);
int undo_probe(const r3::Args&);
}
