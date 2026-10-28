// Direct3D 11 backend: render target cache (FBP keyed), depth buffers (ZBP keyed), texture cache decoded from GS
// local memory, uber pixel shader implementing the GS pixel pipeline, display (PMODE/DISPFB) presentation.
//
// Private header shared by the d3d11_*.cpp files: the internal types and the Backend::Impl class. Member
// functions are defined in d3d11_device / textures / targets / world / draw / present / capture .cpp and
// d3d11_backend.cpp holds the public Backend API.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include "gs_internal.h"
#include "gs_texdecode.h"
#include "gs_texup.h"
#include "gs_palette_hd.h"
#include "gs_shaders.h"
#include "gs_alpha.h"
#include "hud_page.h"
#include "menu_artwork.h"
#include "sprite_footprint.h"
#include "temporal.h"
#include "motion_history.h"
#include "model_motion.h"
#include "nr_guides.h"
#include "sr_combined.h"
#include "upscale_controls.h"
#include "cpu_profile.h"
#include "rt_upload.h"
#include "rt_readback.h"
#include "temporal_presentation.h"
#include "surface_geometry.h"
#include "ray_water.h"
#include "surface_builder.h"
#include "runtime_flags.h"
#include <gsr/scene_models.h>
#include "resident_scene.h"
#include <gsr/scene_guide_camera.h>
#include <gsr/object_motion_scope.h>

#include <algorithm>
#include <set>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace gsr {

namespace d3d11_detail {
// PS constant flag bits (mirror gs_shaders.h)
enum : uint32_t {
    PF_TME = 1u << 0,
    PF_FGE = 1u << 1,
    PF_FST = 1u << 2,
    PF_TCC = 1u << 3,
    PF_ATE = 1u << 6,
    PF_DATE = 1u << 12,
    PF_DATM = 1u << 13,
    PF_ABE = 1u << 14,
    PF_PABE = 1u << 15,
    PF_DTHE = 1u << 16,
    PF_CCLAMP = 1u << 17,
    PF_FBA = 1u << 18,
    PF_FB16 = 1u << 19,
    PF_TEXRT = 1u << 22,
};

struct PSConsts {
    float rt[4];
    uint32_t a[4];
    uint32_t b[4];
    uint32_t c[4];
    float fog[4];
    uint32_t d[4];
    float e[4];
    float src[4];
    float f[4];
    float g[4];
    float h[4];
    float spriteBounds[4];
};
static_assert(sizeof(PSConsts) % 16 == 0, "cb alignment");

struct DispConsts {
    float r1[4], r2[4], bg[4];
    uint32_t m[4];
};

inline uint32_t roundUp(uint32_t v, uint32_t a) {
    return (v + a - 1) / a * a;
}

struct TexKey {
    uint32_t tbp, tbw, psm, tw, th;
    uint32_t mxl, clutKind;
    uint32_t pad;
    uint64_t clutHash, texa, mip1, mip2;
    bool operator==(const TexKey& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};
struct TexKeyHash {
    size_t operator()(const TexKey& k) const {
        const uint64_t* p = reinterpret_cast<const uint64_t*>(&k);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(TexKey) / 8; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return (size_t)h;
    }
};
static_assert(sizeof(TexKey) % 8 == 0, "TexKey must have no padding");

// upscaled / replaced texture, shared by every TexEntry with the same content (hash of the decoded level 0 + dims)
struct UpKey {
    uint64_t hash;
    uint32_t factor;
    uint32_t packed; // algo | wrapS << 8 | wrapT << 9 | mxl << 12 | kind << 16 (0 upscale, 1 replacement)
    bool operator==(const UpKey& o) const { return hash == o.hash && factor == o.factor && packed == o.packed; }
};
struct UpKeyHash {
    size_t operator()(const UpKey& k) const {
        return (size_t)(k.hash ^ ((uint64_t)k.factor * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)k.packed << 20));
    }
};
struct UpEntry {
    ComPtr<ID3D11ShaderResourceView> srv; // null = the job failed, keep the native texture
    ComPtr<ID3D11Texture2D> tex;
    uint32_t factor = 1;
    uint64_t bytes = 0;
    uint64_t lastFrame = 0;
};
struct UpDone {
    UpKey key;
    uint32_t factor = 1, w = 0, h = 0; // physical level 0 size
    std::vector<std::vector<uint32_t>> levels;
    uint64_t us = 0;
    bool replaced = false;
    uint32_t gen = 0; // upscale cache generation the job was started in; stale results are dropped
};

struct TexEntry {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    std::shared_ptr<UpEntry> up; // upscaled / replaced version (preferred when set)
    UpKey upKey{};
    bool upWanted = false;
    bool fontAtlas = false, menuArtwork = false;
    uint64_t previewVersion = 0, previewHash = 0;
    uint64_t paletteWait = 0; // index-plane hash whose HD index map was still building: decode again once it is ready
    float averageColor[3]{.35f, .35f, .35f};
    uint8_t minimumAlpha = 0;
    uint32_t reflectionTexture = ~0u;
    uint64_t stamp = 0;
    uint64_t checkedClock = 0;
    uint64_t lastFrame = 0;
    uint64_t bytes = 0;
    uint64_t rawHash = 0;              // LocalMemory::hashBlocks over spans when decoded
    const void* aliasTarget = nullptr; // RT whose dirty blocks were copied in from its GPU texture
    uint64_t aliasWrites = 0;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    std::vector<std::vector<uint32_t>>
        cpu; // CPU copies of the levels (only kept when Config::onTexture is set, for the audit)
    uint32_t w = 0, h = 0;
};

// Per texture-memory slot bookkeeping for palette / pixel animated textures (see resolveTexture): a slot that is
// re-decoded many times in a short time is "volatile" (the highlighted menu entry, water / fire palette effects); it is
// never dumped beyond the first variants and never replaced / upscaled.
struct SlotInfo {
    uint64_t windowStart = 0, lastDecode = 0, lastHash = 0;
    uint32_t count = 0, changes = 0, sameRun = 0;
    bool isVolatile = false, hashValid = false;
    // indexed volatile slots: index-plane hash of the last decode; unchanged indices with a new decode = palette-only
    // animation, which gets the HD index map path (gs_palette_hd.h)
    uint64_t lastIndexHash = 0;
    bool indexValid = false;
};

// HD index map of one palette-animated index plane, built once on the worker pool (ready is set last).
struct PalettePlane {
    std::atomic<bool> ready{false};
    bool fromPack = false; // guided by an HD pack image (preferred) instead of the live upscaler
    uint32_t factor = 1;
    std::vector<uint8_t> map; // (w * factor) x (h * factor) palette indices
    uint64_t lastFrame = 0;
};

struct RT {
    uint32_t fbp = 0, fbw = 1, cls = 0, psm = 0; // cls: 0 = 32-bit, 1 = 16-bit
    uint32_t w = 0, h = 0;                       // native pixels
    ComPtr<ID3D11Texture2D> tex, copy;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv, copySrv;
    bool dirty = false;
    uint64_t writes = 0;    // GPU content changes (draws, uploads); see aliased textures in resolveTexture
    uint64_t drawFrame = 0; // frame of the last draw into this target
    uint32_t dy0 = 0, dy1 = 0;
    std::vector<uint64_t> rowStamp;
    uint64_t seen = 0;
    uint64_t lastUse = 0;
    bool copyValid = false;
    hud::CanvasFrame inventoryCanvasFrame;
    uint64_t displaySeenFrame = ~0ull;
    uint32_t displayHeight = 0;
    // temporal upscaler (temporal.h): 0 = full resolution, 1 = the 3D world of this frame is being drawn into the
    // top-left render rectangle, 2 = world resolved, the rest of the frame (HUD, fades) is drawn at full resolution;
    // back to 0 when the target was displayed
    uint8_t wstate = 0;
    TemporalPipeline::Geom wgeom;
    SceneCamera wcam;
    float wjx = 0, wjy = 0;
    uint32_t wzbp = 0;
    float wzmax = 4294967295.0f;
    uint64_t wframe = 0; // vsync counter at the last state change
    uint8_t neuralState = 0;
    bool temporalSR = false;
    uint64_t temporalEpoch = 0;
    uint64_t neuralOutputEpoch = 0;
    ComPtr<ID3D11Texture2D> compareTex;
    ComPtr<ID3D11RenderTargetView> compareRtv;
    ComPtr<ID3D11ShaderResourceView> compareSrv;
    uint64_t compareEpoch = 0;
    uint64_t waterOutputEpoch = 0, rayOutputEpoch = 0;
    surface::SurfaceList surface;       // this world's reconstructed surface triangles (written by surfaceBuilder)
    uint64_t surfaceSubmittedEpoch = 0; // GS worker side: last epoch a surface job was submitted for
    uint64_t worldPhaseSeen = 0;
    uint64_t worldEpoch = 0; // world ownership also exists with SR/NR disabled
    SceneGuideCamera neuralCamera;
    uint32_t neuralDepth = 0;
    float neuralZmax = 4294967295.0f;
    float neuralXY[2]{};
    ComPtr<ID3D11Texture2D> motionTex;
    ComPtr<ID3D11RenderTargetView> motionRtv;
    ComPtr<ID3D11ShaderResourceView> motionSrv;
    uint64_t motionEpoch = 0;
    uint32_t pageH() const { return cls == 0 ? 32u : 64u; }
    uint32_t base() const { return fbp * 32u; }
};

struct DepthBuf {
    uint32_t zbp = 0, w = 0, h = 0;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11Texture2D> copy;
    ComPtr<ID3D11ShaderResourceView> copySrv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> srv; // only when the temporal upscaler is active (typeless R32 texture)
    uint64_t lastUse = 0;
};

} // namespace d3d11_detail
using namespace d3d11_detail;

struct Backend::Impl {
    CpuProfile cpuProfile;
    LocalMemory& mem;
    Config cfg;
    Stats& st;
    uint32_t scale = 1;
    // ---- temporal upscaler (DLSS, optional): see temporal.h / docs/reference/dlss.md. upMode 0 = everything below is
    // dormant.
    uint32_t upMode = 0, upPreset = 0;
    float upSharp = 0.0f;
    SceneCamera camCur, camPrev;
    SceneGuideCamera sceneCamera;
    bool nativeSubtitleOpen = false; // ordered native Subtitle_Draw scope
    // Off-screen capture of a host screen's native item grid (marker sourcePc 0x437170, reserved 1 = begin / 0 = end;
    // viewportOffset = the grid's box in GUI frame units): the region the grid's draws touch is saved at the scope's
    // first display draw, copied into `tex` at its end and restored, so the icons can be cropped from `tex` while the
    // presented frame shows what was there before (the host I strip, drakan_settings_host.cpp).
    struct GridCapture {
        bool open = false;
        RT* rt = nullptr;
        float box[4]{};
        D3D11_BOX px{};
        ComPtr<ID3D11Texture2D> save, tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        uint32_t texW = 0, texH = 0;
        DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
        uint32_t rtW = 0, rtH = 0; // native size of the captured target
        float hudScale = 1.0f;
        bool centred = false;
        uint64_t serial = 0, frame = 0;
        uint32_t latches = 0; // display latches while open
    } gridCapture;
    void gridCaptureStart(RT& rt);
    void gridCaptureEnd();
    hud::SubtitleBounds nativeSubtitleBounds;
    hud::TutorialBandRows tutorialBandRows;
    bool neuralWorldClosed = true;
    ObjectMotionScope objectScope;
    motion::History motionHistory{64ull * 1024 * 1024, true};
    motion::Models modelMotion;
    motion::Models::Frame modelFrame; // reused every frame (capacity kept)
    // Model motion prepared on the surface builder when the scene models arrive (world start); used by
    // finalizeNeuralWorld after its drain when the scene and the committed previous scene are still the ones used.
    struct PendingModels {
        std::shared_ptr<const SceneModels> scene, previous;
        motion::Models::Frame frame;
        motion::Models::Scratch scratch;
        bool ready = false;
    } pendingModels;
    std::map<motion::Key, uint32_t> motionOrdinals;
    uint64_t motionEpoch = 0, motionGuideSerial = 0, worldPhaseSerial = 0;
    RT* temporalOwner = nullptr;
    uint64_t temporalOwnerEpoch = 0;
    bool motionWorldOpen = false;
    // Engine draw phase includes sky/backdrops before a validated camera. It
    // controls UI classification only; temporal history uses validated cameras.
    bool engineWorldOpen = false;
    bool nativeWaterPass = false;
    // Last Valid|World camera of the open engine phase; reconstructs native water draws when a mid-world preview
    // viewport has replaced sceneCamera (see d3d11_draw.cpp).
    SceneGuideCamera waterWorldCamera;
    bool pendingTemporalEnd = false;
    std::shared_ptr<const SceneModels> nativeModels;
    uint64_t motionMatchedBatches = 0, motionMatchedVertices = 0, motionRejectedBatches = 0;
    uint64_t motionPackets = 0, motionEligiblePackets = 0, motionScopedBatches = 0;
    uint32_t motionValidationSamples = 0;
    std::vector<motion::Vertex> motionVertices;
    std::vector<motion::Previous> motionPrevious;
    bool camPrevValid = false, upReset = true, upFault = false;
    uint32_t worldFrames = 0; // world frames resolved so far (Halton index)
    TemporalPipeline::Geom lastWorldGeom;
    uint64_t lastWorldFrame = 0; // vsync counter of the last resolve
    float curJx = 0, curJy = 0;  // jitter (render pixels) of the world frame in progress
    bool tpLog = false, tpTiming = false;
    std::string upStatusText;

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11DeviceContext1> inventoryContext1;
    ComPtr<ID3D11PixelShader> psInventoryCanvas;
    // Optional wide menu backdrops from <texDir>/wide (d3d11_backdrop.cpp).
    struct WideBackdrop {
        ComPtr<ID3D11ShaderResourceView> srv;
        float aspect = 0;
    };
    std::unordered_map<std::string, std::vector<WideBackdrop>> wideBackdrops;
    ComPtr<ID3D11PixelShader> psWideBackdrop;
    bool wideBackdropShaderFailed = false;
    // A screen-specific wide image (screen<N>_*) carries the whole picture: the engine's own art is skipped on the
    // frame/target where it was drawn. `default_*` images only fill margins.
    uint64_t wideReplaceFrame = ~0ull;
    const RT* wideReplaceRT = nullptr;
    // HD movie frames drawn over the native movie (d3d11_movie.cpp, docs/reference/fmv.md section 9).
    ComPtr<ID3D11Texture2D> movieTex;
    ComPtr<ID3D11ShaderResourceView> movieSrv;
    ComPtr<ID3D11PixelShader> psMovie;
    ComPtr<ID3D11SamplerState> smpMovie;
    uint32_t movieTexW = 0, movieTexH = 0;
    uint64_t movieTexId = ~0ull;
    bool movieShaderFailed = false;
    void drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                        const MovieImage& image, uint64_t imageId);
    uint32_t inventoryCanvasLogLines = 0;
    std::unique_ptr<TemporalPipeline> tp; // after dev / ctx: destroyed first
    std::unique_ptr<nr::guides::Guides> neuralGuides;
    std::unique_ptr<sr_combined::Pipeline> combinedSr;
    bool neuralEnabled = false;
    bool neuralAtRender = false; // NR on the reduced raster before SR (cfg.neuralAtRenderResolution / live setting)
    std::string neuralStatusText = "DLSS 5 neural rendering off";
    TemporalPresentation neuralPresentation;
    bool modernWater = runtimeFlags().modernWater;
    bool rayReflections = runtimeFlags().rayReflections;
    bool rayShadows = runtimeFlags().rayShadows;
    std::unique_ptr<surface::RayWater> waterRenderer;
    surface::ResidentScene residentScene;
    std::vector<surface::Texture> reflectionTextures;
    std::unordered_map<uint64_t, uint32_t> reflectionTextureHashes;

    const std::chrono::steady_clock::time_point waterStartTime = std::chrono::steady_clock::now();
    // Surface audit/pick diagnostics only; the ray-scene reconstruction uses surfaceBuilder's own camera.
    surface::CachedRasterToWorld surfaceCamera;
    uint64_t surfaceCameraSerial = ~0ull, surfaceXY = 0;
    std::set<uint64_t> surfaceAuditMaterials;
    SceneGuideCamera neuralPrevious;
    float neuralPreviousXY[2]{};
    bool neuralReset = true;
    uint64_t neuralLastPresent = ~0ull;
    bool latchedNeuralWorld = false, latchedGameWorld = false;
    uint64_t latchedWorldEpoch = 0;
    bool neuralValidated = false;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> swapRtv;
    uint32_t swapW = 0, swapH = 0;
    std::string adapter;
    bool ok = false;

    ComPtr<ID3D11VertexShader> vs, vsFull;
    ComPtr<ID3D11VertexShader> vsMotion;
    ComPtr<ID3D11PixelShader> psMotion;
    ComPtr<ID3D11InputLayout> motionLayout;
    ComPtr<ID3D11Buffer> motionVb;
    uint32_t motionVbCapacity = 0, motionVbOffset = 0, motionVbDrawOffset = 0; // ring: NO_OVERWRITE appends
    ComPtr<ID3D11PixelShader> psHw, psSb, psDisplay, psBlit, psBlitPt, psPresent, psCopyRGBA, psStretchDepth;
    ComPtr<ID3D11Buffer> cbPresent;
    HANDLE swapWait = nullptr;    // frame-latency waitable object (flip model)
    bool presentSlotHeld = false; // swapWait was consumed; the next Present uses that slot
    bool tearingOk =
        false; // DXGI_FEATURE_PRESENT_ALLOW_TEARING (VRR / G-SYNC: vsync off presents without waiting for the blank)
    UINT swapFlags() const {
        return DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
               (tearingOk ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
    }
    bool vsyncOn = true;
    std::function<void(const OverlayContext&)> overlayCb; // settings overlay hook, see gsr.h
    uint32_t latchedW = 0, latchedH = 0, latchedField = 0,
             latchedScale = 1; // native size of the latched display image, scale it was rendered at
    float latchedHudAnchorX = 0.5f;
    uint64_t worldShotEpoch = 0;
    ComPtr<ID3D11Texture2D> compareDisplayTex;
    ComPtr<ID3D11ShaderResourceView> compareDisplaySrv;
    bool latchedWaterWorld = false, lastPresentedWater = false, latchedRayWorld = false, lastPresentedRay = false;
    bool compareReady = false;
    uint64_t compareSaved = 0;
    bool haveLatch = false;
    uint32_t vpW = 0, vpH = 0;
    float vpX = 0, vpY = 0; // present viewport origin (back buffer pixels), reported to the overlay callback
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb, cbPS, cbDisp;
    PSConsts cbPSContent{}; // last upload to cbPS (draw() is its only writer)
    bool cbPSValid = false;
    uint32_t vbCapacity = 0, vbOffset = 0;
    ComPtr<ID3D11SamplerState> smpPt, smpLin;
    ComPtr<ID3D11RasterizerState> rast;
    std::unordered_map<uint32_t, ComPtr<ID3D11BlendState>> blendStates;
    std::unordered_map<uint32_t, ComPtr<ID3D11DepthStencilState>> depthStates;
    ComPtr<ID3D11BlendState> blendOff;

    std::vector<std::unique_ptr<RT>> rts;
    // Declared after rts and residentScene: destroyed (and joined) before the targets and scene its jobs use.
    std::unique_ptr<surface::SurfaceBuilder> surfaceBuilder =
        std::make_unique<surface::SurfaceBuilder>(residentScene, runtimeFlags().surfaceAsync);
    std::vector<std::unique_ptr<DepthBuf>> depths;
    std::unordered_map<TexKey, TexEntry, TexKeyHash> texCache;
    uint64_t texBytes = 0;
    uint64_t frame = 0;
    bool texLog = runtimeFlags().texLog;

    // ---- texture upscaling / replacement / dump (core in gs_texup.{h,cpp}) ----
    uint32_t upFactorCfg = 0; // 0 auto, 1 off, 2/4/8
    texup::Algo upAlgoCfg = texup::Algo::Auto;
    bool upSync = false, upLog = false, texDump = false, texReplace = true;
    bool texPaletteHd = true; // HD index maps for palette-only animated textures (GSR_PALETTE_HD=0 turns them off)
    uint32_t aniso = 0;
    float mipBias = 0.0f;
    float userMipBias = 0.0f;
    float hudScale = 1.0f; // Config::hudScaleX
    bool nativeMenu = false;
    int nativeMenuScreen = -1; // GUI screen the host saw when it set nativeMenu (wide backdrop fallback)
    float hudSize = 1.0f;      // hud_scale
    struct HudBox {
        float x0, y0, x1, y1;
    };
    std::vector<HudBox> hudCur, hudPrev; // UI sprite boxes of this frame / UI clusters of the previous frame
    hud::InventoryFrame inventoryHudFrame;
    uint32_t hudCornerLogLines = 0;
    std::unordered_map<uint64_t, SlotInfo> slots;
    std::string winShotPrefix; // GSR_WINSHOT / GSR_WINSHOT_AT (debug / test screenshots of the presented window)
    std::vector<uint64_t> winShots;
    size_t winShotNext = 0;
    uint64_t presentCount = 0;
    bool winShotByPresent = false; // GSR_WINSHOT_PRESENT=1: GSR_WINSHOT_AT counts presented frames (advances while the
                                   // game is held by the F1 menu)
    uint64_t hlFrame = 0;          // last frame the palette-animated menu highlight (fire texture) was drawn
    bool smoothHL = false;         // Config::smoothMenuHighlight
    uint64_t texBudget = 1536ull << 20;
    std::string texDir = "textures", dumpDir;
    std::unordered_map<uint64_t, std::string> replIndex; // content hash -> file
    std::unordered_map<uint64_t, uint64_t>
        aliasIndex; // RGB-only hash -> content hash of a pack file (same picture, palette alpha edited by the game)
    bool texBudgetAuto = false;
    std::unordered_set<uint64_t> excluded, dumped;
    std::unordered_map<UpKey, std::shared_ptr<UpEntry>, UpKeyHash> upCache;
    std::unordered_map<uint64_t, std::shared_ptr<PalettePlane>> palettePlanes; // by index-plane hash
    std::unordered_set<UpKey, UpKeyHash> upInflight;
    uint32_t upGen = 0;
    std::mutex doneMu;
    std::vector<UpDone> done;
    std::atomic<uint32_t> doneCount{0};
    std::atomic<uint64_t> dumpedCount{0};
    uint64_t upBytes = 0;
    uint64_t texPeakBytes = 0;
    std::unique_ptr<texup::Pool> pool; // declared last among the upscale members: joined first on destruction
    // lod statistics (GSR_LODSTAT=1)
    bool lodStat = runtimeFlags().lodStat;
    struct LodAcc {
        double area = 0, gs = 0, major = 0, minor = 0, ratio = 0, dMaj = 0, dMin = 0;
        double hist[8] = {};  // area by anisotropy ratio bucket: <1.25, <2, <4, <8, >=8
        double histD[9] = {}; // area by (log2 major - gsLod) bucket
        uint64_t tris = 0;
    } lodAcc;
    // GSR_DRAWLOG=<first>-<last>: print every draw call of the vsync frames in that range to stderr (state + first
    // vertices)
    int64_t drawLogFirst = -1, drawLogLast = -1;
    int64_t drawLogTbp = -1; // GSR_DRAWLOGTBP=<tbp>: log draws (any frame) that sample this TBP
    uint32_t drawLogVerts = 3;
    uint64_t inventoryLogStart = ~uint64_t{0};
    int64_t inventoryLogDelay = -1;
    bool spriteFootprint = true; // GSR_SPRITE_FOOTPRINT=0 restores legacy atlas coverage
    // GSR_DRAWSTOP=<n>: only the first n draw calls of the run are executed (bisecting a bad frame);
    // GSR_DRAWSKIP=<a>-<b> skips draws a..b
    int64_t drawStop = -1, skipA = -1, skipB = -1, drawIndex = 0;
    std::set<int64_t> rtDump;
    // debug: GSR_RTDUMPD=<first>-<last>: dump the target after every draw of these frames
    int64_t rtDumpFrame = runtimeFlags().rtDumpFrameFirst;
    int64_t rtDumpFrameLast = runtimeFlags().rtDumpFrameLast;
    uint32_t dumpRows = runtimeFlags().rtDumpRows;
    ComPtr<ID3D11Texture2D> staging;
    NativeReadback nativeReadback;
    bool nativeReadbackEnabled = runtimeFlags().nativeReadback;
    uint32_t stagingW = 0, stagingH = 0;

    ComPtr<ID3D11Texture2D> dispTex;
    ComPtr<ID3D11Texture2D> savePreviewTex;
    // Last presented menu frame. The game's loading screen redraws its own native-resolution grab of the menu; that
    // grab is drawn from this HD copy instead (d3d11_draw.cpp, holdGrab).
    ComPtr<ID3D11Texture2D> menuHoldTex;
    ComPtr<ID3D11ShaderResourceView> menuHoldSrv;
    bool menuHoldValid = false;
    uint32_t savePreviewW = 0, savePreviewH = 0;
    float savePreviewAspect = 4.0f / 3.0f, gameplayAspect = 4.0f / 3.0f;
    ComPtr<ID3D11RenderTargetView> dispRtv;
    ComPtr<ID3D11ShaderResourceView> dispSrv;
    uint32_t dispW = 0, dispH = 0;
    Impl(LocalMemory& m, const Config& c, Stats& s);

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
    void pumpUp();
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
    bool compile(const char* entry, const char* target, const char* defines, ComPtr<ID3DBlob>& out);
    bool init();
    void createSwapchain();
    std::wstring neuralDirectory() const;
    bool neuralAvailable(std::string& status) const;
    std::string neuralStatus() const;
    void setNeural(bool on);
    void retireNeural(const std::string& failure);
    void makeSwapRtv();
    void resize(uint32_t w, uint32_t h);

    // ---------------------------------------------------------------------------------------------
    // render targets
    ComPtr<ID3D11Texture2D> makeTex(uint32_t w, uint32_t h, UINT bind, DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM);
    void allocRT(RT& rt, uint32_t newH);
    RT* getRT(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t needH);
    void clearInventoryCanvas(RT& rt, bool artwork = false);
    const std::vector<WideBackdrop>& wideBackdropSet(const std::string& name);
    bool drawWideBackdrop(RT& rt, int guiScreen);
    void rowSpan(const RT& rt, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) const;
    static uint32_t packPixel(uint32_t psm, const uint8_t* p);
    static uint32_t unpackPixel(uint32_t psm, uint32_t v);
    void ensureStaging(uint32_t w, uint32_t h);
    void readbackRT(RT& rt);
    void syncRT(RT& rt);
    void flushToMemory(uint32_t first, uint32_t last);
    bool beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              bool apply);
    bool uploadCovers(uint32_t block) const;
    RT* aliasedTarget(uint32_t psm, uint32_t mxl, const std::vector<std::pair<uint32_t, uint32_t>>& spans);
    void copyAliasedBlocks(RT& rt, ID3D11Texture2D* texture, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h);
    bool touchesUpload(const RT& rt) const;
    bool uploadRows(const RT& rt, uint32_t& y0, uint32_t& y1) const;
    std::vector<uint64_t> uploadBlocks; // blocks rewritten by the image upload in progress (beginTargetUpload)
    uint32_t uploadLo = 0, uploadHi = 0;
    bool uploadActive = false;

    // ---------------------------------------------------------------------------------------------
    DepthBuf* getDepth(uint32_t zbp, uint32_t w, uint32_t h);
    ID3D11DepthStencilState* getDepthState(D3D11_COMPARISON_FUNC func, bool write);
    ID3D11BlendState* getBlendState(bool enable, D3D11_BLEND_OP op, D3D11_BLEND src, D3D11_BLEND dst, uint8_t mask);

    // ---------------------------------------------------------------------------------------------
    // textures
    void decodeLevel(uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h, uint64_t texa,
                     const ClutBuf& clut, uint32_t csa, std::vector<uint32_t>& out);
    uint64_t hashClut(const ClutBuf& c, uint32_t psm, uint32_t csa);

    struct TexBind {
        ID3D11ShaderResourceView* srv = nullptr;
        bool isRT = false;
        bool fontAtlas = false, menuArtwork = false;
        RT* rt = nullptr;
        float ox = 0, oy = 0;
        uint32_t mxl = 0;
        float averageColor[3]{.35f, .35f, .35f};
        uint8_t minimumAlpha = 0;
        uint32_t reflectionTexture = ~0u;
        uint32_t factor = 1; // physical texture size / logical size
    };
    RT* findRTForTexture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t w, uint32_t h, float& ox, float& oy);
    bool resolveTexture(const GsDrawState& s, const ClutBuf& clut, TexBind& out, bool uiHint = false);
    uint64_t rawHash(const std::vector<std::pair<uint32_t, uint32_t>>& spans) const;

    // ---------------------------------------------------------------------------------------------
    ID3D11ShaderResourceView* snapshot(RT& rt, const D3D11_BOX* box);
    void logDraw(const GsDrawState& s, const DrawVertex* v, uint32_t count);
    static bool oneToOneSprite(const GsDrawState& s, const DrawVertex* v, uint32_t count);
    void accumulateLod(const GsDrawState& s, const DrawVertex* v, uint32_t count);
    void printLodStat();

    // ---------------------------------------------------------------------------------------------
    // temporal upscaler (DLSS) - frame phases. A frame target goes 0 (idle) -> 1 (3D world: every draw lands in the
    // top-left render rectangle of the internal-scale target, jittered) -> 2 (resolved: the upscaled world was written
    // over the whole frame, HUD / fades / text are drawn at full resolution on top, so they never go through the
    // upscaler) -> 0 (the target was displayed). Everything here is dormant unless upActive(). The old VU-per-object
    // heuristic is dormant. Engine scene packets govern the shared SR/NR path, including its exact world-to-HUD
    // boundary.
    bool upActive() const { return false; }

    DepthBuf* worldDepth(RT& rt) { return getDepth(rt.wzbp, rt.w, rt.h); }
    bool enterWorld(RT& rt, DepthBuf& db, uint32_t zbp, float zmax, int sx1, int sy1);
    bool stretchDepth(DepthBuf& depth, uint32_t sourceW, uint32_t sourceH, uint32_t destW, uint32_t destH,
                      float jitterX = 0, float jitterY = 0);
    bool restoreWorldDepth(RT& rt, DepthBuf& depth, bool resolved);
    void enterTemporalWorld(RT& rt, uint32_t zbp, float zmax, uint64_t xyoffset);
    void restoreTemporalAlpha(RT& rt, ID3D11ShaderResourceView* alpha, uint32_t width, uint32_t height);
    bool prepareObjectMotion(RT& rt, const GsDrawState& s, const DrawVertex* vertices, uint32_t count, float sx,
                             float sy);
    void validateObjectMotion(RT& rt, const nr::guides::Frame& guide, const nr::guides::FrameConstants& constants);
    void dumpTemporalStage(const char* stage, ID3D11Texture2D* texture);
    void finalizeNeuralWorld(RT& rt);
    void finalizeWorld(RT& rt);
    void finalizeAllWorlds();
    bool upscalePhase(RT& rt, DepthBuf& db, bool isWorld, bool depthGE, uint32_t zbp, float zmax, int sx1, int sy1);
    void recoverReducedWorld(RT& rt);
    void discardTemporalWorlds();
    void setUpscale(uint32_t mode, uint32_t preset, float sharp);
    void noteVuUpload(uint32_t a, uint32_t n, const float* rows);
    void draw(const GsDrawState& s, const ClutBuf& clut, const DrawVertex* v, uint32_t count);

    // ---------------------------------------------------------------------------------------------
    // display
    void ensureDisp(uint32_t w, uint32_t h);
    bool renderDisplay(const DisplayState& d, uint32_t& outW, uint32_t& outH, bool recordLatch = false);
    void latchDisplay(const DisplayState& d, uint32_t field);
    HANDLE beginPresentWait();
    void present(const DisplayState& d);
    void presentLatched();
    bool readTexture(ID3D11Texture2D* tex, uint32_t w, uint32_t h, Image& out, bool keepAlpha = false);
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
