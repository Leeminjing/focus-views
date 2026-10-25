#include "abi.hpp"
namespace r305 {
std::string edit_json(const EditState& s){return "{\"text\":"+r3::json(s.text)+",\"start\":"+std::to_string(s.start)+",\"end\":"+std::to_string(s.end)+",\"modified\":"+std::to_string(s.modified)+",\"canUndo\":"+std::to_string(s.can_undo)+"}";}
std::string undo_json(const UndoState& s){return "{\"version\":"+std::to_string(s.version)+",\"operations\":"+std::to_string(s.operations)+",\"valid\":"+std::to_string(s.valid)+",\"ownerTid\":"+std::to_string(s.owner_tid)+",\"message\":"+std::to_string(s.message)+",\"character\":"+std::to_string(s.character)+",\"reason\":"+r3::json(s.reason)+",\"before\":"+edit_json(s.before)+",\"after\":"+edit_json(s.after)+"}";}
std::string resources_json(const Resources& r){
    std::ostringstream s;s<<"{\"undo\":"<<undo_json(r.undo)<<",\"queryTid\":"<<r.query_tid<<",\"descendants\":"<<r.descendants<<",\"focus\":"<<r.focus<<",\"selectionStart\":"<<r.selection_start<<",\"selectionEnd\":"<<r.selection_end<<",\"modified\":"<<r.modified<<",\"canUndo\":"<<r.can_undo<<",\"firstVisible\":"<<r.first_visible<<",\"caretX\":"<<r.caret.x<<",\"caretY\":"<<r.caret.y<<",\"imeActive\":"<<r.ime_active<<",\"imeBytes\":"<<r.ime_bytes<<",\"nodes\":[";
    for(unsigned i=0;i<3;++i){if(i)s<<',';const auto& n=r.nodes[i];s<<"{\"logical\":"<<n.logical<<",\"hwnd\":"<<n.hwnd<<",\"parent\":"<<n.parent<<",\"id\":"<<n.id<<",\"pid\":"<<n.pid<<",\"tid\":"<<n.tid<<",\"class\":"<<json(n.klass)<<",\"text\":"<<json(n.caption)<<",\"style\":"<<n.style<<",\"exStyle\":"<<n.ex_style<<",\"left\":"<<n.rect.left<<",\"top\":"<<n.rect.top<<",\"right\":"<<n.rect.right<<",\"bottom\":"<<n.rect.bottom<<'}';}s<<"]}";return s.str();
}
}
