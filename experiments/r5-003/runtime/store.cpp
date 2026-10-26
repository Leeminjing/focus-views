#include "store.hpp"
namespace r5003 {
namespace {
constexpr std::uint64_t disk_magic=0x3150414e535646;
constexpr std::uint32_t disk_version=1;
template<class T>void block(Writer& w,unsigned tag,const T& x){w.pod(tag);w.pod(static_cast<std::uint64_t>(sizeof(x)));w.pod(x);}
template<class T>T block(Reader& r,unsigned tag){if(r.pod<unsigned>()!=tag||r.pod<std::uint64_t>()!=sizeof(T))throw Error("snapshot.block_abi",ERROR_REVISION_MISMATCH);return r.pod<T>();}
bool terminated(const char* x,size_t n){return memchr(x,0,n)!=nullptr;}
template<size_t N>bool terminated(const wchar_t(&x)[N]){return std::find(std::begin(x),std::end(x),L'\0')!=std::end(x);}
void require(bool okay,const char* api){if(!okay)throw Error(api,ERROR_INVALID_DATA);}
void write_file(const std::filesystem::path& p,const Bytes& bytes,bool& owned){
    Handle file(CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.snapshot_staging");
    owned=true;DWORD done=0;check(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&done,nullptr),"WriteFile.snapshot_staging");require(done==bytes.size(),"snapshot.short_write");check(FlushFileBuffers(file.value),"FlushFileBuffers.snapshot_staging");check(CloseHandle(file.value),"CloseHandle.snapshot_staging");file.value=nullptr;
}
}
Environment environment(){Environment x{};check(ProcessIdToSessionId(GetCurrentProcessId(),&x.session),"ProcessIdToSessionId.snapshot");
    auto identity=[](const wchar_t* module){wchar_t path[32768]{};check(GetModuleFileNameW(GetModuleHandleW(module),path,32768)!=0,"GetModuleFileNameW.snapshot_environment");return native::digest(native::read_file(path));};
    x.ntdll=identity(L"ntdll.dll");x.kernel32=identity(L"kernel32.dll");return x;
}
Bytes read(const std::filesystem::path& path){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.snapshot_load");
    BY_HANDLE_FILE_INFORMATION info{};check(GetFileInformationByHandle(file.value,&info),"GetFileInformationByHandle.snapshot");
    require(GetFileType(file.value)==FILE_TYPE_DISK&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&info.nNumberOfLinks==1,"snapshot.ordinary_single_file");
    LARGE_INTEGER size{};check(GetFileSizeEx(file.value,&size),"GetFileSizeEx.snapshot");require(size.QuadPart>=64&&static_cast<std::uint64_t>(size.QuadPart)<=max_packet,"snapshot.file_length");
    Bytes b(static_cast<size_t>(size.QuadPart));DWORD done=0;check(ReadFile(file.value,b.data(),static_cast<DWORD>(b.size()),&done,nullptr),"ReadFile.snapshot_load");require(done==b.size(),"snapshot.short_read");return b;
}
std::string hash(const Bytes& b){return native::digest_text(native::digest(b));}
Bytes envelope(Kind kind,const Bytes& b){Writer w;w.pod(disk_magic);w.pod(disk_version);w.pod(static_cast<unsigned>(IMAGE_FILE_MACHINE_AMD64));w.pod(static_cast<unsigned>(kind));w.pod(0u);w.blob(b);const auto digest=native::digest(w.bytes);w.raw(digest.data(),digest.size());return w.bytes;}
Bytes unpack(Kind kind,const Bytes& b){
    require(b.size()>=64&&b.size()<=max_packet,"snapshot.file_length");Reader r{b};
    if(r.pod<std::uint64_t>()!=disk_magic||r.pod<unsigned>()!=disk_version||r.pod<unsigned>()!=IMAGE_FILE_MACHINE_AMD64||r.pod<unsigned>()!=static_cast<unsigned>(kind)||r.pod<unsigned>()!=0)throw Error("snapshot.format_version",ERROR_REVISION_MISMATCH);
    auto result=r.blob(max_packet-64);std::array<BYTE,32> stored{};r.raw(stored.data(),stored.size());r.end();
    const Bytes signed_bytes(b.begin(),b.end()-32);if(native::digest(signed_bytes)!=stored)throw Error("snapshot.integrity",ERROR_CRC);return result;
}
void publish(const std::filesystem::path& path,Kind kind,const Bytes& b,bool interrupt){
    if(std::filesystem::exists(path))throw Error("snapshot.version_exists",ERROR_ALREADY_EXISTS);
    const auto staging=path.parent_path()/(L".snapshot-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".partial");
    require(!std::filesystem::exists(staging),"snapshot.staging_exists");bool owned=false;
    try{const auto packed=envelope(kind,b);Bytes written=packed;if(interrupt)written.resize(written.size()/2);
        write_file(staging,written,owned);
        if(interrupt)throw Error("snapshot.interrupted",ERROR_OPERATION_ABORTED);
        const auto check_bytes=read(staging);if(check_bytes!=packed||unpack(kind,check_bytes)!=b)throw Error("snapshot.write_verification",ERROR_CRC);
        check(MoveFileExW(staging.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH),"MoveFileExW.snapshot_publish");owned=false;
    }catch(...){if(owned&&!DeleteFileW(staging.c_str()))std::cerr<<"DeleteFileW.owned_snapshot_staging error="<<GetLastError()<<'\n';throw;}
}
Bytes payload(const std::filesystem::path& path,Kind kind){return unpack(kind,read(path));}
void validate(const Snapshot& s){
    const auto& m=s.meta;const auto& l=s.layout;
    if(m.magic!=signature||(m.abi_version!=1&&m.abi_version!=2)||m.abi_bytes!=(m.abi_version==1?sizeof(Metadata):sizeof(MetadataV2))||m.machine!=IMAGE_FILE_MACHINE_AMD64)throw Error("snapshot.runtime_abi",ERROR_REVISION_MISMATCH);
    require(m.resource_kinds==15,"snapshot.resource_profile");
    require(l.version==1&&l.bytes==sizeof(ExecutionLayout)&&(l.frames==3||l.frames==4)&&l.returns==l.frames+2&&s.returns.size()==l.returns,"snapshot.execution_layout");
    require(m.abi_version!=1||(l.frames==3&&l.returns==5),"snapshot.v1_layout");
    require(m.data_bytes>=8&&m.data_bytes<=16*1024*1024&&s.memory.size()==m.data_bytes&&m.stack_bytes>=65536&&m.stack_bytes<=32*1024*1024&&s.stack.size()==m.stack_bytes-4096,"snapshot.region_length");
    require(m.root%65536==0&&m.stack%65536==0&&m.root<=0x7fffffffffffULL-m.data_bytes&&m.stack<=0x7fffffffffffULL-m.stack_bytes&&(m.root+m.data_bytes<=m.stack||m.stack+m.stack_bytes<=m.root),"snapshot.region_address");
    require(s.cpu.ContextFlags==cpu_flags&&s.cpu.Rsp>=m.stack+4096&&s.cpu.Rsp<=m.stack+m.stack_bytes-8&&s.cpu.Rip==m.checkpoint+1&&m.frozen&&!m.in_dispatch&&!m.paint_depth&&m.input_closed&&m.resources_ready,"snapshot.safe_context");
    require(s.image.base==m.image&&s.image.bytes.size()>=sizeof(IMAGE_NT_HEADERS64)&&s.image.bytes.size()<=64*1024*1024,"snapshot.image_length");
    auto image_address=[&](std::uint64_t a){return a>=m.image&&a-m.image<s.image.bytes.size();};
    auto image_span=[&](std::uint64_t a,std::uint64_t size){return a>=m.image&&a-m.image<=s.image.bytes.size()&&size<=s.image.bytes.size()-(a-m.image);};
    require(image_address(m.checkpoint)&&image_span(m.cookie_address,8)&&image_span(m.complement_address,8),"snapshot.image_addresses");
    const auto& dos=*reinterpret_cast<const IMAGE_DOS_HEADER*>(s.image.bytes.data());
    require(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&static_cast<size_t>(dos.e_lfanew)<=s.image.bytes.size()-sizeof(IMAGE_NT_HEADERS64),"snapshot.PE_headers");
    const auto& nt=*reinterpret_cast<const IMAGE_NT_HEADERS64*>(s.image.bytes.data()+dos.e_lfanew);
    require(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&nt.OptionalHeader.SizeOfImage==s.image.bytes.size(),"snapshot.PE_identity");
    require(terminated(m.build,sizeof(m.build))&&terminated(m.window_class),"snapshot.metadata_strings");
    require(m.ref_count>0&&m.ref_count<=8&&m.required_count==m.ref_count&&!memcmp(m.refs,m.required_refs,m.ref_count*sizeof(Reference)),"snapshot.references");
    for(unsigned i=0;i<m.ref_count;++i){const auto& f=m.refs[i];require(f.logical>=1&&f.logical<=3&&((f.address>=m.root&&f.address-m.root<=m.data_bytes-8)||(f.address>=s.cpu.Rsp&&f.address-m.stack<=m.stack_bytes-8)),"snapshot.reference_bounds");}
    for(unsigned i=0;i<l.frames;++i){const auto& f=l.frame[i];require(image_address(f.entry)&&image_address(f.business)&&f.local>=s.cpu.Rsp&&f.local<=m.stack+m.stack_bytes-8&&f.local%8==0,"snapshot.frame_bounds");}
    if(m.abi_version==2)require(image_address(l.caller_entry),"snapshot.caller_bounds");for(auto a:s.returns)require(image_address(a),"snapshot.return_bounds");
    for(unsigned i=0;i<3;++i)require(s.resources.nodes[i].logical==i+1&&terminated(s.resources.nodes[i].klass)&&terminated(s.resources.nodes[i].caption),"snapshot.window_descriptor");
    const auto& u=s.resources.undo;
    require(terminated(u.before.text)&&terminated(u.after.text)&&terminated(u.reason,sizeof(u.reason))&&u.version==1&&u.operations<=1,"snapshot.undo_bounds");
    require(s.resources.selection_start<=s.resources.selection_end&&s.resources.selection_end<=wcslen(s.resources.nodes[1].caption)&&s.resources.focus<=3&&!s.resources.ime_active,"snapshot.control_bounds");
    if(s.resources.can_undo)require(u.valid&&u.operations==1,"snapshot.undo_required");
    if(s.file.present){require(!s.file.name.empty()&&s.file.name!="."&&s.file.name!=".."&&s.file.name.size()<=128&&s.file.name.back()!='.'&&s.file.name.back()!=' '&&s.file.name.find_first_of("\\/:<>\"|?*")==std::string::npos,"snapshot.file_name");
        require(s.file.action&&s.file.pid&&!s.file.view.empty()&&!s.file.content.empty()&&s.file.content.size()<=4096&&s.file.sha256==hash(s.file.content),"snapshot.file_version");
    }else require(s.file.content.empty()&&s.file.name.empty()&&s.file.sha256.empty(),"snapshot.absent_file");
}
Bytes encode(const Snapshot& s){
    Writer w;const auto env=environment();block(w,1,env);block(w,2,s.meta);block(w,3,s.layout);block(w,4,s.cpu);w.blob(s.memory);w.blob(s.stack);block(w,5,s.executable);w.pod(s.cookie);w.pod(s.complement);
    w.pod(static_cast<unsigned>(s.returns.size()));for(auto x:s.returns)w.pod(x);block(w,6,s.resources);
    const auto& f=s.file;w.pod(static_cast<unsigned>(f.present));w.pod(f.pid);w.pod(f.action);w.pod(f.object);w.pod(f.volume);w.text(f.view);w.text(f.sha256);w.text(f.name);w.blob(f.content);w.pod(s.image.base);w.blob(s.image.bytes);return w.bytes;
}
Snapshot decode(const Bytes& b,const std::array<BYTE,32>& expected_image){Reader r{b};const auto saved=block<Environment>(r,1),now=environment();
    if(saved.session!=now.session||saved.ntdll!=now.ntdll||saved.kernel32!=now.kernel32)throw Error("snapshot.windows_session",ERROR_REVISION_MISMATCH);
    Snapshot s;s.meta=block<Metadata>(r,2);s.layout=block<ExecutionLayout>(r,3);s.cpu=block<CONTEXT>(r,4);s.memory=r.blob(16*1024*1024);s.stack=r.blob(32*1024*1024);s.executable=block<std::array<BYTE,32>>(r,5);
    if(s.executable!=expected_image)throw Error("snapshot.target_image",ERROR_REVISION_MISMATCH);
    s.cookie=r.pod<std::uint64_t>();s.complement=r.pod<std::uint64_t>();const auto count=r.pod<unsigned>();if(count!=5&&count!=6)throw Error("snapshot.return_count",ERROR_NOT_SUPPORTED);for(unsigned i=0;i<count;++i)s.returns.push_back(r.pod<std::uint64_t>());s.resources=block<Resources>(r,6);
    auto& f=s.file;const auto present=r.pod<unsigned>();require(present<=1,"snapshot.file_presence");f.present=present!=0;f.pid=r.pod<DWORD>();f.action=r.pod<std::uint64_t>();f.object=r.pod<std::uint64_t>();f.volume=r.pod<DWORD>();f.view=r.text(128);f.sha256=r.text(64);f.name=r.text(128);f.content=r.blob(4096);s.image.base=r.pod<std::uint64_t>();s.image.bytes=r.blob(64*1024*1024);r.end();
    validate(s);return s;
}
void save(const std::filesystem::path& path,const Snapshot& s,bool interrupt){validate(s);publish(path,Kind::snapshot,encode(s),interrupt);}
Snapshot load(const std::filesystem::path& path,const std::array<BYTE,32>& identity){return decode(payload(path,Kind::snapshot),identity);}
}
