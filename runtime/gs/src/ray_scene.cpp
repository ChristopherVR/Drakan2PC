// Ray scene packing shared by the D3D11 (ray_water.cpp) and WebGPU (wgpu_ray.cpp) ray passes. See ray_scene.h.
#include "ray_scene.h"
#include "ray_normals.h"

#include <algorithm>
#include <cmath>

namespace gsr::surface {
std::span<const uint32_t> ModelAlphaCoverage::update(const SceneModels* models) {
    const size_t size = models ? models->textures.size() : 0;
    sources.resize(size);
    coverage.resize(size);
    for (size_t i = 0; i < size; ++i) {
        const auto& texture = models->textures[i];
        auto& source = sources[i];
        // Identity zero is a mutable/ad-hoc test input: revalidate its pixels.
        if (texture && texture->identity && texture == source.texture && texture->identity == source.identity &&
            texture->flags == source.flags)
            continue;
        source = {texture, texture ? texture->identity : 0, texture ? texture->flags : 0};
        coverage[i] = texture && texture->pixels.size() == Texture::size * Texture::size &&
                              std::all_of(texture->pixels.begin(), texture->pixels.end(),
                                          [](uint32_t pixel) { return (pixel >> 24) != 0; })
                          ? kRayAlphaPositive
                          : 0;
    }
    return coverage;
}
bool rayMaterialGlossy(float roughness) {
    return std::isfinite(roughness) && roughness <= kRayGlossyRoughness;
}
float packRayMaterial(float roughness, float reflectance) {
    // A small positive roughness avoids singular specular lobes and reserves zero for old/manual attributes.
    const bool glossy = rayMaterialGlossy(roughness);
    roughness = std::isfinite(roughness) ? std::clamp(roughness, .04f, 1.f) : .75f;
    reflectance = std::isfinite(reflectance) ? std::clamp(reflectance, 0.f, 1.f) : .04f;
    return float(uint32_t(roughness * 255.f + .5f) | (uint32_t(reflectance * 255.f + .5f) << 8) |
                 (glossy ? kRayMaterialGlossy : 0u));
}
uint32_t rayDiagnosticEffects(std::string_view mode) {
    if (mode == "ao")
        return kRayDiagnosticNoDirect;
    if (mode == "direct")
        return kRayDiagnosticNoAo;
    if (mode == "receiver")
        return kRayDiagnosticReceiver;
    return 0;
}
// A small closed shell around the camera with every face plane well clear of it: the underwater tint box (a
// 12-triangle box, faces about 4096 units out) or a sky sphere trailing the camera. Drawn around the view, these
// are not world geometry; as occluders they shadow every receiver inside them (the whole submerged scene).
// Ordinary scenery that encloses the camera (a room, a hut) has its floor or walls within the clearance.
// Open camera-anchored rings count too: two 128-triangle sky barrels (radius about 5000, 900-9600 units above the
// camera, horizontally centred on it) that every sun ray from a receiver near the camera passes through.
ModelPoseShape screenModelPose(std::span<Point> corners, std::vector<float>& scratch) {
    ModelPoseShape shape;
    const size_t count = corners.size() / 3;
    scratch.resize(count * 2);
    // Longest box side of each triangle; -1 marks a triangle that is out of range already.
    for (size_t i = 0; i < count; ++i) {
        const Point* p = &corners[i * 3];
        bool finite = true;
        for (unsigned j = 0; j < 3; ++j)
            finite &= std::abs(p[j].x) <= kModelPoseLimit && std::abs(p[j].y) <= kModelPoseLimit &&
                      std::abs(p[j].z) <= kModelPoseLimit; // false for NaN and both infinities
        if (!finite) {
            scratch[i] = -1;
            continue;
        }
        const float sx = std::max({p[0].x, p[1].x, p[2].x}) - std::min({p[0].x, p[1].x, p[2].x});
        const float sy = std::max({p[0].y, p[1].y, p[2].y}) - std::min({p[0].y, p[1].y, p[2].y});
        const float sz = std::max({p[0].z, p[1].z, p[2].z}) - std::min({p[0].z, p[1].z, p[2].z});
        scratch[i] = std::max({sx, sy, sz});
    }
    float limit = INFINITY;
    if (count) {
        // The median over a copy in the second half of the scratch (no allocation once it has grown).
        const auto spans = scratch.begin() + count;
        std::copy(scratch.begin(), spans, spans);
        auto middle = spans + (count - 1) / 2; // the lower median: one wild triangle of two cannot set it
        std::nth_element(spans, middle, scratch.end());
        limit = std::max(kModelSpikeFloor, std::max(*middle, 0.f) * kModelSpikeFactor);
    }
    Point lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
    for (size_t i = 0; i < count; ++i) {
        Point* p = &corners[i * 3];
        if (scratch[i] < 0 || scratch[i] > limit) {
            const float longest = scratch[i] < 0 ? INFINITY : scratch[i];
            shape.longestInactive = std::max(shape.longestInactive, longest);
            ++shape.inactive;
            for (unsigned j = 0; j < 3; ++j)
                p[j] = {NAN, NAN, NAN};
            continue;
        }
        Point a = p[0], b = p[0];
        for (unsigned j = 1; j < 3; ++j) {
            a = {std::min(a.x, p[j].x), std::min(a.y, p[j].y), std::min(a.z, p[j].z)};
            b = {std::max(b.x, p[j].x), std::max(b.y, p[j].y), std::max(b.z, p[j].z)};
        }
        const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        shape.boxArea += 2 * (dx * dy + dy * dz + dz * dx);
        lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
        hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
    }
    if (lo.x <= hi.x) {
        shape.extent[0] = hi.x - lo.x;
        shape.extent[1] = hi.y - lo.y;
        shape.extent[2] = hi.z - lo.z;
    }
    return shape;
}
bool modelRefitFits(const ModelPoseShape& built, const ModelPoseShape& pose) {
    if (built.inactive || pose.inactive)
        return false;
    // A small absolute slack so a mesh that starts flat (zero extent on an axis) can still bend a little.
    constexpr float kSlack = 64;
    for (unsigned c = 0; c < 3; ++c)
        if (!(pose.extent[c] <= built.extent[c] * float(kModelRefitGrowth) + kSlack))
            return false;
    return pose.boxArea <= built.boxArea * kModelRefitGrowth + 2.0 * kSlack * kSlack;
}
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
        out.push_back({packNormal(normals[0]), packNormal(normals[1]), packNormal(normals[2]),
                       packRayMaterial(tri.roughness, tri.reflectance)});
    else
        out.push_back({packNormal(normal), packNormal(normal), packNormal(normal),
                       packRayMaterial(tri.roughness, tri.reflectance)});
    return tri.texture < textures ? 1u : 0u;
}
uint32_t packModelAttributes(const SceneModelMesh& mesh, const SceneModels& models, uint32_t nativeTextureBase,
                             const std::vector<Point>& normals, std::vector<Attribute>& target,
                             std::span<const float> packedNormals, std::span<const uint32_t> alphaCoverage) {
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
        // Native cutout testing is GREATER 0 with vertex alpha 128 and MODULATE.
        // A positive minimum over the complete bilinear source proves that
        // every sample passes. Keep the original flags for material classification.
        const uint32_t coverage =
            alpha && !blend && tri.material < alphaCoverage.size() ? alphaCoverage[tri.material] : 0;
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
                          float((alpha ? 6u : 1u) | (blend ? 8u : 0u) | (textured ? 16u : 0u) | 128u | coverage), 0});
        const Point* n = &normals[index * 3];
        const float material = textured ? packRayMaterial(models.textures[tri.material]->roughness,
                                                          models.textures[tri.material]->reflectance)
                                        : packRayMaterial(.75f, .04f);
        if (packedNormals.size() == normals.size())
            target.push_back(
                {packedNormals[index * 3], packedNormals[index * 3 + 1], packedNormals[index * 3 + 2], material});
        else
            target.push_back({packNormal(n[0]), packNormal(n[1]), packNormal(n[2]), material});
    }
    return texturedCount;
}
void rebaseModelAttributes(const SceneModelMesh& mesh, const SceneModels& models, uint32_t nativeTextureBase,
                           std::vector<Attribute>& attributes) {
    for (size_t index = 0; index < mesh.triangles.size() && (index + 1) * kTriangleAttributes <= attributes.size();
         ++index) {
        const auto material = mesh.triangles[index].material;
        attributes[index * kTriangleAttributes + 5][1] =
            material < models.textures.size() ? float(nativeTextureBase + material) : -1.f;
    }
}
} // namespace gsr::surface
