#include "profile.hpp"
int wmain(int argc,wchar_t** argv){using namespace r5003;try{
    r3::Args args(argc,argv);const auto image=native::digest(native::read_file(args.get(L"--fixture")));const auto snapshot=load(args.get(L"--snapshot"),image);
    Writer w;w.pod(wait_probe::profile_magic);w.pod(image);w.pod(static_cast<unsigned>(sizeof(Resources)));w.pod(snapshot.resources);const auto sha=native::digest(w.bytes);w.raw(sha.data(),sha.size());
    Handle file(CreateFileW(args.get(L"--output").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));check(file.value!=INVALID_HANDLE_VALUE,"CreateFileW.wait_profile");DWORD written=0;check(WriteFile(file.value,w.bytes.data(),static_cast<DWORD>(w.bytes.size()),&written,nullptr),"WriteFile.wait_profile");if(written!=w.bytes.size())throw Error("wait_profile.short_write",ERROR_WRITE_FAULT);check(FlushFileBuffers(file.value),"FlushFileBuffers.wait_profile");file=Handle{};
    const auto readback=wait_probe::resource_profile(args.get(L"--output"),image);if(memcmp(&readback,&snapshot.resources,sizeof(Resources)))throw Error("wait_profile.readback",ERROR_CRC);
    std::cout<<"{\"resourceProfileOnly\":true,\"sourceSnapshotRestorePerformed\":false,\"bytes\":"<<w.bytes.size()<<",\"sha256\":"<<json(hash(w.bytes))<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
