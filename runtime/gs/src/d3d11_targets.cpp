// Render-target and depth-buffer management, blend/depth state caches, readback to GS memory.
#include "d3d11_backend_impl.h"

#include <array>
#include <map>

#define GSR_IMPL D3D11Backend::Impl
#include "gs_targets.inl"
#undef GSR_IMPL

namespace gsr {

namespace {
// GSR_RT_REFRESH_STATS: at exit, how often each target was refreshed from memory, by rectangle size and path.
void noteRefresh(const RT& rt, uint32_t width, uint32_t height, bool gpu, size_t bytes) {
    static const bool enabled = std::getenv("GSR_RT_REFRESH_STATS") != nullptr;
    if (!enabled)
        return;
    struct Table {
        struct Row {
            uint64_t count = 0, bytes = 0;
        };
        std::map<std::array<uint32_t, 5>, Row> rows;
        ~Table() {
            for (const auto& [key, row] : rows)
                std::fprintf(stderr, "[rt-refresh] fbp=%x fbw=%u rect=%ux%u path=%s count=%llu MiB=%.1f\n", key[0],
                             key[1], key[2], key[3], key[4] ? "gpu" : "cpu", (unsigned long long)row.count,
                             row.bytes / 1048576.0);
        }
    };
    static Table table;
    auto& row = table.rows[{rt.fbp, rt.fbw, width, height, gpu ? 1u : 0u}];
    ++row.count;
    row.bytes += bytes;
}
} // namespace

void D3D11Backend::Impl::allocRT(RT& rt, uint32_t newH) {
    const uint32_t W = rt.w * scale, H = newH * scale;
    ComPtr<ID3D11Texture2D> t = gone() ? nullptr : makeTex(W, H, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11RenderTargetView> rv;
    ComPtr<ID3D11ShaderResourceView> sv;
    // A removed device (d3d11_device_loss.cpp) gives no objects: the old ones stay, the bookkeeping below still grows
    // with the height the shared code expects, and no call reaches the device with a null object.
    if (t && deviceOk(dev->CreateRenderTargetView(t.Get(), nullptr, &rv), "target view") &&
        deviceOk(dev->CreateShaderResourceView(t.Get(), nullptr, &sv), "target view")) {
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
    }
    rt.copy.Reset();
    rt.copySrv.Reset();
    rt.copyValid = false;
    ++rt.writes;
    rt.inventoryCanvasFrame = {};
    const uint32_t oldRows = (uint32_t)rt.rowStamp.size();
    rt.h = newH;
    rt.rowStamp.resize(newH / rt.pageH(), 0);
    rt.rowFromMemory.resize(rt.rowStamp.size(), 1);
    (void)oldRows;
    rt.seen = 0; // force a stamp scan for the new rows
}

void D3D11Backend::Impl::clearInventoryCanvas(RT& rt, bool artwork) {
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

void D3D11Backend::Impl::uploadTargetRect(RT& rt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (gone() || !rt.tex)
        return;
    const size_t count = size_t(width) * height;
    if (uploadPixels.size() < count)
        uploadPixels.resize(count);
    if (rt.psm == PSM_CT32)
        mem.readRectCT32(rt.base(), rt.fbw, x, y, width, height, uploadPixels.data());
    else
        for (uint32_t row = 0; row < height; ++row)
            for (uint32_t col = 0; col < width; ++col)
                uploadPixels[size_t(row) * width + col] =
                    unpackPixel(rt.psm, mem.read(rt.psm, rt.base(), rt.fbw, x + col, y + row));
    const uint32_t* pixels = uploadPixels.data();
    const size_t scaledCount = count * scale * scale;
    // Above scale 1 the GPU replicates the pixels (rt_upload_d3d11.h); tiny rectangles are not worth a compute pass.
    if (rt_upload::gpuReplication(scale, scaledCount)) { // the rule is shared (rt_upload_runs.h)
        bool done = true;
        for (uint32_t band = 0; band < height && done; band += TargetUploadGpu::kBandRows) {
            const uint32_t rows = std::min(TargetUploadGpu::kBandRows, height - band);
            done = targetUploadGpu.upload(dev.Get(), ctx.Get(), rt.tex.Get(), pixels + size_t(band) * width, x,
                                          y + band, width, rows, scale);
        }
        if (done) {
            cpuProfile.uploaded(count * sizeof(uint32_t));
            st.targetRefreshBytes += count * sizeof(uint32_t);
            noteRefresh(rt, width, height, true, count * sizeof(uint32_t));
            return;
        }
        // a failed band wrote nothing: the CPU path below rewrites the whole rectangle
    }
    if (scale > 1) {
        if (scaledUploadPixels.size() < scaledCount)
            scaledUploadPixels.resize(scaledCount);
        expandRenderTargetRows(pixels, scaledUploadPixels.data(), width, height, scale,
                               runtimeFlags().rtUploadSpecialize);
        pixels = scaledUploadPixels.data();
    }
    D3D11_BOX box{x * scale, y * scale, 0, (x + width) * scale, (y + height) * scale, 1};
    ctx->UpdateSubresource(rt.tex.Get(), 0, &box, pixels, width * scale * 4, 0);
    cpuProfile.uploaded(scaledCount * sizeof(uint32_t));
    st.targetRefreshBytes += scaledCount * sizeof(uint32_t);
    noteRefresh(rt, width, height, false, scaledCount * sizeof(uint32_t));
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

void D3D11Backend::Impl::copyAliasedBlocks(RT& rt, GpuTexture& texture, uint32_t tbp, uint32_t tbw, uint32_t w,
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
            ctx->CopySubresourceRegion(texture.tex.Get(), 0, x, y, 0, source, 0, &box);
        }
    st.aliasedTextures++;
}

DepthBuf* D3D11Backend::Impl::getDepth(uint32_t zbp, uint32_t w, uint32_t h) {
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
    if (gone()) { // no objects on a removed device: draws bind no depth, the world passes see no readable depth
        d->lastUse = frame;
        return d;
    }
    if (upMode || neuralEnabled || modernWater || rayShadows || rayReflections || rayGlobalIllumination) {
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

void D3D11Backend::Impl::gridCaptureStart(RT& rt) {
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

void D3D11Backend::Impl::gridCaptureEnd() {
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

} // namespace gsr
