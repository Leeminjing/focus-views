#pragma once
#include <cstdint>
namespace r5004b {
// Independent, opt-in image-reference ABI. Metadata v1/v2 are unchanged.
// Addresses name explicit live slots, never a list inferred by scanning memory.
constexpr unsigned image_capacity=16;
enum class Location:unsigned {stack=1,data=2,r8=4};
struct ImageReference {Location location{};unsigned reserved=0;std::uint64_t slot=0,rva=0;};
struct ImageReferences {
    std::uint64_t magic=0x3152464942564646ULL;
    unsigned version=2,bytes=sizeof(ImageReferences),count=0,required=0;
    ImageReference entries[image_capacity]{};
};
}
