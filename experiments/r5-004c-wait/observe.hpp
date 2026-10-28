#pragma once
#include <tlhelp32.h>
namespace wait_probe {
inline void modules(r305::Child& v,const char* stage,r305::Log& log){using namespace r305;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,v.p.pid));
    if(snapshot.value==INVALID_HANDLE_VALUE){const auto error=GetLastError();log.emit("trim_module_observation","\"stage\":"+json(stage)+",\"complete\":false,\"error\":"+std::to_string(error));return;}
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);BOOL next=Module32FirstW(snapshot.value,&entry);unsigned abort_count=0,known=0,total=0;DWORD error=next?0:GetLastError();
    while(next){++total;const auto name=std::filesystem::path(entry.szExePath).filename().wstring();
        const bool abort=_wcsicmp(name.c_str(),L"abort-runtime.dll")==0;if(abort)++abort_count;
        bool selected=abort;for(const auto item:{L"runtime304.dll",L"user32.dll",L"imm32.dll",L"msctf.dll",L"msctfmonitor.dll",L"combase.dll",L"ole32.dll",L"rpcrt4.dll"})selected=selected||_wcsicmp(name.c_str(),item)==0;
        if(selected){++known;std::string sha,hash_error;try{sha=native::digest_text(native::digest(native::read_file(entry.szExePath)));}catch(const std::exception& e){hash_error=e.what();}
            log.emit("trim_known_module","\"stage\":"+json(stage)+",\"pid\":"+std::to_string(v.p.pid)+",\"name\":"+json(name.c_str())+",\"path\":"+json(entry.szExePath)+",\"base\":"+std::to_string(reinterpret_cast<std::uint64_t>(entry.modBaseAddr))+",\"imageBytes\":"+std::to_string(entry.modBaseSize)+",\"diskSha256\":"+json(sha)+",\"hashError\":"+json(hash_error));}
        next=Module32NextW(snapshot.value,&entry);if(!next)error=GetLastError();
    }
    log.emit("trim_module_observation","\"stage\":"+json(stage)+",\"pid\":"+std::to_string(v.p.pid)+",\"complete\":"+std::string(error==ERROR_NO_MORE_FILES?"true":"false")+",\"error\":"+std::to_string(error)+",\"known\":"+std::to_string(known)+",\"total\":"+std::to_string(total)+",\"cleanupRuntimeModules\":"+std::to_string(abort_count));
}
}
