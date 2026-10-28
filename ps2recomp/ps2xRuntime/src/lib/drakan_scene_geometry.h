#pragma once
#include <gsr/scene_geometry.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace drakan_scene_geometry {
// Layout follows 152330's layer iteration and 158148/161a78/1ef1c8's
// grid inputs. The 0x400000 flag stores ONE height, not an entire array.
inline gsr::SceneGeometry capture(const uint8_t* ram, uint32_t bytes, uint64_t previous = 0) {
    gsr::SceneGeometry out;
    auto span = [&](uint32_t p, uint64_t n) { return p >= 0x100000u && n <= bytes && p <= bytes - n; };
    auto rd = [&](uint32_t p) {
        uint32_t v;
        std::memcpy(&v, ram + p, 4);
        return v;
    };
    auto rf = [&](uint32_t p) {
        float v;
        std::memcpy(&v, ram + p, 4);
        return v;
    };
    if (!ram || bytes < 0x4dc124)
        return out;
    const uint32_t base = rd(0x4dc118), count = rd(0x4dc120);
    if (!count || count > 4096 || !span(base, uint64_t(count) * 0x164))
        return out;
    uint64_t hash = 14695981039346656037ull;
    auto mix = [&](const void* p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            hash ^= static_cast<const uint8_t*>(p)[i];
            hash *= 1099511628211ull;
        }
    };
    mix(&base, 4);
    mix(&count, 4);
    std::vector<uint32_t> accepted;
    uint64_t triangleCount = 0;
    for (uint32_t layer = 0; layer < count; ++layer) {
        const uint32_t a = base + layer * 0x164, type = rd(a + 0x4c), flags = rd(a + 0x50);
        const uint32_t w = rd(a + 0x1c), h = rd(a + 0x20), heights = rd(a + 0x28), colors = rd(a + 0x40),
                       quads = rd(a + 0x24);
        const uint64_t vertices = uint64_t(w + 1) * (h + 1);
        const uint32_t heightCount = (flags & 0x400000) ? 1u : uint32_t(vertices);
        if (type > 2 || (flags & 1) || !w || !h || w > 512 || h > 512 || vertices > 300000 ||
            !span(heights, uint64_t(heightCount) * 2) || !span(colors, vertices * 4) ||
            !span(quads, uint64_t(w) * h * 4))
            continue;
        const float origin[3] = {rf(a + 8), rf(a + 12), rf(a + 16)};
        if (!std::all_of(origin, origin + 3, [](float v) { return std::isfinite(v) && std::abs(v) < 1e8f; }))
            continue;
        if (triangleCount + uint64_t(w) * h * 2 > 450000)
            break;
        triangleCount += uint64_t(w) * h * 2;
        const uint32_t layoutFlags = flags & 0x1c00001;
        mix(origin, sizeof origin);
        mix(&layoutFlags, 4);
        mix(&type, 4);
        mix(&w, 4);
        mix(&h, 4);
        mix(ram + heights, heightCount * 2);
        mix(ram + quads, size_t(w) * h * 4);
        ++out.layers;
        out.waterLayers += type == 2;
        accepted.push_back(layer);
    }
    out.identity = hash;
    if (previous == hash)
        return out;
    out.triangles.reserve(size_t(triangleCount));
    for (auto layer : accepted) {
        const uint32_t a = base + layer * 0x164, type = rd(a + 0x4c), flags = rd(a + 0x50);
        const uint32_t w = rd(a + 0x1c), h = rd(a + 0x20), heights = rd(a + 0x28), colors = rd(a + 0x40),
                       quads = rd(a + 0x24);
        const float origin[3] = {rf(a + 8), rf(a + 12), rf(a + 16)};
        auto vertex = [&](gsr::SceneTriangle& tri, unsigned j, unsigned x, unsigned z) {
            const uint32_t i = z * (w + 1) + x;
            uint16_t height;
            std::memcpy(&height, ram + heights + ((flags & 0x400000) ? 0 : i * 2), 2);
            tri.position[j][0] = origin[0] + x * 2048.f;
            tri.position[j][1] = origin[1] + int(height) * 2 - 65536.f;
            tri.position[j][2] = origin[2] + z * 2048.f;
            for (unsigned c = 0; c < 3; ++c)
                tri.color[j][c] = ram[colors + i * 4 + c] / 128.f;
        };
        for (unsigned z = 0; z < h; ++z)
            for (unsigned x = 0; x < w; ++x) {
                uint16_t q;
                std::memcpy(&q, ram + quads + (z * w + x) * 4, 2);
                // Native 15ff38 rejects a quad only when BOTH 0x50/0xa0 masks
                // reject it. Water strips can still cover both halves of a
                // partially flagged shoreline cell; current GS draws own visibility.
                gsr::SceneTriangle t;
                t.layer = layer;
                t.water = type == 2;
                // Quad bit 0 CLEARED splits along (x+1,z)-(x,z+1), set along (x,z)-(x+1,z+1). Measured against
                // the GS-drawn terrain (2026-10-24 scene dumps: every full-detail sample on either flag value
                // lies on this diagonal). The opposite split left the resident surface up to 0.5 m off the
                // drawn one, so ray receivers only matched it along shared quad edges (thin shadow lines).
                if (!(q & 1)) {
                    vertex(t, 0, x, z);
                    vertex(t, 1, x, z + 1);
                    vertex(t, 2, x + 1, z);
                } else {
                    vertex(t, 0, x, z);
                    vertex(t, 1, x, z + 1);
                    vertex(t, 2, x + 1, z + 1);
                }
                if (!(q & 0x50) || (type == 2 && !(q & 0xa0)))
                    out.triangles.push_back(t);
                if (!(q & 1)) {
                    vertex(t, 0, x + 1, z);
                    vertex(t, 1, x, z + 1);
                    vertex(t, 2, x + 1, z + 1);
                } else {
                    vertex(t, 0, x, z);
                    vertex(t, 1, x + 1, z + 1);
                    vertex(t, 2, x + 1, z);
                }
                if (!(q & 0xa0) || (type == 2 && !(q & 0x50)))
                    out.triangles.push_back(t);
            }
    }
    out.identity = hash;
    return out;
}
} // namespace drakan_scene_geometry
