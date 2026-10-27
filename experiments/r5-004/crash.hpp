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
struct Certificate {Identity manager;std::vector<Identity> processes;std::vector<std::string> desktops;std::string point;DWORD manager_exit=0;bool forced=false,rescue=false;std::string session_hash,owner_hash;};
inline r5003::Bytes encode(const Certificate& c){r5003::Writer w;w.pod(2u);w.pod(r5003::environment().session);w.pod(c.manager.pid);w.pod(c.manager.created);w.pod(c.manager_exit);w.pod(unsigned(c.forced));w.pod(unsigned(c.rescue));w.text(c.point);w.text(c.session_hash);w.text(c.owner_hash);w.pod(unsigned(c.processes.size()));for(const auto& p:c.processes){w.pod(p.pid);w.pod(p.created);}w.pod(unsigned(c.desktops.size()));for(const auto& d:c.desktops)w.text(d);return w.bytes;}
inline Certificate decode(const r5003::Bytes& bytes){r5003::Reader r{bytes};if(r.pod<unsigned>()!=2||r.pod<DWORD>()!=r5003::environment().session)throw Error("recovery.certificate_version",ERROR_REVISION_MISMATCH);Certificate c;c.manager.pid=r.pod<DWORD>();c.manager.created=r.pod<std::uint64_t>();c.manager_exit=r.pod<DWORD>();const auto forced=r.pod<unsigned>(),rescue=r.pod<unsigned>();if(forced>1||rescue>1)throw Error("recovery.certificate_boolean",ERROR_INVALID_DATA);c.forced=forced!=0;c.rescue=rescue!=0;c.point=r.text(128);c.session_hash=r.text(64);c.owner_hash=r.text(64);auto count=r.pod<unsigned>();if(!count||count>128)throw Error("recovery.process_bounds",ERROR_INVALID_DATA);for(unsigned n=0;n<count;++n){Identity i;i.pid=r.pod<DWORD>();i.created=r.pod<std::uint64_t>();if(!i.pid||!i.created)throw Error("recovery.process_identity",ERROR_INVALID_DATA);c.processes.push_back(i);}count=r.pod<unsigned>();if(count>16)throw Error("recovery.desktop_bounds",ERROR_INVALID_DATA);for(unsigned n=0;n<count;++n)c.desktops.push_back(r.text(256));r.end();if(!c.manager.pid||!c.manager.created)throw Error("recovery.manager_identity",ERROR_INVALID_DATA);std::set<std::pair<DWORD,std::uint64_t>> unique;bool manager_found=false;for(const auto& p:c.processes){if(!unique.emplace(p.pid,p.created).second)throw Error("recovery.duplicate_instance",ERROR_INVALID_DATA);manager_found|=p.pid==c.manager.pid&&p.created==c.manager.created;}if(!manager_found||c.owner_hash.size()!=64||(c.forced&&(c.manager_exit!=crash_exit||c.point.empty())))throw Error("recovery.certificate_consistency",ERROR_INVALID_DATA);return c;}
inline bool desktop_exists(const std::string& full){
    const auto station=r3::utf8(user_object_name(GetProcessWindowStation()));const auto prefix=station+"\\";
    if(!full.starts_with(prefix)||full.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_\\")!=std::string::npos||full.find("\\",prefix.size())!=std::string::npos)throw Error("recovery.desktop_identity",ERROR_INVALID_DATA);
    struct Search {std::wstring name;bool found=false;};
    const auto name=full.substr(prefix.size());Search search{std::wstring(name.begin(),name.end()),false};
    check(EnumDesktopsW(GetProcessWindowStation(),[](LPWSTR value,LPARAM state)->BOOL{auto& x=*reinterpret_cast<Search*>(state);if(x.name==value)x.found=true;return TRUE;},reinterpret_cast<LPARAM>(&search)),"EnumDesktopsW.crash_probe");
    return search.found;
}
inline std::filesystem::path owners(const std::filesystem::path& store){return std::filesystem::path(store.wstring()+L".owners");}
inline std::filesystem::path owner_path(const std::filesystem::path& store,const Identity& id){return owners(store)/(std::to_wstring(id.pid)+L"-"+std::to_wstring(id.created)+L".owner");}
inline r5003::Bytes owner_bytes(const Identity& id){r5003::Writer w;w.pod(id.pid);w.pod(id.created);w.pod(r5003::environment().session);return w.bytes;}
inline void verify_exited(const Certificate& c){for(const auto& d:c.desktops)if(desktop_exists(d))throw Error("recovery.desktop_still_present",ERROR_BUSY);if(!gone(c.manager))throw Error("recovery.previous_manager_alive",ERROR_BUSY);for(const auto& i:c.processes)if(!gone(i))throw Error("recovery.previous_instance_alive",ERROR_BUSY);}
inline void verify_previous(const std::filesystem::path& path,const std::filesystem::path& store,Log& log){
    const auto packet=r5003::read(path);const auto c=decode(r5003::unpack(certificate_kind,packet));verify_exited(c);
    const auto session=store/"session.fvs";if(std::filesystem::exists(session)&&r5003::hash(r5003::read(session))!=c.session_hash)throw Error("recovery.owner_session_mismatch",ERROR_INVALID_DATA);
    const Identity self{GetCurrentProcessId(),creation(GetCurrentProcess())};const auto current=owner_path(store,self);if(!std::filesystem::exists(current))throw Error("recovery.current_owner_missing",ERROR_INVALID_OWNER);
    std::uint64_t latest=0;std::string expected;
    for(const auto& entry:std::filesystem::directory_iterator(owners(store))){if(entry.path().extension()!=L".owner"||entry.path()==current)continue;
        const auto record=r5003::read(entry.path());const auto bytes=r5003::unpack(static_cast<r5003::Kind>(4),record);r5003::Reader r{bytes};Identity id;id.pid=r.pod<DWORD>();id.created=r.pod<std::uint64_t>();if(r.pod<DWORD>()!=r5003::environment().session)throw Error("recovery.owner_session",ERROR_INVALID_DATA);r.end();
        auto done=entry.path();done+=L".done";if(!std::filesystem::exists(done))throw Error("recovery.owner_cleanup_unconfirmed",ERROR_BUSY);const auto completed=r5003::read(done);const auto previous=decode(r5003::unpack(certificate_kind,completed));
        if(previous.manager.pid!=id.pid||previous.manager.created!=id.created||previous.owner_hash!=r5003::hash(record))throw Error("recovery.owner_binding",ERROR_INVALID_DATA);verify_exited(previous);if(id.created>latest){latest=id.created;expected=r5003::hash(completed);}
    }
    if(latest&&expected!=r5003::hash(packet))throw Error("recovery.stale_owner_certificate",ERROR_INVALID_DATA);
    log.emit("predecessor_verified","\"managerPid\":"+std::to_string(c.manager.pid)+",\"managerCreated\":"+std::to_string(c.manager.created)+",\"instances\":"+std::to_string(c.processes.size())+",\"desktops\":"+std::to_string(c.desktops.size())+",\"allExited\":true,\"allDesktopsAbsent\":true,\"sessionBound\":true,\"lineageVerified\":true");
}
constexpr std::uint64_t gate_magic=0x3154455441475646;
struct GateBlock {std::uint64_t magic=gate_magic;DWORD supervisor=0,manager=0;std::uint64_t supervisor_created=0;volatile LONG ready=0;DWORD thread=0;char wanted[128]{},reached[128]{};};
class Gate {
    Mapping mapping_;GateBlock& block_;Handle ready_,release_,supervisor_;Log log_;
public:
    Gate(const std::wstring& name,const std::filesystem::path& out):mapping_(name,sizeof(GateBlock),false),block_(*static_cast<GateBlock*>(mapping_.view)),log_(out/"fault-points.jsonl"){
        ready_.value=OpenEventW(EVENT_MODIFY_STATE,FALSE,(name+L"_ready").c_str());check(ready_.value!=nullptr,"OpenEventW.crash_ready");release_.value=OpenEventW(SYNCHRONIZE,FALSE,(name+L"_release").c_str());check(release_.value!=nullptr,"OpenEventW.crash_release");supervisor_.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,block_.supervisor);check(supervisor_.value!=nullptr,"OpenProcess.crash_supervisor");if(block_.magic!=gate_magic||block_.manager!=GetCurrentProcessId()||creation(supervisor_.value)!=block_.supervisor_created||!memchr(block_.wanted,0,sizeof(block_.wanted)))throw Error("crash_gate.identity",ERROR_INVALID_OWNER);
        experimental::generation=std::to_wstring(creation(GetCurrentProcess()));
        experimental::observer_state=this;experimental::observer=[](void* self,const char* phase,const std::filesystem::path& identity){static_cast<Gate*>(self)->point(phase,identity);};
    }
    ~Gate(){experimental::generation.clear();experimental::observer=nullptr;experimental::observer_state=nullptr;}
    void point(const char* phase,const std::filesystem::path& identity){const auto name=identity.filename().string();const std::string point=std::string(phase)+(name.empty()?"":":"+name);if(point!=block_.wanted)return;
        if(block_.ready||point.size()>=sizeof(block_.reached))throw Error("crash_gate.duplicate_point",ERROR_INVALID_STATE);
        strcpy_s(block_.reached,point.c_str());block_.thread=GetCurrentThreadId();log_.emit("synchronous_crash_point","\"point\":"+json(point)+",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"thread\":"+std::to_string(block_.thread));MemoryBarrier();InterlockedExchange(&block_.ready,1);check(SetEvent(ready_.value),"SetEvent.crash_ready");
        const HANDLE waits[]={release_.value,supervisor_.value};const auto result=WaitForMultipleObjects(2,waits,FALSE,30000);if(result!=WAIT_OBJECT_0)throw Error("crash_gate.supervisor_or_timeout",result==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_BROKEN_PIPE);
    }
};
}
