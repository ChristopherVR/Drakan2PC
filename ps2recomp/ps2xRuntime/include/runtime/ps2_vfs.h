#pragma once

#include "ps2x/iop/ps2_path.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class PS2RomDevice;

// fopen for a host path. On Windows path::string() throws for a name outside the ANSI code page (a play folder under a
// CJK user name), so the wide path is opened directly there.
inline std::FILE* ps2OpenHostFile(const std::filesystem::path& path, const char* mode) {
#if defined(_WIN32)
    std::wstring wideMode;
    for (const char* c = mode; *c != '\0'; ++c)
        wideMode.push_back(static_cast<wchar_t>(*c));
    return ::_wfopen(path.c_str(), wideMode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

// A host path as UTF-8 text (for logs, keys and names handed to the guest). Unlike path::string() it never throws and
// keeps every character on Windows.
inline std::string ps2PathUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// The inverse: UTF-8 text back to a path (std::filesystem::path(std::string) reads the ANSI code page on Windows).
inline std::filesystem::path ps2PathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

struct PS2VfsMounts {
    std::filesystem::path hostRoot;
    std::filesystem::path cdRoot;
    std::filesystem::path memoryCard0Root;
};

struct PS2VfsStat {
    bool directory = false;
    bool readOnly = false;
    uint64_t size = 0u;
    std::time_t created = 0;
    std::time_t accessed = 0;
    std::time_t modified = 0;
};

struct PS2VfsDescriptorInfo {
    int32_t descriptor = -1;
    std::string device;
    std::string path;
};

class IPS2OpenFile {
public:
    virtual ~IPS2OpenFile() = default;

    [[nodiscard]] virtual int64_t read(void* destination, size_t size) = 0;
    [[nodiscard]] virtual int64_t write(const void* source, size_t size) = 0;
    [[nodiscard]] virtual int64_t seek(int64_t offset, int whence) = 0;
};

class PS2Vfs {
public:
    PS2Vfs() = default;
    ~PS2Vfs();

    PS2Vfs(const PS2Vfs&) = delete;
    PS2Vfs& operator=(const PS2Vfs&) = delete;

    [[nodiscard]] int32_t open(std::string_view path, uint32_t flags, const PS2VfsMounts& mounts,
                               const PS2RomDevice& rom);
    [[nodiscard]] int32_t close(int32_t descriptor);
    [[nodiscard]] int64_t read(int32_t descriptor, void* destination, size_t size);
    [[nodiscard]] int64_t write(int32_t descriptor, const void* source, size_t size);
    [[nodiscard]] int64_t seek(int32_t descriptor, int64_t offset, int whence);

    [[nodiscard]] bool stat(std::string_view path, const PS2VfsMounts& mounts, const PS2RomDevice& rom,
                            PS2VfsStat& result) const;
    [[nodiscard]] bool resolveHostPath(std::string_view path, const PS2VfsMounts& mounts,
                                       std::filesystem::path& result) const;
    [[nodiscard]] std::vector<PS2VfsDescriptorInfo> descriptors() const;

private:
    struct OpenDescriptor {
        std::unique_ptr<IPS2OpenFile> file;
        std::string device;
        std::string path;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<int32_t, OpenDescriptor> m_descriptors;
    int32_t m_nextDescriptor = 3;
};
