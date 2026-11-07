// The ray-traced scene shared by both renderers' ray passes (D3D11 DXR ray_water.cpp, WebGPU compute
// wgpu_ray.cpp): reconstructed and resident world triangles, their material textures and the per-pass options, plus
// the packing of those triangles into the float4 material attributes both shaders index.
#pragma once
#include "surface_geometry.h"
#include <gsr/scene_models.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace gsr::surface {
// Diagnostic bits share Effects.w in both shaders. Unknown modes preserve normal rendering.
constexpr uint32_t kRayDiagnosticNoAo = 32, kRayDiagnosticNoDirect = 64, kRayDiagnosticReceiver = 128;
constexpr uint32_t kDirectShadowMask = 8; // separate from land (1), water coverage (2) and native water (4)
constexpr uint32_t kValidatedGeometryMask = 16;
uint32_t rayDiagnosticEffects(std::string_view mode);
struct Triangle {
    Point p[3];
    float color[3]{};
    bool water = false;
    float lighting[3][3]{};
    float uv[3][2]{};
    uint32_t texture = ~0u;
    bool vertexLit = false;
    float opacity[3]{1, 1, 1};
    uint32_t alphaTest = 1, textureFunction = 0;
    float alphaReference = 0;
    bool textureAlpha = false, transparent = false;
    bool shadowGeometry = true; // native topology; reconstructed GS draws are reflection fallback only
    // Native diffuse assets have no authored PBR parameters: use a rough dielectric unless explicitly supplied.
    float roughness = .75f, reflectance = .04f;
};
struct Texture {
    static constexpr unsigned size = 128;
    uint64_t identity = 0;
    std::vector<uint32_t> pixels;
};
struct RayOptions {
    bool water = true, shadows = false;
    const char* diagnosticDump = nullptr;
    bool requireWaterCoverage = false;
    bool globalIllumination = false, materialReflections = false;
};
using Attribute = std::array<float, 4>;
// A native cutout's complete texture footprint has alpha > 0. Its original
// alpha-test/material-class bits stay intact; only evaluation can be skipped.
constexpr uint32_t kRayAlphaPositive = 512;
// Published textures are immutable. Retain their ownership so pointer reuse
// cannot make a replacement texture inherit an earlier coverage proof.
class ModelAlphaCoverage {
    struct Source {
        std::shared_ptr<const SceneModelTexture> texture;
        uint64_t identity = 0;
        uint32_t flags = 0;
    };
    std::vector<Source> sources;
    std::vector<uint32_t> coverage;

public:
    std::span<const uint32_t> update(const SceneModels* models);
};
// Attribute 6.w: two UNORM8 channels, represented exactly as a float integer (roughness, normal-incidence F0),
// plus kRayMaterialGlossy. Both shaders trace opaque-material reflections only for glossy materials.
// Native textures carry their (baked) lighting already; a rough dielectric's specular lobe is close to the
// irradiance that lighting represents, so adding it again lifted dark ground towards the sky colour (a milky,
// low-contrast haze, GitHub issue 1). Rough materials therefore keep their native colour.
constexpr float kRayGlossyRoughness = .5f;
constexpr uint32_t kRayMaterialGlossy = 1u << 16;
bool rayMaterialGlossy(float roughness);
float packRayMaterial(float roughness, float reflectance);
// Capturing a pose can publish a new immutable snapshot of unchanged geometry.
// Pointer identity alone would rebuild its normals/materials (and WebGPU BLAS).
// Verify bytes as well as the hash so a collision cannot reuse stale geometry.
inline bool sameModelGeometry(const SceneModelMesh& a, const SceneModelMesh& b) {
    return &a == &b || (a.identity == b.identity && a.topology == b.topology && a.nativeId == b.nativeId &&
                        a.triangles.size() == b.triangles.size() &&
                        (a.triangles.empty() || std::memcmp(a.triangles.data(), b.triangles.data(),
                                                            a.triangles.size() * sizeof(SceneModelTriangle)) == 0));
}
// Instance group of a triangle: 0 opaque / material geometry, 1 water coverage, 2 native water.
inline unsigned group(const Triangle& tri) {
    return tri.water ? (tri.shadowGeometry ? 2u : 1u) : 0u;
}
inline bool hasValidatedOpaque(std::span<const Triangle> triangles) {
    return std::any_of(triangles.begin(), triangles.end(),
                       [](const Triangle& tri) { return !tri.water && tri.shadowGeometry; });
}
// A camera-anchored shell (underwater tint box, sky dome or sky barrels) that must not occlude rays.
bool cameraShell(const SceneModelMesh& mesh, const float* transform, Point camera);
// kTriangleAttributes float4 attributes of one triangle (`normals` = its three vertex normals, else the face normal).
// Returns 1 when the triangle has a material texture below `textures`.
uint32_t packAttributes(const Triangle& tri, size_t textures, std::vector<Attribute>& out,
                        const Point* normals = nullptr);
// The attributes of a native model mesh (its texture slots start at nativeTextureBase); `normals` are its welded vertex
// normals, three per triangle. Optional packedNormals contains the exact packed value per corner, in triangle order.
// Returns the textured triangle count.
uint32_t packModelAttributes(const SceneModelMesh& mesh, const SceneModels& models, uint32_t nativeTextureBase,
                             const std::vector<Point>& normals, std::vector<Attribute>& out,
                             std::span<const float> packedNormals = {}, std::span<const uint32_t> alphaCoverage = {});
// Moves attributes packed by packModelAttributes to a new nativeTextureBase (the frame's texture count changes often
// while the camera moves); the result equals a fresh pack with the same models.
void rebaseModelAttributes(const SceneModelMesh& mesh, const SceneModels& models, uint32_t nativeTextureBase,
                           std::vector<Attribute>& attributes);
} // namespace gsr::surface
