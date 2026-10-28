#pragma once
#include "image.hpp"
#include "files.hpp"
#ifdef FOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT
#include "../../r5-004b/image-contract.hpp"
#endif
namespace r305 {
struct Snapshot {
    Metadata meta;ExecutionLayout layout;CONTEXT cpu{};std::vector<BYTE> memory,stack;
    std::array<BYTE,32> executable{};std::uint64_t cookie=0,complement=0;
    std::vector<std::uint64_t> returns;Resources resources{};FileVersion file;ImageBytes image;
#ifdef FOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT
    r5004b::ImageReferences image_references{};std::vector<std::uint64_t> image_return_slots;
#endif
};
class Execution {
    Log& log_;std::filesystem::path file_,out_;
    static void write_bytes(const std::filesystem::path&,const void*,size_t);std::array<BYTE,32> identity_;
    void verify(const std::string& name,bool okay,const std::string& detail);
public:
    Execution(Log& log,const std::filesystem::path& file,const std::filesystem::path& output);
    void verify_file(const std::filesystem::path&,const std::array<BYTE,32>&);
    std::uint64_t xstate(Child&,const char*);
    std::vector<std::uint64_t> chain(Child&,const char*);
    void unwind(const Snapshot&);
    Snapshot save(Child&,const std::string& label="S0");
    void restore(Child&,const Snapshot&,const std::string& fault="");
};
}
