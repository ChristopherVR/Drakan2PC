// Texture cache on Direct3D 11: the API calls behind the shared cache (gs_texture_cache.inl).
#include "d3d11_backend_impl.h"

namespace gsr {

GpuTexture D3D11Backend::Impl::createSampledTexture(const std::vector<uint32_t>* levels, const uint32_t* lw,
                                                    const uint32_t* lh, uint32_t count, bool updatable, const char*) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = lw[0];
    d.Height = lh[0];
    d.MipLevels = count;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = updatable ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(count);
    for (uint32_t l = 0; l < count; ++l) {
        init[l].pSysMem = levels[l].data();
        init[l].SysMemPitch = lw[l] * 4;
    }
    GpuTexture texture;
    if (FAILED(dev->CreateTexture2D(&d, init.data(), &texture.tex)) ||
        FAILED(dev->CreateShaderResourceView(texture.tex.Get(), nullptr, &texture.srv)))
        texture.reset();
    return texture;
}

} // namespace gsr

#define GSR_IMPL D3D11Backend::Impl
#include "gs_texture_cache.inl"
#undef GSR_IMPL
