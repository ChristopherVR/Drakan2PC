// Device, swap chain, neural bridge lifecycle, live settings and reset.
#include "d3d11_backend_impl.h"

namespace gsr {

Backend::Impl::Impl(LocalMemory& m, const Config& c, Stats& s) : mem(m), cfg(c), st(s) {
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

bool Backend::Impl::compile(const char* entry, const char* target, const char* defines, ComPtr<ID3DBlob>& out) {
    D3D_SHADER_MACRO macros[] = {{"HW_BLEND", defines}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompile(kHlslMain, std::strlen(kHlslMain), "gs_shaders", macros, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[gsr] shader compile failed (%s): %s\n", entry,
                     err ? (const char*)err->GetBufferPointer() : "?");
        return false;
    }
    return true;
}

bool Backend::Impl::init() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (cfg.debugLayer)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    const D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = E_FAIL;
    if (!cfg.forceWarp)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 2, D3D11_SDK_VERSION, &dev, &got,
                               &ctx);
    if (FAILED(hr) && cfg.debugLayer) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        if (!cfg.forceWarp)
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 2, D3D11_SDK_VERSION, &dev,
                                   &got, &ctx);
    }
    if (FAILED(hr) && (cfg.allowWarp || cfg.forceWarp))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, lv, 2, D3D11_SDK_VERSION, &dev, &got,
                               &ctx);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[gsr] D3D11CreateDevice failed 0x%08lx\n", (unsigned long)hr);
        return false;
    }
    ctx.As(&inventoryContext1);
    if (!inventoryContext1) {
        // D3D11.0 fallback uses the existing fullscreen vertex shader and
        // normal scissor/raster states, without adding shader-file dependencies.
        constexpr char solid[] = "float4 main():SV_Target{return float4(0,0,0,128.0/255.0);}";
        ComPtr<ID3DBlob> shader, errors;
        if (FAILED(D3DCompile(solid, sizeof(solid) - 1, "inventory_canvas", nullptr, nullptr, "main", "ps_5_0", 0, 0,
                              &shader, &errors)) ||
            FAILED(dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr,
                                          &psInventoryCanvas)))
            return false;
    }
    {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> ad;
        if (SUCCEEDED(dev.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&ad))) {
            DXGI_ADAPTER_DESC d{};
            ad->GetDesc(&d);
            char buf[256];
            WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, buf, sizeof(buf), nullptr, nullptr);
            adapter = buf;
            if (texBudgetAuto) {
                const uint64_t vram = (uint64_t)d.DedicatedVideoMemory;
                texBudget = std::clamp<uint64_t>(vram * 4 / 10, 1536ull << 20, 8192ull << 20);
            }
        }
    }

    ComPtr<ID3DBlob> b;
    if (!compile("VSMain", "vs_5_0", "0", b))
        return false;
    dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vs);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    dev->CreateInputLayout(il, 3, b->GetBufferPointer(), b->GetBufferSize(), &layout);
    {
        ComPtr<ID3DBlob> motionVS, motionPS;
        if (!compile("VSMotion", "vs_5_0", "0", motionVS) || !compile("PSMotion", "ps_5_0", "0", motionPS))
            return false;
        if (FAILED(
                dev->CreateVertexShader(motionVS->GetBufferPointer(), motionVS->GetBufferSize(), nullptr, &vsMotion)) ||
            FAILED(dev->CreatePixelShader(motionPS->GetBufferPointer(), motionPS->GetBufferSize(), nullptr, &psMotion)))
            return false;
        const D3D11_INPUT_ELEMENT_DESC mi[] = {
            il[0], il[1], il[2], {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        if (FAILED(
                dev->CreateInputLayout(mi, 4, motionVS->GetBufferPointer(), motionVS->GetBufferSize(), &motionLayout)))
            return false;
    }
    ComPtr<ID3DBlob> p;
    if (!compile("PSMain", "ps_5_0", "1", p))
        return false;
    dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &psHw);
    p.Reset();
    if (!compile("PSMain", "ps_5_0", "0", p))
        return false;
    dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &psSb);
    auto mk = [&](const char* e, const char* t, ComPtr<ID3D11PixelShader>* ps, ComPtr<ID3D11VertexShader>* v) {
        ComPtr<ID3DBlob> bb;
        if (!compile(e, t, "0", bb))
            return false;
        if (ps)
            dev->CreatePixelShader(bb->GetBufferPointer(), bb->GetBufferSize(), nullptr, ps->GetAddressOf());
        if (v)
            dev->CreateVertexShader(bb->GetBufferPointer(), bb->GetBufferSize(), nullptr, v->GetAddressOf());
        return true;
    };
    if (!mk("VSFull", "vs_5_0", nullptr, &vsFull) || !mk("PSDisplay", "ps_5_0", &psDisplay, nullptr) ||
        !mk("PSBlit", "ps_5_0", &psBlit, nullptr) || !mk("PSBlitPt", "ps_5_0", &psBlitPt, nullptr) ||
        !mk("PSPresent", "ps_5_0", &psPresent, nullptr) || !mk("PSCopyRGBA", "ps_5_0", &psCopyRGBA, nullptr) ||
        !mk("PSStretchDepth", "ps_5_0", &psStretchDepth, nullptr))
        return false;

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(PSConsts);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&bd, nullptr, &cbPS);
    cbPSValid = false;
    bd.ByteWidth = sizeof(DispConsts);
    dev->CreateBuffer(&bd, nullptr, &cbDisp);
    bd.ByteWidth = 32;
    dev->CreateBuffer(&bd, nullptr, &cbPresent);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &smpPt);
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    dev->CreateSamplerState(&sd, &smpLin);

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&rd, &rast);

    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].RenderTargetWriteMask = 0xF;
    dev->CreateBlendState(&bl, &blendOff);

    vsyncOn = cfg.vsync;
    if (cfg.hwnd)
        createSwapchain();
    ok = true;
    { // temporal upscaler: config + environment (GSR_UPSCALE=off|dlaa|quality|balanced|performance,
      // GSR_UPSCALE_PRESET=auto|j|k|l|m, GSR_UPSCALE_SHARP, DRAKAN_DLSS_LOG)
        uint32_t m = cfg.upscaleMode, pr = cfg.upscalePreset;
        float sh = cfg.upscaleSharpness;
        if (runtimeFlags().upscaleMode)
            m = *runtimeFlags().upscaleMode;
        if (runtimeFlags().upscalePreset)
            pr = *runtimeFlags().upscalePreset;
        if (runtimeFlags().upscaleSharp)
            sh = *runtimeFlags().upscaleSharp;
        tpLog = runtimeFlags().dlssLog;
        tpTiming = runtimeFlags().upscaleTiming;
        if (m)
            setUpscale(m, pr, sh);
    }
    neuralAtRender = cfg.neuralAtRenderResolution;
    if (cfg.neuralRendering)
        setNeural(true);
    return true;
}

void Backend::Impl::createSwapchain() {
    HWND hwnd = (HWND)cfg.hwnd;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    swapW = cfg.windowWidth ? cfg.windowWidth : std::max<LONG>(rc.right - rc.left, 1);
    swapH = cfg.windowHeight ? cfg.windowHeight : std::max<LONG>(rc.bottom - rc.top, 1);
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> ad;
    ComPtr<IDXGIFactory2> fac;
    dev.As(&dxgi);
    dxgi->GetAdapter(&ad);
    ad->GetParent(IID_PPV_ARGS(&fac));
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = swapW;
    sd.Height = swapH;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 3;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    {
        ComPtr<IDXGIFactory5> fac5;
        BOOL allow = FALSE;
        if (SUCCEEDED(fac.As(&fac5)) &&
            SUCCEEDED(fac5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)))
            tearingOk = allow != FALSE;
    }
    sd.Flags = swapFlags();
    if (FAILED(fac->CreateSwapChainForHwnd(dev.Get(), hwnd, &sd, nullptr, nullptr, &swap))) {
        std::fprintf(stderr, "[gsr] CreateSwapChainForHwnd failed\n");
        swap.Reset();
        return;
    }
    fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    {
        ComPtr<IDXGISwapChain2> sc2;
        if (SUCCEEDED(swap.As(&sc2))) {
            sc2->SetMaximumFrameLatency(1);
            swapWait = sc2->GetFrameLatencyWaitableObject();
            presentSlotHeld = false;
        }
    }
    makeSwapRtv();
}

std::wstring Backend::Impl::neuralDirectory() const {
    if (!cfg.neuralRuntimeDir.empty())
        return std::filesystem::u8path(cfg.neuralRuntimeDir).wstring();
    wchar_t executable[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, executable, (DWORD)std::size(executable));
    if (!n || n >= std::size(executable))
        return {};
    return (std::filesystem::path(executable).parent_path() / L"nr").wstring();
}

bool Backend::Impl::neuralAvailable(std::string& status) const {
    status = "DLSS 5 bridge is not included in this build";
    return false;
}

std::string Backend::Impl::neuralStatus() const {
    if (!cfg.neuralRendering || !neuralEnabled)
        return neuralStatusText;
    return neuralPresentation.status(neuralStatusText);
}

void Backend::Impl::setNeural(bool on) {
    neuralPresentation.reset();
    discardTemporalWorlds();
    for (auto& target : rts) {
        if (target->wstate == 1)
            finalizeWorld(*target);
        target->neuralState = 0;
    }
    motionHistory.reset();
    motionOrdinals.clear();
    motionWorldOpen = false;
    objectScope = {};
    neuralPrevious = {};
    if (combinedSr)
        combinedSr->reset();
    cfg.neuralRendering = on;
    upReset = true;
    camPrevValid = false;
    neuralEnabled = false;
    neuralStatusText = on ? "DLSS 5 bridge is not included in this build" : "DLSS 5 neural rendering off";
}

void Backend::Impl::retireNeural(const std::string& failure) {
    discardTemporalWorlds();
    neuralEnabled = false;
    neuralReset = true;
    neuralWorldClosed = true;
    neuralPresentation.reset();
    motionHistory.reset();
    motionOrdinals.clear();
    motionWorldOpen = false;
    objectScope = {};
    if (combinedSr)
        combinedSr->reset();
    for (auto& target : rts)
        target->neuralState = 0;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    swapRtv.Reset();
    ctx->Flush();
    createSwapchain();
    neuralStatusText = "DLSS 5 disabled after failure: " + failure;
    if (!swapRtv) {
        ok = false;
        neuralStatusText += "; presentation unavailable, restart required";
    }
    std::fprintf(stderr, "[dlss-nr] %s\n", neuralStatusText.c_str());
}

void Backend::Impl::makeSwapRtv() {
    swapRtv.Reset();
    ComPtr<ID3D11Texture2D> bb;
    if (swap && SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&bb))))
        dev->CreateRenderTargetView(bb.Get(), nullptr, &swapRtv);
}

void Backend::Impl::resize(uint32_t w, uint32_t h) {
    if (!w || !h)
        return;
    if (!swap)
        return;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    swapRtv.Reset();
    swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, swapFlags());
    swapW = w;
    swapH = h;
    makeSwapRtv();
}

ComPtr<ID3D11Texture2D> Backend::Impl::makeTex(uint32_t w, uint32_t h, UINT bind, DXGI_FORMAT fmt) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bind;
    ComPtr<ID3D11Texture2D> t;
    dev->CreateTexture2D(&d, nullptr, &t);
    return t;
}

void Backend::Impl::changeScale(uint32_t ns) {
    if (ns == scale)
        return;
    // GPU render targets -> local memory (so nothing the game drew is lost), then drop every scale dependent
    // object; targets are re-created at the new size on demand and re-filled from local memory (syncRT), upscaled
    // with nearest filtering.
    discardTemporalWorlds();
    finalizeAllWorlds();
    flushAll();
    if (tp)
        tp->reset();
    upReset = true;
    camPrevValid = false;
    worldFrames = 0;
    lastWorldFrame = 0;
    surfaceBuilder->drain();
    rts.clear();
    depths.clear();
    neuralReset = true;
    neuralWorldClosed = true;
    neuralPrevious = {};
    motionHistory.reset();
    motionOrdinals.clear();
    motionWorldOpen = false;
    objectScope = {};
    if (combinedSr)
        combinedSr->reset();
    scale = ns; // dispTex (latched image) stays valid until the next latch re-renders it; latchedScale keeps
                // presenting it correctly
    mipBias = cfg.mipBiasAuto ? -std::log2((float)scale) : cfg.mipBias;
    if (upFactorCfg == 0)
        flushTexCaches(); // "auto" upscale factor depends on the internal scale
    st.liveScaleChanges++;
}

void Backend::Impl::applyLive(const LiveSettings& ls) {
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
        lastPresentedWater = false;
        lastPresentedRay = false;
        if (newlyEnabled)
            flushTexCaches();
        // Disabling does not leave stale AS triangles or an enhanced epoch.
        surfaceBuilder->drain();
        for (auto& target : rts) {
            target->surface.triangles.clear();
            target->surface.epoch = target->surfaceSubmittedEpoch = 0;
            target->waterOutputEpoch = target->rayOutputEpoch = 0;
        }
        depths.clear();
        neuralReset = true;
        motionHistory.reset();
        if (neuralGuides)
            neuralGuides->reset();
        if (combinedSr)
            combinedSr->reset();
    }
    if (ls.mask & L::F_NEURAL)
        setNeural(ls.neural);
    if ((ls.mask & L::F_NEURAL_RES) && ls.neuralRender != neuralAtRender) {
        // NR and SR now see different images: neither history carries over.
        discardTemporalWorlds();
        neuralAtRender = ls.neuralRender;
        neuralReset = true;
        motionHistory.reset();
        if (neuralGuides)
            neuralGuides->reset();
        if (combinedSr)
            combinedSr->reset();
    }
    if (flush || (ls.mask & L::F_SCALE)) {
        // released textures / targets are only destroyed by D3D once the context no longer references them: unbind
        // and flush
        ID3D11ShaderResourceView* nulSrv[16] = {};
        ctx->PSSetShaderResources(0, 16, nulSrv);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ctx->Flush();
    }
    if (ls.mask & L::F_VSYNC)
        vsyncOn = ls.vsync;
    if (ls.mask & L::F_LATENCY) {
        ComPtr<IDXGISwapChain2> sc2;
        if (swap && SUCCEEDED(swap.As(&sc2)))
            sc2->SetMaximumFrameLatency(ls.latency);
    }
}

void Backend::Impl::reset() {
    engineWorldOpen = false;
    gridCapture.open = false;
    gridCapture.rt = nullptr;
    // Guest reset invalidates scene and GPU history; discard pending world phases.
    for (auto& r : rts)
        r->wstate = 0;
    if (tp)
        tp->reset();
    camCur = {};
    camPrev = {};
    neuralReset = true;
    neuralWorldClosed = true;
    neuralPrevious = {};
    sceneCamera = {};
    waterWorldCamera = {};
    motionHistory.reset();
    motionOrdinals.clear();
    motionWorldOpen = false;
    objectScope = {};
    if (combinedSr)
        combinedSr->reset();
    upReset = true;
    camPrevValid = false;
    worldFrames = 0;
    lastWorldFrame = 0;
    curJx = curJy = 0;
    surfaceBuilder->drain();
    rts.clear();
    depths.clear();
    texCache.clear();
    texBytes = 0;
    upCache.clear();
    upBytes = 0;
}

} // namespace gsr
