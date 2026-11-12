// WebGPU backend (docs/proposals/webgpu-renderer.md): the GS renderer's WgpuBackend::Impl on gsr::rhi. The same render
// target cache, texture cache, GS pixel pipeline (gs_shaders_wgsl.h) and display presentation as the Direct3D 11
// backend (d3d11_backend_impl.h), whose policy code the wgpu_*.cpp files keep. The temporal upscaler, neural guides
// and ray-traced water are not part of this backend yet (phase 6): their entry points are inert.
//
// Private header shared by the wgpu_*.cpp files.
#pragma once
#include "gs_internal.h"
#include "gs_backend_shared.h"
#include "gs_texdecode.h"
#include "gs_texup.h"
#include "gs_palette_hd.h"
#include "gs_alpha.h"
#include "hud_page.h"
#include "menu_artwork.h"
#include "sprite_footprint.h"
#include "motion_history.h"
#include "upscale_controls.h"
#include "cpu_profile.h"
#include "rt_upload.h"
#include "rt_upload_runs.h"
#include "rt_texture_source.h"
#include "rt_sync_rows.h"
#include "rt_local_copy.h"
#include "partial_upload.h"
#include "material_detail.h"
#include "temporal_presentation.h"
#include "runtime_flags.h"
#include "rhi.h"
#include "resident_scene.h"
#include "surface_builder.h"
#include "ray_pass_loader.h"
#include "wgpu_ray.h"
#include <gsr/scene_models.h>
#include <gsr/scene_guide_camera.h>
#include <gsr/object_motion_scope.h>

#include <gpu/Gpu.h>

#include <algorithm>
#include <set>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace gsr {

namespace wgpu_detail {
// gs_shaders_wgsl.h kWgslRect
struct RectConsts {
    float rect[4];
    float target[4];
    float color[4];
    uint32_t mode[4];
};

// A sampled texture as the shared texture cache holds it (GpuTexture of gs_backend_shared.h).
using GpuTexture = rhi::TexturePtr;
using UpEntry = UpEntryOf<GpuTexture>;
using UpDone = UpDoneOf<GpuTexture>;
using TexEntry = TexEntryOf<GpuTexture>;
// What a draw binds to sample a texture or a target.
using GpuView = rhi::Texture*;

struct RT {
    uint32_t fbp = 0, fbw = 1, cls = 0, psm = 0; // cls: 0 = 32-bit, 1 = 16-bit
    uint32_t w = 0, h = 0;                       // native pixels
    rhi::TexturePtr tex, copy;                   // target (scaled) and its destination / feedback copy
    bool dirty = false;
    uint64_t writes = 0;
    uint64_t drawFrame = 0;
    uint32_t dy0 = 0, dy1 = 0;
    std::vector<uint64_t> rowStamp;
    std::vector<uint8_t> rowFromMemory; // see the D3D11 RT: 1 = row filled from memory, 0 = holds read-back draws
    // A row-range refresh (syncRows) left rows outside its range unchecked: `seen` must not short-cut the next scan.
    bool partialSync = false;
    uint64_t seen = 0;
    uint64_t lastUse = 0;
    bool copyValid = false;
    // Blocks an applied upload made identical on the GPU and in memory (completeTargetUpload), valid while rt.writes
    // still equals cleanWrites: any later GPU write invalidates the whole set. See d3d11_targets.cpp.
    std::vector<uint64_t> cleanBlocks;
    std::unordered_map<uint32_t, uint32_t> cleanWords; // kCaps.partialUploads: never filled here
    uint64_t cleanWrites = ~0ull;
    hud::CanvasFrame inventoryCanvasFrame;
    uint64_t displaySeenFrame = ~0ull;
    uint32_t displayHeight = 0;
    // World / temporal phase bookkeeping kept by the shared draw policy; the temporal AA world (wgpu_temporal.cpp).
    uint8_t wstate = 0;
    float wjx = 0, wjy = 0; // the open temporal world's jitter (render pixels)
    // The open world's render rectangle (enterTemporalWorld): the temporal AA runs at full resolution, so it is the
    // whole target. The shared draw code scales a world draw by render / frame.
    struct WorldGeom {
        uint32_t frameW = 512, frameH = 512, renderW = 0, renderH = 0;
        float lodBias = 0.0f; // texture LOD bias of a world draw: none at full resolution
    } wgeom;
    uint64_t readbackFrame = ~0ull;
    SceneGuideCamera neuralCamera;
    uint32_t neuralDepth = 0;
    float neuralZmax = 1.0f;
    float neuralXY[2]{}; // GS XYOFFSET of the world (1/16 pixels)
    uint8_t neuralState = 0;
    bool temporalSR = false;
    uint64_t temporalEpoch = 0;
    uint64_t neuralOutputEpoch = 0;
    uint64_t waterOutputEpoch = 0, rayOutputEpoch = 0;
    surface::SurfaceList surface;       // this world's reconstructed surface triangles (written by surfaceBuilder)
    uint64_t surfaceSubmittedEpoch = 0; // the world whose list the builder has started
    uint64_t worldPhaseSeen = 0;
    uint64_t worldEpoch = 0;
    uint32_t pageH() const { return cls == 0 ? 32u : 64u; }
    uint32_t base() const { return fbp * 32u; }
    GpuView view() const { return tex.get(); }
    GpuView copyView() const { return copy.get(); }
};

struct DepthBuf {
    uint32_t zbp = 0, w = 0, h = 0;
    rhi::TexturePtr tex;
    uint64_t lastUse = 0;
};

} // namespace wgpu_detail
using namespace wgpu_detail;

class WgpuBackend final : public BackendApi {
public:
    WgpuBackend(LocalMemory& mem, const Config& cfg, Stats& stats);
    ~WgpuBackend() override;
    bool init() override;
    bool valid() const override;
    std::string adapterName() const override;
    void draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count) override;
    void flushToMemory(uint32_t firstBlock, uint32_t lastBlock) override;
    void flushForRead(uint32_t firstBlock, uint32_t lastBlock, const VramRect* rect = nullptr) override;
    bool deviceLost(std::string* reason = nullptr) const override;
    bool copyLocal(uint32_t spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, uint32_t dpsm, uint32_t dbp,
                   uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) override;
    bool beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                           uint32_t h) override;
    void completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              bool apply, uint64_t written) override;
    void flushAll() override;
    void drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const MovieImage& image, uint64_t imageId) override;
    void present(const DisplayState& d) override;
    void latchDisplay(const DisplayState& d, uint32_t field) override;
    void presentLatched() override;
    void* beginPresentWait() override;
    void endPresentWait(bool ready) override;
    void setVsync(bool on) override;
    bool vsyncEnabled() const override;
    void applyLive(const LiveSettings& ls) override;
    uint32_t internalScale() const override;
    void getViewport(uint32_t& w, uint32_t& h) const override;
    void setOverlayCallback(std::function<void(const OverlayContext&)> cb) override;
    bool capture(const DisplayState& d, Image& out) override;
    bool captureLastGameplay(Image& out, float& aspect) override;
    bool captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) override;
    void resize(uint32_t w, uint32_t h) override;
    void reset() override;
    void endFrame() override;
    void noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* vuRows0to6) override;
    void noteSceneCamera(const SceneGuideCamera& camera) override;
    void noteObjectMotion(const ObjectMotionScope& scope) override;
    void noteSceneGeometry(const SceneGeometry& geometry) override;
    void noteSceneModels(std::shared_ptr<const SceneModels> models) override;
    std::string upscaleStatus() const override;
    double upscaleGpuMs() const override;
    void upscaleCut() override;
    bool upscaleAvailable(std::string& status) const override;
    bool neuralRenderingAvailable(std::string& status) const override;
    std::string neuralRenderingStatus() const override;
    bool waterRenderingAvailable(std::string& status) const override;
    std::string waterRenderingStatus() const override;
    std::string rayTracingStatus() const override;
    static bool waitPresentHandle(void* wait, uint32_t timeoutMs);
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

struct WgpuBackend::Impl {
    // What this backend can do beyond the common interface (gs_backend_shared.h). Textures are created on the render
    // thread (rhi::Device::writeTexture is ordered with the draws); the material and terrain shader fragments exist
    // in HLSL only; there is no velocity program, readback prefetch or GSR_RB_VERIFY reader yet.
    // Clean-block retention and exact partial uploads (preparePartialPatch, patchTargetBlock) are on, as on Direct3D
    // 11: a read of blocks an upload or a row refresh proved equal skips the readback. The skips are exact for GS
    // memory; with automatic texture upscaling a texture in memory a still-dirty target occupies stays at native
    // resolution where the readback path would have upscaled it (docs/journal/2026-11-12/renderer-shared-code.md).
    static constexpr BackendCaps kCaps{.cleanRowRefresh = true, .partialUploads = true};
    // Behind kCaps.terrainDetail / kCaps.materialDetail: declared for the shared code, never called.
    void initTerrain();
    void initMaterial();
    void scanMaterials();
    const material::Record* materialFor(uint64_t packHash) const;
    void loadMaterialLevels(UpDone& d, const std::string& packDir, uint64_t packHash, const material::Record& record,
                            uint32_t logicalW, uint32_t logicalH) const;
    void createMaterialTexture(UpDone& d) const;
    void noteMaterialTexture(uint64_t bytes);
    uint32_t prepareTerrain(const GsDrawState& s, const DrawVertex* v, uint32_t count, bool haveTex, bool texIsTarget,
                            const RT& rt, bool depthMasked, float dsx, float dsy, PSConsts& k);
    bool prepareMaterial(const GsDrawState& s, GpuView materialSrv, uint8_t materialClass, bool haveTex,
                         bool texIsTarget, const RT& rt, bool depthTested, uint32_t pixelFlags, float dsx, float dsy,
                         bool hwBlend);
    // Behind kCaps.motionVectors / temporalUpscaler / readbackPrefetch / readbackVerify: declared, never called.
    bool prepareObjectMotion(RT& rt, const GsDrawState& s, const DrawVertex* vertices, uint32_t count, float sx,
                             float sy);
    // Vertex-program draws (kCaps.vertexPrograms, vertex_program_draw.h): not implemented on this backend yet.
    const VertexProgramBatch* programBatch = nullptr;
    bool prepareProgramMotion(RT& rt, const GsDrawState& s, const VertexProgramBatch& batch, float sx, float sy);
    void bindProgramDraw(const GsDrawState& s, const VertexProgramBatch& batch);
    void drawProgramVertices(const VertexProgramBatch& batch);
    void drawProgramVelocity(RT& rt, DepthBuf& db, const VertexProgramBatch& batch, rhi::Compare depthCompare,
                             bool restore, bool materialDraw, bool useHw);
    void drawVelocity(RT& rt, DepthBuf& db, uint32_t count, rhi::Compare depthCompare, bool restore, bool materialDraw,
                      bool useHw);
    bool upscalePhase(RT& rt, DepthBuf& db, bool isWorld, bool depthGE, uint32_t zbp, float zmax, int sx1, int sy1);
    void logTemporalDraw(const RT& rt, const GsDrawState& s, uint32_t fpsm, int sx0, int sy0, int sx1, int sy1,
                         bool zte, uint32_t ztst);
    void prefetchReadback(RT& rt, bool stale = false);
    void verifyRTBlocks(RT& rt, uint32_t first, uint32_t last, uint32_t rowLo, uint32_t rowHi, uint32_t cmpBase = ~0u,
                        uint32_t cmpFbw = 0, const PartialUpload* requested = nullptr);
    bool readBitsAreClean(const RT& rt, const PartialUpload& requested) const;
    bool beginPartialUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void completePartialUpload(bool apply, uint64_t written);
    bool uploadPartial = false;
    // Surface audit / pick diagnostics only (gs_draw.inl); the ray-scene reconstruction uses surfaceBuilder's camera.
    surface::CachedRasterToWorld surfaceCamera;
    uint64_t surfaceCameraSerial = ~0ull, surfaceXY = 0;
    std::set<uint64_t> surfaceAuditMaterials;
    CpuProfile cpuProfile;
    LocalMemory& mem;
    Config cfg;
    Stats& st;
    uint32_t scale = 1;
    uint32_t upMode = 0, upPreset = 0; // 0 off, else temporal AA (every mode at full resolution)
    float upSharp = 0.0f;
    SceneGuideCamera sceneCamera;
    bool nativeSubtitleOpen = false;
    struct GridCapture {
        bool open = false;
        RT* rt = nullptr;
        float box[4]{};
        uint32_t px[4]{}; // left top right bottom (target pixels)
        rhi::TexturePtr save, tex;
        uint32_t texW = 0, texH = 0;
        uint32_t rtW = 0, rtH = 0;
        float hudScale = 1.0f;
        bool centred = false;
        uint64_t serial = 0, frame = 0;
        uint32_t latches = 0;
    } gridCapture;
    void gridCaptureStart(RT& rt);
    void gridCaptureEnd();
    hud::SubtitleBounds nativeSubtitleBounds;
    hud::TutorialBandRows tutorialBandRows;
    bool neuralWorldClosed = true;
    ObjectMotionScope objectScope;
    motion::History motionHistory{64ull * 1024 * 1024, true};
    uint64_t motionEpoch = 0, worldPhaseSerial = 0;
    bool motionWorldOpen = false;
    bool engineWorldOpen = false;
    bool nativeWaterPass = false;
    SceneGuideCamera waterWorldCamera;
    bool pendingTemporalEnd = false;
    std::shared_ptr<const SceneModels> nativeModels;
    uint64_t motionMatchedBatches = 0, motionMatchedVertices = 0, motionRejectedBatches = 0;
    uint64_t motionPackets = 0, motionEligiblePackets = 0, motionScopedBatches = 0;
    bool neuralEnabled = false;
    // Ray-traced modern water, reflections and shadows (wgpu_ray.cpp, run from finalizeNeuralWorld).
    bool modernWater = runtimeFlags().modernWater;
    bool rayReflections = runtimeFlags().rayReflections;
    bool rayShadows = runtimeFlags().rayShadows;
    bool rayGlobalIllumination = runtimeFlags().rayGlobalIllumination;
    std::unique_ptr<ray::WgpuRay> rayPass;
    // The ray programs, compiled on a background thread when the device allows it (native): rayPass stays null until
    // a world frame adopts them (acquireRayPass), and frames meanwhile draw without ray effects. Never waits.
    raystatus::PassLoader<ray::WgpuRay::Programs> rayLoader;
    ray::WgpuRay* acquireRayPass();
    void startRayPrewarm();
    bool rayStarting() const { return !rayPass && rayLoader.pending(); }
    surface::ResidentScene residentScene;
    std::vector<surface::Texture> reflectionTextures;
    std::unordered_map<uint64_t, uint32_t> reflectionTextureHashes;
    const std::chrono::steady_clock::time_point waterStartTime = std::chrono::steady_clock::now();
    void traceWorld(RT& rt, bool jittered);
    std::string upStatusText = "Temporal AA off";
    // temporal AA (wgpu_temporal.cpp)
    rhi::ProgramPtr progTaa;
    rhi::TexturePtr taaTex[2]; // history and output, swapped every resolve
    uint32_t taaCurrent = 0;   // taaTex index holding the history
    RT* taaOwner = nullptr;
    uint64_t taaOwnerEpoch = 0, taaLastFrame = 0, taaFrames = 0;
    SceneGuideCamera taaPrevCamera;
    float taaPrevXY[2]{};
    uint32_t taaPrevSize[2]{};
    bool taaPrevValid = false, taaReset = true;
    bool tpLog = false;

    // ---- device ----
    std::unique_ptr<rhi::Device> gpu; // owns the context and the window surface (rhi::Device::open)
    rhi::ProgramPtr progHw, progSb, progDisplay, progPresent, progRect, progGather;
    rhi::TexturePtr gatherTex; // native-resolution centre samples of a scaled target (nativeGather)
    bool nativeReadbackEnabled = runtimeFlags().nativeReadback;
    rhi::TexturePtr dummy; // bound where a texture slot is unused (WebGPU has no null bindings)
    uint32_t swapW = 0, swapH = 0;
    std::string adapter;
    bool ok = false;

    // Optional wide menu backdrops from <texDir>/wide (wgpu_backdrop.cpp).
    struct WideBackdrop {
        rhi::TexturePtr tex;
        float aspect = 0;
    };
    std::unordered_map<std::string, std::vector<WideBackdrop>> wideBackdrops;
    uint64_t wideReplaceFrame = ~0ull;
    const RT* wideReplaceRT = nullptr;
    // The loading screen redraws the grabbed title page as backdrop tiles, which the menu map stretches from 4:3 to the
    // output. From the last title frame until another screen starts, the wide title image (screen8) replaces those
    // tiles.
    std::chrono::steady_clock::time_point titleBackdropAt{};
    bool titleBackdropRecent() const {
        return titleBackdropAt.time_since_epoch().count() != 0 &&
               std::chrono::steady_clock::now() - titleBackdropAt < std::chrono::minutes(2);
    }
    // HD movie frames drawn over the native movie (wgpu_movie.cpp).
    rhi::TexturePtr movieTex;
    uint32_t movieTexW = 0, movieTexH = 0;
    uint64_t movieTexId = ~0ull;
    void drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const MovieImage& image, uint64_t imageId);
    uint32_t inventoryCanvasLogLines = 0;
    TemporalPresentation neuralPresentation;
    uint64_t neuralLastPresent = ~0ull;
    bool latchedNeuralWorld = false, latchedGameWorld = false;
    uint64_t latchedWorldEpoch = 0;

    bool vsyncOn = true;
    std::function<void(const OverlayContext&)> overlayCb; // after the present pass, onto the surface image
    uint32_t latchedW = 0, latchedH = 0, latchedField = 0, latchedScale = 1;
    float latchedHudAnchorX = 0.5f;
    bool latchedWaterWorld = false, lastPresentedWater = false, latchedRayWorld = false, lastPresentedRay = false;
    bool haveLatch = false;
    uint32_t vpW = 0, vpH = 0;
    float vpX = 0, vpY = 0;
    PSConsts cbPSContent{};

    std::vector<std::unique_ptr<RT>> rts;
    // Declared after rts and residentScene: destroyed (and joined) before the targets and scene its jobs use.
    std::unique_ptr<surface::SurfaceBuilder> surfaceBuilder = std::make_unique<surface::SurfaceBuilder>(
        residentScene, runtimeFlags().surfaceAsync, runtimeFlags().surfaceOverlap);
    std::vector<std::unique_ptr<DepthBuf>> depths;
    std::unordered_map<TexKey, TexEntry, TexKeyHash> texCache;
    uint64_t texBytes = 0;
    uint64_t frame = 0;
    bool texLog = runtimeFlags().texLog;

    // ---- texture upscaling / replacement / dump (core in gs_texup.{h,cpp}) ----
    uint32_t upFactorCfg = 0;
    texup::Algo upAlgoCfg = texup::Algo::Auto;
    bool upSync = false, upLog = false, texDump = false, texReplace = true;
    bool texPaletteHd = true;
    uint32_t aniso = 0;
    float mipBias = 0.0f;
    float userMipBias = 0.0f;
    float hudScale = 1.0f;
    bool nativeMenu = false;
    hud::StartupTitle startupTitle{cfg.forcedAspect, cfg.stretchToWindow};
    int nativeMenuScreen = -1;
    float hudSize = 1.0f;
    struct HudBox {
        float x0, y0, x1, y1;
    };
    std::vector<HudBox> hudCur, hudPrev;
    hud::InventoryFrame inventoryHudFrame;
    uint32_t hudCornerLogLines = 0;
    std::unordered_map<uint64_t, SlotInfo> slots;
    std::string winShotPrefix;
    std::vector<uint64_t> winShots;
    size_t winShotNext = 0;
    uint64_t presentCount = 0;
    bool winShotByPresent = false;
    uint64_t hlFrame = 0;
    bool smoothHL = false;
    uint64_t texBudget = 1536ull << 20;
    std::string texDir = "textures", dumpDir;
    std::unordered_map<uint64_t, std::string> replIndex;
    std::unordered_map<uint64_t, uint64_t> aliasIndex;
    bool texBudgetAuto = false;
    std::unordered_set<uint64_t> excluded, dumped;
    std::unordered_map<UpKey, std::shared_ptr<UpEntry>, UpKeyHash> upCache;
    std::unordered_map<uint64_t, std::shared_ptr<PalettePlane>> palettePlanes;
    std::unordered_set<UpKey, UpKeyHash> upInflight;
    uint32_t upGen = 0;
    std::mutex doneMu;
    std::vector<UpDone> done;
    std::atomic<uint32_t> doneCount{0};
    std::atomic<uint64_t> dumpedCount{0};
    uint64_t upBytes = 0;
    uint64_t texPeakBytes = 0;
    std::unique_ptr<texup::Pool> pool;
    bool lodStat = runtimeFlags().lodStat;
    struct LodAcc {
        double area = 0, gs = 0, major = 0, minor = 0, ratio = 0, dMaj = 0, dMin = 0;
        double hist[8] = {};
        double histD[9] = {};
        uint64_t tris = 0;
    } lodAcc;
    int64_t drawLogFirst = -1, drawLogLast = -1;
    int64_t drawLogTbp = -1;
    uint32_t drawLogVerts = 3;
    uint64_t inventoryLogStart = ~uint64_t{0};
    int64_t inventoryLogDelay = -1;
    bool spriteFootprint = true;
    int64_t drawStop = -1, skipA = -1, skipB = -1, drawIndex = 0;
    std::set<int64_t> rtDump;
    int64_t rtDumpFrame = runtimeFlags().rtDumpFrameFirst;
    int64_t rtDumpFrameLast = runtimeFlags().rtDumpFrameLast;
    uint32_t dumpRows = runtimeFlags().rtDumpRows;

    rhi::TexturePtr dispTex;
    rhi::TexturePtr savePreviewTex;
    rhi::TexturePtr menuHoldTex;
    bool menuHoldValid = false;
    uint32_t savePreviewW = 0, savePreviewH = 0;
    float savePreviewAspect = 4.0f / 3.0f, gameplayAspect = 4.0f / 3.0f;
    uint32_t dispW = 0, dispH = 0;
    Impl(LocalMemory& m, const Config& c, Stats& s);
    ~Impl();

    // ---------------------------------------------------------------------------------------------
    // texture quality options / HD texture pack
    static bool iequals(const char* a, const char* b);
    void initTexQuality();
    static uint32_t normUpFactor(uint32_t f);
    void ensurePool();
    void scanTexDir();
    uint32_t pickFactor(uint32_t W, uint32_t H, bool ui = false) const;
    void submitJob(std::function<void()> job, bool urgent = false);
    void pushDone(UpDone&& d);
    void createUpTexture(UpDone& d);
    // Backend interface of the texture cache (see d3d11_backend_impl.h).
    GpuTexture createSampledTexture(const std::vector<uint32_t>* levels, const uint32_t* lw, const uint32_t* lh,
                                    uint32_t count, bool updatable, const char* label);
    void pumpUp(std::chrono::microseconds budget = std::chrono::microseconds(0)); // 0: drain everything
    void adoptUp(TexEntry& e);
    static void resampleBilinear(const std::vector<uint32_t>& img, uint32_t rw, uint32_t rh, uint32_t tw, uint32_t th,
                                 std::vector<uint32_t>& out);
    static void applyGameAlpha(std::vector<uint32_t>& hi, uint32_t tw, uint32_t th, const std::vector<uint32_t>& base,
                               uint32_t W, uint32_t H);
    static void smoothBinomial3(std::vector<uint32_t>& img, uint32_t w, uint32_t h);
    bool setupPaletteHd(TexEntry& e, const std::vector<uint8_t>& indices, uint64_t indexHash,
                        const std::vector<uint32_t>& base, uint32_t W, uint32_t H, uint32_t mxl, const GsDrawState& s,
                        bool ui);
    void setupUpscale(TexEntry& e, const std::vector<uint32_t>& base, uint32_t W, uint32_t H, uint32_t mxl,
                      const GsDrawState& s, bool ui, bool volatileSlot, const uint64_t* decodedHash);

    // ---------------------------------------------------------------------------------------------
    bool init();
    bool configureSurface();
    void resize(uint32_t w, uint32_t h);
    rhi::TexturePtr createTexture(uint32_t w, uint32_t h, uint32_t usage, rhi::Format fmt = rhi::Format::RGBA8,
                                  uint32_t mips = 1, const char* label = nullptr);
    // A placed rectangle (kWgslRect): textured (src) or solid (src null) into `target` (target pixels).
    void drawRect(rhi::Texture& target, const float rect[4], rhi::Texture* src, const float color[4],
                  const rhi::Rect& clip);

    // ---------------------------------------------------------------------------------------------
    // render targets
    void allocRT(RT& rt, uint32_t newH);
    RT* getRT(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t needH);
    void clearInventoryCanvas(RT& rt, bool artwork = false);
    const std::vector<WideBackdrop>& wideBackdropSet(const std::string& name);
    bool drawWideBackdrop(RT& rt, int guiScreen);
    void rowSpan(const RT& rt, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) const;
    static uint32_t packPixel(uint32_t psm, const uint8_t* p);
    static uint32_t unpackPixel(uint32_t psm, uint32_t v);
    // Rows [y0, y0 + rows) of a scaled target at native resolution (one exact centre sample per native pixel), in
    // the top-left of gatherTex; null when unavailable.
    rhi::Texture* nativeGather(RT& rt, uint32_t rows, uint32_t y0);
    void readbackRT(RT& rt);
    // Backend interface of the target policy (gs_targets.inl).
    const uint8_t* beginReadback(RT& rt, uint32_t y0, uint32_t y1, uint32_t& readScale, size_t& pitch);
    void endReadback() {}
    std::vector<uint8_t> readbackPixels; // beginReadback
    void copyTargetTop(RT& src, RT& dst, uint32_t w, uint32_t h);
    bool preparePartialPatch();
    // 8 x 8 native pixels at (bx, by): 64 pairs {bit mask, value}; the masked bits of the target become the value.
    void patchTargetBlock(RT& rt, uint32_t bx, uint32_t by, const uint32_t* patch);
    rhi::ProgramPtr progPatch;                       // kWgslPatch
    rhi::TexturePtr patchTex, patchMask, patchValue; // the patched block (8 x scale square) and its two inputs
    PartialUpload partialUpload;
    void syncRT(RT& rt);
    void syncRows(RT& rt, rt_sync::Rows rows);
    bool copyLocal(uint32_t spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, uint32_t dpsm, uint32_t dbp,
                   uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
    void uploadTargetRect(RT& rt, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    std::vector<uint32_t> uploadPixels, scaledUploadPixels; // uploadTargetRect scratch
    rhi::TexturePtr uploadStage; // native pixels of a refreshed rectangle before the GPU replicates them
    void flushToMemory(uint32_t first, uint32_t last);
    void flushForRead(uint32_t first, uint32_t last, const VramRect* rect = nullptr);
    bool blocksAreClean(const RT& rt, uint32_t first, uint32_t last) const;
    void trimDirtyRows(RT& rt);
    bool beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              bool apply, uint64_t written);
    bool uploadCovers(uint32_t block) const;
    RT* aliasedTarget(uint32_t psm, uint32_t mxl, const std::vector<std::pair<uint32_t, uint32_t>>& spans);
    void copyAliasedBlocks(RT& rt, GpuTexture& texture, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h);
    bool touchesUpload(const RT& rt) const;
    bool uploadRows(const RT& rt, uint32_t& y0, uint32_t& y1) const;
    std::vector<uint64_t> uploadBlocks;
    uint32_t uploadLo = 0, uploadHi = 0;
    bool uploadActive = false;

    // ---------------------------------------------------------------------------------------------
    DepthBuf* getDepth(uint32_t zbp, uint32_t w, uint32_t h);

    // ---------------------------------------------------------------------------------------------
    // textures
    void decodeLevel(uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h, uint64_t texa,
                     const ClutBuf& clut, uint32_t csa, std::vector<uint32_t>& out);
    uint64_t hashClut(const ClutBuf& c, uint32_t psm, uint32_t csa);

    struct TexBind {
        rhi::Texture* srv = nullptr;
        bool isRT = false;
        bool fontAtlas = false, menuArtwork = false, titleArtwork = false;
        RT* rt = nullptr;
        float ox = 0, oy = 0;
        uint32_t mxl = 0;
        float averageColor[3]{.35f, .35f, .35f};
        uint8_t minimumAlpha = 0;
        uint32_t reflectionTexture = ~0u;
        uint32_t factor = 1;
        rhi::Texture* materialSrv = nullptr; // never set: kCaps.materialDetail is false
        uint8_t materialClass = 0;
    };
    RT* findRTForTexture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t w, uint32_t h, float& ox, float& oy);
    std::pair<uint32_t, uint32_t> rowsFromMemory(const RT& rt, uint32_t py, uint32_t h) const;
    bool resolveTexture(const GsDrawState& s, const ClutBuf& clut, TexBind& out, bool uiHint = false);
    bool readBoundTexture(const TexBind& tb, Image& out); // debug dumps
    uint64_t rawHash(const std::vector<std::pair<uint32_t, uint32_t>>& spans) const;

    // ---------------------------------------------------------------------------------------------
    // Copy the target into rt.copy (the whole target, or the rectangle x0 y0 x1 y1 in target pixels).
    GpuView snapshot(RT& rt, const uint32_t* box);
    // Backend interface of the draw (gs_draw.inl).
    void clearTarget(RT& rt, const float rgba[4]);
    bool fixedFunctionBlend() const { return progHw != nullptr; } // needs dual-source blending
    DrawVertex* beginVertices(const DrawVertex* v, uint32_t count);
    void endVertices() {}
    void bindDraw(RT& rt, DepthBuf& db, bool useHw, GpuView texture, GpuView dest, float viewportW, float viewportH,
                  const int32_t scissor[4]);
    void setPass(const PSConsts& k, const rhi::BlendState& blend, const rhi::DepthState& depth);
    void drawVertices(uint32_t count);
    void endDraw() {}
    bool readTarget(RT& rt, uint32_t w, uint32_t h, Image& out, bool keepAlpha = false);
    std::vector<DrawVertex> drawVertexCopy; // beginVertices: the device copies it at draw time
    void logDraw(const GsDrawState& s, const DrawVertex* v, uint32_t count);
    static bool oneToOneSprite(const GsDrawState& s, const DrawVertex* v, uint32_t count);
    void accumulateLod(const GsDrawState& s, const DrawVertex* v, uint32_t count);
    void printLodStat();

    // ---------------------------------------------------------------------------------------------
    // World / temporal phases: the temporal AA world (wgpu_temporal.cpp); the legacy upscaler world stays dormant.
    bool upActive() const { return false; }
    void enterTemporalWorld(RT& rt, uint32_t zbp, float zmax, uint64_t xyoffset);
    void finalizeNeuralWorld(RT& rt);
    void finalizeWorld(RT& rt);
    void finalizeAllWorlds();
    void discardTemporalWorlds();
    void setUpscale(uint32_t mode, uint32_t preset, float sharp);
    void noteVuUpload(uint32_t a, uint32_t n, const float* rows);
    void draw(const GsDrawState& s, const ClutBuf& clut, const DrawVertex* v, uint32_t count);

    // ---------------------------------------------------------------------------------------------
    // display
    void ensureDisp(uint32_t w, uint32_t h);
    bool renderDisplay(const DisplayState& d, uint32_t& outW, uint32_t& outH, bool recordLatch = false);
    void latchDisplay(const DisplayState& d, uint32_t field);
    void present(const DisplayState& d);
    void presentLatched();
    // RGBA (BGRA targets are swizzled) of the top-left w x h of a texture.
    bool readTexture(rhi::Texture& tex, uint32_t w, uint32_t h, Image& out, bool keepAlpha = false);
    bool capture(const DisplayState& d, Image& out);
    bool captureLastGameplay(Image& out, float& aspect);
    bool captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out);
    void flushAll();

    // ---------------------------------------------------------------------------------------------
    // live settings (frame boundary, renderer lock held, core batch flushed)
    void flushTexCaches();
    void changeScale(uint32_t ns);
    void applyLive(const LiveSettings& ls);
    void reset();

    uint32_t hudEmptyFrames = 0;
    void clusterHud();
    void endFrame();
};

} // namespace gsr
