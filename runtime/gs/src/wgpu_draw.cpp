// Draws on WebGPU: the API calls behind the shared per-draw code (gs_draw.inl).
#include "wgpu_backend_impl.h"

namespace gsr {

void WgpuBackend::Impl::clearTarget(RT& rt, const float rgba[4]) {
    gpu->clearColor(*rt.tex, rgba);
}

DrawVertex* WgpuBackend::Impl::beginVertices(const DrawVertex* v, uint32_t count) {
    drawVertexCopy.assign(v, v + count);
    return drawVertexCopy.data();
}

void WgpuBackend::Impl::bindDraw(RT& rt, DepthBuf& db, bool useHw, GpuView texture, GpuView dest, float viewportW,
                                 float viewportH, const int32_t scissor[4]) {
    gpu->setTargets(rt.tex.get(), db.tex.get(), 0);
    gpu->setProgram(useHw ? progHw.get() : progSb.get());
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setTexture(0, texture ? texture : dummy.get());
    if (!useHw)
        gpu->setTexture(1, dest ? dest : dummy.get());
    gpu->setViewport(0, 0, viewportW, viewportH);
    gpu->setScissor({scissor[0], scissor[1], (uint32_t)std::max(0, scissor[2] - scissor[0]),
                     (uint32_t)std::max(0, scissor[3] - scissor[1])});
}

void WgpuBackend::Impl::setPass(const PSConsts& k, const rhi::BlendState& blend, const rhi::DepthState& depth) {
    gpu->setUniforms(0, &k, sizeof(k));
    gpu->setBlend(blend);
    gpu->setDepth(depth);
}

void WgpuBackend::Impl::drawVertices(uint32_t count) {
    gpu->draw(drawVertexCopy.data(), count);
}

bool WgpuBackend::Impl::readTarget(RT& rt, uint32_t w, uint32_t h, Image& out, bool keepAlpha) {
    return readTexture(*rt.tex, w, h, out, keepAlpha);
}

bool WgpuBackend::Impl::readBoundTexture(const TexBind& tb, Image& out) {
    return readTexture(*tb.srv, tb.srv->width(), tb.srv->height(), out);
}

} // namespace gsr

#define GSR_IMPL WgpuBackend::Impl
#include "gs_draw.inl"
#undef GSR_IMPL
