#pragma once
#include "snapshot.hpp"
#include "../common/native.hpp"
#include <map>
#include <set>

namespace r2 {
using namespace native;
struct FileImage { std::uint64_t id; std::vector<std::uint8_t> content; };
struct OpenImage { std::uint64_t id, file, position; DWORD access, share, flags; };
struct HandleImage { std::uint64_t logical, instance; };
struct FileSnapshot {
    std::vector<FileImage> files;
    std::vector<OpenImage> instances;
    std::vector<HandleImage> handles;
    std::uint64_t next_handle = 1, next_instance = 1;
    bool supported = true;
};

inline std::uint64_t file_position(HANDLE file, std::int64_t offset = 0, DWORD method = FILE_CURRENT) {
    LARGE_INTEGER delta{}, result{};
    delta.QuadPart = offset;
    checked(SetFilePointerEx(file, delta, &result, method), "SetFilePointerEx");
    return static_cast<std::uint64_t>(result.QuadPart);
}
inline Handle open_native(const std::filesystem::path& path, DWORD disposition = OPEN_EXISTING,
    DWORD access = GENERIC_READ | GENERIC_WRITE, DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE, DWORD flags = FILE_ATTRIBUTE_NORMAL) {
    Handle file(CreateFileW(path.c_str(), access, share, nullptr, disposition, flags, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) fail("CreateFileW", GetLastError(), path.string());
    SetLastError(ERROR_SUCCESS);
    const auto type = GetFileType(file.value);
    if (type != FILE_TYPE_DISK) fail("GetFileType", type == FILE_TYPE_UNKNOWN ? GetLastError() : ERROR_NOT_SUPPORTED, "only ordinary disk files", 3);
    BY_HANDLE_FILE_INFORMATION info{};
    checked(GetFileInformationByHandle(file.value, &info), "GetFileInformationByHandle");
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        fail("resource_policy", ERROR_NOT_SUPPORTED, "directory/reparse point not supported", 3);
    return file;
}
inline Handle duplicate_native(HANDLE original) {
    Handle copy;
    checked(DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copy.value,
        0, FALSE, DUPLICATE_SAME_ACCESS), "DuplicateHandle");
    return copy;
}
inline void write_all(HANDLE file, const std::vector<std::uint8_t>& bytes) {
    DWORD actual = 0;
    checked(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr), "WriteFile");
    require(actual == bytes.size(), "partial synchronous file write");
    checked(FlushFileBuffers(file), "FlushFileBuffers");
}
inline std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    auto file = open_native(path, OPEN_EXISTING, GENERIC_READ);
    LARGE_INTEGER size{};
    checked(GetFileSizeEx(file.value, &size), "GetFileSizeEx");
    require(size.QuadPart >= 0 && size.QuadPart <= 4096, "experimental file size limit");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD actual = 0;
    checked(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &actual, nullptr), "ReadFile.inspect");
    require(actual == bytes.size(), "partial inspection read");
    return bytes;
}
inline std::string files_fingerprint(const FileSnapshot& snapshot) {
    std::vector<std::uint8_t> bytes;
    auto number = [&](std::uint64_t n) {
        for (int i = 0; i < 8; ++i) { bytes.push_back(static_cast<std::uint8_t>(n & 255)); n >>= 8; }
    };
    number(snapshot.files.size()); number(snapshot.instances.size()); number(snapshot.handles.size());
    for (const auto& file : snapshot.files) {
        number(file.id); number(file.content.size()); bytes.insert(bytes.end(), file.content.begin(), file.content.end());
    }
    for (const auto& open : snapshot.instances) {
        number(open.id); number(open.file); number(open.position); number(open.access); number(open.share); number(open.flags);
    }
    for (const auto& handle : snapshot.handles) { number(handle.logical); number(handle.instance); }
    number(snapshot.next_handle); number(snapshot.next_instance); number(snapshot.supported);
    return digest_text(digest(bytes));
}

// Runtime-owned native handles. The fixture contains ONLY logical handle IDs.
// Paths identify file versions; instance IDs identify distinct CreateFile opens.
class Files {
    struct Reference { std::uint64_t instance; Handle native; };
    std::filesystem::path root_;
    std::map<std::uint64_t, std::filesystem::path> paths_;
    std::map<std::uint64_t, OpenImage> instances_;
    std::map<std::uint64_t, Reference> handles_;
    std::uint64_t next_handle_ = 1, next_instance_ = 1;
    std::string role_;
    std::filesystem::path path(std::uint64_t id) const {
        const auto found = paths_.find(id);
        if (found == paths_.end()) fail("file_catalog", ERROR_FILE_NOT_FOUND, "unknown managed file ID", 3);
        return found->second;
    }
    Reference& reference(std::uint64_t id) {
        const auto found = handles_.find(id);
        if (found == handles_.end()) fail("logical_handle", ERROR_INVALID_HANDLE, "unknown/closed managed handle", 3);
        return found->second;
    }
    void create_file(std::uint64_t id, const std::vector<std::uint8_t>& bytes) {
        const auto target = root_ / ("file-" + std::to_string(id) + ".bin");
        auto file = open_native(target, CREATE_NEW);
        paths_.emplace(id, target); // track immediately so rollback owns the file
        write_all(file.value, bytes);
        std::cout << "FILE_COPY role=" << role_ << " id=" << id << " size=" << bytes.size()
            << " sha256=" << digest_text(digest(bytes)) << '\n';
    }
public:
    Files(const std::filesystem::path& root, const std::string& role) : root_(root), role_(role) {
        require(!std::filesystem::exists(root_), "file resource directory must be fresh");
        std::filesystem::create_directories(root_);
    }
    void seed() { create_file(0, {'0','1','2','3','4','5','6','7','8','9'}); }
    std::size_t handle_count() const { return handles_.size(); }
    void close_all() { handles_.clear(); instances_.clear(); }
    void rollback() {
        close_all();
        for (const auto& [id, target] : paths_) {
            (void)id;
            // Exact files created by this Files instance; never recursive delete.
            checked(DeleteFileW(target.c_str()), "DeleteFileW.rollback");
        }
        paths_.clear();
        require(std::filesystem::is_empty(root_), "partial branch directory not empty");
        std::cout << "FILE_ROLLBACK role=" << role_ << " handles=0 files=0\n";
    }
    std::vector<std::uint8_t> content(std::uint64_t id = 0) const {
        phase = role_ + "/inspect_file";
        return read_all(path(id));
    }
    std::uint64_t identity(std::uint64_t logical) {
        BY_HANDLE_FILE_INFORMATION info{};
        checked(GetFileInformationByHandle(reference(logical).native.value, &info), "GetFileInformationByHandle.identity");
        return (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    }
    void service(Request& request) {
        const auto op = static_cast<Operation>(request.op);
        request.response = request.error = 0;
        if (op == Operation::open) {
            // Managed catalog IDs only. No arbitrary paths, async, mappings,
            // locks, devices or unknown flags are accepted at this boundary.
            if (request.length != 0) fail("resource_policy", ERROR_NOT_SUPPORTED, "unsupported open flags", 3);
            auto native = open_native(path(request.argument));
            const auto instance = next_instance_++;
            instances_.emplace(instance, OpenImage{instance, request.argument, 0,
                GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_ATTRIBUTE_NORMAL});
            const auto logical = next_handle_++;
            handles_.emplace(logical, Reference{instance, std::move(native)});
            request.response = logical;
        } else if (op == Operation::duplicate) {
            auto& original = reference(request.handle);
            auto duplicate = duplicate_native(original.native.value);
            const auto logical = next_handle_++;
            handles_.emplace(logical, Reference{original.instance, std::move(duplicate)});
            request.response = logical;
        } else if (op == Operation::close) {
            const auto instance = reference(request.handle).instance;
            auto& native = reference(request.handle).native;
            checked(CloseHandle(native.value), "CloseHandle.file_alias");
            native.value = nullptr;
            handles_.erase(request.handle);
            if (std::none_of(handles_.begin(), handles_.end(), [&](const auto& pair) { return pair.second.instance == instance; }))
                instances_.erase(instance);
        } else if (op == Operation::read || op == Operation::write) {
            require(request.length <= sizeof(request.bytes), "service transfer buffer exceeded");
            auto& native = reference(request.handle).native;
            DWORD actual = 0;
            if (op == Operation::read) {
                std::memset(request.bytes, 0, sizeof(request.bytes));
                checked(ReadFile(native.value, request.bytes, static_cast<DWORD>(request.length), &actual, nullptr), "ReadFile.service");
            } else {
                const auto position = file_position(native.value);
                require(position <= 4096 && request.length <= 4096 - position, "experimental file write limit");
                checked(WriteFile(native.value, request.bytes, static_cast<DWORD>(request.length), &actual, nullptr), "WriteFile.service");
                checked(FlushFileBuffers(native.value), "FlushFileBuffers.service");
            }
            request.response = actual;
        } else if (op == Operation::seek || op == Operation::tell) {
            if (op == Operation::seek) require(request.argument <= 4096, "experimental seek limit");
            request.response = file_position(reference(request.handle).native.value,
                op == Operation::seek ? static_cast<std::int64_t>(request.argument) : 0, op == Operation::seek ? FILE_BEGIN : FILE_CURRENT);
        } else {
            std::cout << "UNSUPPORTED_MANAGED_REQUEST role=" << role_ << " op=" << request.op << '\n';
            fail("resource_policy", ERROR_NOT_SUPPORTED, "async/mapping/lock/device/unknown managed resource rejected", 3);
        }
        std::cout << "FILE_SERVICE role=" << role_ << " op=" << request.op << " logical=" << request.handle
            << " response=" << request.response << " error=" << request.error << '\n';
    }
    FileSnapshot capture() {
        FileSnapshot snapshot;
        snapshot.next_handle = next_handle_; snapshot.next_instance = next_instance_;
        for (const auto& [id, target] : paths_) snapshot.files.push_back({id, read_all(target)});
        for (const auto& [id, open] : instances_) {
            auto image = open;
            const auto first = std::find_if(handles_.begin(), handles_.end(), [&](const auto& pair) { return pair.second.instance == id; });
            require(first != handles_.end(), "open instance without live reference");
            image.position = file_position(first->second.native.value);
            snapshot.instances.push_back(image);
        }
        for (const auto& [logical, ref] : handles_) snapshot.handles.push_back({logical, ref.instance});
        std::cout << "FILE_SNAPSHOT files=" << snapshot.files.size() << " instances=" << snapshot.instances.size()
            << " handles=" << snapshot.handles.size() << " sha256=" << files_fingerprint(snapshot) << '\n';
        for (const auto& open : snapshot.instances)
            std::cout << "OPEN_INSTANCE id=" << open.id << " file=" << open.file << " position=" << open.position << '\n';
        for (const auto& handle : snapshot.handles)
            std::cout << "HANDLE_RELATION logical=" << handle.logical << " instance=" << handle.instance << '\n';
        return snapshot;
    }
    void restore(const FileSnapshot& snapshot, const std::string& fault = "", Files* other = nullptr) {
        phase = role_ + "/restore_files_preflight";
        if (!snapshot.supported) fail("resource_policy", ERROR_NOT_SUPPORTED, "unsupported snapshot resource", 3);
        std::set<std::uint64_t> file_ids, instance_ids, handle_ids;
        for (const auto& file : snapshot.files)
            require(file.content.size() <= 4096 && file_ids.insert(file.id).second, "invalid file image");
        for (const auto& open : snapshot.instances) {
            if (open.access != (GENERIC_READ | GENERIC_WRITE) || open.share != (FILE_SHARE_READ | FILE_SHARE_WRITE) ||
                open.flags != FILE_ATTRIBUTE_NORMAL) fail("resource_policy", ERROR_NOT_SUPPORTED, "unsupported open description", 3);
            require(file_ids.contains(open.file) && instance_ids.insert(open.id).second && open.position <= 4096,
                "invalid open instance");
        }
        for (const auto& handle : snapshot.handles)
            require(instance_ids.contains(handle.instance) && handle_ids.insert(handle.logical).second && handle.logical < snapshot.next_handle,
                "invalid handle relation");
        phase = role_ + "/restore_files_copy";
        for (const auto& file : snapshot.files) {
            const auto bytes = fault == "omit-content" ? std::vector<std::uint8_t>(file.content.size()) : file.content;
            create_file(file.id, bytes);
        }
        std::map<std::uint64_t, Handle> opens;
        for (const auto& open : snapshot.instances) {
            Handle native;
            if (fault == "shared-open") {
                require(other != nullptr, "shared-open injection requires A");
                const auto first = std::find_if(snapshot.handles.begin(), snapshot.handles.end(), [&](const auto& h) { return h.instance == open.id; });
                require(first != snapshot.handles.end(), "injection instance has no handle");
                native = duplicate_native(other->reference(first->logical).native.value);
            } else {
                native = open_native(path(open.file), OPEN_EXISTING, open.access, open.share, open.flags);
                if (fault != "omit-position") file_position(native.value, static_cast<std::int64_t>(open.position), FILE_BEGIN);
            }
            opens.emplace(open.id, std::move(native));
            instances_.emplace(open.id, open);
            if (fault == "partial-create") {
                std::cout << "INJECTED_PARTIAL_CREATE role=" << role_ << " copied_files=" << paths_.size() << " native_opens=" << opens.size() << '\n';
                fail("injected_create_failure", ERROR_GEN_FAILURE, "failure after file copy and native open, before publication", 3);
            }
        }
        for (const auto& image : snapshot.handles) {
            Handle native;
            if (fault == "split-alias") {
                const auto open = instances_.at(image.instance);
                native = open_native(path(open.file));
                file_position(native.value, static_cast<std::int64_t>(open.position), FILE_BEGIN);
            } else native = duplicate_native(opens.at(image.instance).value);
            handles_.emplace(image.logical, Reference{image.instance, std::move(native)});
        }
        next_handle_ = snapshot.next_handle; next_instance_ = snapshot.next_instance;
        std::cout << "FILE_GRAPH_RESTORED role=" << role_ << " instances=" << instances_.size() << " handles=" << handles_.size()
            << " fault=" << (fault.empty() ? "none" : fault) << '\n';
    }
};
}
