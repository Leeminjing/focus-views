#pragma once
#include "channel.hpp"

namespace r304 {
// Two checked named imports in the owned main EXE. A single lifetime pin covers
// both entries; partially installed transactions are rolled back before release.
class IatPatch {
    Channel& c_;
    BYTE* base_ = nullptr;
    struct Entry { const char* name; void** slot = nullptr; void* native = nullptr; void* replacement = nullptr; DWORD protection = 0; };
    Entry entries_[2]{{"GetAsyncKeyState"},{"GetCursorPos"}};
    HMODULE pin_ = nullptr;
    template<class T> T* at(std::uint64_t rva, std::size_t count = 1) {
        demand(rva < c_.image_size && count <= (c_.image_size-rva)/sizeof(T), "PE RVA outside image");
        return reinterpret_cast<T*>(base_+rva);
    }
    const char* text(DWORD rva) {
        const auto p = at<char>(rva);
        demand(strnlen_s(p,static_cast<size_t>(c_.image_size-rva)) < c_.image_size-rva,"PE string bound"); return p;
    }
    void metadata() {
        auto& a = entries_[0]; auto& b = entries_[1];
        c_.slot = reinterpret_cast<std::uint64_t>(a.slot); c_.native = reinterpret_cast<std::uint64_t>(a.native);
        c_.replacement = reinterpret_cast<std::uint64_t>(a.replacement); c_.slot_after = a.slot ? reinterpret_cast<std::uint64_t>(*a.slot) : 0;
        c_.cursor_slot = reinterpret_cast<std::uint64_t>(b.slot); c_.cursor_native = reinterpret_cast<std::uint64_t>(b.native);
        c_.cursor_replacement = reinterpret_cast<std::uint64_t>(b.replacement); c_.cursor_after = b.slot ? reinterpret_cast<std::uint64_t>(*b.slot) : 0;
        c_.page_before = a.protection; c_.cursor_page_before = b.protection;
        c_.iat_installed = (a.slot && *a.slot == a.replacement) || (b.slot && *b.slot == b.replacement);
        MEMORY_BASIC_INFORMATION info{};
        if (a.slot && VirtualQuery(a.slot,&info,sizeof(info))) c_.page_after = info.Protect;
        if (b.slot && VirtualQuery(b.slot,&info,sizeof(info))) c_.cursor_page_after = info.Protect;
    }
    void exchange(Entry& e, void* from, void* to) {
        DWORD previous = 0, discarded = 0;
        check(VirtualProtect(e.slot,sizeof(void*),PAGE_READWRITE,&previous),"VirtualProtect.EXE_IAT.writable");
        auto actual = InterlockedCompareExchangePointer(e.slot,to,from);
        const auto restored = VirtualProtect(e.slot,sizeof(void*),previous,&discarded);
        const auto error = restored ? 0 : GetLastError(); metadata();
        if (!restored) throw Error("VirtualProtect.EXE_IAT.restore",error);
        demand(actual == from && c_.page_before == c_.page_after && c_.cursor_page_before == c_.cursor_page_after,"IAT exchange/protection");
    }
public:
    explicit IatPatch(Channel& c) : c_(c) {}
    void install(void* async, void* cursor) {
        base_ = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)); c_.image = reinterpret_cast<std::uint64_t>(base_);
        MEMORY_BASIC_INFORMATION info{}; check(VirtualQuery(base_,&info,sizeof(info)) != 0,"VirtualQuery.EXE_headers");
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base_);
        demand(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && static_cast<size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64) <= info.RegionSize,"PE DOS");
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base_+dos->e_lfanew);
        demand(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,"PE x64");
        c_.image_size = nt->OptionalHeader.SizeOfImage;
        check(GetModuleFileNameW(reinterpret_cast<HMODULE>(base_),c_.module_path,512) != 0,"GetModuleFileNameW.EXE");
        const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        demand(imports.VirtualAddress && imports.Size >= sizeof(IMAGE_IMPORT_DESCRIPTOR),"PE imports");
        auto ds = at<IMAGE_IMPORT_DESCRIPTOR>(imports.VirtualAddress,imports.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR));
        unsigned matches[2]{};
        for (unsigned d=0;d<imports.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR) && ds[d].Name;++d) {
            if (_stricmp(text(ds[d].Name),"USER32.dll")) continue;
            demand(ds[d].OriginalFirstThunk && ds[d].FirstThunk,"PE original thunk required");
            for (std::uint64_t i=0;;++i) {
                const auto name = at<IMAGE_THUNK_DATA64>(ds[d].OriginalFirstThunk+i*sizeof(IMAGE_THUNK_DATA64));
                if (!name->u1.AddressOfData) break;
                if (IMAGE_SNAP_BY_ORDINAL64(name->u1.Ordinal)) continue;
                const auto function = text(static_cast<DWORD>(name->u1.AddressOfData)+offsetof(IMAGE_IMPORT_BY_NAME,Name));
                for (unsigned n=0;n<2;++n) {
                    const auto wanted = c_.fail_install == 1 && n == 1 ? "R304MissingImport" : entries_[n].name;
                    if (!strcmp(function,wanted)) {
                        entries_[n].slot = reinterpret_cast<void**>(at<IMAGE_THUNK_DATA64>(ds[d].FirstThunk+i*sizeof(IMAGE_THUNK_DATA64))); ++matches[n];
                    }
                }
            }
        }
        for (unsigned n=0;n<2;++n) {
            if (!matches[n]) throw Error(entries_[n].name,ERROR_PROC_NOT_FOUND);
            auto& e = entries_[n];
            demand(matches[n] == 1 && reinterpret_cast<std::uintptr_t>(e.slot)%alignof(void*) == 0,"unique aligned import");
            e.native = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),e.name));
            demand(e.native && *e.slot == e.native,"native import identity");
            check(VirtualQuery(e.slot,&info,sizeof(info)) != 0,"VirtualQuery.IAT"); e.protection = info.Protect;
        }
        entries_[0].replacement = async; entries_[1].replacement = cursor; metadata();
        HMODULE runtime = nullptr, native = nullptr;
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(async),&runtime),"GetModuleHandleExW.runtime");
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(entries_[0].native),&native),"GetModuleHandleExW.native");
        c_.runtime_image = reinterpret_cast<std::uint64_t>(runtime);
        check(GetModuleFileNameW(runtime,c_.runtime_path,512) != 0,"GetModuleFileNameW.runtime");
        check(GetModuleFileNameW(native,c_.native_path,512) != 0,"GetModuleFileNameW.native");
        if (c_.virtualize) {
            check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(async),&pin_),"GetModuleHandleExW.pin"); c_.pinned = 1;
            try {
                for (unsigned n=0;n<2;++n) {
                    exchange(entries_[n],entries_[n].native,entries_[n].replacement);
                    if (c_.fail_install == 2 && n == 0) throw Error("injected.partial_IAT_install",ERROR_CANCELLED);
                }
            } catch (...) { remove(); throw; }
        }
        c_.initialized = 1;
    }
    void remove(bool release_pin = true) {
        if (c_.iat_installed && c_.fail_restore) throw Error("injected.IAT.restore_refused",ERROR_ACCESS_DENIED);
        for (auto& e : entries_) if (e.slot && *e.slot == e.replacement) exchange(e,e.replacement,e.native);
        metadata(); c_.restored = c_.slot_after == c_.native && c_.cursor_after == c_.cursor_native;
        demand(c_.restored && !c_.iat_installed,"two imports restoration");
        // An orphan watchdog executes from this DLL: retain its final reference.
        if (release_pin && pin_) { check(FreeLibrary(pin_),"FreeLibrary.self_pin"); pin_ = nullptr; c_.pinned = 0; }
    }
};
}
