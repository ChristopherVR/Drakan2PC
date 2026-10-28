// Render-target and depth-buffer management, blend/depth state caches, readback to GS memory.
#include "d3d11_backend_impl.h"

namespace gsr {

void Backend::Impl::allocRT(RT& rt, uint32_t newH) {
    const uint32_t W = rt.w * scale, H = newH * scale;
    ComPtr<ID3D11Texture2D> t = makeTex(W, H, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11RenderTargetView> rv;
    ComPtr<ID3D11ShaderResourceView> sv;
    dev->CreateRenderTargetView(t.Get(), nullptr, &rv);
    dev->CreateShaderResourceView(t.Get(), nullptr, &sv);
    const float clr[4] = {0, 0, 0, rt.psm == PSM_CT24 ? 0.5f : 0.0f};
    ctx->ClearRenderTargetView(rv.Get(), clr);
    if (rt.tex) {
        const uint32_t keepH = std::min(rt.h, newH) * scale;
        D3D11_BOX box{0, 0, 0, W, keepH, 1};
        ctx->CopySubresourceRegion(t.Get(), 0, 0, 0, 0, rt.tex.Get(), 0, &box);
    }
    rt.tex = t;
    rt.rtv = rv;
    rt.srv = sv;
    rt.copy.Reset();
    rt.copySrv.Reset();
    rt.copyValid = false;
    ++rt.writes;
    rt.inventoryCanvasFrame = {};
    const uint32_t oldRows = (uint32_t)rt.rowStamp.size();
    rt.h = newH;
    rt.rowStamp.resize(newH / rt.pageH(), 0);
    (void)oldRows;
    rt.seen = 0; // force a stamp scan for the new rows
}

RT* Backend::Impl::getRT(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t needH) {
    fbw = std::max(fbw, 1u);
    const uint32_t cls = (psm == PSM_CT16 || psm == PSM_CT16S) ? 1u : 0u;
    const uint32_t pitchBytes = fbw * 64u * (cls ? 2u : 4u);
    const uint32_t base = fbp * 32u * 256u;
    uint32_t maxRows = base < kVramSize ? (kVramSize - base) / pitchBytes : 0;
    maxRows = std::max(maxRows, 64u);
    maxRows = std::min(maxRows, 2048u);
    RT* rt = nullptr;
    for (auto& r : rts)
        if (r->fbp == fbp && r->fbw == fbw && r->cls == cls) {
            rt = r.get();
            break;
        }
    const uint32_t ph = cls ? 64u : 32u;
    needH = std::min(std::max(needH, 1u), maxRows);
    if (!rt) {
        rts.push_back(std::make_unique<RT>());
        rt = rts.back().get();
        rt->fbp = fbp;
        rt->fbw = fbw;
        rt->cls = cls;
        rt->psm = psm;
        rt->w = fbw * 64u;
        allocRT(*rt, roundUp(std::min(std::max(needH, 512u), maxRows), ph));
    } else {
        rt->psm = psm;
        if (needH > rt->h)
            allocRT(*rt, roundUp(std::min(needH, maxRows), ph));
    }
    rt->lastUse = frame;
    return rt;
}

void Backend::Impl::clearInventoryCanvas(RT& rt, bool artwork) {
    const bool displayTarget =
        rt.displaySeenFrame != ~0ull && frame >= rt.displaySeenFrame && frame - rt.displaySeenFrame <= 4;
    auto margins =
        hud::inventoryCanvasMargins(rt.w, std::min(rt.h, rt.displayHeight), scale, hudScale, true, displayTarget);
    if (artwork && hudScale > 0 && hudScale < 1) {
        const unsigned height = std::min(rt.h, 512u) * scale;
        margins.count = 1;
        margins.rect[0] = {0, 0, rt.w * scale, height};
    }
    if (!margins.count || !rt.inventoryCanvasFrame.begin(frame))
        return;
    D3D11_RECT rect[2]{};
    for (unsigned i = 0; i < margins.count; ++i) {
        const auto& r = margins.rect[i];
        rect[i] = {(LONG)r.left, (LONG)r.top, (LONG)r.right, (LONG)r.bottom};
    }
    ID3D11ShaderResourceView* nulls[2]{};
    ctx->PSSetShaderResources(0, 2, nulls);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    const float black[4] = {0, 0, 0, 128.0f / 255.0f};
    if (inventoryContext1)
        inventoryContext1->ClearView(rt.rtv.Get(), black, rect, margins.count);
    else {
        // Called before vertex-buffer mapping. The ensuing normal draw
        // rebinds every state touched here; no DSV is bound or cleared.
        ID3D11Buffer* noVB = nullptr;
        const UINT zero = 0;
        ctx->IASetVertexBuffers(0, 1, &noVB, &zero, &zero);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsFull.Get(), nullptr, 0);
        ctx->PSSetShader(psInventoryCanvas.Get(), nullptr, 0);
        ctx->RSSetState(rast.Get());
        D3D11_VIEWPORT vp{0, 0, (float)(rt.w * scale), (float)(rt.h * scale), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ID3D11RenderTargetView* target = rt.rtv.Get();
        ctx->OMSetRenderTargets(1, &target, nullptr);
        for (unsigned i = 0; i < margins.count; ++i) {
            ctx->RSSetScissorRects(1, &rect[i]);
            ctx->Draw(3, 0);
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    if (artwork)
        drawWideBackdrop(rt, sceneCamera.guiScreen);
    rt.copyValid = false;
    const uint32_t height = std::min(rt.h, rt.displayHeight);
    ++rt.writes;
    if (!rt.dirty) {
        rt.dirty = true;
        rt.dy0 = 0;
        rt.dy1 = height;
    } else {
        rt.dy0 = 0;
        rt.dy1 = std::max(rt.dy1, height);
    }
    if (runtimeFlags().hudCanvasLog && inventoryCanvasLogLines < 32) {
        ++inventoryCanvasLogLines;
        std::fprintf(stderr, "[hud-canvas] f=%llu fbp=%u method=%s rects=%u leftEnd=%ld rightStart=%ld height=%u\n",
                     (unsigned long long)frame, rt.fbp, inventoryContext1 ? "ClearView" : "solidPS", margins.count,
                     rect[0].right, margins.count > 1 ? rect[1].left : rect[0].left, height * scale);
    }
}

void Backend::Impl::rowSpan(const RT& rt, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) const {
    const uint32_t ph = rt.pageH();
    const uint32_t r0 = y0 / ph, r1 = (y1 + ph - 1) / ph;
    lo = rt.base() + r0 * rt.fbw * 32u;
    hi = rt.base() + r1 * rt.fbw * 32u - 1u;
}

uint32_t Backend::Impl::packPixel(uint32_t psm, const uint8_t* p) {
    switch (psm) {
        case PSM_CT16:
        case PSM_CT16S:
            return (uint32_t)(p[0] >> 3) | ((uint32_t)(p[1] >> 3) << 5) | ((uint32_t)(p[2] >> 3) << 10) |
                   ((p[3] >= 128) ? 0x8000u : 0u);
        case PSM_CT24:
            return p[0] | (p[1] << 8) | (p[2] << 16);
        default:
            return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

uint32_t Backend::Impl::unpackPixel(uint32_t psm, uint32_t v) {
    switch (psm) {
        case PSM_CT16:
        case PSM_CT16S:
            return ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) |
                   ((v & 0x8000) ? 0x80000000u : 0u);
        case PSM_CT24:
            return (v & 0xFFFFFF) | 0x80000000u;
        default:
            return v;
    }
}

void Backend::Impl::ensureStaging(uint32_t w, uint32_t h) {
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
    dev->CreateTexture2D(&d, nullptr, &staging);
}

void Backend::Impl::readbackRT(RT& rt) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ReadbackRT);
    if (rt.wstate == 1)
        finalizeWorld(rt); // temporal upscaler: the target holds the half-drawn render rectangle
    if (!rt.dirty || rt.dy1 <= rt.dy0) {
        rt.dirty = false;
        return;
    }
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    const uint32_t rows = y1 - y0;
    ID3D11Texture2D* compact = scale > 1 && nativeReadbackEnabled
                                   ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, rows, scale, y0)
                                   : nullptr;
    const uint32_t readScale = compact ? 1 : scale;
    ensureStaging(rt.w * readScale, rows * readScale);
    const uint32_t readY = compact ? 0 : y0 * scale;
    D3D11_BOX box{0, readY, 0, rt.w * readScale, readY + rows * readScale, 1};
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, compact ? compact : rt.tex.Get(), 0, &box);
    cpuProfile.copied(uint64_t(rt.w) * rows * readScale * readScale * 4);
    D3D11_MAPPED_SUBRESOURCE ms{};
    HRESULT mapResult;
    {
        CpuProfile::Scope mapProfile(cpuProfile, CpuProfile::ReadbackMap);
        mapResult = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms);
    }
    if (SUCCEEDED(mapResult)) {
        CpuProfile::Scope packProfile(cpuProfile, CpuProfile::ReadbackPack);
        const uint8_t* src = (const uint8_t*)ms.pData;
        const uint32_t half = readScale / 2;
        for (uint32_t y = 0; y < rows; ++y) {
            const uint8_t* row = src + (size_t)(y * readScale + half) * ms.RowPitch;
            for (uint32_t x = 0; x < rt.w; ++x)
                // An upload streaming into this target owns its blocks (see beginTargetUpload).
                if (!uploadActive || !uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, x, y0 + y)))
                    mem.write(rt.psm, rt.base(), rt.fbw, x, y0 + y,
                              packPixel(rt.psm, row + (size_t)(x * readScale + half) * 4));
        }
        ctx->Unmap(staging.Get(), 0);
    }
    mem.touchRect(rt.psm, rt.base(), rt.fbw, 0, y0, rt.w, rows);
    const uint32_t ph = rt.pageH();
    for (uint32_t r = y0 / ph; r < (y1 + ph - 1) / ph && r < rt.rowStamp.size(); ++r)
        rt.rowStamp[r] = mem.clock();
    rt.seen = mem.clock();
    rt.dirty = false;
    st.rtReadbacks++;
}

void Backend::Impl::syncRT(RT& rt) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::SyncRT);
    if (rt.seen == mem.clock())
        return;
    const uint32_t ph = rt.pageH();
    const uint32_t pagesPerRow = rt.fbw;
    std::vector<uint32_t> buf, up;
    for (uint32_t r = 0; r < rt.rowStamp.size(); ++r) {
        const uint32_t lo = rt.base() + r * pagesPerRow * 32u;
        if (lo >= kVramBlocks)
            break;
        if (mem.maxStamp(lo, pagesPerRow * 32u) <= rt.rowStamp[r])
            continue;
        const uint32_t y0 = r * ph, y1 = std::min(y0 + ph, rt.h);
        const uint32_t rows = y1 - y0;
        cpuProfile.uploaded(uint64_t(rt.w) * rows * scale * scale * 4);
        buf.resize((size_t)rt.w * rows);
        for (uint32_t y = 0; y < rows; ++y)
            for (uint32_t x = 0; x < rt.w; ++x)
                buf[(size_t)y * rt.w + x] = unpackPixel(rt.psm, mem.read(rt.psm, rt.base(), rt.fbw, x, y0 + y));
        D3D11_BOX box{0, y0 * scale, 0, rt.w * scale, y1 * scale, 1};
        if (scale == 1)
            ctx->UpdateSubresource(rt.tex.Get(), 0, &box, buf.data(), rt.w * 4, 0);
        else {
            const uint32_t W = rt.w * scale;
            up.resize((size_t)W * rows * scale);
            expandRenderTargetRows(buf.data(), up.data(), rt.w, rows, scale);
            ctx->UpdateSubresource(rt.tex.Get(), 0, &box, up.data(), W * 4, 0);
        }
        rt.rowStamp[r] = mem.clock();
        rt.copyValid = false;
        ++rt.writes;
        st.rtUploads++;
    }
    rt.seen = mem.clock();
}

void Backend::Impl::flushToMemory(uint32_t first, uint32_t last) {
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        if (hi < first || lo > last)
            continue;
        readbackRT(*r);
    }
}

// Image upload into memory a dirty render target occupies. The game stamps 16x16 patches into both frame buffers and
// streams textures over scratch targets every frame; reading the whole target back first is a GPU sync per upload.
// An upload aligned to its format's blocks rewrites whole 256-byte blocks, so instead those blocks are copied into the
// target texture when the upload completes (memory read in the target's format, as the GS would). Blocks the upload
// does not cover stay GPU-owned, so a later readback still yields the right bytes.
bool Backend::Impl::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                      uint32_t h) {
    const PsmGeom g = geomOf(psm);
    if (!w || !h || x % g.blockW || y % g.blockH || w % g.blockW || h % g.blockH)
        return false;
    uint32_t lo, hi;
    spanBlocks(psm, bp, bw, x, y, w, h, lo, hi);
    bool overlaps = false;
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t rlo, rhi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), rlo, rhi);
        if (rhi < lo || rlo > hi)
            continue;
        // Temporal world pass in progress (the texture holds a render rectangle), or a stale target: one readback
        // makes a target the GPU no longer draws into clean, instead of patching it on every later upload.
        if (r->wstate == 1 || frame > r->drawFrame + 1)
            return false;
        overlaps = true;
    }
    if (!overlaps)
        return false; // nothing to read back anyway
    uploadBlocks.assign(kVramBlocks / 64, 0);
    uploadLo = lo;
    uploadHi = hi;
    for (uint32_t by = y; by < y + h; by += g.blockH)
        for (uint32_t bx = x; bx < x + w; bx += g.blockW) {
            const uint32_t b = blockOf(psm, bp, bw, bx, by);
            uploadBlocks[b / 64] |= 1ull << (b % 64);
        }
    // Rows of these targets that memory already leads must reach the GPU before the rows are claimed below.
    for (auto& r : rts)
        if (r->dirty && touchesUpload(*r))
            syncRT(*r);
    uploadActive = true;
    return true;
}

bool Backend::Impl::uploadCovers(uint32_t block) const {
    return (uploadBlocks[block / 64] >> (block % 64)) & 1;
}

// Pixel rows [y0, y1) of the target whose page rows intersect the upload's block span.
bool Backend::Impl::uploadRows(const RT& rt, uint32_t& y0, uint32_t& y1) const {
    const uint32_t ph = rt.pageH(), rowBlocks = rt.fbw * 32u;
    y0 = rt.h;
    y1 = 0;
    for (uint32_t r = 0; r * ph < rt.h; ++r) {
        const uint32_t first = rt.base() + r * rowBlocks, last = first + rowBlocks - 1;
        if (last < uploadLo || first > uploadHi)
            continue;
        y0 = std::min(y0, r * ph);
        y1 = std::max(y1, std::min(rt.h, (r + 1) * ph));
    }
    return y0 < y1;
}

bool Backend::Impl::touchesUpload(const RT& rt) const {
    const PsmGeom g = geomOf(rt.psm);
    uint32_t y0, y1;
    if (!uploadRows(rt, y0, y1))
        return false;
    for (uint32_t by = y0; by < y1; by += g.blockH)
        for (uint32_t bx = 0; bx < rt.w; bx += g.blockW)
            if (uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, bx, by)))
                return true;
    return false;
}

void Backend::Impl::completeTargetUpload(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                         bool apply) {
    if (!uploadActive)
        return;
    for (auto& r : rts) {
        RT& rt = *r;
        if (!rt.dirty)
            continue; // read back meanwhile: memory is authoritative and syncRT uploads the rows as usual
        const PsmGeom g = geomOf(rt.psm);
        const uint32_t ph = rt.pageH();
        uint32_t y0, y1;
        if (!uploadRows(rt, y0, y1))
            continue;
        bool touched = false;
        std::vector<uint32_t> buf, up;
        for (uint32_t by = y0; by < y1; by += g.blockH)
            for (uint32_t bx = 0; bx < rt.w;) {
                if (!uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, bx, by))) {
                    bx += g.blockW;
                    continue;
                }
                uint32_t end = bx + g.blockW; // run of covered blocks in this block row
                while (end < rt.w && uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, end, by)))
                    end += g.blockW;
                const uint32_t runW = std::min(end, rt.w) - bx, runH = std::min(by + g.blockH, rt.h) - by;
                touched = true;
                // The GPU copy is (or will be) current for these rows; never refresh them from memory.
                if (by / ph < rt.rowStamp.size())
                    rt.rowStamp[by / ph] = mem.clock();
                if (apply) {
                    buf.resize((size_t)runW * runH);
                    for (uint32_t row = 0; row < runH; ++row)
                        for (uint32_t col = 0; col < runW; ++col)
                            buf[(size_t)row * runW + col] =
                                unpackPixel(rt.psm, mem.read(rt.psm, rt.base(), rt.fbw, bx + col, by + row));
                    D3D11_BOX box{bx * scale, by * scale, 0, (bx + runW) * scale, (by + runH) * scale, 1};
                    if (scale == 1)
                        ctx->UpdateSubresource(rt.tex.Get(), 0, &box, buf.data(), runW * 4, 0);
                    else {
                        up.resize((size_t)runW * scale * runH * scale);
                        expandRenderTargetRows(buf.data(), up.data(), runW, runH, scale);
                        ctx->UpdateSubresource(rt.tex.Get(), 0, &box, up.data(), runW * scale * 4, 0);
                    }
                    cpuProfile.uploaded(uint64_t(runW) * runH * scale * scale * 4);
                }
                bx = end;
            }
        if (touched && apply) {
            rt.copyValid = false;
            ++rt.writes;
            st.targetUploads++;
        }
    }
    if (apply)
        uploadActive = false;
}

// A 32-bit texture whose memory overlaps a dirty 32-bit render target at another base (the HUD atlas aliases the
// target the orb liquid is rendered into). Reading the target back is a full GPU sync per frame; both store raw GS
// words, so the target's blocks are copied into the texture on the GPU instead.
RT* Backend::Impl::aliasedTarget(uint32_t psm, uint32_t mxl, const std::vector<std::pair<uint32_t, uint32_t>>& spans) {
    if (psm != PSM_CT32 || mxl != 0 || (scale > 1 && !nativeReadbackEnabled))
        return nullptr;
    RT* found = nullptr;
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        for (const auto& sp : spans) {
            if (hi < sp.first || lo > sp.second)
                continue;
            if (found || r->psm != PSM_CT32 || r->wstate == 1) {
                return nullptr;
            }
            found = r.get();
            break;
        }
    }
    return found;
}

void Backend::Impl::copyAliasedBlocks(RT& rt, ID3D11Texture2D* texture, uint32_t tbp, uint32_t tbw, uint32_t w,
                                      uint32_t h) {
    syncRT(rt); // rows memory leads must be in the target before its blocks are copied
    ID3D11Texture2D* source =
        scale > 1 ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, rt.h, scale, 0) : rt.tex.Get();
    if (!source)
        return;
    // Native position of every target block (32-bit blocks are 8x8 pixels).
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> position;
    for (uint32_t y = 0; y < rt.h; y += 8)
        for (uint32_t x = 0; x < rt.w; x += 8)
            position[blockOf(PSM_CT32, rt.base(), rt.fbw, x, y)] = {x, y};
    for (uint32_t y = 0; y < h; y += 8)
        for (uint32_t x = 0; x < w; x += 8) {
            const auto it = position.find(blockOf(PSM_CT32, tbp, std::max(tbw, 1u), x, y));
            if (it == position.end())
                continue;
            const auto [sx, sy] = it->second;
            const D3D11_BOX box{sx, sy, 0, std::min(sx + 8, rt.w), std::min(sy + 8, rt.h), 1};
            ctx->CopySubresourceRegion(texture, 0, x, y, 0, source, 0, &box);
        }
    st.aliasedTextures++;
}

DepthBuf* Backend::Impl::getDepth(uint32_t zbp, uint32_t w, uint32_t h) {
    for (auto& d : depths)
        if (d->zbp == zbp && d->w == w && d->h == h) {
            d->lastUse = frame;
            return d.get();
        }
    depths.push_back(std::make_unique<DepthBuf>());
    DepthBuf* d = depths.back().get();
    d->zbp = zbp;
    d->w = w;
    d->h = h;
    if (upMode || neuralEnabled || modernWater || rayShadows) {
        d->tex = makeTex(w * scale, h * scale, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                         DXGI_FORMAT_R32_TYPELESS);
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev->CreateDepthStencilView(d->tex.Get(), &dd, &d->dsv);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(d->tex.Get(), &sd, &d->srv);
    } else {
        d->tex = makeTex(w * scale, h * scale, D3D11_BIND_DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT);
        dev->CreateDepthStencilView(d->tex.Get(), nullptr, &d->dsv);
    }
    if (!d->dsv) {
        d->srv.Reset();
        d->tex = makeTex(w * scale, h * scale, D3D11_BIND_DEPTH_STENCIL, DXGI_FORMAT_D32_FLOAT);
        if (d->tex)
            dev->CreateDepthStencilView(d->tex.Get(), nullptr, &d->dsv);
    }
    if (d->dsv)
        ctx->ClearDepthStencilView(d->dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    d->lastUse = frame;
    return d;
}

ID3D11DepthStencilState* Backend::Impl::getDepthState(D3D11_COMPARISON_FUNC func, bool write) {
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

ID3D11BlendState* Backend::Impl::getBlendState(bool enable, D3D11_BLEND_OP op, D3D11_BLEND src, D3D11_BLEND dst,
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

// Render targets are allocated with at least 512 rows, so several of them can cover the same local memory (a 128
// wide shadow map next to older 128 wide targets of other effects). The texture must come from the target that
// really owns it: exact base address first, then a target that was drawn to in the rows of the texture, then the
// most recently used one. (The first match in creation order sampled a stale target at an offset.)
RT* Backend::Impl::findRTForTexture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t w, uint32_t h, float& ox,
                                    float& oy) {
    uint32_t cls;
    if (psm == PSM_CT32 || psm == PSM_CT24)
        cls = 0;
    else if (psm == PSM_CT16 || psm == PSM_CT16S)
        cls = 1;
    else
        return nullptr;
    RT* best = nullptr;
    int bestScore = -1;
    float bx = 0, by = 0;
    for (auto& r : rts) {
        if (r->cls != cls || r->fbw != std::max(tbw, 1u))
            continue;
        if (cls == 1 && r->psm != psm)
            continue;
        if (tbp < r->base())
            continue;
        const uint32_t off = tbp - r->base();
        if (off % 32u)
            continue;
        const uint32_t page = off / 32u;
        const uint32_t px = (page % r->fbw) * 64u, py = (page / r->fbw) * r->pageH();
        if (px >= r->w || py >= r->h)
            continue;
        // A right-page origin cannot be a linear SRV subview when the
        // texture crosses the parent row: GS addresses carry to the next
        // page, while SampleLevel clamps at the parent's right edge.
        // Decode through VRAM instead. Keep zero-origin partial-use views
        // (e.g. logical1024 sampling only512) on their existing fast path.
        if (px != 0 && w > r->w - px)
            continue;
        int score = 0;
        if (off == 0)
            score = 1000;
        else if (r->dirty && r->dy0 < py + h && r->dy1 > py)
            score = 500;
        if (score < bestScore || (score == bestScore && r->lastUse <= (best ? best->lastUse : 0)))
            continue;
        best = r.get();
        bestScore = score;
        bx = (float)px;
        by = (float)py;
    }
    if (best) {
        ox = bx;
        oy = by;
    }
    return best;
}

ID3D11ShaderResourceView* Backend::Impl::snapshot(RT& rt, const D3D11_BOX* box) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::Snapshot);
    if (!rt.copy) {
        rt.copy = makeTex(rt.w * scale, rt.h * scale, D3D11_BIND_SHADER_RESOURCE);
        dev->CreateShaderResourceView(rt.copy.Get(), nullptr, &rt.copySrv);
    }
    ctx->CopySubresourceRegion(rt.copy.Get(), 0, box ? box->left : 0, box ? box->top : 0, 0, rt.tex.Get(), 0, box);
    return rt.copySrv.Get();
}

void Backend::Impl::gridCaptureStart(RT& rt) {
    GridCapture& g = gridCapture;
    g.rt = &rt;
    D3D11_TEXTURE2D_DESC d{};
    rt.tex->GetDesc(&d);
    if (!g.save || !g.tex || g.texW != d.Width || g.texH != d.Height || g.fmt != d.Format) {
        g.save = makeTex(d.Width, d.Height, D3D11_BIND_SHADER_RESOURCE, d.Format);
        g.tex = makeTex(d.Width, d.Height, D3D11_BIND_SHADER_RESOURCE, d.Format);
        g.srv.Reset();
        if (g.tex)
            dev->CreateShaderResourceView(g.tex.Get(), nullptr, &g.srv);
        g.texW = d.Width;
        g.texH = d.Height;
        g.fmt = d.Format;
    }
    if (!g.save || !g.tex || !g.srv) {
        g.rt = nullptr;
        return;
    }
    // The grid's box (GUI frame units) through the page map the inventory draws use, plus a margin for the icons that
    // stand out of their cells; whole target when the box is unusable.
    const auto map = hud::inventoryMap(rt.w, hudScale, inventoryHudFrame.centred);
    float x0 = map.x(g.box[0]), x1 = map.x(g.box[2]), y0 = g.box[1], y1 = g.box[3];
    constexpr float kMargin = 24.0f;
    if (!(x1 > x0 && y1 > y0) || !std::isfinite(x0 + x1 + y0 + y1)) {
        x0 = y0 = 0;
        x1 = (float)rt.w;
        y1 = (float)rt.h;
    }
    const auto clampPx = [&](float v, uint32_t hi) { return (UINT)std::clamp(v * (float)scale, 0.0f, (float)hi); };
    g.px = {clampPx(x0 - kMargin, d.Width), clampPx(y0 - kMargin, d.Height), 0,
            clampPx(x1 + kMargin, d.Width), clampPx(y1 + kMargin, d.Height), 1};
    if (g.px.right <= g.px.left || g.px.bottom <= g.px.top) {
        g.rt = nullptr;
        return;
    }
    ctx->CopySubresourceRegion(g.save.Get(), 0, g.px.left, g.px.top, 0, rt.tex.Get(), 0, &g.px);
    g.rtW = rt.w;
    g.rtH = rt.h;
    g.hudScale = hudScale;
    g.centred = inventoryHudFrame.centred;
}

void Backend::Impl::gridCaptureEnd() {
    GridCapture& g = gridCapture;
    if (g.open && g.rt && g.save && g.tex) {
        // Keep what the grid drew, then put back what was there before it.
        ctx->CopySubresourceRegion(g.tex.Get(), 0, g.px.left, g.px.top, 0, g.rt->tex.Get(), 0, &g.px);
        ctx->CopySubresourceRegion(g.rt->tex.Get(), 0, g.px.left, g.px.top, 0, g.save.Get(), 0, &g.px);
        g.rt->copyValid = false;
        ++g.rt->writes;
        ++g.serial;
        g.frame = frame;
        if (g.serial <= 3 || g.serial % 1000 == 0)
            std::fprintf(stderr,
                         "[gs-grid-capture] #%llu target %ux%u fbp %x box %.1f,%.1f..%.1f,%.1f -> px %u,%u..%u,%u "
                         "(hud %.3f centred %d)\n",
                         (unsigned long long)g.serial, g.rt->w, g.rt->h, g.rt->fbp, g.box[0], g.box[1], g.box[2],
                         g.box[3], g.px.left, g.px.top, g.px.right, g.px.bottom, g.hudScale, g.centred ? 1 : 0);
    } else if (g.open) {
        static unsigned misses = 0;
        if (misses++ < 3)
            std::fprintf(stderr, "[gs-grid-capture] scope ended without a display draw\n");
    }
    g.open = false;
    g.rt = nullptr;
}

void Backend::Impl::flushAll() {
    for (auto& r : rts)
        readbackRT(*r);
}

} // namespace gsr
