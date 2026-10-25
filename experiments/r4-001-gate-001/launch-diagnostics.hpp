#pragma once
#include <sddl.h>
// Experiment-only read-only probes. Never change ACLs, tokens or launch policy.
namespace gate001 {
inline std::string identity() {
    HANDLE token=nullptr;std::string sid="unavailable";DWORD elevated=0,integrity=0,error=0;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<BYTE> bytes(n);
        if(GetTokenInformation(token,TokenUser,bytes.data(),n,&n)) {LPWSTR text=nullptr;if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid,&text)){sid=r3::utf8(text);LocalFree(text);}}
        TOKEN_ELEVATION elevation{};if(GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&n))elevated=elevation.TokenIsElevated;
        GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&n);bytes.resize(n);
        if(GetTokenInformation(token,TokenIntegrityLevel,bytes.data(),n,&n)){auto p=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(bytes.data())->Label.Sid;integrity=*GetSidSubAuthority(p,*GetSidSubAuthorityCount(p)-1);}
        CloseHandle(token);
    }else error=GetLastError();
    return "\"callerSid\":"+r3::json(sid)+",\"tokenError\":"+std::to_string(error)+",\"elevated\":"+std::to_string(elevated)+",\"integrityRid\":"+std::to_string(integrity);
}
inline std::string launch_state(const std::filesystem::path& exe,const std::wstring& desktop,const std::wstring& command,DWORD flags) {
    FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER clock{};clock.LowPart=ft.dwLowDateTime;clock.HighPart=ft.dwHighDateTime;
    DWORD count=0;GetProcessHandleCount(GetCurrentProcess(),&count);
    const auto leaf=desktop.substr(desktop.find_last_of(L'\\')+1);
    const auto desk=OpenDesktopW(leaf.c_str(),0,FALSE,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_HOOKCONTROL|READ_CONTROL);
    const auto desk_error=desk?0:GetLastError();std::string sddl="unavailable";DWORD security_error=0;
    if(desk){SECURITY_INFORMATION requested=DACL_SECURITY_INFORMATION;DWORD n=0;GetUserObjectSecurity(desk,&requested,nullptr,0,&n);std::vector<BYTE> bytes(n);
        if(GetUserObjectSecurity(desk,&requested,bytes.data(),n,&n)){LPWSTR text=nullptr;if(ConvertSecurityDescriptorToStringSecurityDescriptorW(bytes.data(),SDDL_REVISION_1,requested,&text,nullptr)){sddl=r3::utf8(text);LocalFree(text);}else security_error=GetLastError();}else security_error=GetLastError();CloseDesktop(desk);}
    const auto file=CreateFileW(exe.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const auto file_error=file==INVALID_HANDLE_VALUE?GetLastError():0;if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    const auto executable=CreateFileW(exe.c_str(),GENERIC_EXECUTE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const auto execute_error=executable==INVALID_HANDLE_VALUE?GetLastError():0;if(executable!=INVALID_HANDLE_VALUE)CloseHandle(executable);
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY child{};const auto child_ok=GetProcessMitigationPolicy(GetCurrentProcess(),ProcessChildProcessPolicy,&child,sizeof(child));const auto child_error=child_ok?0:GetLastError();
    BOOL in_job=FALSE;IsProcessInJob(GetCurrentProcess(),nullptr,&in_job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};DWORD job_error=0;
    if(in_job){if(!QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&limits,sizeof(limits),nullptr))job_error=GetLastError();QueryInformationJobObject(nullptr,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr);QueryInformationJobObject(nullptr,JobObjectBasicUIRestrictions,&ui,sizeof(ui),nullptr);}
    MEMORYSTATUSEX memory{sizeof(memory)};GlobalMemoryStatusEx(&memory);
    return "\"exe\":"+r3::json(exe.c_str())+",\"command\":"+r3::json(command.c_str())+",\"workingDirectory\":"+r3::json(exe.parent_path().c_str())+",\"desktop\":"+r3::json(desktop.c_str())+",\"creationFlags\":"+std::to_string(flags)+",\"inheritHandles\":false,\"utcFileTime\":"+std::to_string(clock.QuadPart)+",\"callerPid\":"+std::to_string(GetCurrentProcessId())+",\"callerTid\":"+std::to_string(GetCurrentThreadId())+","+identity()+",\"handleCount\":"+std::to_string(count)+",\"gdiObjects\":"+std::to_string(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))+",\"userObjects\":"+std::to_string(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS))+",\"desktopOpenError\":"+std::to_string(desk_error)+",\"desktopDacl\":"+r3::json(sddl)+",\"desktopDaclError\":"+std::to_string(security_error)+",\"exeReadError\":"+std::to_string(file_error)+",\"exeExecuteError\":"+std::to_string(execute_error)+",\"childProcessPolicy\":"+std::to_string(child.Flags)+",\"childPolicyError\":"+std::to_string(child_error)+",\"inJob\":"+std::to_string(in_job)+",\"jobQueryError\":"+std::to_string(job_error)+",\"jobLimitFlags\":"+std::to_string(limits.BasicLimitInformation.LimitFlags)+",\"jobProcessLimit\":"+std::to_string(limits.BasicLimitInformation.ActiveProcessLimit)+",\"jobActiveProcesses\":"+std::to_string(accounting.ActiveProcesses)+",\"jobUiRestrictions\":"+std::to_string(ui.UIRestrictionsClass)+",\"availablePhysicalBytes\":"+std::to_string(memory.ullAvailPhys);
}
}
