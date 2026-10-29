#pragma once
#include "ray_scene.h"
#include "ray_normals.h"
#include <array>
#include <cstring>
#include <type_traits>

namespace gsr::surface {
// Cache world-space attributes in small blocks. Camera motion still transforms
// every vertex; any geometry, lighting, opacity or material-table change rebuilds
// the affected block. Byte comparison may miss on padding, never falsely hit.
class RayWorldCache {
    static constexpr size_t blockSize = 256;
    using Attribute = std::array<float, 4>;
    struct Block {
        size_t size = 0, minimumTextures = 0, maximumTextures = SIZE_MAX;
        uint32_t counts[3]{}, textured = 0;
    };
    std::vector<Triangle> source;
    std::vector<Point> vertices;
    std::vector<Attribute> attributes;
    std::vector<Block> blocks;
    size_t rebuilt = 0;

public:
    size_t rebuiltBlocks() const { return rebuilt; }
    size_t blockCount() const { return blocks.size(); }
    uint32_t append(std::span<const Triangle> input, Point origin, size_t textures, std::vector<Point>& positions,
                    std::vector<Attribute>& colors, uint32_t (&counts)[3]) {
        static_assert(std::is_trivially_copyable_v<Triangle>);
        source.resize(input.size());
        vertices.resize(input.size() * 3);
        attributes.resize(input.size() * kTriangleAttributes);
        blocks.resize((input.size() + blockSize - 1) / blockSize);
        uint32_t textured = 0;
        counts[0] = counts[1] = counts[2] = 0;
        rebuilt = 0;
        for (size_t b = 0; b < blocks.size(); ++b) {
            const size_t first = b * blockSize, n = (std::min)(blockSize, input.size() - first);
            auto& block = blocks[b];
            if (block.size != n || textures < block.minimumTextures || textures > block.maximumTextures ||
                std::memcmp(source.data() + first, input.data() + first, n * sizeof(Triangle))) {
                block = {};
                block.size = n;
                ++rebuilt;
                std::memcpy(source.data() + first, input.data() + first, n * sizeof(Triangle));
                size_t vertex = first * 3, attribute = first * kTriangleAttributes;
                for (unsigned group = 0; group < 3; ++group)
                    for (size_t i = first; i < first + n; ++i) {
                        const auto& tri = input[i];
                        if ((tri.water ? (tri.shadowGeometry ? 2u : 1u) : 0u) != group)
                            continue;
                        for (const auto& p : tri.p)
                            vertices[vertex++] = p;
                        block.textured += tri.texture < textures;
                        ++block.counts[group];
                        // Texture-table growth only matters when an index crosses
                        // the valid/invalid boundary for a triangle in this block.
                        if (tri.texture < textures)
                            block.minimumTextures = (std::max)(block.minimumTextures, size_t(tri.texture) + 1);
                        else
                            block.maximumTextures = (std::min)(block.maximumTextures, size_t(tri.texture));
                        for (unsigned j = 0; j < 3; ++j)
                            attributes[attribute++] = {tri.vertexLit ? tri.lighting[j][0] : tri.color[0],
                                                       tri.vertexLit ? tri.lighting[j][1] : tri.color[1],
                                                       tri.vertexLit ? tri.lighting[j][2] : tri.color[2],
                                                       tri.opacity[j]};
                        attributes[attribute++] = {tri.uv[0][0], tri.uv[0][1], tri.uv[1][0], tri.uv[1][1]};
                        auto normal = cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0]));
                        float len = length(normal);
                        if (len > 0) {
                            normal.x /= len;
                            normal.y /= len;
                            normal.z /= len;
                        }
                        attributes[attribute++] = {tri.uv[2][0], tri.uv[2][1], normal.x, normal.y};
                        const uint32_t flags = tri.alphaTest | (uint32_t(tri.transparent) << 3) |
                                               (uint32_t(tri.textureAlpha) << 4) | (tri.textureFunction << 5) |
                                               (tri.shadowGeometry ? 0u : 256u);
                        attributes[attribute++] = {normal.z, tri.texture < textures ? float(tri.texture) : -1.f,
                                                   float(flags), tri.alphaReference};
                        // Per-frame draws are not welded (their blocks are cached independently): face normals.
                        const float packed = packNormal(normal);
                        attributes[attribute++] = {packed, packed, packed, 0};
                    }
            }
            textured += block.textured;
            for (unsigned group = 0; group < 3; ++group)
                counts[group] += block.counts[group];
        }
        positions.resize(input.size() * 3);
        size_t output = 0;
        for (unsigned group = 0; group < 3; ++group)
            for (size_t b = 0; b < blocks.size(); ++b) {
                const auto& block = blocks[b];
                size_t first = b * blockSize;
                for (unsigned previous = 0; previous < group; ++previous)
                    first += block.counts[previous];
                const size_t end = first + block.counts[group];
                for (size_t v = first * 3; v < end * 3; ++v)
                    positions[output++] = sub(vertices[v], origin);
                colors.insert(colors.end(), attributes.begin() + first * kTriangleAttributes,
                              attributes.begin() + end * kTriangleAttributes);
            }
        return textured;
    }
};
} // namespace gsr::surface
