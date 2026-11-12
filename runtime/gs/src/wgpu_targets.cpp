// Render-target and depth-buffer management, readback to GS memory (WebGPU backend; policy as d3d11_targets.cpp).
#include "wgpu_backend_impl.h"

#define GSR_IMPL WgpuBackend::Impl
#include "gs_targets.inl"
#undef GSR_IMPL

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
    rt.rowFromMemory.resize(rt.rowStamp.size(), 1);
    rt.seen = 0; // force a stamp scan for the new rows
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

// A rectangle of the target (native pixels) from memory, replicated to the internal scale.
// The dirty rows [y0, y1) of the target on the CPU. Scaled targets: gather the native centre samples on the GPU
// first, so only native pixels cross the bus.
const uint8_t* WgpuBackend::Impl::beginReadback(RT& rt, uint32_t y0, uint32_t y1, uint32_t& readScale, size_t& pitch) {
    const uint32_t rows = y1 - y0;
    traceReadback("readback", frame, rt);
    rhi::Texture* compact = scale > 1 && nativeReadbackEnabled ? nativeGather(rt, rows, y0) : nullptr;
    readScale = compact ? 1 : scale;
    bool read;
    {
        CpuProfile::Scope mapProfile(cpuProfile, CpuProfile::ReadbackMap);
        read = compact ? gpu->readTexture(*compact, 0, 0, 0, rt.w, rows, readbackPixels)
                       : gpu->readTexture(*rt.tex, 0, 0, y0 * scale, rt.w * scale, rows * scale, readbackPixels);
    }
    cpuProfile.copied(uint64_t(rt.w) * rows * readScale * readScale * 4);
    pitch = size_t(rt.w) * readScale * 4;
    return read ? readbackPixels.data() : nullptr;
}

bool WgpuBackend::Impl::preparePartialPatch() {
    if (!progPatch)
        return false;
    if (!patchTex || patchTex->width() != 8 * scale) {
        patchTex = createTexture(8 * scale, 8 * scale, rhi::RenderTarget | rhi::CopySrc, rhi::Format::RGBA8, 1,
                                 "partial patch");
        patchMask = createTexture(8, 8, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "partial patch mask");
        patchValue = createTexture(8, 8, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "partial patch value");
    }
    return patchTex && patchMask && patchValue;
}

void WgpuBackend::Impl::patchTargetBlock(RT& rt, uint32_t bx, uint32_t by, const uint32_t* patch) {
    uint32_t mask[64], value[64];
    for (uint32_t i = 0; i < 64; ++i) {
        mask[i] = patch[i * 2];
        value[i] = patch[i * 2 + 1];
    }
    gpu->writeTexture(*patchMask, 0, 0, 0, 8, 8, mask, 32);
    gpu->writeTexture(*patchValue, 0, 0, 0, 8, 8, value, 32);
    const uint32_t params[4] = {bx, by, scale, 0};
    const uint32_t side = 8 * scale;
    gpu->setTargets(patchTex.get(), nullptr, 0);
    gpu->setProgram(progPatch.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, (float)side, (float)side);
    gpu->setScissor({0, 0, side, side});
    gpu->setUniforms(0, params, sizeof(params));
    gpu->setTexture(0, rt.tex.get());
    gpu->setTexture(1, patchMask.get());
    gpu->setTexture(2, patchValue.get());
    gpu->drawNoInput(3);
    const uint32_t x = bx * scale, y = by * scale;
    if (x < rt.tex->width() && y < rt.tex->height())
        gpu->copyTexture(*patchTex, 0, 0, 0, *rt.tex, 0, x, y, std::min(side, rt.tex->width() - x),
                         std::min(side, rt.tex->height() - y));
}

void WgpuBackend::Impl::copyTargetTop(RT& src, RT& dst, uint32_t w, uint32_t h) {
    gpu->copyTexture(*src.tex, 0, 0, 0, *dst.tex, 0, 0, 0, w * scale, h * scale);
}

void WgpuBackend::Impl::uploadTargetRect(RT& rt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
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
    // Above scale 1 the GPU replicates the pixels (the rule is rt_upload_runs.h, shared with Direct3D 11): the native
    // rectangle goes into a small texture and one quad copies each texel to its scale x scale block.
    if (rt_upload::gpuReplication(scale, scaledCount) && progRect) {
        if (!uploadStage || uploadStage->width() < width || uploadStage->height() < height)
            uploadStage = createTexture(std::max(width, uploadStage ? uploadStage->width() : 0u),
                                        std::max(height, uploadStage ? uploadStage->height() : 0u),
                                        rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "rt upload stage");
        if (uploadStage) {
            gpu->writeTexture(*uploadStage, 0, 0, 0, width, height, pixels, width * 4);
            RectConsts k{};
            k.rect[0] = float(x * scale);
            k.rect[1] = float(y * scale);
            k.rect[2] = float((x + width) * scale);
            k.rect[3] = float((y + height) * scale);
            k.target[0] = float(rt.tex->width());
            k.target[1] = float(rt.tex->height());
            k.color[0] = float(width);
            k.color[1] = float(height);
            k.mode[0] = 2u;
            gpu->setTargets(rt.tex.get(), nullptr, 0);
            gpu->setProgram(progRect.get());
            gpu->setBlend({});
            gpu->setDepth({});
            gpu->setTopology(rhi::Topology::TriangleList);
            gpu->setViewport(0, 0, float(rt.tex->width()), float(rt.tex->height()));
            gpu->setScissor({int32_t(x * scale), int32_t(y * scale), width * scale, height * scale});
            gpu->setUniforms(0, &k, sizeof(k));
            gpu->setTexture(0, uploadStage.get());
            gpu->drawNoInput(6);
            cpuProfile.uploaded(count * sizeof(uint32_t));
            st.targetRefreshBytes += count * sizeof(uint32_t);
            return;
        }
    }
    if (scale > 1) {
        if (scaledUploadPixels.size() < scaledCount)
            scaledUploadPixels.resize(scaledCount);
        expandRenderTargetRows(pixels, scaledUploadPixels.data(), width, height, scale);
        pixels = scaledUploadPixels.data();
    }
    gpu->writeTexture(*rt.tex, 0, x * scale, y * scale, width * scale, height * scale, pixels, width * scale * 4);
    cpuProfile.uploaded(scaledCount * sizeof(uint32_t));
    st.targetRefreshBytes += scaledCount * sizeof(uint32_t);
}

void WgpuBackend::Impl::copyAliasedBlocks(RT& rt, GpuTexture& texture, uint32_t tbp, uint32_t tbw, uint32_t w,
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
            gpu->copyTexture(*source, 0, sx, sy, *texture, 0, x, y, std::min(cw, w - x), std::min(ch, h - y));
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

} // namespace gsr
