#include "shader_cache.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <system_error>
#include <unordered_map>

namespace gsr::shader_cache {
namespace {

constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull, kFnvPrime = 0x100000001b3ull;
constexpr uint32_t kFormatVersion = 1;
constexpr char kMagic[8] = {'G', 'S', 'R', 'S', 'H', 'C', '0', '1'};
constexpr size_t kHeader = 8 + 8 + 4 + 4;

struct Fnv {
    uint64_t h = kFnvOffset;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i)
            h = (h ^ b[i]) * kFnvPrime;
    }
    // Strings are terminated so that ("ab","c") and ("a","bc") differ; a null pointer differs from "".
    void str(const char* s) {
        const uint8_t tag = s ? 1 : 0;
        bytes(&tag, 1);
        if (s)
            bytes(s, std::strlen(s) + 1);
    }
    void u64(uint64_t v) { bytes(&v, sizeof v); }
};

uint32_t checksum(const uint8_t* p, size_t n) {
    Fnv f;
    f.bytes(p, n);
    return static_cast<uint32_t>(f.h ^ (f.h >> 32));
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i)
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
uint64_t get(const uint8_t* p, int n) {
    uint64_t x = 0;
    for (int i = 0; i < n; ++i)
        x |= static_cast<uint64_t>(p[i]) << (8 * i);
    return x;
}

} // namespace

uint64_t key(const Request& r) {
    Fnv f;
    f.u64(kFormatVersion);
    f.u64(r.length);
    if (r.source && r.length)
        f.bytes(r.source, r.length);
    f.str(r.name);
    f.str(r.entry);
    f.str(r.target);
    f.u64(r.macroCount);
    for (size_t i = 0; i < r.macroCount; ++i) {
        f.str(r.macros[i].name);
        f.str(r.macros[i].value);
    }
    f.u64(r.flags);
    return f.h;
}

std::vector<uint8_t> encode(uint64_t k, const std::vector<uint8_t>& blob) {
    std::vector<uint8_t> out;
    out.reserve(kHeader + blob.size());
    out.insert(out.end(), kMagic, kMagic + 8);
    put64(out, k);
    put32(out, static_cast<uint32_t>(blob.size()));
    put32(out, checksum(blob.data(), blob.size()));
    out.insert(out.end(), blob.begin(), blob.end());
    return out;
}

bool decode(uint64_t k, const std::vector<uint8_t>& file, std::vector<uint8_t>& blob) {
    if (file.size() < kHeader || std::memcmp(file.data(), kMagic, 8) != 0 || get(file.data() + 8, 8) != k)
        return false;
    const uint64_t size = get(file.data() + 16, 4);
    if (size == 0 || file.size() != kHeader + size)
        return false;
    if (checksum(file.data() + kHeader, size) != static_cast<uint32_t>(get(file.data() + 20, 4)))
        return false;
    blob.assign(file.begin() + kHeader, file.end());
    return true;
}

struct Cache::Impl {
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::unordered_map<uint64_t, std::vector<uint8_t>> blobs;
    std::set<uint64_t> inFlight;
    std::string dir;
    Stats stats;

    bool load(const std::string& d, uint64_t k, std::vector<uint8_t>& out) const {
        if (d.empty())
            return false;
        char name[32];
        std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(k));
        std::ifstream in(pathFromUtf8(d) / name, std::ios::binary);
        if (!in)
            return false;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return decode(k, bytes, out);
    }
    void store(const std::string& d, uint64_t k, const std::vector<uint8_t>& blob) const {
        if (d.empty())
            return;
        std::error_code ec;
        const auto folder = pathFromUtf8(d);
        std::filesystem::create_directories(folder, ec);
        char name[40];
        std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(k));
        // Write a temporary file, then rename: a reader never sees a partial file under the final name.
        char temp[48];
        std::snprintf(temp, sizeof temp, "%016llx.tmp", static_cast<unsigned long long>(k));
        {
            std::ofstream out(folder / temp, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            const auto bytes = encode(k, blob);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out)
                return;
        }
        std::filesystem::rename(folder / temp, folder / name, ec);
        if (ec)
            std::filesystem::remove(folder / temp, ec);
    }
};

Cache::Cache() : impl(new Impl) {}
Cache::~Cache() {
    delete impl;
}

void Cache::setDirectory(std::string directory) {
    std::lock_guard lock(impl->mutex);
    impl->dir = std::move(directory);
}
std::string Cache::directory() const {
    std::lock_guard lock(impl->mutex);
    return impl->dir;
}

Source Cache::get(uint64_t k, const Producer& produce, std::vector<uint8_t>& out) {
    std::string dir;
    {
        std::unique_lock lock(impl->mutex);
        bool waited = false;
        for (;;) {
            auto it = impl->blobs.find(k);
            if (it != impl->blobs.end()) {
                out = it->second;
                ++impl->stats.memoryHits;
                if (waited)
                    ++impl->stats.waits;
                return Source::Memory;
            }
            if (!impl->inFlight.count(k))
                break;
            waited = true;
            impl->ready.wait(lock);
        }
        impl->inFlight.insert(k);
        dir = impl->dir;
    }
    std::vector<uint8_t> blob;
    Source source = Source::Disk;
    double ms = 0;
    if (!impl->load(dir, k, blob)) {
        source = Source::Produced;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = produce && produce(blob) && !blob.empty();
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!ok) {
            blob.clear();
            source = Source::Failed;
        } else {
            impl->store(dir, k, blob);
        }
    }
    {
        std::lock_guard lock(impl->mutex);
        impl->inFlight.erase(k);
        impl->stats.produceMs += ms;
        if (source == Source::Failed) {
            ++impl->stats.failed; // not cached: a later caller retries (and reports the compiler error itself)
        } else {
            ++(source == Source::Disk ? impl->stats.diskHits : impl->stats.produced);
            impl->blobs[k] = blob;
        }
    }
    impl->ready.notify_all();
    out = std::move(blob);
    return source;
}

bool Cache::contains(uint64_t k) const {
    std::lock_guard lock(impl->mutex);
    return impl->blobs.count(k) != 0;
}

Stats Cache::stats() const {
    std::lock_guard lock(impl->mutex);
    return impl->stats;
}

void Cache::clearMemory() {
    std::lock_guard lock(impl->mutex);
    impl->blobs.clear();
}

Cache& global() {
    static Cache cache;
    return cache;
}

} // namespace gsr::shader_cache
