#include "protocol.hpp"
#include <bcrypt.h>
#include <charconv>

namespace r305 {
namespace {
std::string number(std::uint64_t value){
    char buffer[32]{};const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value);
    demand(result.ec==std::errc{},"numeric document format");return {buffer,result.ptr};
}
struct File {
    FileRequest& request;HANDLE value=INVALID_HANDLE_VALUE;
    File(FileRequest& r,const wchar_t* path,DWORD access,DWORD disposition):request(r){
        value=CreateFileW(path,access,FILE_SHARE_READ,nullptr,disposition,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(value!=INVALID_HANDLE_VALUE,"CreateFileW.application");++request.live_handles;
    }
    void close(){check(CloseHandle(value),"CloseHandle.application");value=INVALID_HANDLE_VALUE;--request.live_handles;++request.closes;}
    ~File(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);--request.live_handles;}}
};
void write(FileRequest& r,const wchar_t* path,const std::string& bytes,bool append=false){
    File file(r,path,GENERIC_WRITE,append?OPEN_EXISTING:CREATE_ALWAYS);
    if(append){LARGE_INTEGER zero{};check(SetFilePointerEx(file.value,zero,nullptr,FILE_END),"SetFilePointerEx.application_append");}
    DWORD count=0;check(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr),"WriteFile.application");
    demand(count==bytes.size(),"complete native application write");++r.writes;
    check(FlushFileBuffers(file.value),"FlushFileBuffers.application");++r.flushes;file.close();
}
std::string read(FileRequest& r,const wchar_t* path,unsigned index){
    File file(r,path,GENERIC_READ,OPEN_EXISTING);LARGE_INTEGER size{};
    check(GetFileSizeEx(file.value,&size),"GetFileSizeEx.application");demand(size.QuadPart>0&&size.QuadPart<2048,"application document size");
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD count=0;
    check(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr),"ReadFile.application");
    demand(count==bytes.size(),"complete native application read");++r.reads;
    r.read_size[index]=count;memcpy(r.read_bytes[index],bytes.data(),count);file.close();return bytes;
}
std::string hash(const std::string& bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE value=nullptr;
    auto okay=[](NTSTATUS status,const char* api){if(status<0)throw Error(api,static_cast<DWORD>(status));};
    okay(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0),"BCryptOpenAlgorithmProvider.application");
    struct Release{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Release(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} release{algorithm,value};
    DWORD length=0,count=0;okay(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&count,0),"BCryptGetProperty.application");
    std::vector<BYTE> object(length);BYTE digest[32]{};
    okay(BCryptCreateHash(algorithm,&value,object.data(),length,nullptr,0,0),"BCryptCreateHash.application");
    okay(BCryptHashData(value,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0),"BCryptHashData.application");
    okay(BCryptFinishHash(value,digest,32,0),"BCryptFinishHash.application");
    const char digits[]="0123456789abcdef";std::string result;for(auto b:digest){result+=digits[b>>4];result+=digits[b&15];}return result;
}
}
std::string file_effect_json(const FileRequest& r){
    return "{\"action\":"+number(r.action)+",\"operation\":"+number(r.operation)+",\"pid\":"+number(r.pid)+",\"tid\":"+number(r.tid)+",\"error\":"+number(r.error)+",\"api\":"+json(r.api)+",\"liveHandles\":"+number(r.live_handles)+",\"reads\":"+number(r.reads)+",\"writes\":"+number(r.writes)+",\"flushes\":"+number(r.flushes)+",\"closes\":"+number(r.closes)+",\"readA\":"+json(r.read_bytes[0])+",\"readB\":"+json(r.read_bytes[1])+",\"written\":"+json(r.written_bytes)+",\"writtenSize\":"+number(r.written_size)+",\"sha256\":"+json(r.sha256)+"}";
}
void application_files(Metadata& m,Work& work){
    auto& r=m.file;if(!r.action||r.done==r.action)return;
    r.pid=GetCurrentProcessId();r.tid=GetCurrentThreadId();
    try{
        std::string output;
        if(r.operation==save_document){
            wchar_t text[128]{};GetWindowTextW(work.edit,text,128);
            output="focus-views document\nproducer="+number(r.pid)+"\naction="+number(r.action)+"\nseed="+number(work.seed)+"\nreceipt="+number(work.receipt)+"\ntext="+utf8(text)+"\n";
        }else if(r.operation==process_documents){
            const auto a=read(r,r.path[0],0),b=read(r,r.path[1],1);
            const auto marker="C-private-edit producer="+number(r.pid)+" action="+number(r.action)+"\n";
            // Normal application editing of its own imported document copies.
            write(r,r.path[0],marker,true);write(r,r.path[1],marker,true);
            output="processed-native-documents\nlocal-seed="+number(work.seed)+"\n"+a+"---\n"+b;
            work.document_reads+=2;work.processed_bytes+=a.size()+b.size();
        }else throw Error("application.file_operation",ERROR_INVALID_PARAMETER);
        demand(output.size()<sizeof(r.written_bytes),"application output bound");
        write(r,r.path[2],output);++work.document_writes;
        memcpy(r.written_bytes,output.data(),output.size());r.written_size=static_cast<unsigned>(output.size());
        strcpy_s(r.sha256,hash(output).c_str());strcpy_s(r.api,"CreateFileW/ReadFile/WriteFile/FlushFileBuffers/CloseHandle");
    }catch(const Error& e){r.error=e.code;strncpy_s(r.api,e.api.c_str(),_TRUNCATE);}
    const auto record=file_effect_json(r)+"\n";
    Handle evidence(CreateFileW(r.evidence,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(evidence.value==INVALID_HANDLE_VALUE){r.error=GetLastError();strcpy_s(r.api,"CreateFileW.application_evidence");}
    else{DWORD count=0;if(!WriteFile(evidence.value,record.data(),static_cast<DWORD>(record.size()),&count,nullptr)||count!=record.size()||!FlushFileBuffers(evidence.value)){r.error=GetLastError();strcpy_s(r.api,"WriteFile.application_evidence");}}
    MemoryBarrier();InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&r.done),static_cast<LONG64>(r.action));
}
}
