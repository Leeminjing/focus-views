#pragma once
#include "channel.hpp"

namespace r303 {
// Only the already-loaded, owned main EXE is inspected. No system module writes,
// delay imports, missing OriginalFirstThunk, or unbounded PE walks are accepted.
class IatPatch {
    Channel& c_;
    BYTE* base_ = nullptr;
    void** slot_ = nullptr;
    HMODULE pin_ = nullptr;
    template<class T> T* at(std::uint64_t rva, std::size_t count = 1) {
        demand(rva < c_.image_size && count <= (c_.image_size-rva)/sizeof(T), "PE RVA outside image");
        return reinterpret_cast<T*>(base_+rva);
    }
    const char* string_at(DWORD rva) {
        const auto text = at<char>(rva);
        demand(strnlen_s(text,static_cast<std::size_t>(c_.image_size-rva)) < c_.image_size-rva, "unterminated PE name");
        return text;
    }
    void exchange(void* expected, void* replacement) {
        DWORD protection = 0;
        check(VirtualProtect(slot_,sizeof(void*),PAGE_READWRITE,&protection), "VirtualProtect.EXE_IAT.writable");
        const auto actual = InterlockedCompareExchangePointer(slot_,replacement,expected);
        c_.slot_after = reinterpret_cast<std::uint64_t>(*slot_);
        c_.iat_installed = *slot_ == reinterpret_cast<void*>(c_.replacement);
        DWORD discarded = 0;
        check(VirtualProtect(slot_,sizeof(void*),protection,&discarded), "VirtualProtect.EXE_IAT.restore_protection");
        MEMORY_BASIC_INFORMATION info{};
        check(VirtualQuery(slot_,&info,sizeof(info)) != 0, "VirtualQuery.EXE_IAT");
        c_.page_after = info.Protect;
        demand(actual == expected && c_.page_after == c_.page_before, "IAT compare/exchange or protection mismatch");
    }
    void unpin() {
        if (pin_) { check(FreeLibrary(pin_), "FreeLibrary.runtime_self_reference"); pin_ = nullptr; c_.pinned = 0; }
    }
public:
    explicit IatPatch(Channel& c) : c_(c) {}
    void discover() {
        base_ = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
        c_.image = reinterpret_cast<std::uint64_t>(base_);
        MEMORY_BASIC_INFORMATION info{};
        check(VirtualQuery(base_,&info,sizeof(info)) != 0,"VirtualQuery.EXE_headers");
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base_);
        demand(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<std::size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64) <= info.RegionSize,"PE DOS header");
        const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base_+dos->e_lfanew);
        demand(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
            nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,"PE x64 headers");
        c_.image_size = nt->OptionalHeader.SizeOfImage;
        check(GetModuleFileNameW(reinterpret_cast<HMODULE>(base_),c_.module_path,512) != 0,"GetModuleFileNameW.EXE");
        const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        demand(imports.VirtualAddress && imports.Size >= sizeof(IMAGE_IMPORT_DESCRIPTOR),"PE import directory");
        auto descriptors = at<IMAGE_IMPORT_DESCRIPTOR>(imports.VirtualAddress,imports.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR));
        unsigned matches = 0;
        for (unsigned d=0; d<imports.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR) && descriptors[d].Name; ++d) {
            const auto& descriptor = descriptors[d];
            if (_stricmp(string_at(descriptor.Name),"USER32.dll") != 0) continue;
            demand(descriptor.OriginalFirstThunk && descriptor.FirstThunk,"unsupported missing name thunk");
            for (std::uint64_t i=0;;++i) {
                const auto name = at<IMAGE_THUNK_DATA64>(descriptor.OriginalFirstThunk+i*sizeof(IMAGE_THUNK_DATA64));
                if (!name->u1.AddressOfData) break;
                if (IMAGE_SNAP_BY_ORDINAL64(name->u1.Ordinal)) continue;
                const auto function = string_at(static_cast<DWORD>(name->u1.AddressOfData)+offsetof(IMAGE_IMPORT_BY_NAME,Name));
                const char* wanted = c_.fail_install == 1 ? "R303MissingImport" : "GetAsyncKeyState";
                if (std::strcmp(function,wanted) != 0) continue;
                slot_ = reinterpret_cast<void**>(at<IMAGE_THUNK_DATA64>(descriptor.FirstThunk+i*sizeof(IMAGE_THUNK_DATA64)));
                ++matches;
            }
        }
        if (!matches) throw Error("PE.import.USER32.GetAsyncKeyState",ERROR_PROC_NOT_FOUND);
        demand(matches == 1 && reinterpret_cast<std::uintptr_t>(slot_) % alignof(void*) == 0,"unique aligned IAT entry");
        const auto native = GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetAsyncKeyState");
        demand(native != nullptr && *slot_ == reinterpret_cast<void*>(native),"IAT entry differs from native export");
        c_.slot = reinterpret_cast<std::uint64_t>(slot_); c_.native = reinterpret_cast<std::uint64_t>(native);
        c_.slot_after = c_.native;
        HMODULE owner = nullptr;
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(native),&owner),"GetModuleHandleExW.native_owner");
        check(GetModuleFileNameW(owner,c_.native_path,512) != 0,"GetModuleFileNameW.native_owner");
        check(VirtualQuery(slot_,&info,sizeof(info)) != 0,"VirtualQuery.IAT_protection");
        c_.page_before = info.Protect; c_.page_after = info.Protect;
    }
    void install(void* replacement) {
        discover(); c_.replacement = reinterpret_cast<std::uint64_t>(replacement);
        HMODULE runtime = nullptr;
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(replacement),&runtime),"GetModuleHandleExW.replacement_owner");
        c_.runtime_image = reinterpret_cast<std::uint64_t>(runtime);
        check(GetModuleFileNameW(runtime,c_.runtime_path,512) != 0,"GetModuleFileNameW.replacement_owner");
        if (c_.virtualize) {
            check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(replacement),&pin_),"GetModuleHandleExW.pin_runtime");
            c_.pinned = 1;
            try {
                exchange(reinterpret_cast<void*>(c_.native),replacement);
                if (c_.fail_install == 2) throw Error("injected.IAT.after_exchange",ERROR_CANCELLED);
            } catch (...) {
                if (c_.iat_installed) exchange(replacement,reinterpret_cast<void*>(c_.native));
                c_.restored = c_.slot_after == c_.native;
                if (!c_.iat_installed) unpin();
                throw;
            }
        }
        c_.initialized = 1;
    }
    void remove() {
        if (c_.iat_installed) {
            if (c_.fail_restore) throw Error("injected.IAT.restore_refused",ERROR_ACCESS_DENIED);
            exchange(reinterpret_cast<void*>(c_.replacement),reinterpret_cast<void*>(c_.native));
        }
        c_.restored = c_.slot_after == c_.native;
        demand(c_.restored && !c_.iat_installed,"IAT restoration verification");
        unpin();
    }
};
}
