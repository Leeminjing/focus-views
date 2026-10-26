#pragma once
#include "../../r5-002/runtime/execution.hpp"
#include <type_traits>

namespace r5003 {
using namespace r305;
// Experiment-only, little-endian x64, exact-build storage. POD blocks are
// explicitly sized native ABI records; C++ containers and handles never enter it.
using Bytes=std::vector<BYTE>;
constexpr std::uint64_t max_packet=128ULL*1024*1024;
struct Writer {
    Bytes bytes;
    void raw(const void* p,size_t n){if(n>max_packet-bytes.size())throw Error("snapshot.length",ERROR_FILE_TOO_LARGE);if(n){const auto b=static_cast<const BYTE*>(p);bytes.insert(bytes.end(),b,b+n);}}
    template<class T>void pod(const T& x){static_assert(std::is_trivially_copyable_v<T>);raw(&x,sizeof(x));}
    void text(const std::string& x){pod(static_cast<std::uint32_t>(x.size()));raw(x.data(),x.size());}
    void blob(const Bytes& x){pod(static_cast<std::uint64_t>(x.size()));raw(x.data(),x.size());}
};
struct Reader {
    const Bytes& bytes;size_t at=0;
    void raw(void* p,size_t n){if(at>bytes.size()||n>bytes.size()-at)throw Error("snapshot.truncated",ERROR_HANDLE_EOF);if(n)memcpy(p,bytes.data()+at,n);at+=n;}
    template<class T>T pod(){static_assert(std::is_trivially_copyable_v<T>);T x{};raw(&x,sizeof(x));return x;}
    std::string text(size_t limit=512){const auto n=pod<std::uint32_t>();if(n>limit||n>bytes.size()-at)throw Error("snapshot.string_length",ERROR_INVALID_DATA);std::string x(n,'\0');raw(x.data(),n);if(x.find('\0')!=std::string::npos)throw Error("snapshot.string_nul",ERROR_INVALID_DATA);return x;}
    Bytes blob(std::uint64_t limit){const auto n=pod<std::uint64_t>();if(n>limit||n>bytes.size()-at)throw Error("snapshot.blob_length",ERROR_INVALID_DATA);Bytes x(static_cast<size_t>(n));raw(x.data(),x.size());return x;}
    void end(){if(at!=bytes.size())throw Error("snapshot.trailing_data",ERROR_INVALID_DATA);}
};
enum class Kind:std::uint32_t {snapshot=1,session=2};
struct Environment {
    DWORD session=0;std::array<BYTE,32> ntdll{},kernel32{};
};
Environment environment();
Bytes read(const std::filesystem::path&);
std::string hash(const Bytes&);
Bytes envelope(Kind,const Bytes&);
Bytes unpack(Kind,const Bytes&);
void publish(const std::filesystem::path&,Kind,const Bytes&,bool interrupt=false);
Bytes payload(const std::filesystem::path&,Kind);
Bytes encode(const Snapshot&);
Snapshot decode(const Bytes&,const std::array<BYTE,32>& expected_image);
void validate(const Snapshot&);
void save(const std::filesystem::path&,const Snapshot&,bool interrupt=false);
Snapshot load(const std::filesystem::path&,const std::array<BYTE,32>&);
}
