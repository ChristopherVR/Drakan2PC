// The ray-traced scene shared by both renderers' ray passes (D3D11 DXR ray_water.cpp, WebGPU compute
// wgpu_ray.cpp): reconstructed and resident world triangles, their material textures and the per-pass options, plus
// the packing of those triangles into the float4 material attributes both shaders index.
#pragma once
#include "surface_geometry.h"
#include <gsr/scene_models.h>
#include <array>
#include <cstdint>
#include <vector>

namespace gsr::surface {
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
};
using Attribute = std::array<float, 4>;
// Instance group of a triangle: 0 opaque / material geometry, 1 water coverage, 2 native water.
inline unsigned group(const Triangle& tri) {
    return tri.water ? (tri.shadowGeometry ? 2u : 1u) : 0u;
}
// A camera-anchored shell (underwater tint box, sky dome or sky barrels) that must not occlude rays.
bool cameraShell(const SceneModelMesh& mesh, const float* transform, Point camera);
// kTriangleAttributes float4 attributes of one triangle (`normals` = its three vertex normals, else the face normal).
// Returns 1 when the triangle has a material texture below `textures`.
uint32_t packAttributes(const Triangle& tri, size_t textures, std::vector<Attribute>& out,
                        const Point* normals = nullptr);
// The attributes of a native model mesh (its texture slots start at nativeTextureBase); `normals` are its welded vertex
// normals, three per triangle. Returns the textured triangle count.
uint32_t packModelAttributes(const SceneModelMesh& mesh, const SceneModels& models, uint32_t nativeTextureBase,
                             const std::vector<Point>& normals, std::vector<Attribute>& out);
} // namespace gsr::surface
