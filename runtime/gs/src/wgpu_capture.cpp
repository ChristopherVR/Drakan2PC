// Reading textures back to the CPU (WebGPU backend). The captures that use it are shared (gs_images.inl).
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

} // namespace gsr
