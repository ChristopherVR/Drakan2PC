// Compiled shader blob cache (portable core). Three levels, all thread-safe:
//   1. process memory: a blob compiled once (by the start-up prewarm thread or the first user) is reused;
//   2. a directory of files "<key>.bin" (optional): later launches skip the HLSL compiler entirely;
//   3. the producer (the real compiler), run once per key even when several threads ask at the same time.
// The cache only stores bytes; it knows nothing about Direct3D (see shader_compile_d3d.h).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gsr::shader_cache {

struct Macro {
    const char* name;
    const char* value;
};

struct Request {
    const char* source = nullptr;
    size_t length = 0;
    const char* name = nullptr;   // source name (debug info only, still part of the key)
    const char* entry = nullptr;  // entry point
    const char* target = nullptr; // profile, e.g. ps_5_0
    const Macro* macros = nullptr;
    size_t macroCount = 0;
    uint32_t flags = 0;
};

// Stable 64-bit key of everything that changes the compiled bytes (FNV-1a, format version included).
uint64_t key(const Request& request);

// File image: magic, key, payload size and checksum, payload. decode() rejects a wrong key, a truncated file or a
// payload that does not match its checksum (a half-written file from a crash is treated as a miss).
std::vector<uint8_t> encode(uint64_t key, const std::vector<uint8_t>& blob);
bool decode(uint64_t key, const std::vector<uint8_t>& file, std::vector<uint8_t>& blob);

enum class Source { Memory, Disk, Produced, Failed };

struct Stats {
    uint64_t memoryHits = 0, diskHits = 0, produced = 0, failed = 0, waits = 0;
    double produceMs = 0; // time spent inside producers
};

using Producer = std::function<bool(std::vector<uint8_t>&)>;

class Cache {
public:
    Cache();
    ~Cache();
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // "" turns the disk level off. Created on first store.
    void setDirectory(std::string directory);
    std::string directory() const;
    // Memory, then disk, then produce (once per key; concurrent callers of a key in flight wait for it).
    Source get(uint64_t key, const Producer& produce, std::vector<uint8_t>& out);
    bool contains(uint64_t key) const; // memory level only
    Stats stats() const;
    void clearMemory();

private:
    struct Impl;
    Impl* impl;
};

Cache& global();

// UTF-8 text <-> path (std::filesystem's u8 spellings differ between C++17 and C++20).
inline std::string utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
inline std::filesystem::path pathFromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
    return std::filesystem::u8path(s);
#endif
}

} // namespace gsr::shader_cache
