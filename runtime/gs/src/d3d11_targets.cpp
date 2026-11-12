// Render-target and depth-buffer management, blend/depth state caches, readback to GS memory.
#include "d3d11_backend_impl.h"

#include <array>
#include <map>

namespace gsr {

namespace {
bool cleanSyncEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_RB_CLEAN_SYNC");
        return !value || value[0] != '0';
    }();
    return enabled;
}
void traceReadback(const char* kind, uint64_t frame, const RT& rt, uint32_t first = 0, uint32_t last = 0) {
    static const bool enabled = std::getenv("GSR_RB_TRACE") != nullptr;
    static unsigned remaining = 4096;
    if (enabled && remaining) {
        --remaining;
        std::fprintf(stderr,
                     "[rb-trace] %s frame=%llu fbp=%x fbw=%u psm=%x size=%ux%u dirty=%u..%u state=%u blocks=%x..%x "
                     "writes=%llu clean=%llu words=%zu\n",
                     kind, static_cast<unsigned long long>(frame), rt.fbp, rt.fbw, rt.psm, rt.w, rt.h, rt.dy0, rt.dy1,
                     rt.wstate, first, last, static_cast<unsigned long long>(rt.writes),
                     static_cast<unsigned long long>(rt.cleanWrites), rt.cleanWords.size());
    }
}
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
    rt.rowFromMemory.resize(rt.rowStamp.size(), 1);
    (void)oldRows;
    rt.seen = 0; // force a stamp scan for the new rows
}

RT* D3D11Backend::Impl::getRT(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t needH) {
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

void D3D11Backend::Impl::rowSpan(const RT& rt, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) const {
    const uint32_t ph = rt.pageH();
    const uint32_t r0 = y0 / ph, r1 = (y1 + ph - 1) / ph;
    lo = rt.base() + r0 * rt.fbw * 32u;
    hi = rt.base() + r1 * rt.fbw * 32u - 1u;
}

uint32_t D3D11Backend::Impl::packPixel(uint32_t psm, const uint8_t* p) {
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

uint32_t D3D11Backend::Impl::unpackPixel(uint32_t psm, uint32_t v) {
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
    dev->CreateTexture2D(&d, nullptr, &staging);
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
    if (!enabled || !predicted || !rt.dirty || rt.wstate || rt.neuralState || rt.temporalEpoch ||
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
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &copy.texture)))
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

void D3D11Backend::Impl::readbackRT(RT& rt) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ReadbackRT);
    // A guest screenshot can read the closed world before any HUD draw/display latch triggers its deferred pass.
    // Do not finalize an active world or a late native-water submission that still owns its geometry stream.
    if (pendingTemporalEnd && !engineWorldOpen && !nativeWaterPass && rt.neuralState == 1) {
        pendingTemporalEnd = false;
        for (auto& target : rts)
            finalizeNeuralWorld(*target);
    }
    if (rt.wstate == 1)
        finalizeWorld(rt); // temporal upscaler: the target holds the half-drawn render rectangle
    if (!rt.dirty || rt.dy1 <= rt.dy0) {
        rt.dirty = false;
        return;
    }
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    const uint32_t rows = y1 - y0;
    rt.readbackFrame = frame;
    auto& copy = rt.readbackCopy;
    const bool prefetched =
        copy.texture && copy.writes == rt.writes && copy.y0 == y0 && copy.y1 == y1 && copy.renderScale == scale;
    traceReadback(prefetched ? "readback-prefetched" : "readback", frame, rt);
    ID3D11Texture2D* compact = !prefetched && scale > 1 && nativeReadbackEnabled
                                   ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, rows, scale, y0)
                                   : nullptr;
    const uint32_t readScale = prefetched ? copy.readScale : compact ? 1 : scale;
    if (!prefetched) {
        ensureStaging(rt.w * readScale, rows * readScale);
        const uint32_t readY = compact ? 0 : y0 * scale;
        const D3D11_BOX box{0, readY, 0, rt.w * readScale, readY + rows * readScale, 1};
        ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, compact ? compact : rt.tex.Get(), 0, &box);
        cpuProfile.copied(uint64_t(rt.w) * rows * readScale * readScale * 4);
    }
    ID3D11Texture2D* mappedTexture = prefetched ? copy.texture.Get() : staging.Get();
    D3D11_MAPPED_SUBRESOURCE ms{};
    HRESULT mapResult;
    {
        CpuProfile::Scope mapProfile(cpuProfile, CpuProfile::ReadbackMap);
        mapResult = ctx->Map(mappedTexture, 0, D3D11_MAP_READ, 0, &ms);
    }
    if (SUCCEEDED(mapResult)) {
        CpuProfile::Scope packProfile(cpuProfile, CpuProfile::ReadbackPack);
        const uint8_t* src = (const uint8_t*)ms.pData;
        const uint32_t half = readScale / 2;
        for (uint32_t y = 0; y < rows; ++y) {
            const uint8_t* row = src + (size_t)(y * readScale + half) * ms.RowPitch;
            for (uint32_t x = 0; x < rt.w; ++x) {
                // An upload streaming into this target owns its blocks (see beginTargetUpload).
                if (uploadActive && uploadPartial) {
                    const uint32_t address = pixelAddr(rt.psm, rt.base(), rt.fbw, x, y0 + y) & (kVramSize - 1);
                    const uint32_t shift = (address & 3) * 8;
                    const uint32_t pixelMask = rt.psm == PSM_CT32   ? ~0u
                                               : rt.psm == PSM_CT24 ? 0xffffffu
                                                                    : 0xffffu << shift;
                    const uint32_t mask = pixelMask & ~partialUpload.mask(address);
                    const uint32_t gpu = packPixel(rt.psm, row + size_t(x * readScale + half) * 4);
                    uint32_t cpu;
                    std::memcpy(&cpu, mem.data() + (address & ~3u), 4);
                    const uint32_t merged = (cpu & ~mask) | ((gpu << shift) & mask);
                    std::memcpy(mem.data() + (address & ~3u), &merged, 4);
                } else if (!uploadActive || !uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, x, y0 + y)))
                    mem.write(rt.psm, rt.base(), rt.fbw, x, y0 + y,
                              packPixel(rt.psm, row + (size_t)(x * readScale + half) * 4));
            }
        }
        ctx->Unmap(mappedTexture, 0);
    }
    mem.touchRect(rt.psm, rt.base(), rt.fbw, 0, y0, rt.w, rows);
    const uint32_t ph = rt.pageH();
    for (uint32_t r = y0 / ph; r < (y1 + ph - 1) / ph && r < rt.rowStamp.size(); ++r) {
        rt.rowStamp[r] = mem.clock();
        rt.rowFromMemory[r] = 0; // these rows keep the draws that were just read back
    }
    rt.seen = mem.clock();
    rt.dirty = false;
    st.rtReadbacks++;
}

void D3D11Backend::Impl::uploadTargetRect(RT& rt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
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

// True for the game's small scratch targets (shadows, distant characters, HUD liquid): never shown, never part of a
// world pass. Only their uses may refresh a row range (syncRows); every other target is always refreshed whole.
bool D3D11Backend::Impl::lazySyncTarget(const RT& rt) const {
    return rt_sync::lazySyncTarget(rt); // the rule is shared with the WebGPU backend (rt_sync_rows.h)
}

void D3D11Backend::Impl::syncRT(RT& rt) {
    syncRows(rt, {0, (uint32_t)rt.rowStamp.size()});
}

// Refresh the page rows [rows.first, rows.end) the CPU wrote since the target last had them. Rows outside keep their
// stamp (rt.partialSync), so the stamp scan of a later call still finds them stale.
void D3D11Backend::Impl::syncRows(RT& rt, rt_sync::Rows rows) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::SyncRT);
    const bool whole = rows.first == 0 && rows.end >= rt.rowStamp.size();
    if (rt.seen == mem.clock() && !rt.partialSync)
        return;
    const uint32_t ph = rt.pageH();
    const uint32_t pagesPerRow = rt.fbw;
    for (uint32_t r = rows.first; r < rows.end && r < rt.rowStamp.size(); ++r) {
        const uint32_t lo = rt.base() + r * pagesPerRow * 32u;
        if (lo >= kVramBlocks)
            break;
        if (mem.maxStamp(lo, pagesPerRow * 32u) <= rt.rowStamp[r])
            continue;
        const uint32_t y0 = r * ph, y1 = std::min(y0 + ph, rt.h);
        const uint32_t rows = y1 - y0;
        uploadTargetRect(rt, 0, y0, rt.w, rows);
        rt.rowStamp[r] = mem.clock();
        rt.rowFromMemory[r] = 1;
        rt.copyValid = false;
        // A CPU row refresh establishes equality, unlike a draw. Keep prior
        // known-equal blocks in other rows instead of discarding that proof
        // when a disjoint texture upload refreshes another page row.
        if (rt.psm == PSM_CT32 && cleanSyncEnabled()) {
            if (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty()) {
                rt.cleanBlocks.assign(kVramBlocks / 64, 0);
                rt.cleanWords.clear();
            }
            for (uint32_t b = lo; b < std::min(kVramBlocks, lo + pagesPerRow * 32u); ++b)
                rt.cleanBlocks[b / 64] |= 1ull << (b % 64);
            rt.cleanWrites = rt.writes + 1;
        }
        ++rt.writes;
        st.rtUploads++;
    }
    if (whole) {
        rt.seen = mem.clock();
        rt.partialSync = false;
    } else
        rt.partialSync = true;
}

void D3D11Backend::Impl::flushToMemory(uint32_t first, uint32_t last) {
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        if (hi < first || lo > last)
            continue;
        traceReadback("write", frame, *r, first, last);
        readbackRT(*r);
    }
}

// True when every block of [first, last] that lies in the target's dirty rows was made identical on the GPU and in
// memory by an applied upload and nothing has written to the target since.
bool D3D11Backend::Impl::blocksAreClean(const RT& rt, uint32_t first, uint32_t last) const {
    if (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty())
        return false;
    uint32_t lo, hi;
    rowSpan(rt, rt.dy0, std::min(rt.dy1, rt.h), lo, hi);
    for (uint32_t b = std::max(first, lo); b <= std::min(hi, last); ++b)
        if (!((rt.cleanBlocks[b / 64] >> (b % 64)) & 1))
            return false;
    return true;
}

bool D3D11Backend::Impl::readBitsAreClean(const RT& rt, const PartialUpload& requested) const {
    if (rt.cleanWrites != rt.writes || rt.cleanWords.empty())
        return false;
    uint32_t lo, hi;
    rowSpan(rt, rt.dy0, std::min(rt.dy1, rt.h), lo, hi);
    for (const auto& [block, masks] : requested.blocks) {
        if (block < lo || block > hi)
            continue;
        if (!rt.cleanBlocks.empty() && ((rt.cleanBlocks[block / 64] >> (block % 64)) & 1))
            continue;
        for (uint32_t i = 0; i < 64; ++i) {
            if (!masks[i])
                continue;
            const auto found = rt.cleanWords.find(block * 256 + i * 4);
            if (found == rt.cleanWords.end() || (found->second & masks[i]) != masks[i])
                return false;
        }
    }
    return true;
}

// GSR_RB_VERIFY=1 (debug): reads the target back anyway and compares the pixels of blocks [first, last] with memory;
// a difference means a skipped readback would have served stale data.
void D3D11Backend::Impl::verifyRTBlocks(RT& rt, uint32_t first, uint32_t last, uint32_t rowLo, uint32_t rowHi,
                                        uint32_t cmpBase, uint32_t cmpFbw, const PartialUpload* requested) {
    if (rt.wstate == 1 || rt.dy1 <= rt.dy0)
        return;
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h), rows = y1 - y0;
    ID3D11Texture2D* compact = scale > 1 && nativeReadbackEnabled
                                   ? nativeReadback.gather(dev.Get(), ctx.Get(), rt.srv.Get(), rt.w, rows, scale, y0)
                                   : nullptr;
    const uint32_t readScale = compact ? 1 : scale;
    ensureStaging(rt.w * readScale, rows * readScale);
    const uint32_t readY = compact ? 0 : y0 * scale;
    D3D11_BOX box{0, readY, 0, rt.w * readScale, readY + rows * readScale, 1};
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, compact ? compact : rt.tex.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms)))
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

// Local-to-local transfer between two whole PSMCT32 render targets (the game copies its 512x512 frame buffer to
// the other one several times a second). Through memory that is two full readbacks, a per-pixel CPU copy and a later
// re-upload of the destination; here it is one GPU copy, and the destination becomes GPU-owned like after a draw. Only
// taken when the result is provably the same: both ends are exactly the targets, no other target shares either
// memory range (it would need the memory the copy no longer writes), and neither target is mid temporal world pass.
bool D3D11Backend::Impl::copyLocal(uint32_t spsm, uint32_t sbp, uint32_t sbw, uint32_t sx, uint32_t sy, uint32_t dpsm,
                                   uint32_t dbp, uint32_t dbw, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    // The decision and the bookkeeping are shared with the WebGPU backend (rt_local_copy.h).
    const auto planned = rt_copy::plan(
        rts, spsm, sbp, sbw, sx, sy, dpsm, dbp, dbw, dx, dy, w, h,
        [&](const RT& t, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) { rowSpan(t, y0, y1, lo, hi); });
    if (!planned)
        return false;
    RT *src = planned.src, *dst = planned.dst;
    const uint32_t slo = planned.sourceFirst, shi = planned.sourceLast;
    syncRT(*src); // rows of the source the CPU wrote since the target was last current
    if (h < dst->h)
        syncRT(*dst); // rows below the copy stay as they are
    D3D11_BOX box{0, 0, 0, w * scale, h * scale, 1};
    ctx->CopySubresourceRegion(dst->tex.Get(), 0, 0, 0, 0, src->tex.Get(), 0, &box);
    rt_copy::finish(mem, *dst, dbp, dbw, w, h, frame);
    ++st.rtGpuCopies;
    static const bool verify = std::getenv("GSR_RB_VERIFY") != nullptr;
    if (verify) {
        // The memory path would have produced the source's memory contents at the destination.
        flushToMemory(slo, shi);
        verifyRTBlocks(*dst, 0, ~0u, 0, h, src->base(), src->fbw);
    }
    return true;
}

// flushToMemory for a reader of memory. The game streams textures through VRAM every frame, a CLUT right before its
// image, over blocks a scratch target occupies; those uploads are applied to the target as well, so asking for exactly
// those blocks must not read the whole target back (a GPU sync) to learn what the CPU just wrote.
void D3D11Backend::Impl::flushForRead(uint32_t first, uint32_t last, const VramRect* rect) {
    if (!readbackSkipEnabled()) {
        flushToMemory(first, last);
        return;
    }
    PartialUpload requested;
    bool prepared = false, usable = false;
    for (auto& r : rts) {
        if (!r->dirty)
            continue;
        uint32_t lo, hi;
        rowSpan(*r, r->dy0, std::min(r->dy1, r->h), lo, hi);
        if (hi < first || lo > last)
            continue;
        bool clean = blocksAreClean(*r, first, last);
        const PartialUpload* exact = nullptr;
        if (!clean && rect && !r->cleanWords.empty()) {
            if (!prepared) {
                prepared = true;
                usable = requested.prepare(rect->psm, rect->bp, rect->bw, rect->x, rect->y, rect->w, rect->h);
                if (usable)
                    requested.advance(uint64_t(rect->w) * rect->h);
            }
            if (usable && readBitsAreClean(*r, requested)) {
                clean = true;
                exact = &requested;
            }
        }
        if (clean) {
            ++st.rtReadbacksSkipped;
            static const bool verify = std::getenv("GSR_RB_VERIFY") != nullptr;
            if (verify)
                verifyRTBlocks(*r, std::max(first, lo), std::min(last, hi), 0, ~0u, ~0u, 0, exact);
            continue;
        }
        traceReadback("read", frame, *r, first, last);
        static const bool traceReader = std::getenv("GSR_RB_DRAW_TRACE") != nullptr;
        static unsigned readersLeft = 64;
        if (traceReader && readersLeft && uint64_t(r->w) * (r->dy1 - r->dy0) <= 16384) {
            --readersLeft;
            if (rect)
                std::fprintf(stderr, "[rb-reader] target=%x psm=%x bp=%x bw=%u xy=%u,%u wh=%u,%u\n", r->fbp, rect->psm,
                             rect->bp, rect->bw, rect->x, rect->y, rect->w, rect->h);
            else
                std::fprintf(stderr, "[rb-reader] target=%x blocks=%x..%x no-rectangle\n", r->fbp, first, last);
        }
        readbackRT(*r);
    }
}

// Image upload into memory a dirty render target occupies. The game stamps 16x16 patches into both frame buffers and
// streams textures over scratch targets every frame; reading the whole target back first is a GPU sync per upload.
// An upload aligned to its format's blocks rewrites whole 256-byte blocks, so instead those blocks are copied into the
// target texture when the upload completes (memory read in the target's format, as the GS would). Blocks the upload
// does not cover stay GPU-owned, so a later readback still yields the right bytes.
bool D3D11Backend::Impl::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                           uint32_t h) {
    uploadPartial = false;
    const PsmGeom g = geomOf(psm);
    if (!w || !h || x % g.blockW || y % g.blockH || w % g.blockW || h % g.blockH)
        return beginPartialUpload(psm, bp, bw, x, y, w, h);
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

bool D3D11Backend::Impl::beginPartialUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
                                            uint32_t h) {
    static const bool enabled = [] {
        const char* value = std::getenv("GSR_PARTIAL_UPLOAD");
        return !value || value[0] != '0';
    }();
    if (!enabled || !readbackSkipEnabled() || !partialUpload.prepare(psm, bp, bw, x, y, w, h))
        return false;
    uploadBlocks.assign(kVramBlocks / 64, 0);
    uploadLo = kVramBlocks;
    uploadHi = 0;
    for (const auto& [block, masks] : partialUpload.blocks) {
        (void)masks;
        uploadBlocks[block / 64] |= 1ull << (block % 64);
        uploadLo = std::min(uploadLo, block);
        uploadHi = std::max(uploadHi, block);
    }
    bool overlaps = false;
    for (const auto& r : rts) {
        if (!r->dirty || !touchesUpload(*r))
            continue;
        if (r->psm != PSM_CT32 || r->wstate == 1 || frame > r->drawFrame + 1)
            return false;
        overlaps = true;
    }
    if (!overlaps || !partialUploadGpu.prepare(dev.Get(), scale))
        return false;
    for (auto& r : rts)
        if (r->dirty && touchesUpload(*r))
            syncRT(*r);
    uploadPartial = uploadActive = true;
    return true;
}

void D3D11Backend::Impl::completePartialUpload(bool apply, uint64_t written) {
    partialUpload.advance(written);
    // A progress notification never changes GPU state; only transfer completion
    // (including an abandoned transfer's actual prefix) publishes the patch.
    if (!apply && written)
        return;
    for (auto& r : rts) {
        RT& rt = *r;
        if (!rt.dirty || !touchesUpload(rt))
            continue;
        if (apply && (rt.psm != PSM_CT32 || rt.wstate == 1)) {
            // Another GIF path may change the target while an image streams.
            // Resolve that unusual case through memory, preserving only the
            // actual written prefix, rather than patching a different layout.
            readbackRT(rt);
            std::fill(rt.rowStamp.begin(), rt.rowStamp.end(), 0);
            rt.seen = 0;
            syncRT(rt);
            continue;
        }
        uint32_t y0, y1;
        if (!uploadRows(rt, y0, y1))
            continue;
        const uint32_t ph = rt.pageH();
        if (!apply) {
            for (uint32_t y = y0; y < y1; y += ph)
                rt.rowStamp[y / ph] = mem.clock();
            continue;
        }
        bool touched = false;
        if (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty()) {
            rt.cleanBlocks.assign(kVramBlocks / 64, 0);
            rt.cleanWords.clear();
        }
        for (uint32_t by = y0; by < y1; by += 8)
            for (uint32_t bx = 0; bx < rt.w; bx += 8) {
                const uint32_t block = blockOf(PSM_CT32, rt.base(), rt.fbw, bx, by);
                if (!uploadCovers(block))
                    continue;
                std::array<uint32_t, 128> patch{};
                bool any = false, full = true;
                for (uint32_t yy = 0; yy < 8; ++yy)
                    for (uint32_t xx = 0; xx < 8; ++xx) {
                        const uint32_t i = (yy * 8 + xx) * 2;
                        patch[i] = partialUpload.mask(pixelAddr(PSM_CT32, rt.base(), rt.fbw, bx + xx, by + yy));
                        patch[i + 1] = mem.read(PSM_CT32, rt.base(), rt.fbw, bx + xx, by + yy);
                        any |= patch[i] != 0;
                        full &= patch[i] == ~0u;
                        if (patch[i])
                            rt.cleanWords[pixelAddr(PSM_CT32, rt.base(), rt.fbw, bx + xx, by + yy)] |= patch[i];
                    }
                if (!any)
                    continue;
                partialUploadGpu.patch(ctx.Get(), rt.tex.Get(), rt.srv.Get(), bx, by, patch.data());
                if (full)
                    rt.cleanBlocks[block / 64] |= 1ull << (block % 64);
                rt.rowStamp[by / ph] = mem.clock();
                if (!rt_source::rowInRange(by / ph, ph, rt.dy0, std::min(rt.dy1, rt.h)))
                    rt.rowFromMemory[by / ph] = 1; // as in completeTargetUpload
                touched = true;
            }
        if (touched) {
            rt.copyValid = false;
            ++rt.writes;
            rt.cleanWrites = rt.writes;
            ++st.targetUploads;
        }
    }
    if (apply) {
        uploadActive = uploadPartial = false;
        partialUpload.clear();
    }
}

bool D3D11Backend::Impl::uploadCovers(uint32_t block) const {
    return (uploadBlocks[block / 64] >> (block % 64)) & 1;
}

// Pixel rows [y0, y1) of the target whose page rows intersect the upload's block span.
bool D3D11Backend::Impl::uploadRows(const RT& rt, uint32_t& y0, uint32_t& y1) const {
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

bool D3D11Backend::Impl::touchesUpload(const RT& rt) const {
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

void D3D11Backend::Impl::completeTargetUpload(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                              bool apply, uint64_t written) {
    if (!uploadActive)
        return;
    if (uploadPartial) {
        completePartialUpload(apply, written);
        return;
    }
    if (!apply && written)
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
        // Blocks of consecutive applied uploads accumulate while nothing else wrote to the target in between.
        if (apply && (rt.cleanWrites != rt.writes || rt.cleanBlocks.empty())) {
            rt.cleanBlocks.assign(kVramBlocks / 64, 0);
            rt.cleanWords.clear();
        }
        // The runs of covered blocks and their vertical joins are shared with the WebGPU backend (rt_upload_runs.h).
        touched = rt_upload::uploadedRuns(
            rt, y0, y1, apply, [&](uint32_t block) { return uploadCovers(block); },
            [&](uint32_t bx, uint32_t by, uint32_t, uint32_t, uint32_t end) {
                if (apply)
                    for (uint32_t cx = bx; cx < std::min(end, rt.w); cx += g.blockW) {
                        const uint32_t cb = blockOf(rt.psm, rt.base(), rt.fbw, cx, by);
                        rt.cleanBlocks[cb / 64] |= 1ull << (cb % 64);
                    }
                // The GPU copy is (or will be) current for these rows; never refresh them from memory.
                if (by / ph < rt.rowStamp.size()) {
                    rt.rowStamp[by / ph] = mem.clock();
                    // A row outside the dirty range now holds the uploaded image (it may have held read back draws):
                    // without this, a texture uploaded there was still sampled from the target's mirror of it.
                    if (apply && !rt_source::rowInRange(by / ph, ph, rt.dy0, std::min(rt.dy1, rt.h)))
                        rt.rowFromMemory[by / ph] = 1;
                }
            },
            [&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) { uploadTargetRect(rt, x, y, w, h); });
        if (touched && apply) {
            rt.copyValid = false;
            ++rt.writes;
            rt.cleanWrites = rt.writes;
            st.targetUploads++;
            trimDirtyRows(rt);
        }
    }
    if (apply)
        uploadActive = false;
}

// After an applied upload the GPU rows of every covered block equal memory. Rows of the dirty range whose blocks are
// all covered therefore need no readback any more: take them off both ends of the range, and clear the flag when
// nothing is left. (A texture sampled from the very blocks that were just uploaded used to read the whole target back
// to get data the CPU had just written.)
void D3D11Backend::Impl::trimDirtyRows(RT& rt) {
    if (!rt.dirty || !readbackSkipEnabled())
        return;
    const PsmGeom g = geomOf(rt.psm);
    const auto rowCovered = [&](uint32_t by) {
        for (uint32_t bx = 0; bx < rt.w; bx += g.blockW)
            if (!uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, bx, by)))
                return false;
        return true;
    };
    uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    while (y0 < y1) {
        const uint32_t by = y0 / g.blockH * g.blockH;
        if (!rowCovered(by))
            break;
        y0 = by + g.blockH;
    }
    while (y1 > y0) {
        const uint32_t by = (y1 - 1) / g.blockH * g.blockH;
        if (!rowCovered(by))
            break;
        y1 = by;
    }
    if (std::getenv("GSR_RB_VERIFY") && (y0 > rt.dy0 || y1 < std::min(rt.dy1, rt.h) || y0 >= y1)) {
        // rows about to be declared clean must equal memory
        const uint32_t top = std::min(y0, std::min(rt.dy1, rt.h));
        verifyRTBlocks(rt, 0, ~0u, rt.dy0, top);
        if (y1 > top)
            verifyRTBlocks(rt, 0, ~0u, y1, std::min(rt.dy1, rt.h));
    }
    // Rows that leave the dirty range here hold the uploaded image, not draws.
    const uint32_t ph = rt.pageH(), oldEnd = std::min(rt.dy1, rt.h);
    for (uint32_t r = rt.dy0 / ph; r < (oldEnd + ph - 1) / ph && r < rt.rowFromMemory.size(); ++r)
        if (y0 >= y1 || (r + 1) * ph <= y0 || r * ph >= y1)
            rt.rowFromMemory[r] = 1;
    if (y0 >= y1) {
        rt.dirty = false;
        rt.seen = mem.clock();
        ++st.rtDirtyCleared;
    } else {
        rt.dy0 = y0;
        rt.dy1 = y1;
    }
}

// A 32-bit texture whose memory overlaps a dirty 32-bit render target at another base (the HUD atlas aliases the
// target the orb liquid is rendered into). Reading the target back is a full GPU sync per frame; both store raw GS
// words, so the target's blocks are copied into the texture on the GPU instead.
RT* D3D11Backend::Impl::aliasedTarget(uint32_t psm, uint32_t mxl,
                                      const std::vector<std::pair<uint32_t, uint32_t>>& spans) {
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

// Render targets are allocated with at least 512 rows, so several of them can cover the same local memory (a 128
// wide shadow map next to older 128 wide targets of other effects). The texture must come from the target that
// really owns it: exact base address first, then a target that was drawn to in the rows of the texture, then the
// most recently used one. (The first match in creation order sampled a stale target at an offset.)
RT* D3D11Backend::Impl::findRTForTexture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t w, uint32_t h, float& ox,
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
        if (!rt_source::targetOwnsTexture(rowsFromMemory(*r, py, h), off != 0, py, h, r->h))
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

// Page rows of the texture [py, py + h) inside the target (first = total, second = rows whose GPU copy merely mirrors
// memory: outside the dirty range and filled from memory, or about to be because memory is newer).
std::pair<uint32_t, uint32_t> D3D11Backend::Impl::rowsFromMemory(const RT& rt, uint32_t py, uint32_t h) const {
    const uint32_t ph = rt.pageH(), end = std::min(py + h, rt.h);
    const uint32_t dirtyEnd = std::min(rt.dy1, rt.h);
    uint32_t total = 0, fromMemory = 0;
    for (uint32_t r = py / ph; r * ph < end && r < rt.rowStamp.size(); ++r) {
        ++total;
        if (rt.dirty && rt_source::rowInRange(r, ph, rt.dy0, dirtyEnd))
            continue; // drawn since the last readback: the GPU owns it
        const uint32_t lo = rt.base() + r * rt.fbw * 32u;
        if (rt.rowFromMemory[r] || (lo < kVramBlocks && mem.maxStamp(lo, rt.fbw * 32u) > rt.rowStamp[r]))
            ++fromMemory;
    }
    return {total, fromMemory};
}

ID3D11ShaderResourceView* D3D11Backend::Impl::snapshot(RT& rt, const D3D11_BOX* box) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::Snapshot);
    if (!rt.copy) {
        rt.copy = makeTex(rt.w * scale, rt.h * scale, D3D11_BIND_SHADER_RESOURCE);
        dev->CreateShaderResourceView(rt.copy.Get(), nullptr, &rt.copySrv);
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

void D3D11Backend::Impl::flushAll() {
    for (auto& r : rts)
        readbackRT(*r);
}

} // namespace gsr
