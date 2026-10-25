#pragma once
#include "artifacts.hpp"

namespace r305 {
// Only this experiment's single, completed, closed ordinary workspace file.
// No open Win32 HANDLE, mapping, lock or arbitrary path is checkpointed.
struct FileVersion {
    bool present=false;
    DWORD pid=0;
    std::uint64_t action=0,object=0;
    DWORD volume=0;
    std::string view,sha256;
    std::vector<BYTE> content;
    std::string describe()const{
        return "{\"present\":"+std::string(present?"true":"false")+",\"logical\":\"document.txt\",\"sourceView\":"+json(view)+",\"sourcePid\":"+std::to_string(pid)+",\"sourceAction\":"+std::to_string(action)+",\"sourceVolume\":"+std::to_string(volume)+",\"sourceObject\":"+std::to_string(object)+",\"bytes\":"+std::to_string(content.size())+",\"sha256\":"+json(sha256)+"}";
    }
};
class Files {
public:
    struct Identity {DWORD volume=0;std::uint64_t object=0;};
    static Identity identity(HANDLE file){BY_HANDLE_FILE_INFORMATION n{};check(GetFileInformationByHandle(file,&n),"GetFileInformationByHandle.workspace");demand(GetFileType(file)==FILE_TYPE_DISK&&!(n.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&n.nNumberOfLinks==1,"ordinary single-link workspace file");return {n.dwVolumeSerialNumber,(static_cast<std::uint64_t>(n.nFileIndexHigh)<<32)|n.nFileIndexLow};}
    static Identity identity(const std::filesystem::path& p){Handle f(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(f.value!=INVALID_HANDLE_VALUE,"CreateFileW.workspace_identity");return identity(f.value);}
    static bool distinct(const std::filesystem::path& a,const std::filesystem::path& b){const auto x=identity(a),y=identity(b);return x.volume!=y.volume||x.object!=y.object;}
    static FileVersion capture(const std::string& view,DWORD pid,const FileRequest& receipt,const std::filesystem::path& workspace){
        if(!receipt.action||receipt.done!=receipt.action||receipt.error||receipt.live_handles||receipt.operation!=save_document||!receipt.writes||receipt.closes!=receipt.writes||receipt.flushes!=receipt.writes||receipt.pid!=pid)throw Error("snapshot.file_not_completed_closed",ERROR_BUSY);
        const auto path=workspace/"document.txt";unsigned count=0;
        for(const auto& entry:std::filesystem::directory_iterator(workspace)){if(entry.path().filename()!=L"document.txt")throw Error("snapshot.unknown_workspace_resource",ERROR_NOT_SUPPORTED);++count;}
        demand(count==1,"one declared closed workspace file");
        Handle file(CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.snapshot_exclusive_closed_file");
        const auto id=identity(file.value);LARGE_INTEGER size{};check(GetFileSizeEx(file.value,&size),"GetFileSizeEx.workspace");demand(size.QuadPart>0&&size.QuadPart<=4096,"bounded workspace file");
        FileVersion v;v.present=true;v.view=view;v.pid=pid;v.action=receipt.action;v.volume=id.volume;v.object=id.object;v.content.resize(static_cast<size_t>(size.QuadPart));DWORD read=0;
        check(ReadFile(file.value,v.content.data(),static_cast<DWORD>(v.content.size()),&read,nullptr),"ReadFile.snapshot_workspace");demand(read==v.content.size(),"complete workspace capture");v.sha256=native::digest_text(native::digest(v.content));
        if(v.sha256!=receipt.sha256||v.content.size()!=receipt.written_size)throw Error("snapshot.file_version_mismatch",ERROR_CRC);
        check(CloseHandle(file.value),"CloseHandle.snapshot_workspace");file.value=nullptr;return v;
    }
    static void restore(const FileVersion& v,const std::filesystem::path& workspace,const std::string& fault=""){
        const auto stage=workspace.parent_path()/(workspace.filename().wstring()+L".restore-partial");bool owns_stage=false,owns_final=false;
        try{
            if(std::filesystem::exists(workspace)||std::filesystem::exists(stage))throw Error("restore.workspace_exists",ERROR_ALREADY_EXISTS);
            std::filesystem::create_directories(workspace.parent_path());std::filesystem::create_directory(stage);owns_stage=true;
            if(v.present){
                if(v.sha256!=native::digest_text(native::digest(v.content)))throw Error("restore.snapshot_file_hash",ERROR_CRC);
                auto content=v.content;if(fault=="version")content[0]^=1;
                Handle file(CreateFileW((stage/"document.txt").c_str(),fault=="write"?GENERIC_READ:GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.private_workspace");
                DWORD written=0;check(WriteFile(file.value,content.data(),static_cast<DWORD>(content.size()),&written,nullptr),"WriteFile.private_workspace");demand(written==content.size(),"complete private workspace write");check(FlushFileBuffers(file.value),"FlushFileBuffers.private_workspace");check(CloseHandle(file.value),"CloseHandle.private_workspace");file.value=nullptr;
                if(Artifacts::digest(stage/"document.txt")!=v.sha256)throw Error("restore.private_file_version",ERROR_CRC);
                const auto id=identity(stage/"document.txt");demand(id.volume!=v.volume||id.object!=v.object,"restored workspace is a new file object");
            }
            check(MoveFileExW(stage.c_str(),workspace.c_str(),MOVEFILE_WRITE_THROUGH),"MoveFileExW.private_workspace");owns_stage=false;owns_final=true;
        }catch(...){if(owns_stage)std::filesystem::remove_all(stage);if(owns_final)std::filesystem::remove_all(workspace);throw;}
    }
};
}
