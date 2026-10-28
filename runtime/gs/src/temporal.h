// Temporal upscaling support for the D3D11 backend (optional module, see docs/reference/dlss.md).
//
// Nothing in here is DLSS specific except dlss_ngx.cpp: `Upscaler` is the abstract interface, `createDlssUpscaler` is
// provided either by dlss_ngx.cpp (NVIDIA NGX, Windows / NVIDIA only, built when GSR_DLSS=ON and the SDK is present) or
// by dlss_stub.cpp (returns nullptr). The core renderer only talks to `TemporalPipeline` and to a null `Upscaler`
// pointer; with the module off (or missing) not a single code path of the core changes.
#pragma once
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gsr {

// ---- abstract temporal upscaler (one instance = one feature configuration: sizes, quality mode, preset)
// ------------------------------------------
class Upscaler {
public:
    struct Desc {
        uint32_t renderW = 0, renderH = 0, outW = 0, outH = 0;
        uint32_t mode = 2; // 1 = DLAA, 2 = quality, 3 = balanced, 4 = performance (selects the NGX perf/quality value
                           // and the preset slot)
        uint32_t preset =
            0; // 0 = SDK default for the mode, else 10 = J, 11 = K, 12 = L, 13 = M (NGX render preset hint)
        bool depthInverted = true; // depth buffer: larger = nearer (the GS GEQUAL convention)
    };
    struct Inputs {
        ID3D11Texture2D* color = nullptr; // render resolution colour (top-left renderW x renderH region), jittered
        ID3D11Texture2D* depth = nullptr; // same region, 32 bit float depth
        ID3D11Texture2D* mv = nullptr;    // RG16F motion vectors in render pixels (prev - cur, unjittered)
        ID3D11Texture2D* out = nullptr;   // outW x outH, UAV capable
        float jitterX = 0, jitterY = 0;   // pixels, as applied to the geometry (+x right, +y down)
        float mvScaleX = 1, mvScaleY = 1;
        bool reset = false; // history is invalid (cut / first frame)
    };
    virtual ~Upscaler() = default;
    virtual bool evaluate(const Inputs& in) = 0;
    virtual std::string name() const = 0;
};

// Provided by dlss_ngx.cpp or dlss_stub.cpp. `status` receives a one line human readable result (also when nullptr is
// returned).
std::unique_ptr<Upscaler> createDlssUpscaler(ID3D11Device* dev, ID3D11DeviceContext* ctx, const Upscaler::Desc& d,
                                             std::string& status);
// Cheap capability probe (vendor id + library present), no feature is created.
bool dlssAvailable(ID3D11Device* dev, std::string& status);
// Releases NGX (must run before the device is destroyed).
void dlssShutdown(ID3D11Device* dev);

// ---- camera data captured from the VU1 constant block (VIF1 UNPACK to VU data memory rows 0..6)
// -------------------------------------------------
struct SceneCamera {
    float vp[16] = {};    // 4 qwords of VU row 0..3 = the COLUMNS of the view-projection matrix (clip = M * (x y z 1))
    float scale[4] = {};  // VU row 6
    float offset[4] = {}; // VU row 5
    bool haveVP = false, haveSO = false;
    uint64_t serial = 0; // bumped by every VP upload
};

// ---- the pipeline: jitter, motion vector pass, blit / sharpen, owns the Upscaler
// -------------------------------------------------------------------
class TemporalPipeline {
public:
    TemporalPipeline(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    ~TemporalPipeline();

    // mode: 0 off, 1 DLAA, 2 quality, 3 balanced, 4 performance. Returns the render scale ratio (render / output) of
    // the mode.
    static float ratioForMode(uint32_t mode);

    // geometry of one world frame
    struct Geom {
        uint32_t frameW = 512, frameH = 512; // native frame size (the GS frame region the world is drawn into)
        uint32_t outW = 0, outH = 0;         // frame * internal scale
        uint32_t renderW = 0, renderH = 0;   // sub-rectangle the world is rendered into (top-left of the render target)
    };
    Geom geometry(uint32_t frameW, uint32_t frameH, uint32_t scale, uint32_t mode) const;

    // Halton jitter in render pixels for world frame `idx` (phase count follows the upscale ratio)
    void jitter(uint32_t idx, const Geom& g, float& jx, float& jy) const;

    // Resolve one world frame: `colorRT` holds the jittered world in its top-left renderW x renderH; the upscaled
    // result is written over the top-left outW x outH region of `colorRT`. `depth` is a typeless R32 depth texture with
    // an SRV (the same region). Returns true if the Upscaler ran, false if the plain fallback (bilinear stretch of the
    // sub-rectangle) was used.
    struct ResolveIn {
        ID3D11Texture2D* colorRT = nullptr;
        ID3D11ShaderResourceView* colorSrv = nullptr;
        ID3D11RenderTargetView* colorRtv = nullptr;
        ID3D11Texture2D* depthTex = nullptr;
        ID3D11ShaderResourceView* depthSrv = nullptr;
        Geom g;
        float zmax = 4294967295.0f;
        const SceneCamera* cam = nullptr; // current camera (may be null / incomplete -> fallback)
        const SceneCamera* prev = nullptr;
        float jx = 0, jy = 0;
        bool reset = false;
        uint32_t mode = 2, preset = 0;
        float sharpness = 0.0f;
    };
    bool resolve(const ResolveIn& in);
    bool prepare(const Geom& g, uint32_t mode, uint32_t preset,
                 const SceneCamera& cam); // allocate/create before any jitter or reduced world draw
    void reset();                         // discard feature/history/failure cache; retain shaders and NGX session

    // Plain stretch of the top-left `srcW x srcH` region of `srcSrv` over the top-left `dstW x dstH` region of `dstRtv`
    // (used when entering the world phase: the frame so far, drawn at full resolution, is shrunk into the render
    // rectangle).
    bool stretch(ID3D11ShaderResourceView* srcSrv, uint32_t srcTexW, uint32_t srcTexH, uint32_t srcW, uint32_t srcH,
                 ID3D11RenderTargetView* dstRtv, uint32_t dstW, uint32_t dstH, bool linear, bool preserveAlpha = false);

    bool supported() const { return m_supported; }
    const std::string& status() const { return m_status; }
    std::string upscalerName() const;
    // timing (GPU) of the last resolve, ms (0 when not measured)
    double lastGpuMs() const { return m_gpuMs; }
    void setGpuTiming(bool on) { m_timing = on; }
    uint32_t debugMode =
        0; // 1: write the motion vector magnitude into the output instead of the upscaled colour (DRAKAN_DLSS_DEBUG=mv)

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_supported = false;
    std::string m_status;
    double m_gpuMs = 0;
    bool m_timing = false;
};

} // namespace gsr
