#pragma once
// Opt-in observations at the real legacy input call site. No preload/retry,
// search-path change, hook substitution, or channel/ownership mutation.
#include "../r3/protocol.hpp"
#include <bcrypt.h>
#include <array>
#pragma comment(lib,"bcrypt.lib")
namespace hook_install_diagnostic {
using namespace r3;
inline std::string disk_sha256(const std::filesystem::path& path){
    BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;
    struct Release{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Release(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} release{a,h};
    auto status=[](NTSTATUS s,const char* api){if(s<0)throw Error(api,static_cast<DWORD>(s));};
    status(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0),"BCryptOpenAlgorithmProvider.install_diagnostic");status(BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0),"BCryptCreateHash.install_diagnostic");
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.install_diagnostic_hash");std::array<BYTE,65536> bytes{};DWORD count=0;
    do{check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr),"ReadFile.install_diagnostic_hash");if(count)status(BCryptHashData(h,bytes.data(),count,0),"BCryptHashData.install_diagnostic");}while(count);
    std::array<BYTE,32> digest{};status(BCryptFinishHash(h,digest.data(),32,0),"BCryptFinishHash.install_diagnostic");std::string result;for(auto b:digest){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
}
inline std::wstring module_path(HMODULE m){wchar_t path[32768]{};const auto n=GetModuleFileNameW(m,path,32768);check(n&&n<32768,"GetModuleFileNameW.install_diagnostic");return path;}
inline std::uint64_t creation(HANDLE h,bool thread){FILETIME c{},e{},k{},u{};check(thread?GetThreadTimes(h,&c,&e,&k,&u):GetProcessTimes(h,&c,&e,&k,&u),thread?"GetThreadTimes.install_diagnostic":"GetProcessTimes.install_diagnostic");return(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
class Recorder{
    std::ofstream& log_;
    void write(const char* event,const std::string& fields){log_<<"{\"type\":\"input_install_diagnostic\",\"stage\":\"install_diag\",\"event\":"<<json(event)<<",\"diagnosticVersion\":1,\"tickMs\":"<<GetTickCount64()<<",\"pid\":"<<GetCurrentProcessId()<<",\"tid\":"<<GetCurrentThreadId()<<','<<fields<<"}\n";log_.flush();}
    template<class F>void sample(F f) noexcept{const auto saved=GetLastError();try{f();}catch(const Error& e){try{write("diagnostic_failure","\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code));}catch(...){}}catch(...){try{write("diagnostic_failure","\"api\":\"diagnostic.exception\",\"error\":"+std::to_string(ERROR_UNHANDLED_EXCEPTION));}catch(...){}}SetLastError(saved);}
public:
    explicit Recorder(std::ofstream& log):log_(log){}
    void context(DWORD pid,DWORD tid,const std::wstring& argument){sample([&]{write("dll_argument","\"path\":"+json(argument.c_str())+",\"targetPid\":"+std::to_string(pid)+",\"targetTid\":"+std::to_string(tid));});
        sample([&]{const auto exe=module_path(nullptr);write("helper_identity","\"path\":"+json(exe.c_str())+",\"diskSha256\":"+json(disk_sha256(exe))+",\"created\":"+std::to_string(creation(GetCurrentProcess(),false))+",\"threadCreated\":"+std::to_string(creation(GetCurrentThread(),true))+",\"desktop\":"+json(thread_desktop().c_str()));});
        sample([&]{Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));check(p.value!=nullptr,"OpenProcess.install_diagnostic");Handle t(OpenThread(THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,tid));check(t.value!=nullptr,"OpenThread.install_diagnostic");const auto pw=WaitForSingleObject(p.value,0),tw=WaitForSingleObject(t.value,0);write("target_identity","\"targetPid\":"+std::to_string(pid)+",\"targetTid\":"+std::to_string(tid)+",\"threadOwner\":"+std::to_string(GetProcessIdOfThread(t.value))+",\"created\":"+std::to_string(creation(p.value,false))+",\"threadCreated\":"+std::to_string(creation(t.value,true))+",\"processWait\":"+std::to_string(pw)+",\"threadWait\":"+std::to_string(tw)+",\"desktop\":"+json(user_object_name(GetThreadDesktop(tid)).c_str()));});
        sample([&]{write("argument_file","\"path\":"+json(argument.c_str())+",\"diskSha256\":"+json(disk_sha256(argument)));});
    }
    void api(const char* name,std::uint64_t result,DWORD error){sample([&]{write("api","\"api\":"+json(name)+",\"result\":"+std::to_string(result)+",\"error\":"+std::to_string(error));});}
    void loaded(HMODULE module){if(module)sample([&]{const auto path=module_path(module);write("loaded_module","\"module\":"+std::to_string(reinterpret_cast<std::uint64_t>(module))+",\"path\":"+json(path.c_str())+",\"diskSha256\":"+json(disk_sha256(path)));});}
    void exported(HMODULE module,HOOKPROC proc,const char* name){if(proc)sample([&]{HMODULE owner=nullptr;check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(proc),&owner),"GetModuleHandleExW.install_diagnostic");const auto path=module_path(owner);write("export_owner","\"export\":"+json(name)+",\"address\":"+std::to_string(reinterpret_cast<std::uint64_t>(proc))+",\"owner\":"+std::to_string(reinterpret_cast<std::uint64_t>(owner))+",\"passedModule\":"+std::to_string(reinterpret_cast<std::uint64_t>(module))+",\"sameModule\":"+(owner==module?"true":"false")+",\"path\":"+json(path.c_str()));});}
};
}
