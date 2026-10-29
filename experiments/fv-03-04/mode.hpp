#pragma once
#include "protocol.hpp"
namespace fv_live {
inline std::wstring mode;
inline void set_mode(const wchar_t* selected){mode=selected;demand(mode==L"positive"||mode==L"live-auto"||mode==L"live-manual"||mode==L"live-input-exit"||mode==L"live-manager-exit","explicit bounded interaction scenario");}
}
