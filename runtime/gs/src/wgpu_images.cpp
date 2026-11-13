// Captures, HD movie frames and wide menu backdrops: the WebGPU half. The policy is shared (gs_images.inl); this file
// holds its hooks.
#include "wgpu_backend_impl.h"

namespace gsr::backend_hook {

// Backend interface of gs_images.inl.
inline bool readImage(WgpuBackend::Impl& s, const rhi::TexturePtr& texture, uint32_t w, uint32_t h, Image& out) {
    return texture && s.readTexture(*texture, w, h, out);
}
inline uint32_t maxImageSize(WgpuBackend::Impl& s) {
    return s.gpu->maxTextureSize();
}
inline bool movieProgramReady(WgpuBackend::Impl&) {
    return true; // drawRect
}
inline bool movieImageLoaded(WgpuBackend::Impl& s) {
    return s.movieTex != nullptr;
}
inline bool createMovieTexture(WgpuBackend::Impl& s, uint32_t width, uint32_t height) {
    s.movieTex = s.createTexture(width, height, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "hd movie");
    return s.movieTex != nullptr;
}
inline void writeMovieImage(WgpuBackend::Impl& s, const MovieImage& image) {
    s.gpu->writeTexture(*s.movieTex, 0, 0, 0, image.width, image.height, image.rgba.data(), image.width * 4u);
}
inline void drawMovieImage(WgpuBackend::Impl& s, RT& rt, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    const uint32_t scale = s.scale;
    const float rect[4] = {(float)(x * scale), (float)(y * scale), (float)((x + w) * scale), (float)((y + h) * scale)};
    const float alpha[4] = {0, 0, 0, 128.0f / 255.0f};
    s.drawRect(*rt.tex, rect, s.movieTex.get(), alpha,
               {(int32_t)(x * scale), (int32_t)(y * scale), w * scale, h * scale});
}
inline bool createBackdropTexture(WgpuBackend::Impl& s, WgpuBackend::Impl::WideBackdrop& bd,
                                  const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    bd.tex = s.createTexture(w, h, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "wide backdrop");
    if (!bd.tex)
        return false;
    s.gpu->writeTexture(*bd.tex, 0, 0, 0, w, h, px.data(), w * 4);
    return true;
}
inline bool backdropProgramReady(WgpuBackend::Impl&) {
    return true; // drawRect
}
inline void drawBackdrop(WgpuBackend::Impl& s, RT& rt, const WgpuBackend::Impl::WideBackdrop& bd, const float* place,
                         float W, float H) {
    const float rect[4] = {place[0] * W, place[1] * H, (place[0] + place[2]) * W, (place[1] + place[3]) * H};
    const float alpha[4] = {0, 0, 0, 128.0f / 255.0f};
    s.drawRect(*rt.tex, rect, bd.tex.get(), alpha, {0, 0, (uint32_t)W, (uint32_t)H});
}

} // namespace gsr::backend_hook

#define GSR_IMPL WgpuBackend::Impl
#include "gs_images.inl"
#undef GSR_IMPL
