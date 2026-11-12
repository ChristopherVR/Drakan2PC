// Texture cache on WebGPU: the API calls behind the shared cache (gs_texture_cache.inl).
#include "wgpu_backend_impl.h"

namespace gsr {

GpuTexture WgpuBackend::Impl::createSampledTexture(const std::vector<uint32_t>* levels, const uint32_t* lw,
                                                   const uint32_t* lh, uint32_t count, bool, const char* label) {
    // Every texture can receive target blocks by copy (copyAliasedBlocks), so `updatable` changes nothing here.
    GpuTexture texture = createTexture(lw[0], lh[0], rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, count, label);
    if (texture)
        for (uint32_t l = 0; l < count; ++l)
            gpu->writeTexture(*texture, l, 0, 0, lw[l], lh[l], levels[l].data(), lw[l] * 4);
    return texture;
}

} // namespace gsr

#define GSR_IMPL WgpuBackend::Impl
#include "gs_texture_cache.inl"
#undef GSR_IMPL
