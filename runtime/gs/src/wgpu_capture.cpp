// Image captures: display, last gameplay image, frame buffers (WebGPU backend).
#include "wgpu_backend_impl.h"

namespace gsr {

bool WgpuBackend::Impl::readTexture(rhi::Texture& tex, uint32_t w, uint32_t h, Image& out, bool keepAlpha) {
    w = std::min(w, tex.width());
    h = std::min(h, tex.height());
    std::vector<uint8_t> pixels;
    if (!gpu->readTexture(tex, 0, 0, 0, w, h, pixels))
        return false;
    out.width = w;
    out.height = h;
    out.rgba = std::move(pixels);
    if (tex.desc().format == rhi::Format::BGRA8)
        for (size_t i = 0; i + 3 < out.rgba.size(); i += 4)
            std::swap(out.rgba[i], out.rgba[i + 2]);
    if (!keepAlpha)
        for (size_t i = 3; i < out.rgba.size(); i += 4)
            out.rgba[i] = 255;
    return true;
}

bool WgpuBackend::Impl::capture(const DisplayState& d, Image& out) {
    if (!ok)
        return false;
    uint32_t w, h;
    renderDisplay(d, w, h);
    return readTexture(*dispTex, dispW, dispH, out);
}

bool WgpuBackend::Impl::captureLastGameplay(Image& out, float& aspect) {
    aspect = latchedGameWorld ? gameplayAspect : savePreviewAspect;
    if (latchedGameWorld)
        return dispTex && readTexture(*dispTex, dispW, dispH, out);
    return savePreviewTex && readTexture(*savePreviewTex, savePreviewW, savePreviewH, out);
}

bool WgpuBackend::Impl::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h,
                                           Image& out) {
    if (!ok)
        return false;
    RT* rt = getRT(fbp, fbw, psm, h);
    if (rt->wstate == 1)
        finalizeWorld(*rt);
    syncRT(*rt);
    return readTexture(*rt->tex, std::min(w, rt->w) * scale, std::min(h, rt->h) * scale, out);
}

} // namespace gsr
