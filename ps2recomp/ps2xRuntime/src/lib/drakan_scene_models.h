#pragma once
#include <gsr/scene_models.h>
#include "runtime/object_motion_capture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <functional>
#include <string>

namespace drakan_scene_models {
inline bool enabled = false;
inline std::set<uint32_t> live;
inline std::map<uint32_t, uint64_t> generations;
inline uint64_t nextGeneration = 0;
inline void constructed(uint32_t p) {
    if (enabled) {
        p &= 0x1fffffffu;
        live.insert(p);
        generations[p] = ++nextGeneration;
    }
}
inline void destroyed(uint32_t p) {
    p &= 0x1fffffffu;
    live.erase(p);
    generations.erase(p);
}
struct Reader {
    const uint8_t* ram;
    uint32_t size;
    bool span(uint32_t p, uint64_t n) const { return ram && p >= 0x100000 && n <= size && p <= size - n; }
    uint32_t u(uint32_t p) const {
        uint32_t v;
        std::memcpy(&v, ram + p, 4);
        return v;
    }
    uint16_t h(uint32_t p) const {
        uint16_t v;
        std::memcpy(&v, ram + p, 2);
        return v;
    }
    float f(uint32_t p) const {
        float v;
        std::memcpy(&v, ram + p, 4);
        return v;
    }
    std::map<uint32_t, uint32_t> resources(uint32_t manager, uint32_t vt, uint32_t alternate = 0) const {
        std::map<uint32_t, uint32_t> out;
        if (!span(manager, 0x220))
            return out;
        std::set<uint32_t> seen;
        for (unsigned b = 0; b < 128; ++b) {
            uint32_t p = u(manager + 0x20 + b * 4);
            while (span(p, 0xc8) && seen.size() < 8192 && seen.insert(p).second) {
                if ((u(p + 0x18) == vt || (alternate && u(p + 0x18) == alternate)) && (u(p + 8) & 3) == 3)
                    out[u(p)] = p;
                p = u(p + 20);
            }
        }
        return out;
    }
};
// Interpolate the same split triangles and holes exported by native terrain.
// Selecting lighting from the cell's top-left height makes it pop off as the
// camera crosses a steep slope even while the actual ground stays below it.
inline bool layerGround(const Reader& r, uint32_t layer, float x, float z, float& ground) {
    const auto w = r.u(layer + 0x1c), h = r.u(layer + 0x20), flags = r.u(layer + 0x50);
    if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || z < 0 || x >= w || z >= h)
        return false;
    const auto ix = uint32_t(x), iz = uint32_t(z), quads = r.u(layer + 0x24), heights = r.u(layer + 0x28);
    const uint64_t qa = uint64_t(quads) + (uint64_t(iz) * w + ix) * 4;
    if (qa > UINT32_MAX || !r.span(uint32_t(qa), 2))
        return false;
    const auto q = r.h(uint32_t(qa));
    const float fx = x - ix, fz = z - iz;
    // Same diagonal as drakan_scene_geometry: bit 0 cleared splits along (x+1,z)-(x,z+1).
    const bool first = !(q & 1) ? fx + fz <= 1 : fx <= fz;
    if (q & (first ? 0x50 : 0xa0))
        return false;
    float y[4];
    const uint32_t indices[] = {iz * (w + 1) + ix, (iz + 1) * (w + 1) + ix, iz * (w + 1) + ix + 1,
                                (iz + 1) * (w + 1) + ix + 1};
    for (unsigned i = 0; i < 4; ++i) {
        const uint64_t at = uint64_t(heights) + ((flags & 0x400000) ? 0 : uint64_t(indices[i]) * 2);
        if (at > UINT32_MAX || !r.span(uint32_t(at), 2))
            return false;
        y[i] = r.h(uint32_t(at)) * 2.f - 65536.f;
    }
    const float v = !(q & 1) ? (first ? y[0] * (1 - fx - fz) + y[1] * fz + y[2] * fx
                                      : y[2] * (1 - fz) + y[1] * (1 - fx) + y[3] * (fx + fz - 1))
                             : (first ? y[0] * (1 - fz) + y[1] * (fz - fx) + y[3] * fx
                                      : y[0] * (1 - fx) + y[3] * fz + y[2] * (fx - fz));
    ground = r.f(layer + 12) + v;
    return std::isfinite(ground);
}
inline uint64_t hashBytes(uint64_t h, const void* p, size_t n) {
    return drakan_object_motion::captureHash(h, p, n);
}
inline uint64_t key(unsigned material, std::array<uint16_t, 3> v) {
    std::sort(v.begin(), v.end());
    return uint64_t(material) << 48 | uint64_t(v[2]) << 32 | uint64_t(v[1]) << 16 | v[0];
}
// Conservative fallback until the native sky owner is exported explicitly.
// Generated sky spheres are camera-centred backgrounds, not physical blockers.
// Require the complete large spherical shell; size or camera proximity alone
// would incorrectly discard scenery, weapons and other camera-linked models.
inline bool cameraBackdrop(const gsr::SceneModelMesh& mesh, const float* transform, const float* camera) {
    if (mesh.nativeId >> 16 || mesh.triangles.empty())
        return false;
    for (unsigned c = 0; c < 3; ++c)
        if (!std::isfinite(camera[c]) || !std::isfinite(transform[c * 4 + 3]) ||
            std::abs(transform[c * 4 + 3] - camera[c]) > .125f)
            return false;
    float minimum = INFINITY, maximum = 0, lo[3]{INFINITY, INFINITY, INFINITY}, hi[3]{-INFINITY, -INFINITY, -INFINITY};
    for (const auto& triangle : mesh.triangles)
        for (const auto& p : triangle.position) {
            float q[3];
            for (unsigned c = 0; c < 3; ++c) {
                q[c] = transform[c * 4] * p[0] + transform[c * 4 + 1] * p[1] + transform[c * 4 + 2] * p[2];
                if (!std::isfinite(q[c]))
                    return false;
                lo[c] = std::min(lo[c], q[c]);
                hi[c] = std::max(hi[c], q[c]);
            }
            const float radius = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
            if (!std::isfinite(radius))
                return false;
            minimum = std::min(minimum, radius);
            maximum = std::max(maximum, radius);
        }
    if (minimum < 8192 || maximum > minimum * 1.001f)
        return false;
    for (unsigned c = 0; c < 3; ++c)
        if (lo[c] > -.99f * maximum || hi[c] < .99f * maximum)
            return false;
    return true;
}
// Classification belongs to the lifetime of the object and its immutable mesh.
// The native sky transform can trail the current camera during movement. Once
// proven camera-centred, retain the classification until that identity or basis
// changes; never relax the initial geometric/camera test for ordinary scenery.
class BackdropTracker {
    struct Entry {
        uint64_t generation, mesh;
        std::array<float, 9> basis;
    };
    std::map<uint32_t, Entry> entries;

public:
    void clear() { entries.clear(); }
    bool exclude(const gsr::SceneModelMesh& mesh, const gsr::SceneModelInstance& instance, const float* camera,
                 bool eligible) {
        std::array<float, 9> basis{};
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c)
                basis[r * 3 + c] = instance.transform[r * 4 + c];
        auto found = entries.find(instance.object);
        if (eligible && found != entries.end() && found->second.generation == instance.generation &&
            found->second.mesh == mesh.identity && found->second.basis == basis)
            return true;
        entries.erase(instance.object);
        if (!eligible || !cameraBackdrop(mesh, instance.transform, camera))
            return false;
        entries.emplace(instance.object, Entry{instance.generation, mesh.identity, basis});
        return true;
    }
    void prune() {
        std::erase_if(entries, [](const auto& item) { return !live.contains(item.first); });
    }
};
// 12b640's resource buckets, 189b38/18bb90's model faces and 1ec160/1ec8f0's
// packed position selectors, zero-delimited strips and per-output-vertex UVs.
class Capture {
    BackdropTracker backdrops;
    uint64_t level = ~0ull;
    unsigned ticks = 0;
    std::vector<std::shared_ptr<const gsr::SceneModelTexture>> textures;
    std::map<uint32_t, uint32_t> textureIndices;
    std::unordered_map<uint64_t, std::shared_ptr<const gsr::SceneModelMesh>> meshes;
    std::shared_ptr<const gsr::SceneModelTexture> texture(const Reader& r, uint32_t id, uint32_t p) {
        const auto width = r.u(p + 0x24), height = r.u(p + 0x28), stride = r.u(p + 0x34), bits = r.u(p + 0x38),
                   source = r.u(p + 0x50), palette = r.u(p + 0x3c);
        if (!width || !height || width > 2048 || height > 2048 || !r.span(source, uint64_t(stride) * height))
            return {};
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
            return {};
        uint32_t colors = 0, pal = 0, format = 0;
        if (bits == 8) {
            if (!r.span(palette, 16))
                return {};
            format = r.u(palette);
            colors = r.u(palette + 4);
            pal = r.u(palette + 8);
            if (!colors || colors > 256 || (format != 1 && format != 2) || !r.span(pal, colors * (format == 1 ? 4 : 2)))
                return {};
        }
        if (stride < uint64_t(width) * (bits / 8))
            return {};
        auto t = std::make_shared<gsr::SceneModelTexture>();
        t->nativeId = id;
        t->flags = r.u(p + 0x20);
        t->pixels.resize(128 * 128);
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 128; ++x) {
                const auto a = source + std::min(height - 1, y * height / 128) * stride +
                               std::min(width - 1, x * width / 128) * (bits / 8);
                uint32_t pixel = 0;
                auto color16 = [](uint16_t c) {
                    return uint32_t((c & 31) << 3) | uint32_t(((c >> 5) & 31) << 3) << 8 |
                           uint32_t(((c >> 10) & 31) << 3) << 16 | uint32_t(c & 0x8000 ? 128 : 0) << 24;
                };
                if (bits == 8) {
                    unsigned index = r.ram[a];
                    // 1be468 -> 1e7d20 has already exchanged CLUT lanes 8/16
                    // for the GS upload. Undo that permutation for CPU sampling.
                    if (colors == 256)
                        index = (index & ~24u) | ((index & 8) << 1) | ((index & 16) >> 1);
                    if (index >= colors)
                        return {};
                    pixel = format == 1 ? r.u(pal + index * 4) : color16(r.h(pal + index * 2));
                } else if (bits == 16)
                    pixel = color16(r.h(a));
                else {
                    pixel = r.ram[a] | uint32_t(r.ram[a + 1]) << 8 | uint32_t(r.ram[a + 2]) << 16;
                    pixel |= uint32_t(bits == 32 ? r.ram[a + 3] : 128) << 24;
                }
                t->pixels[y * 128 + x] = pixel;
            }
        t->identity = hashBytes(14695981039346656037ull, t->pixels.data(), t->pixels.size() * 4);
        return t;
    }
    std::shared_ptr<const gsr::SceneModelMesh> mesh(const Reader& r, uint32_t m, uint32_t id, uint32_t positions = 0,
                                                    uint32_t slot = 0) {
        auto nv = r.u(m + 0x1c), nf = r.u(m + 0x20), fp = r.u(m + 0x28), uv = r.u(m + 0x30);
        uint32_t vertexStart = 0, faceStart = 0;
        if (r.u(m + 0x18) == 0x519a00) {
            if (!r.span(m, 0xc8) || slot >= r.u(m + 0xc4) || r.u(m + 0xc4) > 64 ||
                !r.span(r.u(m + 0xbc), uint64_t(r.u(m + 0xc4)) * 16))
                return {};
            const auto descriptor = r.u(r.u(m + 0xbc) + slot * 16 + 4);
            if (!r.span(descriptor, 24))
                return {};
            vertexStart = r.u(descriptor + 8);
            faceStart = r.u(descriptor + 16);
            auto vertices = r.u(descriptor + 12), faces = r.u(descriptor + 20);
            if (vertexStart > nv || vertices > nv - vertexStart || faceStart > nf || faces > nf - faceStart)
                return {};
            nv = vertices;
            nf = faces;
            fp += faceStart * 12;
            if (uv)
                uv += faceStart * 32;
        }
        if (!positions)
            positions = r.u(m + 0x24) + vertexStart * 12;
        if (!nv || nv > 65535 || !nf || nf > 100000 || !r.span(positions, uint64_t(nv) * 12) ||
            !r.span(fp, uint64_t(nf) * 12))
            return {};
        const uint32_t library = r.u(m + 0x38), materialCount = r.u(m + 0x40);
        if (materialCount > 4096 || !r.span(library, uint64_t(materialCount) * 4))
            return {};
        auto out = std::make_shared<gsr::SceneModelMesh>();
        out->nativeId = id;
        out->topology = hashBytes(14695981039346656037ull, &id, sizeof(id));
        struct Face {
            uint16_t v[4];
            uint16_t material, flags;
        };
        std::vector<Face> faces(nf);
        std::memcpy(faces.data(), r.ram + fp, nf * 12);
        std::unordered_map<uint64_t, uint32_t> originals;
        std::set<uint64_t> emitted;
        std::vector<unsigned> covered(nf);
        for (uint32_t f = 0; f < nf; ++f) {
            const auto& a = faces[f];
            const unsigned n = (a.flags & 4) ? 4 : 3;
            bool valid = a.material < materialCount;
            for (unsigned i = 0; i < n; ++i)
                valid &= a.v[i] < nv;
            if (!valid)
                continue;
            for (unsigned i = 0; i < n; ++i)
                for (unsigned j = i + 1; j < n; ++j)
                    for (unsigned k = j + 1; k < n; ++k)
                        originals[key(a.material, {a.v[i], a.v[j], a.v[k]})] = f;
        }
        auto emit = [&](unsigned mat, std::array<uint16_t, 3> indices, const float tex[3][2]) {
            const auto k = key(mat, indices);
            const auto found = originals.find(k);
            if (found == originals.end() || !emitted.insert(k).second)
                return;
            gsr::SceneModelTriangle t;
            const uint32_t textureId = r.u(library + mat * 4);
            if (auto it = textureIndices.find(textureId); it != textureIndices.end())
                t.material = it->second;
            for (unsigned j = 0; j < 3; ++j)
                for (unsigned c = 0; c < 3; ++c) {
                    const auto v = r.f(positions + indices[j] * 12 + c * 4);
                    if (!std::isfinite(v) || std::abs(v) > 1e7)
                        return;
                    t.position[j][c] = v;
                }
            for (unsigned j = 0; j < 3; ++j)
                for (unsigned c = 0; c < 2; ++c) {
                    if (!std::isfinite(tex[j][c]) || std::abs(tex[j][c]) > 1e5)
                        return;
                    t.uv[j][c] = tex[j][c];
                }
            out->topology = hashBytes(out->topology, indices.data(), sizeof(uint16_t) * 3);
            out->topology = hashBytes(out->topology, &textureId, sizeof(textureId));
            out->topology = hashBytes(out->topology, tex, sizeof(float) * 6);
            out->triangles.push_back(t);
            ++covered[found->second];
        };
        const auto slots = r.u(m + 0xa8);
        const auto prepared = r.span(slots, 4) ? r.u(slots) + slot * 40 : 0;
        if (r.span(prepared, 40)) {
            const auto parts = r.u(prepared + 16), count = r.u(prepared + 12), table = r.u(prepared + 8),
                       groups = r.u(prepared + 4);
            if (count <= 4096 && r.span(parts, uint64_t(count) * 48) && groups <= 256 &&
                r.span(table, uint64_t(groups) * 4)) {
                std::array<int, 256> mapping;
                mapping.fill(-1);
                for (unsigned i = 0; i < count; ++i) {
                    const auto p = parts + i * 48, mat = r.u(p), ng = r.u(p + 8), selectors = r.u(p + 12),
                               n = r.u(p + 16), order = r.u(p + 20), tex = r.u(p + 28);
                    if (mat >= materialCount || ng > 256 || !r.span(selectors, ng * 2) || n > 1048576 ||
                        !r.span(order, n) || !r.span(tex, uint64_t(n) * 8))
                        continue;
                    bool valid = true;
                    for (unsigned g = 0; g < ng; ++g) {
                        unsigned group = r.ram[selectors + g * 2], dest = r.ram[selectors + g * 2 + 1];
                        if (group >= groups) {
                            valid = false;
                            break;
                        }
                        auto start = r.h(table + group * 4), length = r.h(table + group * 4 + 2);
                        if (unsigned(start) + length > nv || dest + length > 256) {
                            valid = false;
                            break;
                        }
                        for (unsigned v = 0; v < length; ++v)
                            mapping[dest + v] = start + v;
                    }
                    if (!valid)
                        continue;
                    std::array<uint16_t, 3> inds{};
                    float st[3][2]{};
                    unsigned run = 0;
                    for (unsigned j = 0; j < n; ++j) {
                        auto value = r.ram[order + j];
                        if (!value || mapping[value - 1] < 0) {
                            run = 0;
                            continue;
                        }
                        inds[0] = inds[1];
                        inds[1] = inds[2];
                        inds[2] = uint16_t(mapping[value - 1]);
                        std::memcpy(st[0], st[1], 8);
                        std::memcpy(st[1], st[2], 8);
                        std::memcpy(st[2], r.ram + tex + j * 8, 8);
                        if (++run >= 3 && inds[0] != inds[1] && inds[0] != inds[2] && inds[1] != inds[2])
                            emit(mat, inds, st);
                    }
                }
            }
        }
        // CPU path retains four S values then four T values for each face.
        const bool cpuUV = r.span(uv, uint64_t(nf) * 32);
        for (unsigned f = 0; f < nf; ++f)
            if (!covered[f]) {
                const auto& a = faces[f];
                unsigned n = (a.flags & 4) ? 4 : 3;
                for (unsigned j = 1; j + 1 < n; ++j) {
                    std::array<uint16_t, 3> inds{a.v[0], a.v[j], a.v[j + 1]};
                    float st[3][2]{};
                    unsigned corners[3] = {0, j, j + 1};
                    if (cpuUV)
                        for (unsigned v = 0; v < 3; ++v) {
                            st[v][0] = r.f(uv + f * 32 + corners[v] * 4);
                            st[v][1] = r.f(uv + f * 32 + 16 + corners[v] * 4);
                        }
                    const auto before = out->triangles.size();
                    emit(a.material, inds, st);
                    if (!cpuUV && out->triangles.size() != before)
                        out->triangles.back().material = ~0u;
                }
            }
        uint64_t hash = hashBytes(14695981039346656037ull, &id, 4);
        out->identity = hashBytes(hash, out->triangles.data(), out->triangles.size() * sizeof(gsr::SceneModelTriangle));
        return out;
    }

public:
    using TextureLoader = std::function<std::shared_ptr<const gsr::SceneModelTexture>(uint32_t, const std::string&)>;
    TextureLoader loadTexture;
    void reset() { level = ~0ull; }
    std::shared_ptr<const gsr::SceneModels> capture(const uint8_t* ram, uint32_t bytes, uint64_t scene) {
        auto out = std::make_shared<gsr::SceneModels>();
        out->level = scene;
        Reader r{ram, bytes};
        if (!r.span(0x4dae20, 0x220))
            return out;
        // 185db0 -> 1c4148 transforms the shared scratch light set into
        // object space. Read the native WORLD-space layer source instead.
        if (r.span(0x4dc118, 12)) {
            const auto base = r.u(0x4dc118), count = r.u(0x4dc120);
            float eye[3]{};
            for (unsigned c = 0; c < 3; ++c)
                eye[c] = r.f(0x4dadc4 + c * 4);
            uint32_t selected = 0;
            float closest = 1e30f;
            if (count <= 4096 && r.span(base, uint64_t(count) * 0x164))
                for (unsigned i = 0; i < count; ++i) {
                    auto a = base + i * 0x164;
                    auto w = r.u(a + 0x1c), h = r.u(a + 0x20), flags = r.u(a + 0x50), heights = r.u(a + 0x28);
                    if (r.u(a + 0x4c) != 0 || flags & 1 || !w || !h || w > 512 || h > 512)
                        continue;
                    float x = (eye[0] - r.f(a + 8)) / 2048.f, z = (eye[2] - r.f(a + 16)) / 2048.f;
                    if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || z < 0 || x >= w || z >= h)
                        continue;
                    float ground;
                    if (!layerGround(r, a, x, z, ground))
                        continue;
                    float distance = eye[1] - ground;
                    if (distance >= -512 && distance < closest) {
                        closest = distance;
                        selected = a;
                    }
                }
            if (selected) {
                gsr::SceneLight sun;
                bool valid = true;
                for (unsigned c = 0; c < 3; ++c) {
                    sun.vector[c] = r.f(selected + 0x124 + c * 4);
                    sun.color[c] = r.f(selected + 0x130 + c * 4);
                    out->lighting.ambient[c] = r.f(selected + 0x13c + c * 4);
                    valid &= std::isfinite(sun.vector[c]) && std::isfinite(sun.color[c]) && sun.color[c] >= 0 &&
                             sun.color[c] <= 16 && std::isfinite(out->lighting.ambient[c]) &&
                             out->lighting.ambient[c] >= 0 && out->lighting.ambient[c] <= 16;
                }
                const float n =
                    sun.vector[0] * sun.vector[0] + sun.vector[1] * sun.vector[1] + sun.vector[2] * sun.vector[2];
                if (valid && n > .5f && n < 1.5f)
                    out->lighting.lights.push_back(sun);
                const auto list = r.u(selected + 0x5c), lights = r.u(selected + 0x64);
                std::vector<std::pair<float, gsr::SceneLight>> local;
                if (lights <= 4096 && r.span(list, uint64_t(lights) * 4))
                    for (unsigned i = 0; i < lights; ++i) {
                        auto p = r.u(list + i * 4);
                        if (!r.span(p, 28))
                            continue;
                        gsr::SceneLight light;
                        light.type = gsr::SceneLight::Point;
                        const auto radius = r.f(p + 12);
                        if (!std::isfinite(radius) || radius <= 0 || radius > 1e7)
                            continue;
                        light.radiusSquared = radius * radius;
                        bool finite = true;
                        float distance = 1, brightness = 0;
                        for (unsigned c = 0; c < 3; ++c) {
                            light.vector[c] = r.f(p + 16 + c * 4);
                            light.color[c] = r.f(p + c * 4);
                            finite &= std::isfinite(light.vector[c]) && std::isfinite(light.color[c]) &&
                                      light.color[c] >= 0 && light.color[c] <= 16;
                            distance += (light.vector[c] - eye[c]) * (light.vector[c] - eye[c]);
                            brightness += light.color[c];
                        }
                        if (finite)
                            local.emplace_back(brightness * light.radiusSquared / distance, light);
                    }
                std::stable_sort(local.begin(), local.end(),
                                 [](const auto& a, const auto& b) { return a.first > b.first; });
                for (unsigned i = 0; i < std::min<size_t>(3, local.size()); ++i)
                    out->lighting.lights.push_back(local[i].second);
            }
        }
        if (level != scene) {
            level = scene;
            ticks = 0;
            backdrops.clear();
            meshes.clear();
            textures.clear();
            textureIndices.clear();
            live.clear();
            generations.clear();
            for (uint32_t p = 0x100000; p + 0x120 <= bytes; p += 4)
                if (r.u(p) == 0x51b8b0) {
                    live.insert(p);
                    generations[p] = ++nextGeneration;
                }
        }
        if (textures.empty() || (++ticks % 32) == 0) {
            auto resources = r.resources(0x4db280, 0x51cfe8);
            std::vector<std::shared_ptr<const gsr::SceneModelTexture>> next;
            std::map<uint32_t, uint32_t> indices;
            for (const auto& [id, p] : resources)
                if (auto t = texture(r, id, p)) {
                    indices[id] = uint32_t(next.size());
                    auto previous = textureIndices.find(id);
                    if (previous != textureIndices.end() && textures[previous->second]->identity == t->identity)
                        t = textures[previous->second];
                    next.push_back(t);
                }
            // 12ad60: the high word is the live database namespace, not a
            // fixed level number. Resolve its exact registered path.
            if (loadTexture && r.span(0x4dade8, 12)) {
                std::map<uint32_t, std::string> paths;
                auto list = r.u(0x4dade8), count = r.u(0x4dadf0);
                if (count < 256 && r.span(list, count * 4))
                    for (unsigned i = 0; i < count; ++i) {
                        auto db = r.u(list + i * 4);
                        if (!r.span(db, 260))
                            continue;
                        const char* s = reinterpret_cast<const char*>(ram + db + 4);
                        const auto end = std::find(s, s + 256, '\0');
                        if (end != s + 256)
                            paths[r.u(db)] = std::string(s, end) + ".TXD";
                    }
                for (const auto& [modelId, m] : r.resources(0x4dae20, 0x51cc98, 0x519a00)) {
                    auto library = r.u(m + 0x38), n = r.u(m + 0x40);
                    if (n > 4096 || !r.span(library, uint64_t(n) * 4))
                        continue;
                    for (unsigned i = 0; i < n && next.size() < 768; ++i) {
                        auto id = r.u(library + i * 4);
                        if (indices.contains(id))
                            continue;
                        auto path = paths.find(id >> 16);
                        if (path == paths.end())
                            continue;
                        if (auto t = loadTexture(id, path->second)) {
                            indices[id] = uint32_t(next.size());
                            next.push_back(std::move(t));
                        }
                    }
                }
            }
            if (indices != textureIndices)
                meshes.clear();
            textureIndices = std::move(indices);
            textures = std::move(next);
        }
        out->textures = textures;
        auto resources = r.resources(0x4dae20, 0x51cc98, 0x519a00);
        std::map<uint64_t, uint32_t> used;
        backdrops.prune();
        for (auto object : live) {
            if (out->instances.size() >= 16384)
                break;
            if (!r.span(object, 0x120) || r.u(object) != 0x51b8b0)
                continue;
            // 15f7c8 tests object+4 bit 0 before entering 18b0c8's
            // distance/frustum path. Keep semantic visibility, bypass only
            // camera culling: hidden trigger/helper boxes are not reflectors.
            if (!(r.u(object + 4) & 1))
                continue;
            auto it = resources.find(r.u(object + 8));
            if (it == resources.end())
                continue;
            uint32_t positions = 0, slot = 0;
            uint64_t cacheKey = uint64_t(it->first) << 32 | it->second;
            if (r.u(object + 0x50)) {
                const auto controller = r.u(object + 0x50);
                if (!r.span(controller, 0x12c) || r.u(controller + 0xd4) != object) {
                    ++out->rejectedInstances;
                    continue;
                }
                slot = r.u(controller + 0x10);
                uint32_t vertexCount = r.u(it->second + 0x1c);
                if (r.u(it->second + 0x18) == 0x519a00) {
                    const auto count = r.u(it->second + 0xc4), lods = r.u(it->second + 0xbc);
                    if (count > 64 || slot >= count || !r.span(lods, uint64_t(count) * 16)) {
                        ++out->rejectedInstances;
                        continue;
                    }
                    auto d = r.u(lods + slot * 16 + 4);
                    if (!r.span(d, 24)) {
                        ++out->rejectedInstances;
                        continue;
                    }
                    vertexCount = r.u(d + 12);
                }
                cacheKey = hashBytes(cacheKey, &slot, 4);
                if (r.u(controller + 8) & 0x100000) {
                    auto pose = drakan_object_motion::provenPoseSource(ram, bytes, object);
                    if (!pose.address || pose.count != vertexCount) {
                        ++out->rejectedInstances;
                        continue;
                    }
                    positions = pose.address;
                    cacheKey = hashBytes(cacheKey, ram + positions, pose.count * 12);
                } else if (!(r.u(controller + 8) & 8)) {
                    // 106aa8/1165b0: only an explicitly non-deforming
                    // controller may use source vertices without a pose.
                    ++out->rejectedInstances;
                    continue;
                }
            }
            std::shared_ptr<const gsr::SceneModelMesh> geometry;
            if (auto found = meshes.find(cacheKey); found != meshes.end())
                geometry = found->second;
            else {
                geometry = mesh(r, it->second, it->first, positions, slot);
                if (geometry && !positions)
                    meshes[cacheKey] = geometry;
            }
            if (!geometry || geometry->triangles.empty()) {
                ++out->rejectedInstances;
                continue;
            }
            gsr::SceneModelInstance instance;
            instance.object = object;
            instance.generation = generations[object];
            bool valid = true;
            for (unsigned row = 0; row < 3; ++row) {
                for (unsigned col = 0; col < 3; ++col) {
                    float v = r.f(object + 0x20 + (row * 3 + col) * 4);
                    valid &= std::isfinite(v) && std::abs(v) < 10000;
                    instance.transform[row * 4 + col] = v;
                }
                float v = r.f(object + 0x14 + row * 4);
                valid &= std::isfinite(v) && std::abs(v) < 1e8;
                instance.transform[row * 4 + 3] = v;
            }
            const auto* t = instance.transform;
            const float det = t[0] * (t[5] * t[10] - t[6] * t[9]) - t[1] * (t[4] * t[10] - t[6] * t[8]) +
                              t[2] * (t[4] * t[9] - t[5] * t[8]);
            instance.opacity = r.f(object + 0x114);
            if (!valid || !std::isfinite(det) || std::abs(det) < 1e-12 || !std::isfinite(instance.opacity)) {
                ++out->rejectedInstances;
                continue;
            }
            instance.opacity = std::clamp(instance.opacity, 0.f, 1.f);
            if (instance.opacity <= 0)
                continue;
            if (r.span(0x4dadc4, 12)) {
                const float camera[]{r.f(0x4dadc4), r.f(0x4dadc8), r.f(0x4dadcc)};
                if (backdrops.exclude(*geometry, instance, camera, !r.u(object + 0x50) && (r.u(object + 4) & 0x1000)))
                    continue;
            }
            if (auto found = used.find(cacheKey); found != used.end())
                instance.mesh = found->second;
            else {
                instance.mesh = uint32_t(out->meshes.size());
                used[cacheKey] = instance.mesh;
                out->meshes.push_back(geometry);
                for (const auto& t : geometry->triangles)
                    out->untexturedTriangles += t.material == ~0u;
            }
            out->instances.push_back(instance);
        }
        return out;
    }
};
} // namespace drakan_scene_models
