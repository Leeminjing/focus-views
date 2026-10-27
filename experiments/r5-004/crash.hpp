#pragma once
#include "../r5-003/runtime/store.hpp"
#include "../r5-002/runtime/experiment-points.hpp"
#include <set>
namespace r5004 {
using namespace r305;
constexpr DWORD crash_exit=0xE5040001;
constexpr auto certificate_kind=static_cast<r5003::Kind>(3);
struct Identity {DWORD pid=0;std::uint64_t created=0;};
inline std::uint64_t creation(HANDLE p){FILETIME c{},e{},k{},u{};check(GetProcessTimes(p,&c,&e,&k,&u),"GetProcessTimes.crash_identity");return (std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
inline bool gone(const Identity& i){Handle h(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,i.pid));if(!h.value){const auto e=GetLastError();if(e==ERROR_INVALID_PARAMETER)return true;throw Error("OpenProcess.crash_identity",e);}return creation(h.value)!=i.created||WaitForSingleObject(h.value,0)==WAIT_OBJECT_0;}
struct Certificate {Identity manager;std::vector<Identity> processes;std::vector<std::string> desktops;std::string point;DWORD manager_exit=0;bool forced=false,rescue=false;};
inline r5003::Bytes encode(const Certificate& c){r5003::Writer w;w.pod(1u);w.pod(r5003::environment().session);w.pod(c.manager.pid);w.pod(c.manager.created);w.pod(c.manager_exit);w.pod(unsigned(c.forced));w.pod(unsigned(c.rescue));w.text(c.point);w.pod(unsigned(c.processes.size()));for(const auto& p:c.processes){w.pod(p.pid);w.pod(p.created);}w.pod(unsigned(c.desktops.size()));for(const auto& d:c.desktops)w.text(d);return w.bytes;}
inline Certificate decode(const r5003::Bytes& bytes){r5003::Reader r{bytes};if(r.pod<unsigned>()!=1||r.pod<DWORD>()!=r5003::environment().session)throw Error("recovery.certificate_version",ERROR_REVISION_MISMATCH);Certificate c;c.manager.pid=r.pod<DWORD>();c.manager.created=r.pod<std::uint64_t>();c.manager_exit=r.pod<DWORD>();c.forced=r.pod<unsigned>()!=0;c.rescue=r.pod<unsigned>()!=0;c.point=r.text(128);auto count=r.pod<unsigned>();if(!count||count>128)throw Error("recovery.process_bounds",ERROR_INVALID_DATA);for(unsigned n=0;n<count;++n){Identity i;i.pid=r.pod<DWORD>();i.created=r.pod<std::uint64_t>();if(!i.pid||!i.created)throw Error("recovery.process_identity",ERROR_INVALID_DATA);c.processes.push_back(i);}count=r.pod<unsigned>();if(count>16)throw Error("recovery.desktop_bounds",ERROR_INVALID_DATA);for(unsigned n=0;n<count;++n)c.desktops.push_back(r.text(256));r.end();if(!c.manager.pid||!c.manager.created)throw Error("recovery.manager_identity",ERROR_INVALID_DATA);return c;}
inline bool desktop_exists(const std::string& full){
    const auto station=r3::utf8(user_object_name(GetProcessWindowStation()));const auto prefix=station+"\\";
    if(!full.starts_with(prefix)||full.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_\\")!=std::string::npos||full.find("\\",prefix.size())!=std::string::npos)throw Error("recovery.desktop_identity",ERROR_INVALID_DATA);
    const auto name=full.substr(prefix.size());const std::wstring wide(name.begin(),name.end());SetLastError(0);auto h=OpenDesktopW(wide.c_str(),0,FALSE,DESKTOP_READOBJECTS);
    if(h){check(CloseDesktop(h),"CloseDesktop.crash_probe");return true;}const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_INVALID_NAME)return false;throw Error("OpenDesktopW.crash_probe",error);
}
inline void verify_previous(const std::filesystem::path& path,Log& log){const auto c=decode(r5003::payload(path,certificate_kind));if(!gone(c.manager))throw Error("recovery.previous_manager_alive",ERROR_BUSY);for(const auto& i:c.processes)if(!gone(i))throw Error("recovery.previous_instance_alive",ERROR_BUSY);for(const auto& d:c.desktops)if(desktop_exists(d))throw Error("recovery.desktop_still_present",ERROR_BUSY);log.emit("predecessor_verified","\"managerPid\":"+std::to_string(c.manager.pid)+",\"managerCreated\":"+std::to_string(c.manager.created)+",\"instances\":"+std::to_string(c.processes.size())+",\"desktops\":"+std::to_string(c.desktops.size())+",\"allExited\":true,\"allDesktopsAbsent\":true");}
constexpr std::uint64_t gate_magic=0x3154455441475646;
struct GateBlock {std::uint64_t magic=gate_magic;DWORD supervisor=0,manager=0;std::uint64_t supervisor_created=0;volatile LONG ready=0;DWORD thread=0;char wanted[128]{},reached[128]{};};
class Gate {
    Mapping mapping_;GateBlock& block_;Handle ready_,release_,supervisor_;Log log_;
public:
    Gate(const std::wstring& name,const std::filesystem::path& out):mapping_(name,sizeof(GateBlock),false),block_(*static_cast<GateBlock*>(mapping_.view)),ready_(OpenEventW(EVENT_MODIFY_STATE,FALSE,(name+L"_ready").c_str())),release_(OpenEventW(SYNCHRONIZE,FALSE,(name+L"_release").c_str())),supervisor_(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,block_.supervisor)),log_(out/"fault-points.jsonl"){
        check(ready_.value&&release_.value&&supervisor_.value,"OpenHandle.crash_gate");if(block_.magic!=gate_magic||block_.manager!=GetCurrentProcessId()||creation(supervisor_.value)!=block_.supervisor_created||!memchr(block_.wanted,0,sizeof(block_.wanted)))throw Error("crash_gate.identity",ERROR_INVALID_OWNER);
        experimental::observer_state=this;experimental::observer=[](void* self,const char* phase,const std::filesystem::path& identity){static_cast<Gate*>(self)->point(phase,identity);};
    }
    ~Gate(){experimental::observer=nullptr;experimental::observer_state=nullptr;}
    void point(const char* phase,const std::filesystem::path& identity){const auto name=identity.filename().string();const std::string point=std::string(phase)+(name.empty()?"":":"+name);if(point!=block_.wanted)return;
        if(block_.ready||point.size()>=sizeof(block_.reached))throw Error("crash_gate.duplicate_point",ERROR_INVALID_STATE);
        strcpy_s(block_.reached,point.c_str());block_.thread=GetCurrentThreadId();log_.emit("synchronous_crash_point","\"point\":"+json(point)+",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"thread\":"+std::to_string(block_.thread));MemoryBarrier();InterlockedExchange(&block_.ready,1);check(SetEvent(ready_.value),"SetEvent.crash_ready");
        const HANDLE waits[]={release_.value,supervisor_.value};const auto result=WaitForMultipleObjects(2,waits,FALSE,30000);if(result!=WAIT_OBJECT_0)throw Error("crash_gate.supervisor_or_timeout",result==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_BROKEN_PIPE);
    }
};
}
