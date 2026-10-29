#pragma once
#include "../r3/protocol.hpp"
#include "../r5-001a/runtime/native.hpp"
#include <array>
namespace fv_live {
using namespace r3;
constexpr std::uint64_t feed_magic=0x314556494C564646;
constexpr DWORD fresh_ms=3000;
struct Frame {
    std::uint64_t sequence=0,created=0,begin=0,end=0,counter=0;
    DWORD pid=0,tid=0;char view[8]{},sha256[65]{};wchar_t path[MAX_PATH]{};
};
struct Feed {
    std::uint64_t magic=feed_magic,manager_created=0,heartbeat=0;
    DWORD manager_pid=0;volatile LONG epoch=0;LONG valid=1;
    Frame frames[3]{};
};
struct Control {
    volatile LONG ready=0,request=0,applied=0,operation=0,argument=0;
    volatile LONG selected=0,closed=0,error=0;
    DWORD pid=0;std::uint64_t created=0,window=0;
    std::uint64_t displayed[3]{},last_display_tick=0;
    char displayed_sha[3][65]{};
    LONG read_only=0,invalid_frames=0,painted=0;
    volatile LONG host_epoch=0;
    LONG characters=0,keys=0,moves=0,clicks=0,matched=0,confirmed=0,no_interference=0;
    std::uint64_t started=0,confirmed_tick=0;
    wchar_t challenge[48]{};
};
inline std::uint64_t creation(HANDLE p){FILETIME a{},b{},c{},d{};check(GetProcessTimes(p,&a,&b,&c,&d),"GetProcessTimes.live");ULARGE_INTEGER u{};u.LowPart=a.dwLowDateTime;u.HighPart=a.dwHighDateTime;return u.QuadPart;}
inline bool read_feed(const Feed& shared,Feed& out){
    for(unsigned i=0;i<16;++i){const auto e=shared.epoch;if(e&1){SwitchToThread();continue;}MemoryBarrier();memcpy(&out,&shared,sizeof(out));MemoryBarrier();if(e==shared.epoch)return out.magic==feed_magic;}return false;
}
class ReadOnlyFeed {
    Handle file_;const Feed* view_=nullptr;
public:
    explicit ReadOnlyFeed(const std::wstring& name){file_.value=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());check(file_.value!=nullptr,"OpenFileMappingW.preview_read_only");view_=static_cast<const Feed*>(MapViewOfFile(file_.value,FILE_MAP_READ,0,0,sizeof(Feed)));check(view_!=nullptr,"MapViewOfFile.preview_read_only");}
    ~ReadOnlyFeed(){if(view_)UnmapViewOfFile(view_);}
    const Feed& get()const{return *view_;}
    bool read_only()const{MEMORY_BASIC_INFORMATION m{};check(VirtualQuery(view_,&m,sizeof(m))==sizeof(m),"VirtualQuery.preview_feed");return (m.Protect&0xff)==PAGE_READONLY;}
};
inline std::string frame_json(const Frame& f){return "\"view\":"+json(f.view)+",\"pid\":"+std::to_string(f.pid)+",\"created\":"+std::to_string(f.created)+",\"tid\":"+std::to_string(f.tid)+",\"sequence\":"+std::to_string(f.sequence)+",\"beginMs\":"+std::to_string(f.begin)+",\"endMs\":"+std::to_string(f.end)+",\"counter\":"+std::to_string(f.counter)+",\"sha256\":"+json(f.sha256)+",\"file\":"+json(f.path);}
}
