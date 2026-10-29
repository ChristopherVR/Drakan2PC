// Render-target and depth-buffer management, readback to GS memory (WebGPU backend; policy as d3d11_targets.cpp).
#include "wgpu_backend_impl.h"

namespace gsr {

void WgpuBackend::Impl::allocRT(RT& rt, uint32_t newH) {
    const uint32_t W = rt.w * scale, H = newH * scale;
    auto t = createTexture(W, H, rhi::Sampled | rhi::RenderTarget | rhi::CopySrc | rhi::CopyDst, rhi::Format::RGBA8, 1,
                           "gs target");
    const float clr[4] = {0, 0, 0, rt.psm == PSM_CT24 ? 0.5f : 0.0f};
    gpu->clearColor(*t, clr);
    if (rt.tex) {
        const uint32_t keepH = std::min(rt.h, newH) * scale;
        gpu->copyTexture(*rt.tex, 0, 0, 0, *t, 0, 0, 0, W, keepH);
    }
    rt.tex = t;
    rt.copy.reset();
    rt.copyValid = false;
    ++rt.writes;
    rt.inventoryCanvasFrame = {};
    rt.h = newH;
    rt.rowStamp.resize(newH / rt.pageH(), 0);
    rt.seen = 0; // force a stamp scan for the new rows
}

RT* WgpuBackend::Impl::getRT(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t needH) {
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

void WgpuBackend::Impl::clearInventoryCanvas(RT& rt, bool artwork) {
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
    const float black[4] = {0, 0, 0, 128.0f / 255.0f};
    for (unsigned i = 0; i < margins.count; ++i) {
        const auto& r = margins.rect[i];
        const float rect[4] = {(float)r.left, (float)r.top, (float)r.right, (float)r.bottom};
        drawRect(*rt.tex, rect, nullptr, black,
                 {(int32_t)r.left, (int32_t)r.top, (uint32_t)(r.right - r.left), (uint32_t)(r.bottom - r.top)});
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
        std::fprintf(stderr, "[hud-canvas] f=%llu fbp=%u rects=%u leftEnd=%u rightStart=%u height=%u\n",
                     (unsigned long long)frame, rt.fbp, margins.count, margins.rect[0].right,
                     margins.count > 1 ? margins.rect[1].left : margins.rect[0].left, height * scale);
    }
}

void WgpuBackend::Impl::rowSpan(const RT& rt, uint32_t y0, uint32_t y1, uint32_t& lo, uint32_t& hi) const {
    const uint32_t ph = rt.pageH();
    const uint32_t r0 = y0 / ph, r1 = (y1 + ph - 1) / ph;
    lo = rt.base() + r0 * rt.fbw * 32u;
    hi = rt.base() + r1 * rt.fbw * 32u - 1u;
}

uint32_t WgpuBackend::Impl::packPixel(uint32_t psm, const uint8_t* p) {
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

uint32_t WgpuBackend::Impl::unpackPixel(uint32_t psm, uint32_t v) {
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

rhi::Texture* WgpuBackend::Impl::nativeGather(RT& rt, uint32_t rows, uint32_t y0) {
    if (!rows || !rt.w)
        return nullptr;
    if (!gatherTex || gatherTex->width() < rt.w || gatherTex->height() < rows)
        gatherTex = createTexture(std::max(rt.w, gatherTex ? gatherTex->width() : 0u),
                                  std::max(rows, gatherTex ? gatherTex->height() : 0u),
                                  rhi::RenderTarget | rhi::CopySrc, rhi::Format::RGBA8, 1, "native gather");
    if (!gatherTex)
        return nullptr;
    const uint32_t params[4] = {scale, y0, 0, 0};
    gpu->setTargets(gatherTex.get(), nullptr, 0);
    gpu->setProgram(progGather.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, (float)gatherTex->width(), (float)gatherTex->height());
    gpu->setScissor({0, 0, rt.w, rows});
    gpu->setUniforms(0, params, sizeof(params));
    gpu->setTexture(0, rt.tex.get());
    gpu->drawNoInput(3);
    return gatherTex.get();
}

void WgpuBackend::Impl::readbackRT(RT& rt) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ReadbackRT);
    if (rt.wstate == 1)
        finalizeWorld(rt);
    if (!rt.dirty || rt.dy1 <= rt.dy0) {
        rt.dirty = false;
        return;
    }
    const uint32_t y0 = rt.dy0, y1 = std::min(rt.dy1, rt.h);
    const uint32_t rows = y1 - y0;
    // Scaled targets: gather the native centre samples on the GPU first, so only native pixels cross the bus.
    rhi::Texture* compact = scale > 1 && nativeReadbackEnabled ? nativeGather(rt, rows, y0) : nullptr;
    const uint32_t readScale = compact ? 1 : scale;
    std::vector<uint8_t> pixels;
    bool read;
    {
        CpuProfile::Scope mapProfile(cpuProfile, CpuProfile::ReadbackMap);
        read = compact ? gpu->readTexture(*compact, 0, 0, 0, rt.w, rows, pixels)
                       : gpu->readTexture(*rt.tex, 0, 0, y0 * scale, rt.w * scale, rows * scale, pixels);
    }
    cpuProfile.copied(uint64_t(rt.w) * rows * readScale * readScale * 4);
    if (read) {
        CpuProfile::Scope packProfile(cpuProfile, CpuProfile::ReadbackPack);
        const uint32_t half = readScale / 2, pitch = rt.w * readScale * 4;
        for (uint32_t y = 0; y < rows; ++y) {
            const uint8_t* row = pixels.data() + (size_t)(y * readScale + half) * pitch;
            for (uint32_t x = 0; x < rt.w; ++x)
                // An upload streaming into this target owns its blocks (see beginTargetUpload).
                if (!uploadActive || !uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, x, y0 + y)))
                    mem.write(rt.psm, rt.base(), rt.fbw, x, y0 + y,
                              packPixel(rt.psm, row + (size_t)(x * readScale + half) * 4));
        }
    }
    mem.touchRect(rt.psm, rt.base(), rt.fbw, 0, y0, rt.w, rows);
    const uint32_t ph = rt.pageH();
    for (uint32_t r = y0 / ph; r < (y1 + ph - 1) / ph && r < rt.rowStamp.size(); ++r)
        rt.rowStamp[r] = mem.clock();
    rt.seen = mem.clock();
    rt.dirty = false;
    st.rtReadbacks++;
}

void WgpuBackend::Impl::syncRT(RT& rt) {
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
        if (scale == 1)
            gpu->writeTexture(*rt.tex, 0, 0, y0, rt.w, rows, buf.data(), rt.w * 4);
        else {
            const uint32_t W = rt.w * scale;
            up.resize((size_t)W * rows * scale);
            expandRenderTargetRows(buf.data(), up.data(), rt.w, rows, scale);
            gpu->writeTexture(*rt.tex, 0, 0, y0 * scale, W, rows * scale, up.data(), W * 4);
        }
        rt.rowStamp[r] = mem.clock();
        rt.copyValid = false;
        ++rt.writes;
        st.rtUploads++;
    }
    rt.seen = mem.clock();
}

void WgpuBackend::Impl::flushToMemory(uint32_t first, uint32_t last) {
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

// Image upload into memory a dirty render target occupies: see d3d11_targets.cpp. The blocks an aligned upload
// rewrites are copied into the target when the upload completes instead of reading the whole target back first.
bool WgpuBackend::Impl::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w,
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
        if (r->wstate == 1 || frame > r->drawFrame + 1)
            return false;
        overlaps = true;
    }
    if (!overlaps)
        return false;
    uploadBlocks.assign(kVramBlocks / 64, 0);
    uploadLo = lo;
    uploadHi = hi;
    for (uint32_t by = y; by < y + h; by += g.blockH)
        for (uint32_t bx = x; bx < x + w; bx += g.blockW) {
            const uint32_t b = blockOf(psm, bp, bw, bx, by);
            uploadBlocks[b / 64] |= 1ull << (b % 64);
        }
    for (auto& r : rts)
        if (r->dirty && touchesUpload(*r))
            syncRT(*r);
    uploadActive = true;
    return true;
}

bool WgpuBackend::Impl::uploadCovers(uint32_t block) const {
    return (uploadBlocks[block / 64] >> (block % 64)) & 1;
}

bool WgpuBackend::Impl::uploadRows(const RT& rt, uint32_t& y0, uint32_t& y1) const {
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

bool WgpuBackend::Impl::touchesUpload(const RT& rt) const {
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

void WgpuBackend::Impl::completeTargetUpload(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                             bool apply) {
    if (!uploadActive)
        return;
    for (auto& r : rts) {
        RT& rt = *r;
        if (!rt.dirty)
            continue;
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
                uint32_t end = bx + g.blockW;
                while (end < rt.w && uploadCovers(blockOf(rt.psm, rt.base(), rt.fbw, end, by)))
                    end += g.blockW;
                const uint32_t runW = std::min(end, rt.w) - bx, runH = std::min(by + g.blockH, rt.h) - by;
                touched = true;
                if (by / ph < rt.rowStamp.size())
                    rt.rowStamp[by / ph] = mem.clock();
                if (apply) {
                    buf.resize((size_t)runW * runH);
                    for (uint32_t row = 0; row < runH; ++row)
                        for (uint32_t col = 0; col < runW; ++col)
                            buf[(size_t)row * runW + col] =
                                unpackPixel(rt.psm, mem.read(rt.psm, rt.base(), rt.fbw, bx + col, by + row));
                    if (scale == 1)
                        gpu->writeTexture(*rt.tex, 0, bx, by, runW, runH, buf.data(), runW * 4);
                    else {
                        up.resize((size_t)runW * scale * runH * scale);
                        expandRenderTargetRows(buf.data(), up.data(), runW, runH, scale);
                        gpu->writeTexture(*rt.tex, 0, bx * scale, by * scale, runW * scale, runH * scale, up.data(),
                                          runW * scale * 4);
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

// A 32-bit texture whose memory overlaps a dirty 32-bit render target at another base: the target's blocks are
// copied into the texture on the GPU instead of reading the target back (scaled targets through nativeGather).
RT* WgpuBackend::Impl::aliasedTarget(uint32_t psm, uint32_t mxl,
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
            if (found || r->psm != PSM_CT32 || r->wstate == 1)
                return nullptr;
            found = r.get();
            break;
        }
    }
    return found;
}

void WgpuBackend::Impl::copyAliasedBlocks(RT& rt, rhi::Texture& texture, uint32_t tbp, uint32_t tbw, uint32_t w,
                                          uint32_t h) {
    syncRT(rt); // rows memory leads must be in the target before its blocks are copied
    rhi::Texture* source = scale > 1 ? nativeGather(rt, rt.h, 0) : rt.tex.get();
    if (!source)
        return;
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
            const uint32_t cw = std::min(sx + 8, rt.w) - sx, ch = std::min(sy + 8, rt.h) - sy;
            gpu->copyTexture(*source, 0, sx, sy, texture, 0, x, y, std::min(cw, w - x), std::min(ch, h - y));
        }
    st.aliasedTextures++;
}

DepthBuf* WgpuBackend::Impl::getDepth(uint32_t zbp, uint32_t w, uint32_t h) {
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
    d->tex =
        createTexture(w * scale, h * scale, rhi::RenderTarget | rhi::Sampled, rhi::Format::Depth32F, 1, "gs depth");
    if (d->tex)
        gpu->clearDepth(*d->tex, 0.0f);
    d->lastUse = frame;
    return d;
}

// Render targets are allocated with at least 512 rows, so several of them can cover the same local memory: the
// texture comes from the target that really owns it (see d3d11_targets.cpp).
RT* WgpuBackend::Impl::findRTForTexture(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t w, uint32_t h, float& ox,
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

rhi::Texture* WgpuBackend::Impl::snapshot(RT& rt, const uint32_t* box) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::Snapshot);
    if (!rt.copy || rt.copy->width() != rt.tex->width() || rt.copy->height() != rt.tex->height())
        rt.copy = createTexture(rt.tex->width(), rt.tex->height(), rhi::Sampled | rhi::CopyDst | rhi::CopySrc,
                                rhi::Format::RGBA8, 1, "gs target copy");
    uint32_t x0 = 0, y0 = 0, x1 = rt.tex->width(), y1 = rt.tex->height();
    if (box) {
        x0 = std::min(box[0], x1);
        y0 = std::min(box[1], y1);
        x1 = std::min(box[2], x1);
        y1 = std::min(box[3], y1);
    }
    if (x1 > x0 && y1 > y0)
        gpu->copyTexture(*rt.tex, 0, x0, y0, *rt.copy, 0, x0, y0, x1 - x0, y1 - y0);
    return rt.copy.get();
}

void WgpuBackend::Impl::gridCaptureStart(RT& rt) {
    GridCapture& g = gridCapture;
    g.rt = &rt;
    const uint32_t W = rt.tex->width(), H = rt.tex->height();
    if (!g.save || !g.tex || g.texW != W || g.texH != H) {
        g.save = createTexture(W, H, rhi::Sampled | rhi::CopyDst | rhi::CopySrc, rhi::Format::RGBA8, 1, "grid save");
        g.tex = createTexture(W, H, rhi::Sampled | rhi::CopyDst | rhi::CopySrc, rhi::Format::RGBA8, 1, "grid capture");
        g.texW = W;
        g.texH = H;
    }
    if (!g.save || !g.tex) {
        g.rt = nullptr;
        return;
    }
    const auto map = hud::inventoryMap(rt.w, hudScale, inventoryHudFrame.centred);
    float x0 = map.x(g.box[0]), x1 = map.x(g.box[2]), y0 = g.box[1], y1 = g.box[3];
    constexpr float kMargin = 24.0f;
    if (!(x1 > x0 && y1 > y0) || !std::isfinite(x0 + x1 + y0 + y1)) {
        x0 = y0 = 0;
        x1 = (float)rt.w;
        y1 = (float)rt.h;
    }
    const auto clampPx = [&](float v, uint32_t hi) { return (uint32_t)std::clamp(v * (float)scale, 0.0f, (float)hi); };
    g.px[0] = clampPx(x0 - kMargin, W);
    g.px[1] = clampPx(y0 - kMargin, H);
    g.px[2] = clampPx(x1 + kMargin, W);
    g.px[3] = clampPx(y1 + kMargin, H);
    if (g.px[2] <= g.px[0] || g.px[3] <= g.px[1]) {
        g.rt = nullptr;
        return;
    }
    gpu->copyTexture(*rt.tex, 0, g.px[0], g.px[1], *g.save, 0, g.px[0], g.px[1], g.px[2] - g.px[0], g.px[3] - g.px[1]);
    g.rtW = rt.w;
    g.rtH = rt.h;
    g.hudScale = hudScale;
    g.centred = inventoryHudFrame.centred;
}

void WgpuBackend::Impl::gridCaptureEnd() {
    GridCapture& g = gridCapture;
    if (g.open && g.rt && g.save && g.tex) {
        const uint32_t w = g.px[2] - g.px[0], h = g.px[3] - g.px[1];
        // Keep what the grid drew, then put back what was there before it.
        gpu->copyTexture(*g.rt->tex, 0, g.px[0], g.px[1], *g.tex, 0, g.px[0], g.px[1], w, h);
        gpu->copyTexture(*g.save, 0, g.px[0], g.px[1], *g.rt->tex, 0, g.px[0], g.px[1], w, h);
        g.rt->copyValid = false;
        ++g.rt->writes;
        ++g.serial;
        g.frame = frame;
        if (g.serial <= 3 || g.serial % 1000 == 0)
            std::fprintf(stderr,
                         "[gs-grid-capture] #%llu target %ux%u fbp %x box %.1f,%.1f..%.1f,%.1f -> px %u,%u..%u,%u "
                         "(hud %.3f centred %d)\n",
                         (unsigned long long)g.serial, g.rt->w, g.rt->h, g.rt->fbp, g.box[0], g.box[1], g.box[2],
                         g.box[3], g.px[0], g.px[1], g.px[2], g.px[3], g.hudScale, g.centred ? 1 : 0);
    } else if (g.open) {
        static unsigned misses = 0;
        if (misses++ < 3)
            std::fprintf(stderr, "[gs-grid-capture] scope ended without a display draw\n");
    }
    g.open = false;
    g.rt = nullptr;
}

void WgpuBackend::Impl::flushAll() {
    for (auto& r : rts)
        readbackRT(*r);
}

} // namespace gsr
