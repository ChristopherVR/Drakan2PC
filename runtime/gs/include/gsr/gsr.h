// gsr - PS2 Graphics Synthesizer hardware renderer (Direct3D 11).
// Public API. See README.md for integration notes.
#pragma once
#include "gsr/movie_frames.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gsr {

enum class GifPath : uint32_t {
    Path1 = 0, // VU1 XGKICK
    Path2 = 1, // VIF1 DIRECT/DIRECTHL
    Path3 = 2, // DMA channel 2 / IMAGE uploads
};

// ---- debug / audit hooks (tests/texaudit.cpp); all optional, called on the submitting thread ----------------------
struct TextureEvent // a texture was (re)decoded from local memory
{
    uint64_t tex0 = 0, tex1 = 0, texa = 0, clamp = 0, miptbp1 = 0, miptbp2 = 0;
    uint32_t width = 0, height = 0, mipLevels = 0; // level 0 size, number of levels
    const uint32_t* level[7] = {}; // RGBA8 (R in the low byte), alpha as the GS texture alpha (0x80 = opaque for CT32)
    uint32_t levelW[7] = {}, levelH[7] = {};
    const uint32_t* clutRaw = nullptr; // CLUT buffer (512 raw entries) for indexed formats
    uint32_t clutCpsm = 0;
    uint64_t frame = 0;
    bool cached = false; // true: a draw reused a cached decode (event fired per draw, not per decode)
};
struct UploadEvent // a host->local transfer finished
{
    uint32_t dbp = 0, dbw = 0, dpsm = 0, x = 0, y = 0, w = 0, h = 0;
    const uint8_t* data = nullptr; // raw transfer stream (as received, w*h*bits/8 bytes)
    uint32_t bytes = 0;
};
struct ClutEvent // a CLUT was loaded into the CLUT buffer
{
    uint64_t tex0 = 0, texclut = 0;
    const uint32_t* raw = nullptr; // resulting buffer (512 entries)
    uint32_t cpsm = 0;
};

struct Config {
    // The GS backend: Auto = WebGPU when the build has it (every platform), else Direct3D 11 (Windows builds have
    // both).
    enum class Api : uint8_t { Auto, D3D11, WebGpu } api = Api::Auto;
    std::function<void(const TextureEvent&)> onTexture;
    std::function<void(const UploadEvent&)> onUpload;
    std::function<void(const ClutEvent&)> onClut;
    void* hwnd = nullptr; // HWND of the output window; nullptr = headless (offscreen only)
    // The output window on other platforms (WebGPU renderer): X11 = windowDisplay Display* + xlibWindow, Wayland =
    // windowDisplay wl_display* + hwnd wl_surface*, Canvas = canvasSelector (browser; the renderer's GPU thread takes
    // the canvas over as an OffscreenCanvas).
    enum class WindowKind : uint8_t { Win32, Xlib, Wayland, Canvas } windowKind = WindowKind::Win32;
    void* windowDisplay = nullptr;
    uint64_t xlibWindow = 0;
    std::string canvasSelector;
    uint32_t windowWidth = 0; // swapchain size (0 = query from hwnd)
    uint32_t windowHeight = 0;
    uint32_t scale = 1; // internal resolution multiplier (1..8)
    bool vsync = true;
    bool stretchToWindow = false; // false: keep display aspect (letterbox), true: fill window
    float forcedAspect = 0.0f;    // 0 = native (width/height of the DISPLAY rect); e.g. 4.0f/3.0f to force 4:3
    bool bilinearPresent = true;
    bool debugLayer = false; // D3D11 debug layer (if installed)
    bool allowWarp = true;   // headless/test: fall back to the WARP software adapter when no GPU
    bool forceWarp = false;
    bool logUnsupported = false; // print notes about unimplemented features (once per kind) to stderr
    // ---- presentation / quality options (drakan.ini [video]) ----
    uint32_t deinterlace = 0;   // 0 weave: show the stored frame as is (default, sharp), 1 bob (one field per vsync), 2
                                // blend (vertical 2-tap)
    bool flickerFilter = false; // true: honour the game's 2-circuit anti-flicker blend (same buffer read twice, 1 line
                                // apart, ALP blend). Default off: it only blurs a progressive output
    uint32_t textureFilter = 0; // 0 per-draw TEX1 (MMAG/MMIN), 1 force point sampling, 2 force bilinear
    bool sharpPresent =
        true; // area-average downscale / sharp-bilinear upscale in the final blit (false: plain bilinear)
    bool fixSpriteSeams = true; // close <=2 px gaps between adjacent textured sprites (not GS behaviour; see README).
                                // Env GSR_SEAMFIX=0 disables
    // ---- texture quality (drakan.ini [video]; every field can also be overridden by the GSR_* environment variable
    // named in the comment) ----
    uint32_t textureUpscale =
        0; // texture_upscale: 0 = auto (default: internal scale 2-3 -> 4x, 4+ -> 8x, UI sprites always 8x, capped by
           // the texture budget), 1 = off, 2 / 4 / 8 fixed factor. Env GSR_TEXUP=auto|1|2|4|8
    uint32_t textureUpscaleFilter =
        0; // texture_upscale_filter: 0 = auto (xBRZ for pixel-art / UI / palette-flat, smooth for photographic), 1 =
           // xBRZ, 2 = smooth (bicubic + sharpen). Env GSR_TEXUP_FILTER=auto|xbrz|smooth
    uint32_t anisotropic = 16; // anisotropic (default 16): 0/1 = off, 2..16 = max taps along the footprint's major axis
                               // (shader based, minified mip-mapped draws only). Env GSR_ANISO
    float mipBias = 0.0f;      // mip_bias: added to the GS LOD of minified draws (negative = sharper); only used when
                               // mipBiasAuto is false. Env GSR_MIPBIAS=<float>|auto
    bool mipBiasAuto = true;  // mip_bias = auto: bias = -log2(internal scale). The GS computes LOD for 1x pixels, so at
                              // scale s every mip level is log2(s) levels too blurry (measured, see README)
    bool textureDump = false; // texture_dump: write every unique decoded texture to <textureDir>/dump/<hash>.png (alpha
                              // 0x80 -> 255). Env GSR_TEXDUMP=1
    bool textureReplace = true; // texture_replace: use <textureDir>/<hash>.png|.dds in place of the decoded texture.
                                // Env GSR_TEXREPLACE=0 disables
    std::string textureDir =
        "textures"; // texture pack / dump folder (relative to the working directory). Env GSR_TEXDIR
    uint32_t textureBudgetMB =
        0; // texture cache budget (0 = auto: 40 % of the dedicated VRAM, 1.5 - 8 GB; a number is in MB) (all decoded +
           // upscaled textures); least recently used entries are evicted above it. Env GSR_TEXBUDGET
    float hudScaleX = 1.0f; // hud_aspect_correct: x scale about the screen centre applied to UI sprites (1 = off, e.g.
                            // (4/3)/(16/9) = 0.75 in 16:9 widescreen). See Renderer::setHudScaleX
    bool smoothMenuHighlight = false; // smooth_menu_highlight: 2x2 box filter on the palette-animated fire texture of
                                      // the highlighted menu entry
    uint32_t upscaleMode = 0;     // temporal upscaler (DLSS, optional module): 0 off, 1 DLAA, 2 quality, 3 balanced, 4
                                  // performance. Env GSR_UPSCALE=off|dlaa|quality|balanced|performance
    bool neuralRendering = false; // DLSS 5 neural rendering; does not select an SR quality mode
    bool neuralAtRenderResolution = false; // with a reduced-resolution SR mode, evaluate NR on the reduced raster
                                           // before SR (cheaper) instead of on the SR output. See setNeuralResolution
    std::string neuralRuntimeDir; // signed Streamline / NR runtime bundle; empty selects executable directory / nr
    uint32_t upscalePreset =
        0; // 0 = SDK default for the mode, 10..13 = render preset J K L M. Env GSR_UPSCALE_PRESET=auto|j|k|l|m
    float upscaleSharpness = 0.0f;   // optional post sharpen of the upscaled 3D image, 0..1
    bool textureUpscaleSync = false; // true: wait for upscale / replacement jobs inside the draw (deterministic
                                     // screenshots, replays); default: asynchronous worker threads, the native texture
                                     // is used until the result is ready. Env GSR_TEXUP_SYNC=1
};

struct Stats {
    uint64_t gifPackets = 0, gifTags = 0, vertices = 0, primitives = 0, drawCalls = 0, triangles = 0;
    uint64_t textureDecodes = 0, textureCacheHits = 0, rtSamples = 0;
    uint64_t textureRevalidated = 0; // re-uploaded with identical bytes: decoded texture kept
    uint64_t targetUploads = 0;      // image uploads applied to a GPU-owned render target (no readback)
    uint64_t aliasedTextures = 0;    // textures assembled on the GPU from an aliased render target
    uint64_t rtReadbacks = 0, rtUploads = 0, transfersH2L = 0, transfersL2L = 0, transfersL2H = 0;
    uint64_t clutLoads = 0, shaderBlendDraws = 0, hwBlendDraws = 0;
    uint64_t frames = 0;
    // texture upscaling / replacement
    uint64_t texUpscaled = 0, texUpscaleUs = 0, texUpscaleBytes = 0, texReplaced = 0, texDumped = 0, texUpPending = 0,
             texCacheBytes = 0;
    // live settings (Renderer::set*): batches applied at a frame boundary, and internal-resolution changes among them
    uint64_t liveApplied = 0, liveScaleChanges = 0;
    uint64_t hudSprites = 0, hlSmoothed = 0,
             texVolatile =
                 0; // HUD sprites de-stretched, smoothed highlight textures, textures classified as palette-animated
    // palette-only animated textures: HD index maps built, palette steps drawn from one
    uint64_t texPalettePlanes = 0, texPaletteHd = 0;
    uint64_t movieFrames = 0; // HD movie frames drawn over native ones (Renderer::noteMovieFrame)
};

// Result of Renderer::captureDisplay()
struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba; // tightly packed RGBA8
    explicit operator bool() const { return width && height && !rgba.empty(); }
};

// Overlay hook (runtime/settings menu): handed to the callback at the end of the present path, right before Present().
// Pointers are ID3D11Device*, ID3D11DeviceContext*, ID3D11RenderTargetView* (the swap chain back buffer); sizes are the
// back buffer size.
// The graphics API behind an OverlayContext's handles.
enum class OverlayApi : uint8_t {
    D3D11,  // device = ID3D11Device*, context = ID3D11DeviceContext*, rtv = ID3D11RenderTargetView*, gridSrv =
            // ID3D11ShaderResourceView*
    WebGpu, // device = gsr::rhi::Device*, context = null, rtv = gsr::rhi::Texture* (the surface image), gridSrv =
            // gsr::rhi::Texture*
};

struct OverlayContext {
    OverlayApi api = OverlayApi::D3D11;
    void* device = nullptr;
    void* context = nullptr;
    void* rtv = nullptr;
    uint32_t width = 0, height = 0;
    // Where the game image sits inside the back buffer (letterbox / pillarbox viewport of the present), back buffer
    // pixels. 0 = unknown.
    float viewX = 0, viewY = 0, viewW = 0, viewH = 0;
    float hudScaleX = 1.0f;  // x scale applied to UI sprites (hud_aspect_correct); 1 = none
    float hudAnchorX = 0.5f; // normalized origin actually used for this presented page, including modal widgets
    // Off-screen capture of a host screen's native item grid (marker sourcePc 0x437170): the grid's draws are kept in
    // `gridSrv` (an ID3D11ShaderResourceView*, gridTexW x gridTexH pixels for a gridFrameW x gridFrameH native target;
    // native x maps through the inventory page map: centred about gridFrameW / 2 by gridHudScale when gridCentred) and
    // removed from the presented frame. gridCapture = the renderer supports it; gridSrv null = nothing captured lately.
    bool gridCapture = false;
    void* gridSrv = nullptr;
    uint32_t gridTexW = 0, gridTexH = 0, gridFrameW = 0, gridFrameH = 0;
    float gridHudScale = 1.0f;
    bool gridCentred = false;
    uint64_t gridSerial = 0;
};

struct SceneGuideCamera;
struct ObjectMotionScope;
struct SceneGeometry;
struct SceneModels;
class Renderer {
public:
    static std::unique_ptr<Renderer> create(const Config& cfg);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ---- input from the runtime ------------------------------------------------------------------------
    // Submit raw GIF data (quadword granular). Tags/IMAGE data may continue across calls of the same path.
    // `processGIFPacket` mirrors PS2Recomp's GS::processGIFPacket (treated as PATH3 unless a path is given).
    void submitGifPacket(GifPath path, const uint8_t* data, uint32_t sizeBytes);
    void processGIFPacket(const uint8_t* data, uint32_t sizeBytes) { submitGifPacket(GifPath::Path3, data, sizeBytes); }

    // Write one GS general register (the A+D / PS2Recomp GS::writeRegister path). regAddr = 0x00..0x62.
    void writeRegister(uint8_t regAddr, uint64_t value);

    // Privileged registers. addr is the full EE address (0x12000000..0x12001080) or the offset from 0x12000000.
    void writePriv(uint32_t addr, uint64_t value);
    uint64_t readPriv(uint32_t addr) const;

    // Called once per vertical blank (e.g. 50/60 Hz from the runtime's vsync thread). Sets CSR.VSINT, toggles
    // CSR.FIELD and (optionally) presents the frame.
    void vsync(bool presentFrame = true);

    // Advance VSINT/FIELD and invoke onVsync only. Does not apply settings,
    // flush draws, latch a display or end a renderer frame.
    void signalVblank();
    // Complete a producer-confirmed display transaction at the current field:
    // apply settings, flush, latch, endFrame once. No vblank or presentation.
    // With requireFieldIndependent, refuse bob/blend/flicker modes after applying
    // pending settings; false means no latch or endFrame was performed.
    bool completeDisplayFrame(bool requireFieldIndependent = false);
    // Includes queued deinterlace/flicker settings; does not consume them.
    bool fieldIndependentPresentation() const;

    // Draw the DISPFB/DISPLAY circuits to the window (if any) and the internal display image.
    void present();
    // Present the display image latched by the last vsync() or completeDisplayFrame() without
    // re-reading the framebuffers. This is what a host loop should call; present() renders the live DISPFB state
    // instead. Thread-safe against the submitting thread (the renderer serialises its public API with an internal
    // mutex).
    void presentLatched();
    // Blocks (without holding the renderer lock) until the swapchain can accept a new frame (frame-latency waitable
    // object), at most timeoutMs.
    bool waitForPresent(uint32_t timeoutMs);
    void setVsync(bool on); // live (see "Live settings"); vsyncEnabled() reports the requested value
    bool vsyncEnabled() const;
    // ---- Live settings --------------------------------------------------------------------------------------
    // Callable from ANY thread at any time (they only take a tiny private lock, never the renderer lock, so a UI thread
    // never waits for a long GIF packet). The values are queued and applied together at the next frame boundary - the
    // next vsync(), present(), presentLatched() or capture call - under the renderer's internal mutex, so a draw never
    // sees half a change. Values mirror the Config fields of the same name. Calling a setter with the value already in
    // effect is free of side effects (no cache flush).
    void
    setAspect(float forcedAspect,
              bool stretchToWindow); // 0 = native display aspect, e.g. 4/3 or 16/9 to force; stretch = fill the window
    void setTextureFilter(uint32_t mode); // 0 per-draw TEX1, 1 force point, 2 force bilinear
    void setDeinterlace(uint32_t mode);   // 0 weave, 1 bob, 2 blend
    void setFlickerFilter(bool on);
    void setSharpPresent(bool on);
    void setAnisotropic(uint32_t taps);                       // 0/1 off, 2..16
    void setMipBias(float bias, bool autoBias);               // autoBias: -log2(internal scale) (bias ignored)
    void setTextureUpscale(uint32_t factor, uint32_t filter); // factor 0 auto / 1 off / 2 / 4 / 8, filter 0 auto / 1
                                                              // xBRZ / 2 smooth. Flushes the upscaled-texture cache
    void setTextureReplace(
        bool on); // HD texture replacement on/off; (re)scans Config::textureDir. Flushes the texture caches
    void setTextureDump(bool on);
    void rescanTextures(); // re-read the texture folder (new pack files / exclude.txt) and flush the caches
    // Internal resolution multiplier 1..8. All render targets are flushed to GS local memory and re-created at the new
    // size on demand (their content survives at the old resolution's detail, upscaled); depth buffers and the in-flight
    // frame lose their depth, so the frame in which the change happens may show glitches. The latched display image is
    // re-rendered by the same vsync.
    void setInternalScale(uint32_t scale);
    uint32_t internalScale() const; // scale currently in effect (changes at the frame boundary)
    // HUD de-stretch for anamorphic widescreen: UI sprites (no depth test, drawn into the display-sized frame, narrower
    // than ~85% of the frame width, not sampling a render target) have their x coordinates scaled by `s` about the
    // frame centre. 1 = off. Full-width fades / bars / world draws are untouched.
    void setHudScaleX(float s);
    // Native interactive menu canvas; independent of gameplay HUD clustering. `screen` is the GUI screen id the host
    // saw when it decided the canvas (-1 = unknown); it picks the wide backdrop until the screen's own draw packet
    // arrives.
    void setNativeMenu(bool enabled, int screen = -1);
    // HUD size (hud_scale): UI clusters (groups of overlapping / touching UI sprites, found from the previous frame's
    // sprites) are resized by `s` in both axes about their nearest screen edge / corner (centre for clusters in the
    // middle), together with the x de-stretch of setHudScaleX, so the orbs and their frames stay one piece and stay on
    // screen. Full-width bands (subtitle bar) are resized vertically only. 1 = original size.
    void setHudSize(float s);
    void setSmoothMenuHighlight(
        bool on); // soften the CLUT-animated 128x128 fire texture of the highlighted menu entry (see README)
    void setFrameLatency(uint32_t frames);
    // Temporal upscaler (DLSS, see docs/reference/dlss.md). mode 0 off / 1 DLAA / 2 quality / 3 balanced / 4
    // performance, preset 0 auto or 10..13 (J K L M), sharpness 0..1. Live (queued like the other settings): the 3D
    // world is rendered at a lower resolution inside the internal-scale render target and upscaled before the HUD is
    // drawn.
    void setUpscale(uint32_t mode, uint32_t preset, float sharpness);
    // Status line of the upscaler ("DLSS SR Quality 1024x1024 -> 1536x1536 ..." or the reason it is unavailable); empty
    // when never used
    std::string upscaleStatus() const;
    double upscaleGpuMs() const; // GPU time of the last upscale (needs GSR_UPSCALE_TIMING=1)
    // Camera: the runtime forwards VIF1 UNPACKs that touch VU1 data rows 0..6 (view-projection matrix, viewport scale /
    // offset); rows = 7 qwords (28 floats).
    void noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* vuRows0to6);
    void noteSceneCamera(const SceneGuideCamera& camera);
    void noteObjectMotion(const ObjectMotionScope& scope);
    void noteSceneGeometry(const SceneGeometry& geometry);
    void noteSceneModels(std::shared_ptr<const SceneModels> models);
    // HD movie frames (gsr/movie_frames.h, docs/reference/fmv.md section 9). The host registers every decoded movie
    // frame by its macroblock signature and picture size; once a frame buffer holds all macroblocks of a registered
    // frame (centred in the 512 line screen, as the game's movie blit places it), `source` supplies the HD image and it
    // is drawn over that rectangle before any later draw or present. Thread safe; does not take the renderer lock.
    void noteMovieFrame(uint64_t signature, uint32_t width, uint32_t height, uint32_t movie, uint32_t frame);
    void setMovieFrameSource(MovieFrameSource source);
    // The 3D scene changed completely (cut scene, level load): the next upscaled frame discards its history
    void upscaleCut();
    bool
    upscaleAvailable(std::string& status) const; // preliminary adapter/library probe; feature creation may still fail
    void setNeuralRendering(bool on);
    // Where DLSS 5 runs when DLSS SR renders the world at a reduced size: false = on the SR output (output size),
    // true = on the reduced raster before SR, which then upscales the enhanced image. Resets both histories.
    void setNeuralResolution(bool renderResolution);
    void setWaterRendering(bool modernWater, bool rayReflections, bool rayShadows = false);
    bool waterRenderingRequested() const;
    bool sceneModelsRequested() const;
    bool waterRenderingAvailable(std::string& status) const;
    std::string waterRenderingStatus() const;
    std::string rayTracingStatus() const;
    bool neuralRenderingAvailable(std::string& status) const;
    std::string neuralRenderingStatus() const;

    // Output size actually presented (letterbox viewport), for diagnostics.
    void getViewport(uint32_t& w, uint32_t& h) const;
    // Called on the presenting thread after the frame is drawn, before Present(). The callback may draw into the RTV
    // (any D3D11 state it changes must be restored by the callback) and must not call back into this Renderer. nullptr
    // removes it.
    void setOverlayCallback(std::function<void(const OverlayContext&)> cb);

    // Local memory access (GS->EE readback, BUSDIR): after TRXDIR=1 the local->host stream is consumed here.
    uint32_t consumeLocalToHostBytes(uint8_t* dst, uint32_t maxBytes);

    // ---- callbacks (all invoked on the caller's thread, inside submit/write calls) ---------------------
    std::function<void()> onFinish;         // GS FINISH register written (CSR.FINISH set)
    std::function<void(uint64_t)> onSignal; // GS SIGNAL register written (arg = SIGLBLID value)
    std::function<void()> onVsync;          // after vsync() processed

    // ---- direct local memory access ---------------------------------------------------------------------
    uint8_t* vram();      // 4 MB swizzled GS local memory. After direct CPU writes call vramTouched.
    void flushToMemory(); // make sure local memory reflects all GPU draws (readback of dirty targets)
    void vramTouched(uint32_t firstBlock, uint32_t blockCount);
    uint32_t readVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y);
    void writeVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);

    // ---- output -----------------------------------------------------------------------------------------
    // Render the current display (PMODE/DISPFB/DISPLAY) and return it as RGBA8 (display size * scale).
    bool captureDisplay(Image& out);
    // Last completed gameplay image before entering a native menu/loading page.
    bool captureLastGameplay(Image& out, float& aspect);
    // Dump a framebuffer (FBP in 2048 word pages) interpreted as given PSM/FBW/size to RGBA8 (native resolution).
    bool captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t width, uint32_t height, Image& out);
    void resizeWindow(uint32_t w, uint32_t h);

    // ---- misc -------------------------------------------------------------------------------------------
    void reset();
    const Stats& stats() const;
    std::string adapterName() const;
    Config::Api api() const; // the renderer actually in use (never Auto)
    bool valid() const;

    struct Impl;

private:
    Renderer();
    std::unique_ptr<Impl> m_impl;
};

// Content hash of a decoded texture exactly as used for HD dump / replacement file names (level 0, R in the low byte,
// alpha in GS units 0..0x80).
uint64_t textureContentHash(const uint32_t* rgba, uint32_t w, uint32_t h);
// Never dump or replace textures with this content hash (process wide, thread safe, may be called before a Renderer
// exists). Used for textures the runtime generates itself (keyboard prompts), whose pixels depend on the user's key
// bindings. Automatic scaling of those current pixels remains allowed; this only blocks file dumps/replacements.
void excludeTextureHash(uint64_t hash);
// Photographic UI images (save thumbnails) use bicubic sharpening rather than
// the pixel-art scaler. Registration is content-based and process-local.
void preferPhotographicTexture(uint64_t hash);
// Exact native thumbnail -> optional save companion image. Decoded on the
// ordinary texture worker; absent/corrupt files retain the original thumbnail.
void registerSavePreview(uint64_t hash, const std::string& path, bool flipY);
// Register an immutable host-generated atlas before its first guest upload.
// Pixels use the same RGBA/GS-alpha convention as textureContentHash. Native UVs
// and layout stay unchanged. Repeated hashes retain the first registration.
// Images are memory-only, protected from disk dump/replacement, and bounded.
// fontAtlas identifies glyph content for inventory-only shadow styling.
bool registerRuntimeTexture(uint64_t hash, uint32_t w, uint32_t h, const uint32_t* rgba, bool fontAtlas = false);
// Host-drawn HD overlay for a generated texture `hash` whose other pictures are still disc art (the inventory
// page-switch tile shares its atlas with the section icons). `original` is the disc texture before the rewrite (w x h,
// same convention); rgba and coverage are (w*factor) x (h*factor), factor 2, 4 or 8, stored row order. Where coverage
// is 0 the picture comes from the texture pack's image of `original` (exact hash, then RGB alias) when replacement is
// on, else from the usual automatic upscale of the generated texture; 255 takes rgba, values between blend.
bool registerRuntimeOverlay(uint64_t hash, const uint32_t* original, uint32_t w, uint32_t h, uint32_t factor,
                            const uint32_t* rgba, const uint8_t* coverage);
// Write the next presented window image (3D + HUD + overlays, like GSR_WINSHOT) to `path` as PNG. Process wide and
// thread safe; requests queue up and each is served by one present. Used by scripted test runs (`shot` command),
// which cannot rely on desktop captures of a hidden or occluded window.
void requestWindowShot(const std::string& path);

// Image helpers (tests / tools)
bool writePng(const char* path, const Image& img);
bool writeBmp(const char* path, const Image& img);
// Reads a .png (or .dds) into tightly packed RGBA8 (alpha 0..255), e.g. a save's HD preview companion.
bool readImage(const char* path, Image& img);

} // namespace gsr
