#pragma once
#include "nr_guides.h"
#include "surface_geometry.h"
#include <gsr/scene_models.h>
#include <memory>
#include <span>
#include <string>

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
// DXR 1.1 rays intersect resident pre-cull terrain plus submitted object geometry.
// Object/alpha-tested coverage remains partial; water visibility comes from
// the current native water draws rather than unculling distant water into view.
//
// submit() validates its inputs and orders the D3D11 side of the frame (input copies, then a GPU wait for the ray
// result and the output copy) on the calling thread, the only thread that uses the D3D11 context. Packing, uploads
// and the D3D12 recording/submission run on an internal thread, in submission order; the D3D11 context waits for
// each result on the GPU, never on the CPU.
class RayWater {
public:
    RayWater(ID3D11Device*, ID3D11DeviceContext*);
    ~RayWater();
    bool ready() const;
    // Last completed pass (or failure). Asynchronous passes may still be running; finish() waits for them.
    std::string status() const;
    // Resident level geometry that each pass adds before its per-frame triangles. Positions must not change
    // without a new geometryVersion; any attribute change needs a new attributeVersion. Its acceleration
    // structures persist between frames. setStaticWorld copies the triangles when a version changes;
    // shareStaticWorld keeps the (immutable) vector alive while passes use it.
    void setStaticWorld(std::span<const Triangle> triangles, uint64_t geometryVersion, uint64_t attributeVersion);
    void shareStaticWorld(std::shared_ptr<const std::vector<Triangle>> triangles, uint64_t geometryVersion,
                          uint64_t attributeVersion);
    // Asynchronous pass. Takes this frame's triangles out of `triangles` (left empty, with recycled capacity).
    // Returns false (and changes nothing) when the pass is bypassed or the renderer has faulted.
    bool submit(ID3D11Texture2D* color, ID3D11Texture2D* depth, ID3D11Texture2D* output,
                const nr::guides::FrameConstants&, std::vector<Triangle>& triangles, float seconds, bool reflections,
                std::span<const Texture> textures, std::shared_ptr<const SceneModels> models,
                const RayOptions& options = {});
    // Synchronous convenience form: copies the triangles and returns after the pass has been submitted.
    bool render(ID3D11Texture2D* color, ID3D11Texture2D* depth, ID3D11Texture2D* output,
                const nr::guides::FrameConstants&, std::span<const Triangle>, float seconds, bool reflections,
                std::span<const Texture> textures = {}, const SceneModels* models = nullptr,
                const RayOptions& options = {});
    // Waits until every submitted pass has been recorded and submitted to the GPU.
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace gsr::surface
