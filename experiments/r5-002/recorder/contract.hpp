#pragma once
#include "../clients/command.hpp"
namespace recorder {
constexpr std::uint64_t data_base=0x000001B300000000,stack_base=data_base+0x20000;
constexpr size_t data_bytes=8192,stack_bytes=2*1024*1024;
struct Sample {std::uint64_t tick=0,value=0;DWORD quality=0,padding=0;};
struct State {
    std::uint64_t descriptor=0;
    Sample samples[8]{};
    wchar_t note[96]{};
    HWND top=nullptr,editor=nullptr,export_button=nullptr;
    std::uint64_t run_id=0,outcome=0,ticks=0,exports=0,imports=0,processed=0,clicks=0;
    unsigned note_length=0,return_mask=0;
};
struct Contract {std::uint64_t magic=0x325245444F434552ULL;DWORD version=2,state_bytes=sizeof(State),mailbox_bytes=sizeof(r305::ClientCommand),frames=4,returns=6;};
std::string state_json(const State&);
void export_or_process(r305::ClientCommand&,State&);
}
