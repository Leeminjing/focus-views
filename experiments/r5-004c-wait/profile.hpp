#pragma once
#include "../r5-003/runtime/store.hpp"
namespace wait_probe {
constexpr std::uint64_t profile_magic=0x3153455254494157;
inline r305::Resources resource_profile(const std::filesystem::path& path,const std::array<BYTE,32>& image){using namespace r5003;
    static_assert(std::is_trivially_copyable_v<Resources>);
    const auto bytes=read(path);if(bytes.size()!=8+32+4+sizeof(Resources)+32)throw Error("wait_profile.length",ERROR_INVALID_DATA);
    Bytes body(bytes.begin(),bytes.end()-32);const auto sha=native::digest(body);if(memcmp(sha.data(),bytes.data()+body.size(),32))throw Error("wait_profile.hash",ERROR_CRC);
    Reader r{body};if(r.pod<std::uint64_t>()!=profile_magic||r.pod<std::array<BYTE,32>>()!=image||r.pod<unsigned>()!=sizeof(Resources))throw Error("wait_profile.identity",ERROR_REVISION_MISMATCH);
    auto value=r.pod<Resources>();r.end();return value;
}
}
