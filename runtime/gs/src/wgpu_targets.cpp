// Render-target and depth-buffer management, readback to GS memory: the WebGPU half. The policy is shared
// (gs_targets.inl); this file holds its hooks.
#include "wgpu_backend_impl.h"

#include <array>

namespace gsr::backend_hook {

// Backend interface of gs_targets.inl.
inline void replaceTargetTexture(WgpuBackend::Impl& s, RT& rt, uint32_t newH) {
    const uint32_t W = rt.w * s.scale, H = newH * s.scale;
    auto t = s.createTexture(W, H, rhi::Sampled | rhi::RenderTarget | rhi::CopySrc | rhi::CopyDst, rhi::Format::RGBA8,
                             1, "gs target");
    const float clr[4] = {0, 0, 0, rt.psm == PSM_CT24 ? 0.5f : 0.0f};
    s.gpu->clearColor(*t, clr);
    if (rt.tex) {
        const uint32_t keepH = std::min(rt.h, newH) * s.scale;
        s.gpu->copyTexture(*rt.tex, 0, 0, 0, *t, 0, 0, 0, W, keepH);
    }
    rt.tex = t;
    rt.copy.reset();
}
inline const char* clearCanvasMargins(WgpuBackend::Impl& s, RT& rt, const hud::CanvasMargins& margins) {
    const float black[4] = {0, 0, 0, 128.0f / 255.0f};
    for (unsigned i = 0; i < margins.count; ++i) {
        const auto& r = margins.rect[i];
        const float rect[4] = {(float)r.left, (float)r.top, (float)r.right, (float)r.bottom};
        s.drawRect(*rt.tex, rect, nullptr, black,
                   {(int32_t)r.left, (int32_t)r.top, (uint32_t)(r.right - r.left), (uint32_t)(r.bottom - r.top)});
    }
    return "drawRect";
}
inline bool targetUnusable(const WgpuBackend::Impl&, const RT&) {
    return false;
}
// The native rectangle goes into a small texture and one quad copies each texel to its scale x scale block.
inline bool replicateTargetRect(WgpuBackend::Impl& s, RT& rt, const uint32_t* pixels, uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height) {
    if (!s.progRect)
        return false;
    auto& stage = s.uploadStage;
    if (!stage || stage->width() < width || stage->height() < height)
        stage = s.createTexture(std::max(width, stage ? stage->width() : 0u),
                                std::max(height, stage ? stage->height() : 0u), rhi::Sampled | rhi::CopyDst,
                                rhi::Format::RGBA8, 1, "rt upload stage");
    if (!stage)
        return false;
    const uint32_t scale = s.scale;
    auto& gpu = s.gpu;
    gpu->writeTexture(*stage, 0, 0, 0, width, height, pixels, width * 4);
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
    gpu->setProgram(s.progRect.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, float(rt.tex->width()), float(rt.tex->height()));
    gpu->setScissor({int32_t(x * scale), int32_t(y * scale), width * scale, height * scale});
    gpu->setUniforms(0, &k, sizeof(k));
    gpu->setTexture(0, stage.get());
    gpu->drawNoInput(6);
    return true;
}
inline void writeTargetRect(WgpuBackend::Impl& s, RT& rt, const uint32_t* pixels, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height) {
    const uint32_t scale = s.scale;
    s.gpu->writeTexture(*rt.tex, 0, x * scale, y * scale, width * scale, height * scale, pixels, width * scale * 4);
}
inline rhi::Texture* nativeTargetSource(WgpuBackend::Impl& s, RT& rt) {
    return s.scale > 1 ? s.nativeGather(rt, rt.h, 0) : rt.tex.get();
}
inline void copyTargetBlock(WgpuBackend::Impl& s, RT& rt, rhi::Texture* source, GpuTexture& texture, uint32_t sx,
                            uint32_t sy, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const uint32_t cw = std::min(sx + 8, rt.w) - sx, ch = std::min(sy + 8, rt.h) - sy;
    s.gpu->copyTexture(*source, 0, sx, sy, *texture, 0, x, y, std::min(cw, w - x), std::min(ch, h - y));
}
inline void createDepthTexture(WgpuBackend::Impl& s, DepthBuf& d) {
    d.tex = s.createTexture(d.w * s.scale, d.h * s.scale, rhi::RenderTarget | rhi::Sampled, rhi::Format::Depth32F, 1,
                            "gs depth");
    if (d.tex)
        s.gpu->clearDepth(*d.tex, 0.0f);
}
inline bool prepareGridCapture(WgpuBackend::Impl& s, RT& rt, uint32_t& W, uint32_t& H) {
    auto& g = s.gridCapture;
    W = rt.tex->width();
    H = rt.tex->height();
    if (!g.save || !g.tex || g.texW != W || g.texH != H) {
        g.save = s.createTexture(W, H, rhi::Sampled | rhi::CopyDst | rhi::CopySrc, rhi::Format::RGBA8, 1, "grid save");
        g.tex =
            s.createTexture(W, H, rhi::Sampled | rhi::CopyDst | rhi::CopySrc, rhi::Format::RGBA8, 1, "grid capture");
        g.texW = W;
        g.texH = H;
    }
    return g.save && g.tex;
}
inline void saveGridRect(WgpuBackend::Impl& s, RT& rt, const uint32_t* px) {
    auto& g = s.gridCapture;
    for (int i = 0; i < 4; ++i)
        g.px[i] = px[i];
    s.gpu->copyTexture(*rt.tex, 0, g.px[0], g.px[1], *g.save, 0, g.px[0], g.px[1], g.px[2] - g.px[0],
                       g.px[3] - g.px[1]);
}
inline void restoreGridRect(WgpuBackend::Impl& s) {
    auto& g = s.gridCapture;
    const uint32_t w = g.px[2] - g.px[0], h = g.px[3] - g.px[1];
    s.gpu->copyTexture(*g.rt->tex, 0, g.px[0], g.px[1], *g.tex, 0, g.px[0], g.px[1], w, h);
    s.gpu->copyTexture(*g.save, 0, g.px[0], g.px[1], *g.rt->tex, 0, g.px[0], g.px[1], w, h);
}
inline std::array<uint32_t, 4> gridRect(const WgpuBackend::Impl& s) {
    const auto& px = s.gridCapture.px;
    return {px[0], px[1], px[2], px[3]};
}

} // namespace gsr::backend_hook

#define GSR_IMPL WgpuBackend::Impl
#include "gs_targets.inl"
#undef GSR_IMPL

namespace gsr {

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

} // namespace gsr
