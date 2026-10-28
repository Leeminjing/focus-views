#pragma once
#include "../r5-002/runtime/execution.hpp"
#include "faults.hpp"
namespace r5004b {
using namespace r305;
inline DWORD optional_reference_export(const std::vector<BYTE>& disk){
    auto read=[&]<class T>(size_t offset){if(offset>disk.size()||sizeof(T)>disk.size()-offset)throw Error("relocation.export_bounds",ERROR_INVALID_DATA);T value{};memcpy(&value,disk.data()+offset,sizeof(value));return value;};
    const auto dos=read.template operator()<IMAGE_DOS_HEADER>(0);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0)throw Error("relocation.export_DOS",ERROR_INVALID_DATA);
    const auto nt=read.template operator()<IMAGE_NT_HEADERS64>(dos.e_lfanew);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)throw Error("relocation.export_PE",ERROR_INVALID_DATA);
    auto offset=[&](DWORD rva)->size_t{
        if(rva<nt.OptionalHeader.SizeOfHeaders)return rva;
        const auto table=static_cast<size_t>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){const auto s=read.template operator()<IMAGE_SECTION_HEADER>(table+i*sizeof(IMAGE_SECTION_HEADER));
            if(rva>=s.VirtualAddress&&rva-s.VirtualAddress<s.SizeOfRawData)return static_cast<size_t>(s.PointerToRawData)+(rva-s.VirtualAddress);}
        throw Error("relocation.export_RVA",ERROR_INVALID_DATA);
    };
    const auto dir=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];if(!dir.VirtualAddress)return 0;
    const auto exports=read.template operator()<IMAGE_EXPORT_DIRECTORY>(offset(dir.VirtualAddress));
    if(exports.NumberOfNames>65536||exports.NumberOfFunctions>65536)throw Error("relocation.export_count",ERROR_INVALID_DATA);
    constexpr char name[]="R5004ImageReferences";
    for(DWORD i=0;i<exports.NumberOfNames;++i){
        const auto rva=read.template operator()<DWORD>(offset(exports.AddressOfNames)+i*sizeof(DWORD));const auto begin=offset(rva);
        if(begin>disk.size()||sizeof(name)>disk.size()-begin)throw Error("relocation.export_name",ERROR_INVALID_DATA);
        if(memcmp(disk.data()+begin,name,sizeof(name)))continue;
        const auto ordinal=read.template operator()<WORD>(offset(exports.AddressOfNameOrdinals)+i*sizeof(WORD));if(ordinal>=exports.NumberOfFunctions)throw Error("relocation.export_ordinal",ERROR_INVALID_DATA);
        const auto symbol=read.template operator()<DWORD>(offset(exports.AddressOfFunctions)+ordinal*sizeof(DWORD));
        if(symbol>=nt.OptionalHeader.SizeOfImage||sizeof(ImageReferences)>nt.OptionalHeader.SizeOfImage-symbol||(symbol>=dir.VirtualAddress&&symbol-dir.VirtualAddress<dir.Size))throw Error("relocation.export_symbol",ERROR_INVALID_DATA);return symbol;
    }return 0;
}
inline ImageReferences read_references(Child& v,const std::filesystem::path& file){
    const auto rva=optional_reference_export(native::read_file(file));if(!rva)return {};
    const auto refs=native::read_value<ImageReferences>(v.p.process.value,v.image+rva);
    if(refs.magic!=ImageReferences{}.magic||refs.version!=2||refs.bytes!=sizeof(refs)||!refs.count||refs.count>image_capacity||refs.required!=refs.count)throw Error("relocation.reference_ABI",ERROR_NOT_SUPPORTED);
    return refs;
}
// Walk only real PE-unwind return slots. This includes the leaf trap and the
// declared caller boundary; no integer-range heuristic discovers pointers.
inline std::vector<std::uint64_t> return_slots(const Snapshot& s){
    ImageMirror mirror(s.image);
    auto buffer=VirtualAlloc(reinterpret_cast<void*>(s.meta.stack),s.meta.stack_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(buffer==reinterpret_cast<void*>(s.meta.stack),"VirtualAlloc.relocation_stack_mirror");
    struct Release{void* p;~Release(){VirtualFree(p,0,MEM_RELEASE);}} release{buffer};
    memcpy(static_cast<BYTE*>(buffer)+4096,s.stack.data(),s.stack.size());auto cpu=s.cpu;
    std::vector<std::uint64_t> slots;
    for(unsigned i=0;i<s.returns.size();++i){
        demand(cpu.Rsp>=s.cpu.Rsp&&cpu.Rsp<=s.meta.stack+s.meta.stack_bytes-8,"relocation unwind bounds");
        mirror.step(cpu);const auto slot=cpu.Rsp-8;
        if(slot<s.cpu.Rsp||slot>s.meta.stack+s.meta.stack_bytes-8||cpu.Rip!=s.returns[i])throw Error("relocation.unwind_return",ERROR_INVALID_DATA);
        slots.push_back(slot);
    }return slots;
}
inline Snapshot relocate(Child& v,const Snapshot& original,const std::filesystem::path& file,Log& log){
    demand(v.pending&&v.halt=="checkpoint"&&!v.m.published,"relocation only at private target checkpoint");
    auto candidate=original;const auto& s=candidate;
    if(v.name=="RB"&&!relocation_fault.empty()){
        if(relocation_fault=="relocation-missing-return")candidate.image_return_slots.pop_back();
        else if(relocation_fault=="relocation-bad-offset")candidate.image_references.entries[0].rva=s.image.bytes.size()+8;
        else if(relocation_fault=="relocation-missing-reference")--candidate.image_references.count;
        else if(relocation_fault=="relocation-wrong-image")candidate.executable[0]^=1;
        else throw Error("relocation.unknown_test_fault",ERROR_NOT_SUPPORTED);
        log.emit("relocation_fault_injected","\"view\":"+json(v.name)+",\"fault\":"+json(relocation_fault)+",\"originalSnapshotUnchanged\":true,\"sourceStateWritten\":false");
    }
    if(s.meta.image!=s.image.base||s.executable!=native::digest(native::read_file(file))||strcmp(v.m.build,s.meta.build))throw Error("restore.build_or_image_identity",ERROR_REVISION_MISMATCH);
    if(v.image==s.meta.image)return s; // Old ABI same-base path remains exact.
    if(!s.image_references.count)throw Error("relocation.explicit_profile_required",ERROR_NOT_SUPPORTED);
    const auto& declared=s.image_references;
    if(declared.magic!=ImageReferences{}.magic||declared.version!=2||declared.bytes!=sizeof(declared)||declared.count>image_capacity||declared.count!=declared.required)throw Error("relocation.source_reference_ABI",ERROR_INVALID_DATA);
    if(v.m.root!=s.meta.root||v.m.stack!=s.meta.stack||v.m.data_bytes!=s.meta.data_bytes||v.m.stack_bytes!=s.meta.stack_bytes)throw Error("relocation.fixed_regions",ERROR_NOT_SUPPORTED);
    Snapshot t=s;const auto target_image=capture_image(v.p.process.value,v.image,file);
    if(target_image.bytes.size()!=s.image.bytes.size())throw Error("relocation.image_size",ERROR_REVISION_MISMATCH);
    auto address=[&](std::uint64_t old){if(old<s.meta.image||old-s.meta.image>=s.image.bytes.size())throw Error("relocation.image_offset",ERROR_INVALID_ADDRESS);return v.image+(old-s.meta.image);};
    auto note=[&](const char* kind,std::uint64_t slot,std::uint64_t before,std::uint64_t after){log.emit("image_reassociation","\"view\":"+json(v.name)+",\"kind\":"+json(kind)+",\"slot\":"+std::to_string(slot)+",\"rva\":"+std::to_string(before-s.meta.image)+",\"before\":"+std::to_string(before)+",\"after\":"+std::to_string(after)+",\"sourceStateWritten\":false");};
    // Finite, explicit inventory of every image address in the supported ABI.
    auto field=[&](std::uint64_t& a){if(a){const auto old=a;a=address(a);note("execution-descriptor",0,old,a);}};
    auto& m=t.meta;
    const std::uint64_t* actual_fields[]={&v.m.ready,&v.m.init,&v.m.checkpoint,&v.m.continued,&v.m.rejected,&v.m.cookie_address,&v.m.complement_address,&v.m.alt_checkpoint,&v.m.changed,&v.m.rebuild_address};
    std::uint64_t* saved_fields[]={&m.ready,&m.init,&m.checkpoint,&m.continued,&m.rejected,&m.cookie_address,&m.complement_address,&m.alt_checkpoint,&m.changed,&m.rebuild_address};
    for(unsigned i=0;i<std::size(saved_fields);++i){field(*saved_fields[i]);if(*saved_fields[i]!=*actual_fields[i])throw Error("relocation.metadata_RVA_mismatch",ERROR_INVALID_DATA);}
    auto captured_cookie=[&](std::uint64_t slot){if(slot<s.meta.image||slot-s.meta.image>s.image.bytes.size()-8)throw Error("relocation.cookie_bounds",ERROR_INVALID_DATA);std::uint64_t value=0;memcpy(&value,s.image.bytes.data()+static_cast<size_t>(slot-s.meta.image),8);return value;};
    if(captured_cookie(s.meta.cookie_address)!=s.cookie||captured_cookie(s.meta.complement_address)!=s.complement||s.complement!=~s.cookie)throw Error("relocation.cookie_preimage",ERROR_INVALID_DATA);
    for(unsigned i=0;i<3;++i){field(m.frame_entry[i]);field(m.business_sites[i]);}
    field(t.layout.caller_entry);for(unsigned i=0;i<t.layout.frames;++i){field(t.layout.frame[i].entry);field(t.layout.frame[i].business);}
    field(t.cpu.Rip);m.image=v.image;
    const auto slots=return_slots(s);
    if(slots!=s.image_return_slots)throw Error("relocation.return_slot_registry",ERROR_INVALID_DATA);
    for(unsigned i=0;i<slots.size();++i){const auto offset=static_cast<size_t>(slots[i]-s.meta.stack-4096);std::uint64_t old=0;memcpy(&old,s.stack.data()+offset,8);
        if(old!=s.returns[i])throw Error("relocation.return_preimage",ERROR_INVALID_DATA);
        t.returns[i]=address(old);memcpy(t.stack.data()+offset,&t.returns[i],8);note("unwind-return",slots[i],old,t.returns[i]);}
    const auto target_refs=read_references(v,file);const auto& source=s.image_references;
    if(target_refs.count!=source.count||target_refs.required!=source.required)throw Error("relocation.required_reference_missing",ERROR_INVALID_DATA);
    std::set<std::uint64_t> occupied(slots.begin(),slots.end());bool r8=false;
    for(unsigned i=0;i<source.count;++i){const auto& r=source.entries[i];const auto& q=target_refs.entries[i];
        if(r.location!=q.location||r.reserved||q.reserved||r.slot!=q.slot||r.rva!=q.rva||r.rva>=s.image.bytes.size())throw Error("relocation.reference_shape",ERROR_INVALID_DATA);
        const auto expected=s.meta.image+r.rva,new_value=v.image+r.rva;std::uint64_t old=0;
        if(r.location==Location::r8){if(r8||r.slot)throw Error("relocation.register_duplicate",ERROR_INVALID_DATA);r8=true;old=s.cpu.R8;if(v.context().R8!=new_value)throw Error("relocation.target_register_preimage",ERROR_INVALID_DATA);t.cpu.R8=new_value;}
        else {
            const bool stack=r.location==Location::stack;const auto first=stack?s.meta.stack+4096:s.meta.root;const auto& input=stack?s.stack:s.memory;auto& output=stack?t.stack:t.memory;
            if((!stack&&r.location!=Location::data)||r.slot<first||r.slot-first>input.size()-8||r.slot%8||!occupied.insert(r.slot).second||(stack&&r.slot<s.cpu.Rsp))throw Error("relocation.reference_bounds",ERROR_INVALID_ADDRESS);
            for(unsigned j=0;j<s.meta.ref_count;++j)if(r.slot==s.meta.refs[j].address)throw Error("relocation.overlapping_resource_reference",ERROR_INVALID_DATA);
            memcpy(&old,input.data()+static_cast<size_t>(r.slot-first),8);
            if(native::read_value<std::uint64_t>(v.p.process.value,r.slot)!=new_value)throw Error("relocation.target_slot_preimage",ERROR_INVALID_DATA);
            memcpy(output.data()+static_cast<size_t>(r.slot-first),&new_value,8);
        }
        if(old!=expected)throw Error("relocation.reference_preimage",ERROR_INVALID_DATA);
        note("registered-image-reference",r.slot,old,new_value);
    }
    t.image=target_image;
    // Verify transformed normal returns against the target's real protected
    // chain before writing a single source byte. The protected chain is read only.
    const auto boot=v.context();const auto target_slots=return_slots([&]{Snapshot b=t;b.cpu=boot;b.cpu.Rip=v.m.checkpoint+1;native::read_memory(v.p.process.value,v.m.stack+4096,b.stack.data(),b.stack.size());return b;}());
    if(target_slots!=slots)throw Error("relocation.target_return_slots",ERROR_INVALID_DATA);
    for(unsigned i=0;i<t.returns.size();++i){const auto normal=native::read_value<std::uint64_t>(v.p.process.value,slots[i]);const auto shadow=native::read_value<std::uint64_t>(v.p.process.value,boot.R11+8*i);if(normal!=t.returns[i]||shadow!=normal)throw Error("relocation.target_CET_relation",ERROR_INVALID_DATA);}
    log.emit("image_reassociation_prepared","\"view\":"+json(v.name)+",\"sourceBase\":"+std::to_string(s.meta.image)+",\"targetBase\":"+std::to_string(v.image)+",\"sameExeSha256\":"+json(native::digest_text(s.executable))+",\"returnSlots\":"+std::to_string(slots.size())+",\"registeredReferences\":"+std::to_string(source.count)+",\"sourceSnapshotUnchanged\":true,\"sourceStateWritten\":false,\"shadowStackWritten\":false");
    log.emit("image_context_layout","\"view\":"+json(v.name)+",\"ripOffset\":"+std::to_string(offsetof(CONTEXT,Rip))+",\"rspOffset\":"+std::to_string(offsetof(CONTEXT,Rsp))+",\"r10Offset\":"+std::to_string(offsetof(CONTEXT,R10))+",\"r11Offset\":"+std::to_string(offsetof(CONTEXT,R11))+",\"r8Offset\":"+std::to_string(offsetof(CONTEXT,R8)));
    return t;
}
}
