// Device, swap chain, neural bridge lifecycle, live settings and reset.
#include "d3d11_backend_impl.h"
#include "shader_cache.h"

namespace gsr {

D3D11Backend::Impl::Impl(LocalMemory& m, const Config& c, Stats& s) : mem(m), cfg(c), st(s) {
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

bool D3D11Backend::Impl::compile(const char* entry, const char* target, const char* defines, ComPtr<ID3DBlob>& out,
                                 bool material) {
    // MATERIAL adds the material detail variant of the pixel shader (gs_material.inc); without it the source is
    // the one every other draw uses.
    D3D_SHADER_MACRO macros[] = {{"HW_BLEND", defines}, {material ? "MATERIAL" : nullptr, "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> err;
    HRESULT hr = shader_compile::compile(kHlslMain, std::strlen(kHlslMain), "gs_shaders", macros, entry, target,
                                         D3DCOMPILE_OPTIMIZATION_LEVEL3, &out, &err);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[gsr] shader compile failed (%s): %s\n", entry,
                     err ? (const char*)err->GetBufferPointer() : "?");
        return false;
    }
    return true;
}

bool D3D11Backend::Impl::init() {
    const auto initStart = std::chrono::steady_clock::now();
    shader_compile::configureDirectory();
    {
        // The uber shader variants are independent compiles of one source: run them side by side (the sequential
        // creation below then takes them from the cache).
        struct Variant {
            const char* entry;
            const char* target;
            const char* define;
        };
        static constexpr Variant kMain[] = {
            {"VSMain", "vs_5_0", "0"},    {"VSMotion", "vs_5_0", "0"},   {"PSMotion", "ps_5_0", "0"},
            {"PSMain", "ps_5_0", "1"},    {"PSMain", "ps_5_0", "0"},     {"VSFull", "vs_5_0", "0"},
            {"PSDisplay", "ps_5_0", "0"}, {"PSBlit", "ps_5_0", "0"},     {"PSBlitPt", "ps_5_0", "0"},
            {"PSPresent", "ps_5_0", "0"}, {"PSCopyRGBA", "ps_5_0", "0"}, {"PSStretchDepth", "ps_5_0", "0"}};
        std::vector<std::thread> compilers;
        for (const Variant& v : kMain)
            compilers.emplace_back([this, v] {
                ComPtr<ID3DBlob> ignored;
                compile(v.entry, v.target, v.define, ignored);
            });
        for (auto& t : compilers)
            t.join();
    }
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
        if (FAILED(shader_compile::compile(solid, sizeof(solid) - 1, "inventory_canvas", nullptr, "main", "ps_5_0", 0,
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
    warmMainPipelines();
    startPrewarm();
    shader_compile::markStartupDone();
    const auto stats = shader_cache::global().stats();
    const std::string cacheDir = shader_cache::global().directory();
    std::fprintf(stderr,
                 "[gsr-shader] renderer start-up %.0f ms: %llu compiled (%.0f ms), %llu from the disk cache, %llu "
                 "from memory; cache folder %s\n",
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initStart).count(),
                 (unsigned long long)stats.produced, stats.produceMs, (unsigned long long)stats.diskHits,
                 (unsigned long long)stats.memoryHits, cacheDir.empty() ? "(off)" : cacheDir.c_str());
    return true;
}

void D3D11Backend::Impl::warmMainPipelines() {
    // Drivers translate a shader for the GPU at its first draw, not at Create*Shader: on a clean machine under load
    // that took 13 to 30 s inside the first menu frame, on the GS worker, while the EE waited for its commit
    // (docs/journal/2026-11-06/boot-hang.md). Draw every start-up pipeline once into tiny targets here, before the
    // guest runs, and wait for the GPU.
    if (const char* v = std::getenv("GSR_SHADER_WARM_DRAWS"); v && v[0] == '0')
        return;
    shader_compile::Timed timed("main pipelines (warm draws)");
    auto colorTex = makeTex(8, 8, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    auto sourceTex = makeTex(8, 8, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    auto motionTex = makeTex(8, 8, D3D11_BIND_RENDER_TARGET, DXGI_FORMAT_R32G32B32A32_FLOAT);
    auto depthTex = makeTex(8, 8, D3D11_BIND_DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT);
    ComPtr<ID3D11RenderTargetView> colorRtv, motionRtv;
    ComPtr<ID3D11ShaderResourceView> sourceSrv;
    ComPtr<ID3D11DepthStencilView> dsv;
    if (!colorTex || !sourceTex || !motionTex || !depthTex ||
        FAILED(dev->CreateRenderTargetView(colorTex.Get(), nullptr, &colorRtv)) ||
        FAILED(dev->CreateRenderTargetView(motionTex.Get(), nullptr, &motionRtv)) ||
        FAILED(dev->CreateShaderResourceView(sourceTex.Get(), nullptr, &sourceSrv)) ||
        FAILED(dev->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv)))
        return;
    ComPtr<ID3D11Buffer> vb, mvb;
    {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = 3 * sizeof(DrawVertex);
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        const DrawVertex tri[3] = {{0, 0, 0, 0, 255, 255, 255, 128, 0, 0, 1},
                                   {8, 0, 0, 0, 255, 255, 255, 128, 1, 0, 1},
                                   {0, 8, 0, 0, 255, 255, 255, 128, 0, 1, 1}};
        D3D11_SUBRESOURCE_DATA vertices{tri, 0, 0};
        dev->CreateBuffer(&d, &vertices, &vb);
        d.ByteWidth = 3 * sizeof(motion::Previous);
        const motion::Previous prev[3]{};
        D3D11_SUBRESOURCE_DATA previous{prev, 0, 0};
        dev->CreateBuffer(&d, &previous, &mvb);
    }
    if (!vb || !mvb)
        return;
    const D3D11_VIEWPORT vp{0, 0, 8, 8, 0, 1};
    const D3D11_RECT sc{0, 0, 8, 8};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetScissorRects(1, &sc);
    ctx->RSSetState(rast.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11SamplerState* smps[2] = {smpPt.Get(), smpLin.Get()};
    ctx->PSSetSamplers(0, 2, smps);
    ID3D11ShaderResourceView* srvs[2] = {sourceSrv.Get(), sourceSrv.Get()};
    ID3D11ShaderResourceView* nulls[2] = {nullptr, nullptr};
    // GS pixel pipeline: both uber shader variants, opaque and alpha blended, with and without depth writes.
    const UINT stride = sizeof(DrawVertex), mstride = sizeof(motion::Previous), zero = 0;
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &zero);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, cbPS.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, cbPS.GetAddressOf());
    ctx->PSSetShaderResources(0, 2, srvs);
    ctx->OMSetRenderTargets(1, colorRtv.GetAddressOf(), dsv.Get());
    ID3D11BlendState* blends[2] = {
        blendOff.Get(), getBlendState(true, D3D11_BLEND_OP_ADD, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, 0xF)};
    for (ID3D11PixelShader* ps : {psHw.Get(), psSb.Get()})
        for (ID3D11BlendState* bs : blends)
            for (bool zw : {false, true}) {
                ctx->PSSetShader(ps, nullptr, 0);
                ctx->OMSetBlendState(bs, nullptr, 0xffffffffu);
                ctx->OMSetDepthStencilState(getDepthState(D3D11_COMPARISON_GREATER_EQUAL, zw), 0);
                ctx->Draw(3, 0);
            }
    // Velocity pass of the motion pipeline.
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(1, motionRtv.GetAddressOf(), dsv.Get());
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(getDepthState(D3D11_COMPARISON_EQUAL, false), 0);
    ctx->IASetInputLayout(motionLayout.Get());
    ctx->IASetVertexBuffers(1, 1, mvb.GetAddressOf(), &mstride, &zero);
    ctx->VSSetShader(vsMotion.Get(), nullptr, 0);
    ctx->PSSetShader(psMotion.Get(), nullptr, 0);
    ctx->Draw(3, 0);
    ID3D11Buffer* noBuffer = nullptr;
    ctx->IASetVertexBuffers(1, 1, &noBuffer, &zero, &zero);
    // Full-screen passes (display, blits, present, copies, inventory canvas, depth stretch).
    ctx->IASetInputLayout(nullptr);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cbDisp.GetAddressOf());
    ctx->PSSetConstantBuffers(2, 1, cbPresent.GetAddressOf());
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->OMSetRenderTargets(1, colorRtv.GetAddressOf(), nullptr);
    ctx->PSSetShaderResources(0, 2, srvs);
    for (ID3D11PixelShader* ps :
         {psDisplay.Get(), psBlit.Get(), psBlitPt.Get(), psPresent.Get(), psCopyRGBA.Get(), psInventoryCanvas.Get()}) {
        if (!ps)
            continue;
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->Draw(3, 0);
    }
    ctx->PSSetShaderResources(0, 2, nulls);
    if (psStretchDepth) {
        ctx->OMSetRenderTargets(0, nullptr, dsv.Get());
        ctx->OMSetDepthStencilState(getDepthState(D3D11_COMPARISON_ALWAYS, true), 0);
        ctx->PSSetShaderResources(0, 1, srvs);
        ctx->PSSetShader(psStretchDepth.Get(), nullptr, 0);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 2, nulls);
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> done;
    if (FAILED(dev->CreateQuery(&qd, &done)))
        return;
    ctx->End(done.Get());
    ctx->Flush();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    BOOL finished = FALSE;
    while (ctx->GetData(done.Get(), &finished, sizeof finished, 0) == S_FALSE &&
           std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void D3D11Backend::Impl::startPrewarm() {
    if (const char* v = std::getenv("GSR_SHADER_PREWARM"); v && v[0] == '0')
        return;
    // The device outlives the thread: ~D3D11Backend joins it before releasing anything. D3D11 device methods are
    // free-threaded; the immediate context is never touched here.
    ID3D11Device* device = dev.Get();
    prewarmThread = std::thread([device] {
        shader_compile::markPrewarmThread();
        const auto t0 = std::chrono::steady_clock::now();
        TemporalPipeline::prewarm(device);
        sr_combined::Pipeline::prewarm(device);
        nr::guides::prewarm(device);
        NativeReadback::prewarm(device);
        PartialUploadGpu::prewarm(device);
        TargetUploadGpu::prewarm(device);
        prewarmMovieShader(device);
        prewarmBackdropShader(device);
        std::fprintf(stderr, "[gsr-shader] background prewarm done in %.0f ms\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    });
    if (modernWater || rayShadows || rayReflections || rayGlobalIllumination)
        startRayPrewarm();
}

// The ray tracing renderer (its own D3D12 device and three compute pipelines) takes about a second to create, 18 s
// with a cold driver cache. Build it on a background thread as soon as a ray effect is configured (start-up or the
// live settings the host applies right after it); world frames adopt it once it is finished (rayPass) and draw
// without the ray effects until then. Nothing waits for it.
void D3D11Backend::Impl::startRayPrewarm() {
    ID3D11Device* device = dev.Get();
    rayLoader.start(
        [device] {
            shader_compile::markPrewarmThread();
            shader_compile::Timed timed("ray tracing device and pipelines");
            return std::make_unique<surface::RayWater>(device, nullptr);
        },
        raystatus::prewarmDelay());
}

void D3D11Backend::Impl::joinPrewarm() {
    if (prewarmThread.joinable())
        prewarmThread.join();
    rayLoader.join();
}

// The ray pass for this frame, or null while it is still being built (the frame then draws the plain raster). Starts
// the build when no setting did (GSR_SHADER_PREWARM=0 or a renderer created with the effects off).
surface::RayWater* D3D11Backend::Impl::rayPass() {
    return raystatus::acquirePass(
        waterRenderer, rayLoader, [&] { startRayPrewarm(); },
        [](std::unique_ptr<surface::RayWater> built) {
            std::fprintf(stderr, "[modern-water] ray tracing ready: %s\n", built->status().c_str());
            return built;
        });
}

void D3D11Backend::Impl::createSwapchain() {
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

std::wstring D3D11Backend::Impl::neuralDirectory() const {
    if (!cfg.neuralRuntimeDir.empty())
        return std::filesystem::u8path(cfg.neuralRuntimeDir).wstring();
    wchar_t executable[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, executable, (DWORD)std::size(executable));
    if (!n || n >= std::size(executable))
        return {};
    return (std::filesystem::path(executable).parent_path() / L"nr").wstring();
}

bool D3D11Backend::Impl::neuralAvailable(std::string& status) const {
    status = "DLSS 5 bridge is not included in this build";
    return false;
}

std::string D3D11Backend::Impl::neuralStatus() const {
    if (!cfg.neuralRendering || !neuralEnabled)
        return neuralStatusText;
    return neuralPresentation.status(neuralStatusText);
}

void D3D11Backend::Impl::setNeural(bool on) {
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

void D3D11Backend::Impl::retireNeural(const std::string& failure) {
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

void D3D11Backend::Impl::makeSwapRtv() {
    swapRtv.Reset();
    ComPtr<ID3D11Texture2D> bb;
    if (swap && SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&bb))))
        dev->CreateRenderTargetView(bb.Get(), nullptr, &swapRtv);
}

void D3D11Backend::Impl::resize(uint32_t w, uint32_t h) {
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

ComPtr<ID3D11Texture2D> D3D11Backend::Impl::makeTex(uint32_t w, uint32_t h, UINT bind, DXGI_FORMAT fmt) {
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

void D3D11Backend::Impl::changeScale(uint32_t ns) {
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

void D3D11Backend::Impl::applyLive(const LiveSettings& ls) {
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
    if (ls.mask & L::F_TERRAIN) {
        cfg.terrainDetail = ls.terrainDetail;
        cfg.terrainCliffs = ls.terrainCliffs;
        cfg.terrainDetailStrength = ls.terrainStrength;
        initTerrain();
    }
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
    if (ls.mask & L::F_MATERIAL) {
        const material::Mode before = materialMode;
        cfg.materialDetail = std::min(ls.materialDetail, 2u);
        cfg.materialDetailStrength = ls.materialStrength;
        cfg.materialSpecular = ls.materialSpecular;
        initMaterial();
        if (materialMode != before) { // other classes, or maps to load / drop: decode the textures again
            rescan = true;
            flush = true;
        }
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
        if (ls.modernWater || ls.rayShadows || ls.rayReflections || ls.rayGlobalIllumination)
            startRayPrewarm(); // build the ray tracing device now, not inside the first world frame
        discardTemporalWorlds();
        const bool newlyEnabled = !(modernWater || rayShadows || rayReflections || rayGlobalIllumination) &&
                                  (ls.modernWater || ls.rayShadows || ls.rayReflections || ls.rayGlobalIllumination);
        modernWater = ls.modernWater;
        rayReflections = ls.rayReflections;
        rayShadows = ls.rayShadows;
        rayGlobalIllumination = ls.rayGlobalIllumination;
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

void D3D11Backend::Impl::reset() {
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
