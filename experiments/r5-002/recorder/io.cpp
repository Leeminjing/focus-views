#include "contract.hpp"
#include "../../r5-001a/runtime/native.hpp"
namespace recorder {
using namespace r3;
namespace {
struct File {
    r305::ClientCommand& command;HANDLE handle=INVALID_HANDLE_VALUE;
    File(r305::ClientCommand& c,const wchar_t* path,DWORD access,DWORD mode):command(c){handle=CreateFileW(path,access,FILE_SHARE_READ,nullptr,mode,FILE_ATTRIBUTE_NORMAL,nullptr);check(handle!=INVALID_HANDLE_VALUE,"CreateFileW.recorder");++command.live_handles;}
    ~File(){if(handle!=INVALID_HANDLE_VALUE){CloseHandle(handle);--command.live_handles;}}
    void close(){check(CloseHandle(handle),"CloseHandle.recorder");handle=INVALID_HANDLE_VALUE;--command.live_handles;++command.closes;}
};
void write(r305::ClientCommand& c,const wchar_t* path,const std::string& body,bool append=false){File f(c,path,GENERIC_WRITE,append?OPEN_EXISTING:CREATE_ALWAYS);if(append){LARGE_INTEGER zero{};check(SetFilePointerEx(f.handle,zero,nullptr,FILE_END),"SetFilePointerEx.recorder");}DWORD n=0;check(WriteFile(f.handle,body.data(),static_cast<DWORD>(body.size()),&n,nullptr),"WriteFile.recorder");demand(n==body.size(),"recorder full output");++c.writes;check(FlushFileBuffers(f.handle),"FlushFileBuffers.recorder");++c.flushes;f.close();}
std::string read(r305::ClientCommand& c,const wchar_t* path,unsigned index){File f(c,path,GENERIC_READ,OPEN_EXISTING);LARGE_INTEGER size{};check(GetFileSizeEx(f.handle,&size),"GetFileSizeEx.recorder");demand(size.QuadPart>0&&size.QuadPart<2048,"bounded recorder import");std::string s(static_cast<size_t>(size.QuadPart),0);DWORD n=0;check(ReadFile(f.handle,s.data(),static_cast<DWORD>(s.size()),&n,nullptr),"ReadFile.recorder");demand(n==s.size(),"recorder full import");++c.reads;c.read_size[index]=n;memcpy(c.read_bytes[index],s.data(),n);f.close();return s;}
}
std::string state_json(const State& s){std::string rows="[";for(unsigned i=0;i<8;++i){if(i)rows+=',';rows+="{\"tick\":"+std::to_string(s.samples[i].tick)+",\"value\":"+std::to_string(s.samples[i].value)+",\"quality\":"+std::to_string(s.samples[i].quality)+"}";}return "{\"schema\":2,\"descriptor\":"+std::to_string(s.descriptor)+",\"runId\":"+std::to_string(s.run_id)+",\"note\":"+json(s.note)+",\"ticks\":"+std::to_string(s.ticks)+",\"outcome\":"+std::to_string(s.outcome)+",\"returnMask\":"+std::to_string(s.return_mask)+",\"samples\":"+rows+"]}";}
void export_or_process(r305::ClientCommand& c,State& s){
    if(!c.action||c.done==c.action)return;c.pid=GetCurrentProcessId();c.tid=GetCurrentThreadId();
    try{
        std::string body;
        if(c.operation==r305::client_save){wchar_t label[128]{};GetWindowTextW(s.editor,label,128);body="RECORDER/2\nrun,"+std::to_string(s.run_id)+"\nlabel,"+utf8(label)+"\ntick,value,quality\n";for(const auto& sample:s.samples)body+=std::to_string(sample.tick)+","+std::to_string(sample.value)+","+std::to_string(sample.quality)+"\n";}
        else if(c.operation==r305::client_process){const auto a=read(c,c.path[0],0),b=read(c,c.path[1],1);body="RECORDER-COMBINE/2\nlocal-run,"+std::to_string(s.run_id)+"\n"+a+"--second-series--\n"+b;const auto marker="C-private-edit recorder="+std::to_string(c.pid)+"\n";write(c,c.path[0],marker,true);write(c,c.path[1],marker,true);s.imports+=2;s.processed+=a.size()+b.size();}
        else throw Error("recorder.command",ERROR_NOT_SUPPORTED);
        demand(body.size()<sizeof(c.written_bytes),"bounded recorder result");write(c,c.path[2],body);++s.exports;memcpy(c.written_bytes,body.data(),body.size());c.written_size=static_cast<unsigned>(body.size());std::vector<BYTE> bytes(body.begin(),body.end());strcpy_s(c.sha256,native::digest_text(native::digest(bytes)).c_str());strcpy_s(c.api,"recorder/native-sync-file-operation");
    }catch(const Error& e){c.error=e.code;strncpy_s(c.api,e.api.c_str(),_TRUNCATE);}
    const auto line=r305::file_effect_json(c)+"\n";Handle log(CreateFileW(c.evidence,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));if(log.value==INVALID_HANDLE_VALUE){c.error=GetLastError();strcpy_s(c.api,"CreateFileW.recorder_evidence");}else{DWORD n=0;if(!WriteFile(log.value,line.data(),static_cast<DWORD>(line.size()),&n,nullptr)||n!=line.size()||!FlushFileBuffers(log.value)){c.error=GetLastError();strcpy_s(c.api,"WriteFile.recorder_evidence");}}
    MemoryBarrier();InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&c.done),static_cast<LONG64>(c.action));
}
}
