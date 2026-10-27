#pragma once
#include "../r5-004/crash.hpp"
namespace r5004a {
inline void inspect_desktop(const std::string& full,r305::Log& log){
    if(!r5004::desktop_exists(full)){log.emit("desktop_diagnostic_absent","\"name\":"+r305::json(full));return;}
    const auto pos=full.find('\\');if(pos==std::string::npos)throw r305::Error("cleanup.desktop_name",ERROR_INVALID_DATA);
    const auto short_name=full.substr(pos+1);const std::wstring name(short_name.begin(),short_name.end());
    auto desk=OpenDesktopW(name.c_str(),0,FALSE,DESKTOP_ENUMERATE|DESKTOP_READOBJECTS);
    const auto error=desk?0:GetLastError();log.emit("desktop_diagnostic_open","\"name\":"+r305::json(full)+",\"error\":"+std::to_string(error));
    if(!desk)return;
    struct State{r305::Log& log;unsigned windows=0;} state{log};
    SetLastError(0);const auto okay=EnumDesktopWindows(desk,[](HWND w,LPARAM p)->BOOL{
        auto& s=*reinterpret_cast<State*>(p);++s.windows;DWORD pid=0;const auto tid=GetWindowThreadProcessId(w,&pid);wchar_t cls[256]{};GetClassNameW(w,cls,256);
        r305::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));const auto open_error=process.value?0:GetLastError();wchar_t image[32768]{};DWORD n=32768;const auto image_ok=process.value?QueryFullProcessImageNameW(process.value,0,image,&n):FALSE;const auto image_error=image_ok?0:GetLastError();
        s.log.emit("desktop_diagnostic_window","\"hwnd\":"+std::to_string(reinterpret_cast<ULONG_PTR>(w))+",\"pid\":"+std::to_string(pid)+",\"tid\":"+std::to_string(tid)+",\"class\":"+r305::json(cls)+",\"processOpenError\":"+std::to_string(open_error)+",\"imageError\":"+std::to_string(image_error)+",\"image\":"+r305::json(image));return TRUE;
    },reinterpret_cast<LPARAM>(&state));const auto enum_error=okay?0:GetLastError();
    log.emit("desktop_diagnostic_enumeration","\"okay\":"+(okay?std::string("true"):"false")+",\"error\":"+std::to_string(enum_error)+",\"windows\":"+std::to_string(state.windows));
    const auto closed=CloseDesktop(desk);const auto close_error=closed?0:GetLastError();log.emit("desktop_diagnostic_closed","\"closed\":"+(closed?std::string("true"):"false")+",\"error\":"+std::to_string(close_error));
}
}
