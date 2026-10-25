#pragma once
#include "actions.hpp"
#include <functional>

namespace r305 {
// Experiment-private completed-file protocol, not a filesystem sandbox.
class Artifacts {
public:
    struct Image {
        std::string id,view,action,sha256;unsigned version=0;
        DWORD pid=0;std::uint64_t bytes=0;
        std::filesystem::path directory;
        Handle data_guard,manifest_guard;
    };
private:
    std::filesystem::path root_;
    Actions& actions_;
    std::function<void(const char*,const std::string&)> emit_;
    std::vector<std::unique_ptr<Image>> images_;
    unsigned attempt_=0;
    static std::string hash(const std::vector<BYTE>& bytes){return native::digest_text(native::digest(bytes));}
    static std::vector<BYTE> read(HANDLE handle){
        LARGE_INTEGER size{},zero{};check(GetFileSizeEx(handle,&size),"GetFileSizeEx.artifact");
        demand(size.QuadPart>0&&size.QuadPart<=4096,"bounded ordinary artifact file");
        BY_HANDLE_FILE_INFORMATION info{};check(GetFileInformationByHandle(handle,&info),"GetFileInformationByHandle.artifact");
        demand(!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileType(handle)==FILE_TYPE_DISK,"ordinary unmapped file only");
        check(SetFilePointerEx(handle,zero,nullptr,FILE_BEGIN),"SetFilePointerEx.artifact_read");
        std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));DWORD count=0;
        check(ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr),"ReadFile.artifact");
        demand(count==bytes.size(),"complete artifact read");return bytes;
    }
    static Handle open_read(const std::filesystem::path& path){
        Handle handle(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        check(handle.value!=INVALID_HANDLE_VALUE,"CreateFileW.artifact_read");return handle;
    }
    static void write(const std::filesystem::path& path,const void* data,size_t size,DWORD access=GENERIC_WRITE){
        Handle handle(CreateFileW(path.c_str(),access,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        check(handle.value!=INVALID_HANDLE_VALUE,"CreateFileW.artifact_stage");
        DWORD count=0;check(WriteFile(handle.value,data,static_cast<DWORD>(size),&count,nullptr),"WriteFile.artifact_stage");
        demand(count==size,"complete artifact stage write");check(FlushFileBuffers(handle.value),"FlushFileBuffers.artifact_stage");
        check(CloseHandle(handle.value),"CloseHandle.artifact_stage");handle.value=nullptr;
    }
    static void remove_owned(const std::filesystem::path& stage){
        // Every argument is constructed below from a new child of root_; never
        // accepts an application/caller-supplied recursive deletion target.
        if(std::filesystem::exists(stage))std::filesystem::remove_all(stage);
    }
    static bool same_file(HANDLE a,HANDLE b){
        BY_HANDLE_FILE_INFORMATION x{},y{};
        check(GetFileInformationByHandle(a,&x),"GetFileInformationByHandle.identityA");
        check(GetFileInformationByHandle(b,&y),"GetFileInformationByHandle.identityB");
        return x.dwVolumeSerialNumber==y.dwVolumeSerialNumber&&x.nFileIndexHigh==y.nFileIndexHigh&&x.nFileIndexLow==y.nFileIndexLow;
    }
public:
    Artifacts(const std::filesystem::path& root,Actions& actions,std::function<void(const char*,const std::string&)> emit):root_(root),actions_(actions),emit_(std::move(emit)){std::filesystem::create_directories(root_);}
    static std::vector<BYTE> bytes(const std::filesystem::path& path){auto handle=open_read(path);return read(handle.value);}
    static std::string digest(const std::filesystem::path& path){return hash(bytes(path));}
    std::string describe(const Image& a)const{
        return "{\"id\":"+json(a.id)+",\"sourceView\":"+json(a.view)+",\"sourcePid\":"+std::to_string(a.pid)+",\"version\":"+std::to_string(a.version)+",\"producedAction\":"+json(a.action)+",\"bytes\":"+std::to_string(a.bytes)+",\"sha256\":"+json(a.sha256)+"}";
    }
    Image& publish(const std::string& view,DWORD pid,unsigned version,const Actions::Ticket& producer,const FileRequest& completed,const std::filesystem::path& source,bool interrupt=false){
        const auto t=actions_.begin(view,"artifact.publish");
        const auto id=view+"-v"+std::to_string(version);const auto final=root_/id;
        const auto stage=root_/(".export-"+std::to_string(++attempt_)+".partial");
        bool renamed=false;
        try{
            demand(producer.view==view&&completed.pid==pid&&completed.action==producer.sequence&&completed.done==completed.action&&!completed.error&&!completed.live_handles&&completed.writes&&completed.flushes==completed.writes&&completed.closes>=completed.writes,"publish completed closed native application file only");
            if(std::filesystem::exists(final))throw Error("artifact.version_exists",ERROR_ALREADY_EXISTS);
            actions_.state(t,1,"{\"producerAction\":"+json(producer.id)+"}");
            // No SHARE_WRITE/SHARE_DELETE: reject an outstanding writer and
            // keep source bytes stable during publication.
            auto source_handle=open_read(source);const auto content=read(source_handle.value);
            demand(content.size()==completed.written_size&&hash(content)==completed.sha256,"producer receipt matches actual closed file");
            std::filesystem::create_directory(stage);actions_.state(t,2,"{\"source\":"+json(source.wstring().c_str())+"}");
            if(interrupt){
                write(stage/"data.bin",content.data(),content.size()/2);
                emit_("export_interrupted","\"candidate\":"+json(id)+",\"writtenBytes\":"+std::to_string(content.size()/2)+",\"published\":false");
                throw Error("artifact.export_interrupted",ERROR_OPERATION_ABORTED);
            }
            write(stage/"data.bin",content.data(),content.size());
            auto staged=open_read(stage/"data.bin");
            if(hash(read(staged.value))!=hash(content))throw Error("artifact.export_hash",ERROR_CRC);
            demand(!same_file(source_handle.value,staged.value),"artifact is independent file object");staged.close();
            auto image=std::make_unique<Image>();image->id=id;image->view=view;image->pid=pid;image->action=producer.id;image->version=version;image->bytes=content.size();image->sha256=hash(content);image->directory=final;
            const auto manifest=describe(*image)+"\n";write(stage/"manifest.json",manifest.data(),manifest.size());
            check(MoveFileExW(stage.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH),"MoveFileExW.artifact_publish");renamed=true;
            image->data_guard=open_read(final/"data.bin");image->manifest_guard=open_read(final/"manifest.json");
            check(SetFileAttributesW((final/"data.bin").c_str(),FILE_ATTRIBUTE_READONLY),"SetFileAttributesW.artifact_data");
            check(SetFileAttributesW((final/"manifest.json").c_str(),FILE_ATTRIBUTE_READONLY),"SetFileAttributesW.artifact_manifest");
            auto& result=*image;images_.push_back(std::move(image));
            emit_("artifact_published","\"artifact\":"+describe(result));actions_.state(t,3,describe(result));return result;
        }catch(const Error& e){
            remove_owned(stage);if(renamed)remove_owned(final);
            emit_("artifact_rejected","\"candidate\":"+json(id)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"partialExists\":"+(std::filesystem::exists(stage)?"true":"false")+",\"published\":false");
            actions_.fail(t,e);throw;
        }
    }
    std::filesystem::path import_private(const std::string& view,const Image& a,const Image& b,const std::filesystem::path& workspace,const std::string& fault=""){
        const auto t=actions_.begin(view,"artifact.import_private");
        const auto stage=workspace/(".import-"+std::to_string(++attempt_)+".partial");const auto final=workspace/"imports";
        try{
            if(std::filesystem::exists(final))throw Error("artifact.import_destination_exists",ERROR_ALREADY_EXISTS);
            actions_.state(t,1,"{\"A\":"+json(a.id)+",\"B\":"+json(b.id)+"}");
            std::error_code error;std::filesystem::create_directories(workspace,error);if(error)throw Error("CreateDirectory.private_workspace",static_cast<DWORD>(error.value()));
            std::filesystem::create_directory(stage);actions_.state(t,2);
            unsigned index=0;
            for(const auto* artifact:{&a,&b}){
                auto content=read(artifact->data_guard.value);
                if(hash(content)!=artifact->sha256||content.size()!=artifact->bytes)throw Error("artifact.source_hash",ERROR_CRC);
                const auto name=index==0?"A.txt":"B.txt";
                if(fault=="hash"&&index==1){content[0]^=1;emit_("injected_import_corruption","\"artifact\":"+json(artifact->id)+",\"stageOnly\":true");}
                // This negative path uses a real read-only native handle so
                // WriteFile, not a fabricated error code, rejects the write.
                write(stage/name,content.data(),content.size(),fault=="write"&&index==1?GENERIC_READ:GENERIC_WRITE);
                auto copied=open_read(stage/name);
                if(hash(read(copied.value))!=artifact->sha256)throw Error("artifact.import_hash",ERROR_CRC);
                demand(!same_file(artifact->data_guard.value,copied.value),"private import uses independent file object");++index;
            }
            check(MoveFileExW(stage.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH),"MoveFileExW.import_publish");
            emit_("private_import_published","\"view\":"+json(view)+",\"A\":"+json(a.id)+",\"B\":"+json(b.id)+",\"directory\":"+json(final.wstring().c_str()));
            actions_.state(t,3,"{\"directory\":"+json(final.wstring().c_str())+"}");return final;
        }catch(const Error& e){
            remove_owned(stage);
            emit_("private_import_rejected","\"view\":"+json(view)+",\"api\":"+json(e.api)+",\"error\":"+std::to_string(e.code)+",\"partialExists\":"+(std::filesystem::exists(stage)?"true":"false")+",\"published\":false");
            actions_.fail(t,e);throw;
        }
    }
    void verify_all(const std::string& phase){
        for(const auto& a:images_){
            const auto content=read(a->data_guard.value);
            if(hash(content)!=a->sha256||content.size()!=a->bytes)throw Error("artifact.immutable_content",ERROR_CRC);
            const auto manifest=read(a->manifest_guard.value);
            const auto expected=describe(*a)+"\n";
            demand(std::string(manifest.begin(),manifest.end())==expected,"immutable artifact provenance");
            emit_("artifact_verified","\"phase\":"+json(phase)+",\"artifact\":"+describe(*a));
        }
    }
    bool has(const std::string& id)const{return std::filesystem::exists(root_/id);}
};
}
