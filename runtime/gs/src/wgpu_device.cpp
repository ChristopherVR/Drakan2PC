// Device, programs, surface, live settings and reset (WebGPU backend).
#include "wgpu_backend_impl.h"
#include "gs_shaders_wgsl.h"

namespace gsr {

WgpuBackend::Impl::Impl(LocalMemory& m, const Config& c, Stats& s) : mem(m), cfg(c), st(s) {
    scale = std::clamp(c.scale, 1u, 8u);
    initTexQuality();
    const RuntimeFlags& f = runtimeFlags();
    spriteFootprint = f.spriteFootprint;
    if (f.inventoryDrawLogDelay >= 0)
        inventoryLogDelay = f.inventoryDrawLogDelay;
    drawLogVerts = f.drawLogVerts;
    rtDump.insert(f.rtDumpDraws.begin(), f.rtDumpDraws.end());
    drawStop = f.drawStop;
    skipA = f.drawSkipFirst;
    skipB = f.drawSkipLast;
    if (f.winShotPrefix) {
        winShotPrefix = *f.winShotPrefix;
        winShotByPresent = f.winShotByPresent;
        winShots = f.winShotAt;
    }
    drawLogTbp = f.drawLogTbp;
    drawLogFirst = f.drawLogFirst;
    drawLogLast = f.drawLogLast;
}

WgpuBackend::Impl::~Impl() {
    pool.reset(); // workers first: they reference members below
    // GPU objects before the device that created them, then the surface, then the context.
    texCache.clear();
    upCache.clear();
    surfaceBuilder->drain(); // its jobs hold target pointers
    rts.clear();
    depths.clear();
    wideBackdrops.clear();
    dispTex.reset();
    savePreviewTex.reset();
    menuHoldTex.reset();
    movieTex.reset();
    gridCapture.save.reset();
    gridCapture.tex.reset();
    dummy.reset();
    progHw.reset();
    progSb.reset();
    progDisplay.reset();
    progPresent.reset();
    progRect.reset();
    progGather.reset();
    gatherTex.reset();
    progTaa.reset();
    taaTex[0].reset();
    taaTex[1].reset();
    rayPass.reset();
    gpu.reset(); // the device owns its context and the window surface
}

namespace {
rhi::ProgramDesc gsProgram(bool hw) {
    rhi::ProgramDesc d;
    d.wgsl = std::string(hw ? "enable dual_source_blending;\n" : "") + kWgslGsCommon + (hw ? kWgslGsHw : kWgslGsSb);
    d.uniformSlots = 1;
    d.textureSlots = hw ? 1 : 2;
    d.unfilterableMask = 0;
    d.samplers = true;
    d.vertexStride = sizeof(DrawVertex);
    d.attributes = {{0, 0, 4}, {1, 16, 4}, {2, 32, 3}};
    d.dualSource = hw;
    d.label = hw ? "gs hw blend" : "gs shader blend";
    return d;
}
rhi::ProgramDesc fullscreenProgram(const char* fragment, uint32_t textures, const char* label) {
    rhi::ProgramDesc d;
    d.wgsl = std::string(kWgslFull) + fragment;
    d.uniformSlots = 1;
    d.textureSlots = textures;
    d.samplers = true;
    d.vertexInput = false;
    d.label = label;
    return d;
}
} // namespace

bool WgpuBackend::Impl::init() {
    rhi::OpenDesc open;
    open.options.allowSoftwareAdapter = cfg.allowWarp || cfg.forceWarp;
    const bool windowed = cfg.hwnd || cfg.xlibWindow || !cfg.canvasSelector.empty();
    if (windowed) {
        using Kind = drakan::gpu::NativeWindow::Kind;
        open.window.kind = cfg.windowKind == Config::WindowKind::Xlib      ? Kind::Xlib
                           : cfg.windowKind == Config::WindowKind::Wayland ? Kind::Wayland
                           : cfg.windowKind == Config::WindowKind::Canvas  ? Kind::Canvas
                                                                           : Kind::Win32;
        open.window.display = cfg.windowDisplay;
        open.window.window = cfg.hwnd;
        open.window.xlibWindow = cfg.xlibWindow;
        open.window.canvasSelector = cfg.canvasSelector;
        open.width = cfg.windowWidth;
        open.height = cfg.windowHeight;
        open.vsync = cfg.vsync;
    }
    std::string error;
    gpu = rhi::Device::open(open, &error);
    if (!gpu) {
        std::fprintf(stderr, "[gsr-webgpu] %s\n", error.c_str());
        return false;
    }
    adapter = gpu->adapterName();
    if (texBudgetAuto) // no portable VRAM query: the same budget as a 4 GB adapter on D3D11
        texBudget = 1536ull << 20;
    const bool dual = gpu->dualSourceBlending();
    if (dual && !(progHw = gpu->createProgram(gsProgram(true), &error)))
        std::fprintf(stderr, "[gsr-webgpu] fixed-function blend program: %s\n", error.c_str());
    progSb = gpu->createProgram(gsProgram(false), &error);
    progDisplay = gpu->createProgram(fullscreenProgram(kWgslDisplay, 2, "display"), &error);
    progPresent = gpu->createProgram(fullscreenProgram(kWgslPresent, 1, "present"), &error);
    progGather = gpu->createProgram(fullscreenProgram(kWgslGather, 1, "native gather"), &error);
    {
        rhi::ProgramDesc d;
        d.wgsl = kWgslRect;
        d.uniformSlots = 1;
        d.textureSlots = 1;
        d.samplers = true;
        d.vertexInput = false;
        d.label = "rect";
        progRect = gpu->createProgram(d, &error);
    }
    if (!progSb || !progDisplay || !progPresent || !progRect || !progGather) {
        std::fprintf(stderr, "[gsr-webgpu] shader programs failed: %s\n", error.c_str());
        return false;
    }
    dummy = createTexture(1, 1, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "dummy");
    const uint32_t zero = 0;
    gpu->writeTexture(*dummy, 0, 0, 0, 1, 1, &zero);

    vsyncOn = cfg.vsync;
    if (gpu->hasSurface()) { // configured by open() at the window's size
        swapW = gpu->surfaceInfo().width;
        swapH = gpu->surfaceInfo().height;
    } else if (windowed) {
        std::fprintf(stderr, "[gsr-webgpu] no window surface: rendering offscreen\n");
    }
    ok = true;
    std::fprintf(stderr, "[gsr-webgpu] renderer on %s, internal scale %u, %s blending\n", adapter.c_str(), scale,
                 progHw ? "fixed-function" : "shader");
    { // temporal AA: config + environment (GSR_UPSCALE / GSR_UPSCALE_PRESET / GSR_UPSCALE_SHARP), as on D3D11
        const RuntimeFlags& f = runtimeFlags();
        const uint32_t m = f.upscaleMode ? *f.upscaleMode : cfg.upscaleMode;
        if (m)
            setUpscale(m, f.upscalePreset ? *f.upscalePreset : cfg.upscalePreset,
                       f.upscaleSharp ? *f.upscaleSharp : cfg.upscaleSharpness);
    }
    return true;
}

bool WgpuBackend::Impl::configureSurface() {
    return gpu && gpu->hasSurface() && gpu->configureSurface(swapW, swapH, vsyncOn);
}

void WgpuBackend::Impl::resize(uint32_t w, uint32_t h) {
    if (!w || !h || !gpu || !gpu->hasSurface())
        return;
    gpu->flush();
    swapW = w;
    swapH = h;
    configureSurface();
}

rhi::TexturePtr WgpuBackend::Impl::createTexture(uint32_t w, uint32_t h, uint32_t usage, rhi::Format fmt, uint32_t mips,
                                                 const char* label) {
    rhi::TextureDesc d;
    d.width = std::max(1u, w);
    d.height = std::max(1u, h);
    d.mips = mips;
    d.format = fmt;
    d.usage = usage;
    d.label = label;
    return gpu->createTexture(d);
}

void WgpuBackend::Impl::drawRect(rhi::Texture& target, const float rect[4], rhi::Texture* src, const float color[4],
                                 const rhi::Rect& clip) {
    RectConsts k{};
    std::memcpy(k.rect, rect, sizeof(k.rect));
    k.target[0] = (float)target.width();
    k.target[1] = (float)target.height();
    std::memcpy(k.color, color, sizeof(k.color));
    k.mode[0] = src ? 0u : 1u;
    gpu->setTargets(&target, nullptr, 0);
    gpu->setProgram(progRect.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, (float)target.width(), (float)target.height());
    gpu->setScissor(clip);
    gpu->setUniforms(0, &k, sizeof(k));
    gpu->setTexture(0, src ? src : dummy.get());
    gpu->drawNoInput(6);
}

void WgpuBackend::Impl::changeScale(uint32_t ns) {
    if (ns == scale)
        return;
    // GPU render targets -> local memory (so nothing the game drew is lost), then drop every scale dependent
    // object; targets are re-created at the new size on demand and re-filled from local memory (syncRT).
    discardTemporalWorlds();
    finalizeAllWorlds();
    flushAll();
    surfaceBuilder->drain(); // its jobs hold target pointers
    rts.clear();
    depths.clear();
    motionHistory.reset();
    motionWorldOpen = false;
    objectScope = {};
    scale = ns;
    mipBias = cfg.mipBiasAuto ? -std::log2((float)scale) : cfg.mipBias;
    if (upFactorCfg == 0)
        flushTexCaches(); // "auto" upscale factor depends on the internal scale
    st.liveScaleChanges++;
}

void WgpuBackend::Impl::applyLive(const LiveSettings& ls) {
    using L = LiveSettings;
    if (ls.mask & L::F_ASPECT) {
        cfg.forcedAspect = ls.forcedAspect;
        cfg.stretchToWindow = ls.stretch;
    }
    if (ls.mask & L::F_FILTER)
        cfg.textureFilter = std::min(ls.filter, 2u);
    if (ls.mask & L::F_DEINT)
        cfg.deinterlace = std::min(ls.deinterlace, 2u);
    if (ls.mask & L::F_SHARP)
        cfg.sharpPresent = ls.sharp;
    if (ls.mask & L::F_ANISO) {
        cfg.anisotropic = std::min(ls.aniso, 16u);
        aniso = cfg.anisotropic;
    }
    if (ls.mask & L::F_MIP) {
        cfg.mipBiasAuto = ls.mipAuto;
        cfg.mipBias = ls.mipBias;
        userMipBias = ls.mipBias;
        mipBias = ls.mipAuto ? -std::log2((float)scale) : ls.mipBias;
    }
    bool flush = false, rescan = (ls.mask & L::F_RESCAN) != 0;
    if (ls.mask & L::F_TEXUP) {
        const uint32_t f = normUpFactor(ls.upFactor);
        const texup::Algo a = (texup::Algo)std::min(ls.upFilter, 2u);
        if (f != upFactorCfg || a != upAlgoCfg)
            flush = true;
        upFactorCfg = f;
        upAlgoCfg = a;
        cfg.textureUpscale = ls.upFactor;
        cfg.textureUpscaleFilter = ls.upFilter;
    }
    if ((ls.mask & L::F_REPLACE) && ls.replace != texReplace) {
        texReplace = ls.replace;
        cfg.textureReplace = texReplace;
        rescan = true;
    }
    if ((ls.mask & L::F_DUMP) && ls.dump != texDump) {
        texDump = ls.dump;
        cfg.textureDump = texDump;
        rescan = true;
        flush = true;
    }
    if (ls.mask & L::F_NATIVE_MENU) {
        nativeMenu = ls.nativeMenu;
        nativeMenuScreen = ls.nativeMenuScreen;
    }
    if (ls.mask & L::F_HUD)
        hudScale = ls.hudScale;
    if (ls.mask & L::F_HUDSIZE)
        hudSize = ls.hudSize;
    if ((ls.mask & L::F_HLSMOOTH) && ls.hlSmooth != smoothHL) {
        smoothHL = ls.hlSmooth;
        flush = true;
    }
    if (rescan) {
        scanTexDir();
        flush = true;
    }
    if (flush)
        flushTexCaches();
    if (flush || rescan)
        ensurePool();
    if (ls.mask & L::F_SCALE)
        changeScale(ls.scale);
    if (ls.mask & L::F_UPSCALE)
        setUpscale(ls.upMode, ls.upPreset, ls.upSharp);
    if (ls.mask & L::F_WATER) {
        discardTemporalWorlds();
        const bool newlyEnabled = !(modernWater || rayShadows) && (ls.modernWater || ls.rayShadows);
        modernWater = ls.modernWater;
        rayReflections = ls.rayReflections;
        rayShadows = ls.rayShadows;
        lastPresentedWater = lastPresentedRay = false;
        if (newlyEnabled)
            flushTexCaches(); // capture the material texels of the textures already cached
        surfaceBuilder->drain();
        for (auto& target : rts) {
            target->surface.triangles.clear();
            target->surface.epoch = target->surfaceSubmittedEpoch = 0;
            target->waterOutputEpoch = target->rayOutputEpoch = 0;
        }
    }
    // F_NEURAL / F_NEURAL_RES: DLSS ray reconstruction needs the Direct3D 11 renderer.
    if (ls.mask & L::F_VSYNC) {
        if (vsyncOn != ls.vsync) {
            vsyncOn = ls.vsync;
            configureSurface();
        }
    }
}

void WgpuBackend::Impl::reset() {
    engineWorldOpen = false;
    gridCapture.open = false;
    gridCapture.rt = nullptr;
    for (auto& r : rts)
        r->wstate = 0;
    sceneCamera = {};
    waterWorldCamera = {};
    neuralWorldClosed = true;
    motionHistory.reset();
    motionWorldOpen = false;
    objectScope = {};
    surfaceBuilder->drain(); // its jobs hold target pointers
    rts.clear();
    depths.clear();
    texCache.clear();
    texBytes = 0;
    upCache.clear();
    upBytes = 0;
}

void WgpuBackend::Impl::noteVuUpload(uint32_t, uint32_t, const float*) {}

} // namespace gsr
