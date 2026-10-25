#pragma once
#include "../../r1/snapshot.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
namespace native {
std::vector<std::uint8_t> read_file(const std::filesystem::path&);
std::array<std::uint8_t,32> digest(const std::vector<std::uint8_t>&);
std::string digest_text(const std::array<std::uint8_t,32>&);
DWORD metadata_rva(const std::vector<std::uint8_t>&,const char*);
void read_memory(HANDLE,std::uint64_t,void*,std::size_t);
void write_memory(HANDLE,std::uint64_t,const void*,std::size_t);
template<class T>T read_value(HANDLE process,std::uint64_t address){T result{};read_memory(process,address,&result,sizeof(result));return result;}
}
