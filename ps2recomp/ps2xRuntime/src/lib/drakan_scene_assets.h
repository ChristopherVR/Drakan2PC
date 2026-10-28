#pragma once
#include "drakan_scene_models.h"
#include <hle/IsoFs.h>

namespace drakan_scene_models {
// Read-only reflection material source. Never invokes native resource loading
// or changes the game's visibility, allocation or resource request order.
class Assets {
    const hle::IsoFs* fs = nullptr;
    struct File {
        std::vector<uint8_t> bytes;
        std::map<uint16_t, std::pair<uint32_t, uint32_t>> entries;
    };
    std::map<std::string, File> files;
    std::map<std::pair<std::string, uint32_t>, std::shared_ptr<const gsr::SceneModelTexture>> cache;

public:
    void clear() {
        files.clear();
        cache.clear();
    }
    void source(const hle::IsoFs* value) {
        fs = value;
        clear();
    }
    static std::shared_ptr<const gsr::SceneModelTexture> decode(uint32_t id, const uint8_t* b, size_t size) {
        auto read = [&](size_t p) {
            uint32_t v = 0;
            if (p <= size && size - p >= 4)
                std::memcpy(&v, b + p, 4);
            return v;
        };
        if (size < 64)
            return {};
        const auto w = read(0), h = read(4), stride = read(8), bits = read(12);
        if (!w || !h || w > 2048 || h > 2048 || (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
            stride < uint64_t(w) * (bits / 8))
            return {};
        size_t pixels = 64, palette = 0;
        uint32_t kind = 0, count = 0;
        if (bits == 8) {
            kind = read(64);
            count = read(68);
            palette = 80;
            if ((kind != 1 && kind != 2) || !count || count > 256)
                return {};
            pixels = palette + count * (kind == 1 ? 4 : 2);
        }
        if (pixels > size || uint64_t(stride) * h > size - pixels)
            return {};
        auto t = std::make_shared<gsr::SceneModelTexture>();
        t->nativeId = id;
        t->pixels.resize(128 * 128);
        // 1bf838 maps TXD's blend selector and feature flags to native flags.
        const auto flags = read(36), blend = read(16);
        t->flags = 1 | ((flags & 255) << 8);
        if (flags & 0x200) {
            if (blend == 1)
                t->flags |= 0x10000;
            else if (blend == 2)
                t->flags |= 0x20000;
            else if (blend == 4)
                t->flags |= 0x40000;
            else if (blend == 7 || blend == 8)
                t->flags |= 0x80000;
        } else if (flags & 0x100)
            t->flags |= 0x10;
        const int32_t keyIndex = int32_t(read(20));
        if (keyIndex >= 0)
            t->flags |= 8;
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 128; ++x) {
                size_t a = pixels + std::min(h - 1, y * h / 128) * stride + std::min(w - 1, x * w / 128) * (bits / 8);
                uint32_t c = 0;
                if (bits == 8) {
                    const auto index = b[a];
                    if (index >= count)
                        return {};
                    a = palette + index * (kind == 1 ? 4 : 2);
                    if (kind == 1)
                        c = uint32_t(b[a + 2]) | uint32_t(b[a + 1]) << 8 | uint32_t(b[a]) << 16 |
                            uint32_t(b[a + 3]) << 24;
                    else {
                        uint16_t v;
                        std::memcpy(&v, b + a, 2);
                        c = ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) |
                            uint32_t(v & 0x8000 ? 128 : 0) << 24;
                    }
                    if (int(index) == keyIndex)
                        c &= 0xffffff;
                } else if (bits == 16) {
                    uint16_t v;
                    std::memcpy(&v, b + a, 2);
                    c = ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) |
                        uint32_t(v & 0x8000 ? 128 : 0) << 24;
                } else {
                    c = b[a] | uint32_t(b[a + 1]) << 8 | uint32_t(b[a + 2]) << 16 |
                        uint32_t(bits == 32 ? b[a + 3] : 128) << 24;
                }
                t->pixels[y * 128 + x] = c;
            }
        t->identity = hashBytes(14695981039346656037ull, t->pixels.data(), t->pixels.size() * 4);
        return t;
    }
    std::shared_ptr<const gsr::SceneModelTexture> load(uint32_t id, const std::string& path) {
        const auto key = std::make_pair(path, id);
        if (auto it = cache.find(key); it != cache.end())
            return it->second;
        if (!fs)
            return {};
        auto [it, fresh] = files.try_emplace(path);
        auto& f = it->second;
        if (fresh) {
            hle::IsoFs::Node node;
            if (fs->lookup(path, node) && !node.dir && node.size <= 64 * 1024 * 1024)
                f.bytes = fs->readAll(node);
            if (f.bytes.size() >= 16 && std::memcmp(f.bytes.data(), "SRSC", 4) == 0) {
                uint32_t index, count;
                std::memcpy(&index, f.bytes.data() + 6, 4);
                std::memcpy(&count, f.bytes.data() + 10, 4);
                if (index <= f.bytes.size() && uint64_t(count) * 14 <= f.bytes.size() - index)
                    for (unsigned i = 0; i < count; ++i) {
                        const auto p = f.bytes.data() + index + i * 14;
                        uint16_t type, rid, sub;
                        uint32_t off, n;
                        std::memcpy(&type, p, 2);
                        std::memcpy(&rid, p + 2, 2);
                        std::memcpy(&sub, p + 4, 2);
                        std::memcpy(&off, p + 6, 4);
                        std::memcpy(&n, p + 10, 4);
                        if (type == 0x40 && sub == 0 && off <= f.bytes.size() && n <= f.bytes.size() - off)
                            f.entries.try_emplace(rid, off, n);
                    }
            }
        }
        std::shared_ptr<const gsr::SceneModelTexture> result;
        if (auto e = f.entries.find(uint16_t(id)); e != f.entries.end())
            result = decode(id, f.bytes.data() + e->second.first, e->second.second);
        cache[key] = result;
        return result;
    }
};
} // namespace drakan_scene_models
