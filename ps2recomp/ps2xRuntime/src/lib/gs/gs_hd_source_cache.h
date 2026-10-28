#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include "gsr/texture_pack.h"

namespace gs_cpu {
// The renderer owns this cache under its command mutex. Byte comparisons, not
// texture addresses alone, validate a decoded source after slot recycling.
struct SourceImage : gsr::PackImage {
    std::array<uint64_t, 2> hashes;
};
class HdSourceCache {
public:
    using Decoded = SourceImage;
    using Key = std::array<uint32_t, 12>;
    void recordLookup(uint64_t elapsed, uint64_t now) {
        lookupTime_ += elapsed;
        if (!lastReport_)
            lastReport_ = now;
        if (now - lastReport_ < 1000000000u)
            return;
        std::fprintf(stderr, "[texture-source-cache] hits=%llu misses=%llu lookupMs=%.3f bytes=%zu entries=%zu\n",
                     static_cast<unsigned long long>(hits_ - reportedHits_),
                     static_cast<unsigned long long>(misses_ - reportedMisses_), double(lookupTime_) / 1000000.0,
                     bytes_, entries_.size());
        reportedHits_ = hits_;
        reportedMisses_ = misses_;
        lookupTime_ = 0;
        lastReport_ = now;
    }
    ~HdSourceCache() {
        if (std::getenv("DRAKAN_HD_TEXTURE_LOG"))
            std::fprintf(stderr, "[texture-source-cache] hits=%llu misses=%llu bytes=%zu entries=%zu\n",
                         static_cast<unsigned long long>(hits_), static_cast<unsigned long long>(misses_), bytes_,
                         entries_.size());
    }
    template <class Decode>
    std::shared_ptr<const Decoded> getShared(const Key& key, const uint8_t* source, size_t length,
                                             const std::array<uint16_t, 512>& palette, Decode decode) {
        // GS upload slots cycle through many images each frame. Retain several
        // contents per descriptor instead of overwriting the previous image.
        // This sampled fingerprint rejects candidates cheaply; complete byte
        // and palette comparisons below still decide every hit.
        uint64_t fingerprint = 0;
        for (size_t sample = 0; sample < 16; ++sample) {
            uint64_t word;
            std::memcpy(&word, source + (length - sizeof(word)) * sample / 15, sizeof(word));
            fingerprint = (fingerprint ^ word) * 0x9e3779b97f4a7c15ull;
        }
        for (auto& entry : entries_)
            if (entry.key == key && entry.fingerprint == fingerprint && entry.bytes.size() == length &&
                entry.palette == palette && std::memcmp(entry.bytes.data(), source, length) == 0) {
                entry.used = ++clock_;
                ++hits_;
                return entry.decoded;
            }
        ++misses_;
        const size_t needed = length + size_t(key[3]) * key[4] * sizeof(uint32_t);
        while (!entries_.empty() && (bytes_ + needed > kBudget || entries_.size() >= 512)) {
            auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                           [](const auto& a, const auto& b) { return a.used < b.used; });
            bytes_ -= oldest->bytes.size() + oldest->decoded->pixels.size() * sizeof(uint32_t);
            entries_.erase(oldest);
        }
        entries_.push_back(
            {key, {source, source + length}, palette, std::make_shared<Decoded>(), ++clock_, fingerprint});
        auto& entry = entries_.back();
        entry.decoded->width = key[3];
        entry.decoded->height = key[4];
        entry.decoded->hashes = decode(entry.decoded->pixels);
        bytes_ += entry.bytes.size() + entry.decoded->pixels.size() * sizeof(uint32_t);
        return entry.decoded;
    }

    template <class Decode>
    const Decoded& get(const Key& key, const uint8_t* source, size_t length, const std::array<uint16_t, 512>& palette,
                       Decode decode) {
        return *getShared(key, source, length, palette, decode);
    }

private:
    struct Entry {
        Key key;
        std::vector<uint8_t> bytes;
        std::array<uint16_t, 512> palette;
        std::shared_ptr<Decoded> decoded;
        uint64_t used;
        uint64_t fingerprint;
    };
    static constexpr size_t kBudget = 64u * 1024u * 1024u;
    std::vector<Entry> entries_;
    uint64_t clock_ = 0;
    uint64_t hits_ = 0, misses_ = 0;
    uint64_t reportedHits_ = 0, reportedMisses_ = 0, lookupTime_ = 0, lastReport_ = 0;
    size_t bytes_ = 0;
};
} // namespace gs_cpu
