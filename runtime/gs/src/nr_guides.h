#pragma once
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <span>

// No renderer, NR bridge, Streamline or engine header dependencies.
namespace gsr::nr::guides {
struct Size {
    uint32_t width = 0, height = 0;
};
struct Camera {
    // Same storage as SceneCamera: VU qword i is column i, clip = VP * world.
    float vp[16]{};
    // Floating VU viewport values BEFORE FTOI4. XY are native pixels;
    // raw GS Z = trunc(16 * (offset.z + scale.z * clip.z/clip.w)).
    float scale[4]{}, offset[4]{};
    // GS XYOFFSET register values, in 1/16 native pixels; NOT assumed centered.
    float xyOffset[2]{};
    bool valid = false;
};
struct Frame {
    ID3D11ShaderResourceView* sourceColor = nullptr;
    ID3D11ShaderResourceView* sourceDepth = nullptr; // R32_FLOAT, GS Z/zmax
    ID3D11ShaderResourceView* sourceObjectMotion =
        nullptr;                     // optional RGBA32F: pixel motion, valid mask, exact GS depth
    Size colorTexture, depthTexture; // actual SRV mip sizes, can differ
    Size frame;                      // native GS drawing area
    Size guide;                      // exact active color/depth rectangle size
    uint32_t colorX = 0, colorY = 0, depthX = 0, depthY = 0;
    Camera current, previous;
    float zmax = 4294967295.0f;
    // GS-normalized endpoints. Output d=(sourceDepth-far)/(near-far).
    // Larger source GS Z MUST mean nearer. The same mapping transforms VP.
    float nearGs = 1.0f, farGs = 0.0f;
    float skyEpsilon = 1e-7f;
    float jitterPixels[2]{};          // current raster jitter, excluded from VP
    float maxMotionPixels = 16384.0f; // excessive/out-of-bounds reprojections -> 0
    bool reset = true;
    uint32_t frameIndex = 0;
};
// Self-contained guide-buffer description (no vendor dependency).
// Matrices are row-major ROW-VECTOR, unjittered: p' = mul(p, M).
struct FrameConstants {
    float currentViewToClip[16]{}, clipToCameraView[16]{};
    float clipToPrevClip[16]{}, prevClipToClip[16]{};
    float cameraPosition[3]{}, cameraUp[3]{}, cameraRight[3]{}, cameraForward[3]{};
    float nearPlane = 0, farPlane = 0, verticalFov = 0, aspectRatio = 0;
    float motionScale[2]{}, jitterPixels[2]{};
    bool depthInverted = true, cameraMotionIncluded = true, orthographic = false;
    bool reset = true;
    uint32_t frameIndex = 0;
    bool infiniteFar = false; // farPlane=FLT_MAX sentinel; projection stays exact
};
struct Texture {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
};
struct Textures {
    Texture color, depth, motion, output; // RGBA16F, R32F, RG16F, RGBA16F
    Size size;                            // all four EXACTLY this size
    bool historyReset = true;
};
struct Shaders {
    std::vector<uint8_t> prepare, vertex, compose, modelVertex, modelPixel;
};
struct ModelMotionVertex {
    float current[3]{}, previous[3]{}, uv[2]{};
    uint32_t alphaTexture = ~0u;
};
struct ModelMotionTexture {
    uint64_t identity = 0;
    std::span<const uint32_t> pixels;
};

namespace detail {
// Vertex of the model-motion pass and its CPU projection (exposed for tests): projectModelVertices is the SIMD form of
// projectModelVerticesReference and must match it bit for bit. Both return nullptr or the failure status.
struct ModelGpuVertex {
    float current[4], previous[4], uv[2];
    uint32_t texture;
};
static_assert(sizeof(ModelGpuVertex) == 44);
const char* projectModelVertices(std::span<const ModelMotionVertex> vertices,
                                 std::span<const ModelMotionTexture> textures, const double* cur, const double* prev,
                                 ModelGpuVertex* out);
const char* projectModelVerticesReference(std::span<const ModelMotionVertex> vertices,
                                          std::span<const ModelMotionTexture> textures, const double* cur,
                                          const double* prev, ModelGpuVertex* out);
} // namespace detail

// Offline CPU-only D3DCompile of embedded HLSL, cs/vs/ps_5_0. No device needed.
bool compileShaders(Shaders&, std::string& status);
const char* shaderSource(); // optional fxc/build integration

class Guides final {
public:
    // Requires FL11+, immediate context, D3D11.1 context-state isolation.
    // Pass offline-compiled shaders or nullptr to compile the identical HLSL here.
    Guides(ID3D11Device*, ID3D11DeviceContext*, const Shaders* = nullptr);
    ~Guides();
    Guides(const Guides&) = delete;
    Guides& operator=(const Guides&) = delete;
    bool ready() const;
    const std::string& status() const;
    bool prepare(const Frame&);
    // Rasterize verified native current/previous poses over camera motion.
    // Only depth-matching, opaque or alpha-tested fragments may replace it.
    bool modelMotion(std::span<const ModelMotionVertex>, std::span<const ModelMotionTexture>);
    // Lifetime allocation count for diagnosing motion-guide resource churn.
    uint64_t modelAlphaAllocations() const;
    // Borrowed handles, invalid after resize/destruction. Failed prepare clears
    // allocated guides/output and marks the result unusable until next success.
    Textures textures() const;
    // Derive true projection/basis from a VERIFIED row-vector worldToView.
    // VP alone cannot supply this. Fails on non-rigid views or non-D3D/reverse-Z
    // projection endpoints; caller must not submit fabricated camera constants.
    bool frameConstants(const float worldToView[16], FrameConstants&);
    // After external NR evaluation writes output. Writes ONLY RGB to the guide-
    // sized top-left destination region: destination alpha is bit-for-bit kept.
    // Destination must be distinct from output. Caller owns NR success/fallback.
    bool composeRgb(ID3D11RenderTargetView* destination, bool useOutput = true);
    void reset(); // zero guides/output, force reset on the next prepare
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace gsr::nr::guides
