#pragma once
#include "gsr/gsr.h"
#include "gs_mem.h"

#include <cstdint>
#include <string>

namespace gsr {

inline uint64_t bitsOf(uint64_t v, unsigned lo, unsigned n) {
    return (v >> lo) & ((n >= 64) ? ~0ull : ((1ull << n) - 1ull));
}

struct DrawVertex {
    float x, y;       // pixels, drawing-area relative (XYOFFSET already subtracted)
    float z;          // raw GS z (0..2^32)
    float fog;        // 0..255
    float r, g, b, a; // 0..255
    float s, t, q;    // ST/Q, or UV in texels (q = 1) when FST
};
static_assert(sizeof(DrawVertex) == 44, "vertex layout");

// Everything a draw needs from the register file (POD, memcmp-comparable).
struct GsDrawState {
    uint64_t frame, zbuf, scissor, test, alpha, tex0, tex1, clamp, miptbp1, miptbp2, texa, fogcol, dimx, xyoffset;
    uint32_t fba;
    uint32_t flags;
    uint32_t clutSerial;
    uint32_t pad;
};

enum DrawFlags : uint32_t {
    DF_TME = 1u << 0,
    DF_FGE = 1u << 1,
    DF_ABE = 1u << 2,
    DF_FST = 1u << 3,
    DF_IIP = 1u << 4,
    DF_COLCLAMP = 1u << 5,
    DF_DTHE = 1u << 6,
    DF_PABE = 1u << 7,
    DF_AA1 = 1u << 8,
    DF_SPRITE = 1u << 9,
    DF_LINE = 1u << 10,
    DF_POINT = 1u << 11,
};

struct ClutBuf {
    uint32_t raw[512] = {};
    bool is16 = false; // CPSM was a 16-bit format
    uint32_t cpsm = 0; // CT32 / CT24 / CT16 / CT16S
    uint32_t serial = 0;
};

struct DisplayCircuit {
    bool enable = false;
    uint32_t fbp = 0, fbw = 0, psm = 0, dbx = 0, dby = 0;
    uint32_t dx = 0, dy = 0, magh = 0, magv = 0, dw = 0, dh = 0; // dw/dh raw (+1 = clocks / lines)
    uint32_t width() const { return (dw + 1) / (magh + 1); }
    uint32_t height() const { return (dh + 1) / (magv + 1); }
};

struct DisplayState {
    uint64_t pmode = 0, smode2 = 0, bgcolor = 0;
    DisplayCircuit c[2];
};

// Queued live settings (Renderer::set*), handed to Backend::applyLive at a frame boundary. A bit in `mask` says the
// field is valid.
struct LiveSettings {
    enum : uint32_t {
        F_ASPECT = 1u << 0,
        F_FILTER = 1u << 1,
        F_DEINT = 1u << 2,
        F_FLICKER = 1u << 3,
        F_SHARP = 1u << 4,
        F_ANISO = 1u << 5,
        F_MIP = 1u << 6,
        F_TEXUP = 1u << 7,
        F_REPLACE = 1u << 8,
        F_DUMP = 1u << 9,
        F_RESCAN = 1u << 10,
        F_SCALE = 1u << 11,
        F_VSYNC = 1u << 12,
        F_LATENCY = 1u << 13,
        F_HUD = 1u << 14,
        F_HLSMOOTH = 1u << 15,
        F_HUDSIZE = 1u << 16,
        F_UPSCALE = 1u << 17,
        F_NEURAL = 1u << 18,
        F_NATIVE_MENU = 1u << 19,
        F_WATER = 1u << 20,
        F_NEURAL_RES = 1u << 21,
    };
    uint32_t mask = 0;
    float forcedAspect = 0.0f;
    bool stretch = false;
    uint32_t filter = 0, deinterlace = 0, aniso = 0, upFactor = 0, upFilter = 0, scale = 1, latency = 1;
    bool flicker = false, sharp = true, mipAuto = true, replace = true, dump = false, vsync = true;
    float mipBias = 0.0f;
    float hudScale = 1.0f;
    float hudSize = 1.0f;
    bool hlSmooth = false;
    bool nativeMenu = false;
    int nativeMenuScreen = -1;
    uint32_t upMode = 0, upPreset = 0; // temporal upscaler (DLSS): mode 0 off .. 4 performance, preset 0 auto / 10..13
    float upSharp = 0.0f;
    bool neural = false;
    bool neuralRender = false; // F_NEURAL_RES: NR on the reduced raster before SR
    bool modernWater = false, rayReflections = true, rayShadows = false;
};

// process wide texture hash exclusion list (gsr::excludeTextureHash)
bool isTextureHashExcluded(uint64_t hash);
bool isPhotographicTexture(uint64_t hash);
// next queued gsr::requestWindowShot path, or false when none is pending
bool takeWindowShotRequest(std::string& path);
struct RuntimeTexture {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels;
    bool fontAtlas = false;
    // registerRuntimeOverlay: pixels apply where coverage > 0; the rest comes from the pack image of the original
    // texture (exact hash, then RGB alias), else from the automatic upscale of the native texture.
    std::vector<uint8_t> coverage;
    uint64_t packHash = 0, packRgbHash = 0;
};
struct SavePreview {
    std::string path;
    bool flipY = false;
    uint64_t version = 0;
};
SavePreview savePreview(uint64_t hash);
std::shared_ptr<const RuntimeTexture> runtimeTexture(uint64_t hash);

// One GS backend (a graphics API): D3D11Backend (d3d11_*.cpp, Windows) or WgpuBackend (wgpu_*.cpp, every target).
class BackendApi {
public:
    virtual ~BackendApi() = default;
    virtual bool init() = 0;
    virtual bool valid() const = 0;
    virtual std::string adapterName() const = 0;
    virtual void draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count) = 0;
    virtual void flushToMemory(uint32_t firstBlock, uint32_t lastBlock) = 0;
    virtual bool beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                   uint32_t h) = 0;
    virtual void completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t h, bool apply) = 0;
    virtual void flushAll() = 0;
    virtual void drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                const MovieImage& image, uint64_t imageId) = 0;
    virtual void present(const DisplayState& d) = 0;
    virtual void latchDisplay(const DisplayState& d, uint32_t field) = 0;
    virtual void presentLatched() = 0;
    virtual void* beginPresentWait() = 0;
    virtual void endPresentWait(bool ready) = 0;
    virtual void setVsync(bool on) = 0;
    virtual bool vsyncEnabled() const = 0;
    virtual void applyLive(const LiveSettings& ls) = 0;
    virtual uint32_t internalScale() const = 0;
    virtual void getViewport(uint32_t& w, uint32_t& h) const = 0;
    virtual void setOverlayCallback(std::function<void(const OverlayContext&)> cb) = 0;
    virtual bool capture(const DisplayState& d, Image& out) = 0;
    virtual bool captureLastGameplay(Image& out, float& aspect) = 0;
    virtual bool captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) = 0;
    virtual void resize(uint32_t w, uint32_t h) = 0;
    virtual void reset() = 0;
    virtual void endFrame() = 0;
    virtual void noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* vuRows0to6) = 0;
    virtual void noteSceneCamera(const SceneGuideCamera& camera) = 0;
    virtual void noteObjectMotion(const ObjectMotionScope& scope) = 0;
    virtual void noteSceneGeometry(const SceneGeometry& geometry) = 0;
    virtual void noteSceneModels(std::shared_ptr<const SceneModels> models) = 0;
    virtual std::string upscaleStatus() const = 0;
    virtual double upscaleGpuMs() const = 0;
    virtual void upscaleCut() = 0;
    virtual bool upscaleAvailable(std::string& status) const = 0;
    virtual bool neuralRenderingAvailable(std::string& status) const = 0;
    virtual std::string neuralRenderingStatus() const = 0;
    virtual bool waterRenderingAvailable(std::string& status) const = 0;
    virtual std::string waterRenderingStatus() const = 0;
    virtual std::string rayTracingStatus() const = 0;
};

class Backend {
public:
    // The implementation follows cfg.api (backend_select.inl): Auto = WebGPU when this build has it, else D3D11.
    Backend(LocalMemory& mem, const Config& cfg, Stats& stats);
    ~Backend();
    bool init();
    bool valid() const;
    std::string adapterName() const;

    // draw a triangle list
    void draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count);
    // make local memory reflect GPU render targets overlapping blocks [first,last]
    void flushToMemory(uint32_t firstBlock, uint32_t lastBlock);
    // host->local image upload into a GPU-owned render target: true = apply it to the target when it completes
    // (completeTargetUpload) instead of reading the target back first
    bool beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    // apply = false: only mark the target rows current (transfer start); true: write the uploaded pixels (completion)
    void completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              bool apply);
    void flushAll();
    // draw an HD movie frame over the rectangle (x, y, w, h) of the frame buffer at block dbp (gs_core movie tracking);
    // imageId identifies the image so a repeated frame is not uploaded again
    void drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const MovieImage& image, uint64_t imageId);
    void present(const DisplayState& d);                      // render the live display + blit to the window
    void latchDisplay(const DisplayState& d, uint32_t field); // render the display into the latched image (vblank)
    void presentLatched();                                    // blit the latched image to the window
    // Unlocked swapchain wait, in three steps: beginPresentWait (renderer lock held) returns a handle to wait on, or
    // null when there is none or the next Present already has its slot; waitPresentHandle (no lock) waits and releases
    // it; endPresentWait (lock held) records the result.
    void* beginPresentWait();
    static bool waitPresentHandle(void* wait, uint32_t timeoutMs); // any backend's handle
    void endPresentWait(bool ready);
    void setVsync(bool on);
    bool vsyncEnabled() const;
    void applyLive(const LiveSettings& ls); // frame boundary only, renderer lock held, pending batch already flushed
    uint32_t internalScale() const;
    void getViewport(uint32_t& w, uint32_t& h) const;
    void setOverlayCallback(std::function<void(const OverlayContext&)> cb);
    bool capture(const DisplayState& d, Image& out);
    bool captureLastGameplay(Image& out, float& aspect);
    bool captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out);
    void resize(uint32_t w, uint32_t h);
    void reset();
    void endFrame();
    // temporal upscaler (optional module, temporal.h): scene camera from the VU1 constant block, status text, GPU time
    // of the last resolve
    void noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* vuRows0to6);
    void noteSceneCamera(const SceneGuideCamera& camera);
    void noteObjectMotion(const ObjectMotionScope& scope);
    void noteSceneGeometry(const SceneGeometry& geometry);
    void noteSceneModels(std::shared_ptr<const SceneModels> models);
    std::string upscaleStatus() const;
    double upscaleGpuMs() const;
    void upscaleCut();
    bool upscaleAvailable(std::string& status) const;
    bool neuralRenderingAvailable(std::string& status) const;
    std::string neuralRenderingStatus() const;
    bool waterRenderingAvailable(std::string& status) const;
    std::string waterRenderingStatus() const;
    std::string rayTracingStatus() const;

    Config::Api api() const { return m_kind; }

private:
    std::unique_ptr<BackendApi> m_api;
    Config::Api m_kind = Config::Api::Auto;
};

} // namespace gsr
