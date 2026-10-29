// Ray scene packing shared by the D3D11 (ray_water.cpp) and WebGPU (wgpu_ray.cpp) ray passes. See ray_scene.h.
#include "ray_scene.h"
#include "ray_normals.h"

#include <algorithm>
#include <cmath>

namespace gsr::surface {
// A small closed shell around the camera with every face plane well clear of it: the underwater tint box (a
// 12-triangle box, faces about 4096 units out) or a sky sphere trailing the camera. Drawn around the view, these
// are not world geometry; as occluders they shadow every receiver inside them (the whole submerged scene).
// Ordinary scenery that encloses the camera (a room, a hut) has its floor or walls within the clearance.
// Open camera-anchored rings count too: two 128-triangle sky barrels (radius about 5000, 900-9600 units above the
// camera, horizontally centred on it) that every sun ray from a receiver near the camera passes through.
bool cameraShell(const SceneModelMesh& mesh, const float* m, Point camera) {
    constexpr size_t kMaxTriangles = 1024;
    constexpr float kClearance = 2048;
    // Cheap rejection of ordinary scenery first: the native shells are centred near the camera.
    const Point offset = sub({m[3], m[7], m[11]}, camera);
    if (mesh.triangles.empty() || mesh.triangles.size() > kMaxTriangles || !(length(offset) < 16384))
        return false;
    Point lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
    for (const auto& tri : mesh.triangles) {
        Point p[3];
        for (unsigned j = 0; j < 3; ++j) {
            const auto& v = tri.position[j];
            p[j] = sub({m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3], m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7],
                        m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11]},
                       camera);
            lo = {std::min(lo.x, p[j].x), std::min(lo.y, p[j].y), std::min(lo.z, p[j].z)};
            hi = {std::max(hi.x, p[j].x), std::max(hi.y, p[j].y), std::max(hi.z, p[j].z)};
        }
        const auto normal = cross(sub(p[1], p[0]), sub(p[2], p[0]));
        const float len = length(normal);
        if (!std::isfinite(len))
            return false;
        if (len > 0 && std::abs(normal.x * p[0].x + normal.y * p[0].y + normal.z * p[0].z) < kClearance * len)
            return false;
    }
    if (!(lo.x < -kClearance && lo.z < -kClearance && hi.x > kClearance && hi.z > kClearance))
        return false;
    if (lo.y < -kClearance && hi.y > kClearance)
        return true;
    return std::abs(lo.x + hi.x) < .25f * (hi.x - lo.x) && std::abs(lo.z + hi.z) < .25f * (hi.z - lo.z);
}
// kTriangleAttributes float4 material attributes per triangle; the shader indexes them by instance base +
// primitive. `normals` are the triangle's three vertex normals; without them it shades with its face normal.
uint32_t packAttributes(const Triangle& tri, size_t textures, std::vector<Attribute>& out, const Point* normals) {
    for (unsigned j = 0; j < 3; ++j)
        out.push_back({tri.vertexLit ? tri.lighting[j][0] : tri.color[0],
                       tri.vertexLit ? tri.lighting[j][1] : tri.color[1],
                       tri.vertexLit ? tri.lighting[j][2] : tri.color[2], tri.opacity[j]});
    out.push_back({tri.uv[0][0], tri.uv[0][1], tri.uv[1][0], tri.uv[1][1]});
    auto normal = cross(sub(tri.p[1], tri.p[0]), sub(tri.p[2], tri.p[0]));
    float len = length(normal);
    if (len > 0) {
        normal.x /= len;
        normal.y /= len;
        normal.z /= len;
    }
    out.push_back({tri.uv[2][0], tri.uv[2][1], normal.x, normal.y});
    const uint32_t alphaFlags = tri.alphaTest | (uint32_t(tri.transparent) << 3) | (uint32_t(tri.textureAlpha) << 4) |
                                (tri.textureFunction << 5) | (tri.shadowGeometry ? 0u : 256u);
    out.push_back(
        {normal.z, tri.texture < textures ? float(tri.texture) : -1.f, float(alphaFlags), tri.alphaReference});
    if (normals)
        out.push_back({packNormal(normals[0]), packNormal(normals[1]), packNormal(normals[2]), 0});
    else
        out.push_back({packNormal(normal), packNormal(normal), packNormal(normal), 0});
    return tri.texture < textures ? 1u : 0u;
}
uint32_t packModelAttributes(const SceneModelMesh& mesh, const SceneModels& models, uint32_t nativeTextureBase,
                             const std::vector<Point>& normals, std::vector<Attribute>& target) {
    uint32_t texturedCount = 0;
    for (size_t index = 0; index < mesh.triangles.size(); ++index) {
        const auto& tri = mesh.triangles[index];
        const bool textured = tri.material < models.textures.size();
        texturedCount += textured;
        const uint32_t flags = textured ? models.textures[tri.material]->flags : 0;
        // 18c52c enables native alpha for every nonzero blend/alpha
        // selector, including 0x10000's 16-bit binary-alpha materials.
        const bool alpha = (flags & 0xf000a) != 0;
        const bool blend = (flags & 0xf0000) == 0x80000;
        for (unsigned j = 0; j < 3; ++j)
            target.push_back({textured ? 1.f : .35f, textured ? 1.f : .35f, textured ? 1.f : .35f, 1});
        target.push_back({tri.uv[0][0], tri.uv[0][1], tri.uv[1][0], tri.uv[1][1]});
        const auto p = [&](unsigned j) { return Point{tri.position[j][0], tri.position[j][1], tri.position[j][2]}; };
        auto normal = cross(sub(p(1), p(0)), sub(p(2), p(0)));
        float len = length(normal);
        if (len > 0) {
            normal.x /= len;
            normal.y /= len;
            normal.z /= len;
        }
        target.push_back({tri.uv[2][0], tri.uv[2][1], normal.x, normal.y});
        target.push_back({normal.z, textured ? float(nativeTextureBase + tri.material) : -1.f,
                          float((alpha ? 6u : 1u) | (blend ? 8u : 0u) | (textured ? 16u : 0u) | 128u), 0});
        const Point* n = &normals[index * 3];
        target.push_back({packNormal(n[0]), packNormal(n[1]), packNormal(n[2]), 0});
    }
    return texturedCount;
}
} // namespace gsr::surface
