#pragma once
#include "../r3/protocol.hpp"
// Same bounded request protocol used by R4-001, only for legacy experiment IPC.
namespace gate001 {
struct Courier {
    volatile LONG ready=0,request=0,done=0,stop=0;
    unsigned capture=0;UINT message=0;WPARAM wp=0;
    wchar_t reply[512]{},path[512]{};DWORD error=0;
};
inline int courier_main(const r3::Args& args) {
    r3::Mapping mapping(args.get(L"--courier"),sizeof(Courier),false);
    auto& c=*static_cast<Courier*>(mapping.view);
    r3::Handle target(OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(args.number(L"--pid"))));r3::check(target.value!=nullptr,"OpenProcess.courier_target");
    InterlockedExchange(&c.ready,1);LONG last=0;
    for(;;){
        if(c.stop || WaitForSingleObject(target.value,0)==WAIT_OBJECT_0)return 0;
        const auto request=InterlockedCompareExchange(&c.request,0,0);
        if(request==last){Sleep(1);continue;}
        r3::demand(request==last+1,"courier command order");MemoryBarrier();
        std::vector<std::wstring> words{L"courier",L"--state",args.get(L"--state"),L"--reply",c.reply,L"--op",c.capture?L"capture":L"post",L"--target",L"top",L"--message",std::to_wstring(c.message),L"--wparam",std::to_wstring(c.wp),L"--path",c.path};
        std::vector<wchar_t*> pointers;for(auto& word:words)pointers.push_back(word.data());
        const r3::Args command(static_cast<int>(pointers.size()),pointers.data());
        c.error=static_cast<DWORD>(r3::bridge_main(command));last=request;MemoryBarrier();InterlockedExchange(&c.done,request);
    }
}
}
