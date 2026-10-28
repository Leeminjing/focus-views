#pragma once
#include "image-contract.hpp"
#include <windows.h>
extern "C" {
    __declspec(dllexport) r5004b::ImageReferences R5004ImageReferences{};
    std::uint64_t CodeAnchor();
}
namespace r5004b {
inline void image_slot(const volatile void* slot,Location kind,std::uint64_t value){
    auto& r=R5004ImageReferences;
    if(r.count>=image_capacity)ExitProcess(ERROR_BUFFER_OVERFLOW);
    const auto base=reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
    r.entries[r.count++]={kind,0,reinterpret_cast<std::uint64_t>(const_cast<const void*>(slot)),value-base};r.required=r.count;
}
inline void begin_image_references(){R5004ImageReferences=ImageReferences{};}
}
