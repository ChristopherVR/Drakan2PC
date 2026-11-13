// Render-target and depth-buffer management, blend/depth state caches, readback to GS memory: the Direct3D 11 half.
// The policy is shared (gs_targets.inl); this file holds its hooks, the readback prefetch and GSR_RB_VERIFY.
#include "d3d11_backend_impl.h"

#include <array>
#include <map>

namespace gsr::backend_hook {

// Backend interface of gs_targets.inl.
inline void replaceTargetTexture(D3D11Backend::Impl& s, RT& rt, uint32_t newH) {
    const uint32_t W = rt.w * s.scale, H = newH * s.scale;
    ComPtr<ID3D11Texture2D> t =
        s.gone() ? nullptr : s.makeTex(W, H, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11RenderTargetView> rv;
    ComPtr<ID3D11ShaderResourceView> sv;
    // A removed device (d3d11_device_loss.cpp) gives no objects: the old ones stay, the shared bookkeeping still grows
    // with the height the shared code expects, and no call reaches the device with a null object.
    if (t && s.deviceOk(s.dev->CreateRenderTargetView(t.Get(), nullptr, &rv), "target view") &&
        s.deviceOk(s.dev->CreateShaderResourceView(t.Get(), nullptr, &sv), "target view")) {
        const float clr[4] = {0, 0, 0, rt.psm == PSM_CT24 ? 0.5f : 0.0f};
        s.ctx->ClearRenderTargetView(rv.Get(), clr);
        if (rt.tex) {
            const uint32_t keepH = std::min(rt.h, newH) * s.scale;
            D3D11_BOX box{0, 0, 0, W, keepH, 1};
            s.ctx->CopySubresourceRegion(t.Get(), 0, 0, 0, 0, rt.tex.Get(), 0, &box);
        }
        rt.tex = t;
        rt.rtv = rv;
        rt.srv = sv;
    }
    rt.copy.Reset();
    rt.copySrv.Reset();
}
inline const char* clearCanvasMargins(D3D11Backend::Impl& s, RT& rt, const hud::CanvasMargins& margins) {
    auto& ctx = s.ctx;
    D3D11_RECT rect[2]{};
    for (unsigned i = 0; i < margins.count; ++i) {
        const auto& r = margins.rect[i];
        rect[i] = {(LONG)r.left, (LONG)r.top, (LONG)r.right, (LONG)r.bottom};
    }
    ID3D11ShaderResourceView* nulls[2]{};
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    const float black[4] = {0, 0, 0, 128.0f / 255.0f};
    if (s.inventoryContext1) {
        s.inventoryContext1->ClearView(rt.rtv.Get(), black, rect, margins.count);
        return "ClearView";
    }
    // Called before vertex-buffer mapping. The ensuing normal draw
    // rebinds every state touched here; no DSV is bound or cleared.
    ID3D11Buffer* noVB = nullptr;
    const UINT zero = 0;
    ctx->IASetVertexBuffers(0, 1, &noVB, &zero, &zero);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(s.psInventoryCanvas.Get(), nullptr, 0);
    ctx->RSSetState(s.rast.Get());
    D3D11_VIEWPORT vp{0, 0, (float)(rt.w * s.scale), (float)(rt.h * s.scale), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(s.blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ID3D11RenderTargetView* target = rt.rtv.Get();
    ctx->OMSetRenderTargets(1, &target, nullptr);
    for (unsigned i = 0; i < margins.count; ++i) {
        ctx->RSSetScissorRects(1, &rect[i]);
        ctx->Draw(3, 0);
    }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return "solidPS";
}
// A target the device cannot write any more (a removed device, d3d11_device_loss.cpp): refreshes are skipped.
inline bool targetUnusable(const D3D11Backend::Impl& s, const RT& rt) {
    return s.gone() || !rt.tex;
}
// The GPU replicates the pixels (rt_upload_d3d11.h), in bands.
inline bool replicateTargetRect(D3D11Backend::Impl& s, RT& rt, const uint32_t* pixels, uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height) {
    bool done = true;
    for (uint32_t band = 0; band < height && done; band += TargetUploadGpu::kBandRows) {
        const uint32_t rows = std::min(TargetUploadGpu::kBandRows, height - band);
        done = s.targetUploadGpu.upload(s.dev.Get(), s.ctx.Get(), rt.tex.Get(), pixels + size_t(band) * width, x,
                                        y + band, width, rows, s.scale);
    }
    return done;
}
inline void writeTargetRect(D3D11Backend::Impl& s, RT& rt, const uint32_t* pixels, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height) {
    D3D11_BOX box{x * s.scale, y * s.scale, 0, (x + width) * s.scale, (y + height) * s.scale, 1};
    s.ctx->UpdateSubresource(rt.tex.Get(), 0, &box, pixels, width * s.scale * 4, 0);
}
inline ID3D11Texture2D* nativeTargetSource(D3D11Backend::Impl& s, RT& rt) {
    return s.scale > 1 ? s.nativeReadback.gather(s.dev.Get(), s.ctx.Get(), rt.srv.Get(), rt.w, rt.h, s.scale, 0)
                       : rt.tex.Get();
}
inline void copyTargetBlock(D3D11Backend::Impl& s, RT& rt, ID3D11Texture2D* source, GpuTexture& texture, uint32_t sx,
                            uint32_t sy, uint32_t x, uint32_t y, uint32_t, uint32_t) {
    const D3D11_BOX box{sx, sy, 0, std::min(sx + 8, rt.w), std::min(sy + 8, rt.h), 1};
    s.ctx->CopySubresourceRegion(texture.tex.Get(), 0, x, y, 0, source, 0, &box);
}
inline void createDepthTexture(D3D11Backend::Impl& s, DepthBuf& d) {
    if (s.gone()) // no objects on a removed device: draws bind no depth, the world passes see no readable depth
        return;
    const uint32_t w = d.w, h = d.h, scale = s.scale;
    if (s.upMode || s.neuralEnabled || s.modernWater || s.rayShadows || s.rayReflections || s.rayGlobalIllumination) {
        d.tex = s.makeTex(w * scale, h * scale, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                          DXGI_FORMAT_R32_TYPELESS);
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        s.dev->CreateDepthStencilView(d.tex.Get(), &dd, &d.dsv);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        s.dev->CreateShaderResourceView(d.tex.Get(), &sd, &d.srv);
    } else {
        d.tex = s.makeTex(w * scale, h * scale, D3D11_BIND_DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT);
        s.dev->CreateDepthStencilView(d.tex.Get(), nullptr, &d.dsv);
    }
    if (!d.dsv) {
        d.srv.Reset();
        d.tex = s.makeTex(w * scale, h * scale, D3D11_BIND_DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT);
        if (d.tex)
            s.dev->CreateDepthStencilView(d.tex.Get(), nullptr, &d.dsv);
    }
    if (d.dsv)
        s.ctx->ClearDepthStencilView(d.dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
}
inline bool prepareGridCapture(D3D11Backend::Impl& s, RT& rt, uint32_t& W, uint32_t& H) {
    auto& g = s.gridCapture;
    D3D11_TEXTURE2D_DESC d{};
    rt.tex->GetDesc(&d);
    if (!g.save || !g.tex || g.texW != d.Width || g.texH != d.Height || g.fmt != d.Format) {
        g.save = s.makeTex(d.Width, d.Height, D3D11_BIND_SHADER_RESOURCE, d.Format);
        g.tex = s.makeTex(d.Width, d.Height, D3D11_BIND_SHADER_RESOURCE, d.Format);
        g.srv.Reset();
        if (g.tex)
            s.dev->CreateShaderResourceView(g.tex.Get(), nullptr, &g.srv);
        g.texW = d.Width;
        g.texH = d.Height;
        g.fmt = d.Format;
    }
    W = d.Width;
    H = d.Height;
    return g.save && g.tex && g.srv;
}
inline void saveGridRect(D3D11Backend::Impl& s, RT& rt, const uint32_t* px) {
    auto& g = s.gridCapture;
    g.px = {px[0], px[1], 0, px[2], px[3], 1};
    s.ctx->CopySubresourceRegion(g.save.Get(), 0, g.px.left, g.px.top, 0, rt.tex.Get(), 0, &g.px);
}
inline void restoreGridRect(D3D11Backend::Impl& s) {
    auto& g = s.gridCapture;
    s.ctx->CopySubresourceRegion(g.tex.Get(), 0, g.px.left, g.px.top, 0, g.rt->tex.Get(), 0, &g.px);
    s.ctx->CopySubresourceRegion(g.rt->tex.Get(), 0, g.px.left, g.px.top, 0, g.save.Get(), 0, &g.px);
}
inline std::array<uint32_t, 4> gridRect(const D3D11Backend::Impl& s) {
    const auto& px = s.gridCapture.px;
    return {px.left, px.top, px.right, px.bottom};
}

} // namespace gsr::backend_hook

#define GSR_IMPL D3D11Backend::Impl
#include "gs_targets.inl"
#undef GSR_IMPL

namespace gsr {

void D3D11Backend::Impl::ensureStaging(uint32_t w, uint32_t h) {
    if (staging && stagingW >= w && stagingH >= h)
        return;
    stagingW = std::max(w, stagingW);
    stagingH = std::max(h, stagingH);
    D3D11_TEXTURE2D_DESC d{};
    d.Width = stagingW;
    d.Height = stagingH;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging.Reset();
    if (!gone() && !deviceOk(dev->CreateTexture2D(&d, nullptr, &staging), "readback staging"))
        stagingW = stagingH = 0; // created again by the next readback
}

void D3D11Backend::Impl::prefetchReadback(RT& rt, bool stale) {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_RB_PREFETCH");
        return !value || value[0] != '0';
    }();
    // Learn from actual reads. Copy small scratch targets immediately after
    // their draws, before later unrelated GPU work can get ahead of the read.
    // World/effects targets have writes outside draw(), so never predict them.
    // stale: a scratch target the game stopped drawing into (endFrame). The next upload into its memory reads it
    // back (beginTargetUpload no longer patches a stale target), usually many frames later: with the copy made now
    // that readback maps a finished copy instead of waiting for everything the GPU still has queued.
    const bool predicted = stale || (rt.readbackFrame != ~0ull && frame <= rt.readbackFrame + 2);
    if (!enabled || !predicted || gone() || !rt.tex || !rt.dirty || rt.wstate || rt.neuralState || rt.temporalEpoch ||
        rt.surfaceSubmittedEpoch || rt.neuralOutputEpoch)
        return;
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    // One copy per stale event may be larger (a 256 x 256 light or shadow target) than one after every draw.
    if (y1 <= y0 || uint64_t(rt.w) * (y1 - y0) > (stale ? 65536u : 16384u))
        return;
    ID3D11Texture2D* compact = scale > 1 && nativeReadbackEnabled
                                   ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, y1 - y0, scale, y0)
                                   : nullptr;
    const uint32_t readScale = compact ? 1 : scale, w = rt.w * readScale, h = (y1 - y0) * readScale;
    auto& copy = rt.readbackCopy;
    if (!copy.texture || copy.width < w || copy.height < h) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        copy.texture.Reset();
        if (!deviceOk(dev->CreateTexture2D(&d, nullptr, &copy.texture), "readback prefetch copy"))
            return;
        copy.width = w;
        copy.height = h;
    }
    const uint32_t readY = compact ? 0 : y0 * scale;
    const D3D11_BOX box{0, readY, 0, w, readY + h, 1};
    ctx->CopySubresourceRegion(copy.texture.Get(), 0, 0, 0, 0, compact ? compact : rt.tex.Get(), 0, &box);
    cpuProfile.copied(uint64_t(w) * h * 4);
    copy.writes = rt.writes;
    copy.y0 = y0;
    copy.y1 = y1;
    copy.readScale = readScale;
    copy.renderScale = scale;
    traceReadback(stale ? "prefetch-stale" : "prefetch", frame, rt);
}

// The dirty rows [y0, y1) of the target, mapped: from the prefetched copy when it is still current, else through
// the staging texture (scaled targets gather their native centre samples first).
const uint8_t* D3D11Backend::Impl::beginReadback(RT& rt, uint32_t y0, uint32_t y1, uint32_t& readScale, size_t& pitch) {
    const uint32_t rows = y1 - y0;
    if (gone() || !rt.tex)
        return nullptr; // memory keeps what it has; the session is ending (d3d11_device_loss.cpp)
    auto& copy = rt.readbackCopy;
    const bool prefetched =
        copy.texture && copy.writes == rt.writes && copy.y0 == y0 && copy.y1 == y1 && copy.renderScale == scale;
    traceReadback(prefetched ? "readback-prefetched" : "readback", frame, rt);
    ID3D11Texture2D* compact = !prefetched && scale > 1 && nativeReadbackEnabled
                                   ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, rows, scale, y0)
                                   : nullptr;
    readScale = prefetched ? copy.readScale : compact ? 1 : scale;
    if (!prefetched) {
        ensureStaging(rt.w * readScale, rows * readScale);
        if (!staging)
            return nullptr;
        const uint32_t readY = compact ? 0 : y0 * scale;
        const D3D11_BOX box{0, readY, 0, rt.w * readScale, readY + rows * readScale, 1};
        ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, compact ? compact : rt.tex.Get(), 0, &box);
        cpuProfile.copied(uint64_t(rt.w) * rows * readScale * readScale * 4);
    }
    readbackMapped = prefetched ? copy.texture.Get() : staging.Get();
    D3D11_MAPPED_SUBRESOURCE ms{};
    HRESULT mapResult;
    {
        CpuProfile::Scope mapProfile(cpuProfile, CpuProfile::ReadbackMap);
        mapResult = map(readbackMapped, D3D11_MAP_READ, ms, "target readback");
    }
    if (FAILED(mapResult))
        return nullptr;
    pitch = ms.RowPitch;
    return (const uint8_t*)ms.pData;
}

void D3D11Backend::Impl::endReadback() {
    ctx->Unmap(readbackMapped, 0);
}

void D3D11Backend::Impl::copyTargetTop(RT& src, RT& dst, uint32_t w, uint32_t h) {
    if (gone() || !src.tex || !dst.tex)
        return;
    D3D11_BOX box{0, 0, 0, w * scale, h * scale, 1};
    ctx->CopySubresourceRegion(dst.tex.Get(), 0, 0, 0, 0, src.tex.Get(), 0, &box);
}

bool D3D11Backend::Impl::preparePartialPatch() {
    return partialUploadGpu.prepare(dev.Get(), scale);
}

void D3D11Backend::Impl::patchTargetBlock(RT& rt, uint32_t bx, uint32_t by, const uint32_t* patch) {
    if (gone() || !rt.tex)
        return;
    partialUploadGpu.patch(ctx.Get(), rt.tex.Get(), rt.srv.Get(), bx, by, patch);
}

// GSR_RB_VERIFY=1 (debug): reads the target back anyway and compares the pixels of blocks [first, last] with memory;
// a difference means a skipped readback would have served stale data.
void D3D11Backend::Impl::verifyRTBlocks(RT& rt, uint32_t first, uint32_t last, uint32_t rowLo, uint32_t rowHi,
                                        uint32_t cmpBase, uint32_t cmpFbw, const PartialUpload* requested) {
    if (rt.wstate == 1 || rt.dy1 <= rt.dy0 || gone() || !rt.tex)
        return;
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h), rows = y1 - y0;
    ID3D11Texture2D* compact = scale > 1 && nativeReadbackEnabled
                                   ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, rows, scale, y0)
                                   : nullptr;
    const uint32_t readScale = compact ? 1 : scale;
    ensureStaging(rt.w * readScale, rows * readScale);
    const uint32_t readY = compact ? 0 : y0 * scale;
    D3D11_BOX box{0, readY, 0, rt.w * readScale, readY + rows * readScale, 1};
    if (!staging)
        return;
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, compact ? compact : rt.tex.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(map(staging.Get(), D3D11_MAP_READ, ms, "readback verify")))
        return;
    static uint64_t compared = 0, bad = 0, calls = 0;
    const uint8_t* src = (const uint8_t*)ms.pData;
    const uint32_t half = readScale / 2;
    uint64_t badHere = 0;
    for (uint32_t y = 0; y < rows; ++y) {
        const uint8_t* row = src + (size_t)(y * readScale + half) * ms.RowPitch;
        for (uint32_t x = 0; x < rt.w; ++x) {
            const uint32_t b = blockOf(rt.psm, rt.base(), rt.fbw, x, y0 + y);
            if (b < first || b > last || y0 + y < rowLo || y0 + y >= rowHi)
                continue;
            ++compared;
            const uint32_t gpu = packPixel(rt.psm, row + (size_t)(x * readScale + half) * 4);
            const uint32_t cpu =
                mem.read(rt.psm, cmpBase == ~0u ? rt.base() : cmpBase, cmpFbw ? cmpFbw : rt.fbw, x, y0 + y);
            uint32_t mask = ~0u;
            if (requested) {
                const uint32_t address = pixelAddr(rt.psm, rt.base(), rt.fbw, x, y0 + y);
                mask = requested->mask(address);
                if (rt.psm == PSM_CT16 || rt.psm == PSM_CT16S)
                    mask >>= (address & 3) * 8;
                if (rt.psm == PSM_CT24)
                    mask &= 0xffffff;
            }
            if (((gpu ^ cpu) & mask) != 0) {
                ++bad;
                ++badHere;
                if (bad <= 8)
                    std::fprintf(stderr, "[rb-verify] MISMATCH rt=%x (%u,%u) block=%x gpu=%08x mem=%08x\n", rt.base(),
                                 x, y0 + y, b, gpu, cpu);
            }
        }
    }
    ctx->Unmap(staging.Get(), 0);
    if (++calls % 200 == 0 || badHere)
        std::fprintf(stderr, "[rb-verify] skips checked=%llu pixels compared=%llu mismatches=%llu\n",
                     (unsigned long long)calls, (unsigned long long)compared, (unsigned long long)bad);
}

ID3D11DepthStencilState* D3D11Backend::Impl::getDepthState(D3D11_COMPARISON_FUNC func, bool write) {
    const uint32_t key = (uint32_t)func | (write ? 0x100u : 0u);
    auto it = depthStates.find(key);
    if (it != depthStates.end())
        return it->second.Get();
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = TRUE;
    d.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = func;
    ComPtr<ID3D11DepthStencilState> s;
    dev->CreateDepthStencilState(&d, &s);
    depthStates[key] = s;
    return s.Get();
}

ID3D11BlendState* D3D11Backend::Impl::getBlendState(bool enable, D3D11_BLEND_OP op, D3D11_BLEND src, D3D11_BLEND dst,
                                                    uint8_t mask) {
    const uint32_t key =
        (enable ? 1u : 0u) | ((uint32_t)op << 1) | ((uint32_t)src << 4) | ((uint32_t)dst << 9) | ((uint32_t)mask << 14);
    auto it = blendStates.find(key);
    if (it != blendStates.end())
        return it->second.Get();
    D3D11_BLEND_DESC b{};
    auto& r = b.RenderTarget[0];
    r.BlendEnable = enable;
    r.SrcBlend = src;
    r.DestBlend = dst;
    r.BlendOp = op;
    r.SrcBlendAlpha = D3D11_BLEND_ONE;
    r.DestBlendAlpha = D3D11_BLEND_ZERO;
    r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    r.RenderTargetWriteMask = mask;
    ComPtr<ID3D11BlendState> s;
    dev->CreateBlendState(&b, &s);
    blendStates[key] = s;
    return s.Get();
}

GpuView D3D11Backend::Impl::snapshot(RT& rt, const uint32_t* rect) {
    const D3D11_BOX region{rect ? rect[0] : 0, rect ? rect[1] : 0, 0, rect ? rect[2] : 0, rect ? rect[3] : 0, 1};
    const D3D11_BOX* box = rect ? &region : nullptr;
    CpuProfile::Scope profile(cpuProfile, CpuProfile::Snapshot);
    if (gone() || !rt.tex)
        return rt.copySrv.Get(); // whatever it held: nothing is drawn with it any more
    if (!rt.copy) {
        rt.copy = makeTex(rt.w * scale, rt.h * scale, D3D11_BIND_SHADER_RESOURCE);
        rt.copySrv.Reset();
        if (!rt.copy || !deviceOk(dev->CreateShaderResourceView(rt.copy.Get(), nullptr, &rt.copySrv), "target copy")) {
            rt.copy.Reset();
            return nullptr;
        }
    }
    ctx->CopySubresourceRegion(rt.copy.Get(), 0, box ? box->left : 0, box ? box->top : 0, 0, rt.tex.Get(), 0, box);
    return rt.copySrv.Get();
}

} // namespace gsr
