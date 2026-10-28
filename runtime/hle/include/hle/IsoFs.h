// ISO9660 reader (or extracted-folder backend) + RSRCIOP path resolution.
#pragma once
#include "Interfaces.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hle {

// Random-access disc bytes. Implementations own their file/cache and return
// false on a short read. IsoFs serializes calls; no host handles enter the parser.
class IsoSource {
public:
    virtual ~IsoSource() = default;
    virtual bool readAt(u64 offset, void* destination, size_t size) const = 0;
};
std::shared_ptr<const IsoSource> openIsoSource(const std::string& path, std::string* error);

// RsrcLoader's path-component mangler (identical to analysis/scripts/iop_mangle.py:mangle_component).
std::string mangleComponent(const std::string& s);
// Splits on '\' and '/', drops empty parts, mangles each, joins with '\'.
std::string manglePath(const std::string& p);

class IsoFs {
public:
    struct Node {
        bool dir = false;
        u32 lba = 0; // ISO backend
        u64 size = 0;
        std::string host; // folder backend: host path
        std::string key;  // canonical upper-case path, '\' separators, no leading '\'
    };

    IsoFs() = default;
    ~IsoFs();
    IsoFs(const IsoFs&) = delete;
    IsoFs& operator=(const IsoFs&) = delete;

    bool openIso(const std::string& isoPath, std::string* err = nullptr);
    bool openSource(std::shared_ptr<const IsoSource> source, std::string* err = nullptr);
    bool openFolder(const std::string& rootDir, std::string* err = nullptr);

    // Language root as handed to RSRCIOP in argv[2], e.g. "\\ENGLISH" (any case, slashes optional).
    void setLanguageRoot(const std::string& root);
    const std::string& languageRoot() const { return lang_; }

    // Resolve a game path (long names, '\' or '/', optional "cdrom0:" prefix and ";1" suffix,
    // optional language dir) to a canonical key. Does not touch the filesystem.
    std::string resolveKey(const std::string& gamePath) const;
    // resolveKey + lookup (falls back to the path without the language prefix).
    bool lookup(const std::string& gamePath, Node& out) const;
    bool findKey(const std::string& key, Node& out) const;

    // Read file bytes. Returns bytes read (clamped to file size).
    size_t read(const Node& n, u64 offset, void* dst, size_t len) const;
    std::vector<u8> readAll(const Node& n) const;

    // Keys of direct children of a directory key ("" = root).
    std::vector<std::string> listDir(const std::string& dirKey) const;
    const std::map<std::string, Node>& entries() const { return map_; }
    bool isIso() const { return source_ != nullptr; }

private:
    void walk(u32 lba, u32 size, const std::string& prefix, int depth);
    bool readRaw(u64 byteOff, void* dst, size_t n) const;
    std::shared_ptr<const IsoSource> source_;
    mutable std::mutex mu_;
    std::map<std::string, Node> map_;
    std::string lang_;
};

} // namespace hle
