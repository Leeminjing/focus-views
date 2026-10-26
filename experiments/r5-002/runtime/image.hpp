#pragma once
#include "processes.hpp"
namespace r305 {
struct ImageBytes {std::uint64_t base=0;std::vector<BYTE> bytes;};
inline ImageBytes capture_image(HANDLE process,std::uint64_t base,const std::filesystem::path& file){
    const auto disk=native::read_file(file);demand(disk.size()>sizeof(IMAGE_DOS_HEADER),"disk PE header bounds");
    const auto& d=*reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());demand(d.e_magic==IMAGE_DOS_SIGNATURE&&d.e_lfanew>0&&static_cast<size_t>(d.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)<=disk.size(),"disk NT header bounds");
    const auto& expected=*reinterpret_cast<const IMAGE_NT_HEADERS64*>(disk.data()+d.e_lfanew);
    const auto dos=native::read_value<IMAGE_DOS_HEADER>(process,base);
    demand(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew==d.e_lfanew,"remote DOS identity");
    const auto nt=native::read_value<IMAGE_NT_HEADERS64>(process,base+dos.e_lfanew);
    const auto& directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    demand(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC&&nt.FileHeader.TimeDateStamp==expected.FileHeader.TimeDateStamp&&nt.OptionalHeader.SizeOfImage==expected.OptionalHeader.SizeOfImage&&directory.VirtualAddress==expected.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress&&directory.Size==expected.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size,"remote PE matches selected file identity");
    demand(nt.OptionalHeader.SizeOfImage>0&&nt.OptionalHeader.SizeOfImage<=64*1024*1024&&directory.Size>0&&directory.Size%sizeof(RUNTIME_FUNCTION)==0&&static_cast<std::uint64_t>(directory.VirtualAddress)+directory.Size<=nt.OptionalHeader.SizeOfImage,"bounded remote exception directory");
    ImageBytes result;result.base=base;result.bytes.resize(nt.OptionalHeader.SizeOfImage);
    for(std::uint64_t offset=0;offset<result.bytes.size();){
        MEMORY_BASIC_INFORMATION region{};check(VirtualQueryEx(process,reinterpret_cast<void*>(base+offset),&region,sizeof(region))==sizeof(region),"VirtualQueryEx.target_image");
        const auto end=std::min(base+result.bytes.size(),reinterpret_cast<std::uint64_t>(region.BaseAddress)+region.RegionSize);demand(end>base+offset,"image region progress");
        if(region.State==MEM_COMMIT&&!(region.Protect&(PAGE_NOACCESS|PAGE_GUARD)))native::read_memory(process,base+offset,result.bytes.data()+offset,static_cast<size_t>(end-base-offset));
        offset=end-base;
    }
    // The function table must exactly match its immutable on-disk section bytes.
    demand(static_cast<size_t>(d.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)+expected.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=disk.size(),"disk section table bounds");
    const auto sections=IMAGE_FIRST_SECTION(&expected);bool matched=false;
    for(unsigned i=0;i<expected.FileHeader.NumberOfSections;++i){const auto& s=sections[i];if(directory.VirtualAddress>=s.VirtualAddress&&directory.VirtualAddress+directory.Size<=s.VirtualAddress+s.SizeOfRawData){const auto raw=s.PointerToRawData+directory.VirtualAddress-s.VirtualAddress;demand(static_cast<std::uint64_t>(raw)+directory.Size<=disk.size(),"disk exception table bounds");matched=!memcmp(result.bytes.data()+directory.VirtualAddress,disk.data()+raw,directory.Size);}}
    demand(matched,"live target function table matches disk");return result;
}
class ImageMirror {
    void* allocation_=nullptr;
    std::uint64_t base_=0;size_t size_=0;DWORD table_rva_=0,count_=0;
    void range(std::uint64_t offset,size_t size)const{demand(offset<=size_&&size<=size_-offset,"target unwind image bounds");}
public:
    explicit ImageMirror(const ImageBytes& image):base_(image.base),size_(image.bytes.size()){
        demand(base_!=reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)),"manager image must differ from application image");
        allocation_=VirtualAlloc(reinterpret_cast<void*>(base_),size_,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        check(allocation_==reinterpret_cast<void*>(base_),"VirtualAlloc.target_image_readonly_mirror");
        try{memcpy(allocation_,image.bytes.data(),size_);const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base_);range(dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64));const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base_+dos->e_lfanew);const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];range(dir.VirtualAddress,dir.Size);table_rva_=dir.VirtualAddress;count_=static_cast<DWORD>(dir.Size/sizeof(RUNTIME_FUNCTION));
            DWORD previous=0;const auto table=reinterpret_cast<PRUNTIME_FUNCTION>(base_+table_rva_);
            for(DWORD i=0;i<count_;++i){demand(table[i].BeginAddress>=previous&&table[i].EndAddress>table[i].BeginAddress&&table[i].EndAddress<=size_,"ordered bounded target function entries");range(table[i].UnwindData,4);previous=table[i].EndAddress;}
            DWORD old=0;check(VirtualProtect(allocation_,size_,PAGE_READONLY,&old),"VirtualProtect.target_image_no_execute");
        }catch(...){VirtualFree(allocation_,0,MEM_RELEASE);allocation_=nullptr;throw;}
    }
    ~ImageMirror(){if(allocation_)VirtualFree(allocation_,0,MEM_RELEASE);}
    ImageMirror(const ImageMirror&)=delete;
    std::uint64_t base()const{return base_;}
    DWORD count()const{return count_;}
    PRUNTIME_FUNCTION lookup(std::uint64_t pc)const{
        demand(pc>=base_&&pc-base_<size_,"continuation PC belongs to declared target image");
        const auto rva=static_cast<DWORD>(pc-base_);auto table=reinterpret_cast<PRUNTIME_FUNCTION>(base_+table_rva_);DWORD first=0,last=count_;
        while(first<last){const auto mid=first+(last-first)/2;if(rva<table[mid].BeginAddress)last=mid;else if(rva>=table[mid].EndAddress)first=mid+1;else return table+mid;}return nullptr;
    }
    DWORD primary(PRUNTIME_FUNCTION entry)const{
        for(unsigned depth=0;depth<16;++depth){range(entry->UnwindData,4);const auto p=reinterpret_cast<const BYTE*>(base_+entry->UnwindData);demand((p[0]&7)==1||(p[0]&7)==2,"supported target unwind version");const auto offset=4+2*((static_cast<unsigned>(p[2])+1)&~1u);range(entry->UnwindData,offset);if(!((p[0]>>3)&UNW_FLAG_CHAININFO))return entry->BeginAddress;range(entry->UnwindData+offset,sizeof(RUNTIME_FUNCTION));entry=reinterpret_cast<PRUNTIME_FUNCTION>(base_+entry->UnwindData+offset);}
        throw Error("unwind.target_chain_depth",ERROR_INVALID_DATA);
    }
    void step(CONTEXT& context)const{
        const auto entry=lookup(context.Rip);
        if(!entry){context.Rip=*reinterpret_cast<const std::uint64_t*>(context.Rsp);context.Rsp+=8;return;}
        (void)primary(entry);PVOID data=nullptr;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,base_,context.Rip,entry,&context,&data,&frame,nullptr);
    }
};
}
