// Captures, HD movie frames and wide menu backdrops: the Direct3D 11 half. The policy is shared (gs_images.inl);
// this file holds its hooks and the two small pixel programs (prewarmed by d3d11_device.cpp).
#include "d3d11_backend_impl.h"

#include <cmath>
#include <cstdio>

namespace gsr {

namespace {
// Alpha 0x80 like the IPU's RGB32 output, so destination-alpha effects see what the native frame had.
constexpr char kMoviePs[] = "Texture2D<float4> Src : register(t0);\n"
                            "SamplerState Smp : register(s0);\n"
                            "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
                            "{ return float4(Src.Sample(Smp, uv).rgb, 128.0 / 255.0); }\n";
constexpr char kWideBackdropPs[] = "Texture2D<float4> Src : register(t0);\n"
                                   "SamplerState Smp : register(s0);\n"
                                   "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
                                   "{ return float4(Src.SampleLevel(Smp, uv, 0).rgb, 128.0 / 255.0); }\n";
} // namespace

void prewarmMovieShader(ID3D11Device* dev) {
    ComPtr<ID3DBlob> shader;
    ComPtr<ID3D11PixelShader> made;
    if (SUCCEEDED(shader_compile::compile(kMoviePs, sizeof(kMoviePs) - 1, "movie_frame", nullptr, "main", "ps_5_0", 0,
                                          &shader, nullptr)))
        dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &made);
}

void prewarmBackdropShader(ID3D11Device* dev) {
    ComPtr<ID3DBlob> shader;
    ComPtr<ID3D11PixelShader> made;
    if (SUCCEEDED(shader_compile::compile(kWideBackdropPs, sizeof(kWideBackdropPs) - 1, "wide_backdrop", nullptr,
                                          "main", "ps_5_0", 0, &shader, nullptr)))
        dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &made);
}

} // namespace gsr

namespace gsr::backend_hook {

// Backend interface of gs_images.inl.
inline bool readImage(D3D11Backend::Impl& s, const ComPtr<ID3D11Texture2D>& texture, uint32_t w, uint32_t h,
                      Image& out) {
    return s.readTexture(texture.Get(), w, h, out);
}
inline uint32_t maxImageSize(D3D11Backend::Impl&) {
    return 16384;
}
inline bool movieProgramReady(D3D11Backend::Impl& s) {
    if (!s.psMovie && !s.movieShaderFailed) {
        ComPtr<ID3DBlob> shader, errors;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(shader_compile::compile(kMoviePs, sizeof(kMoviePs) - 1, "movie_frame", nullptr, "main", "ps_5_0", 0,
                                           &shader, &errors)) ||
            FAILED(
                s.dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &s.psMovie)) ||
            FAILED(s.dev->CreateSamplerState(&sd, &s.smpMovie))) {
            s.movieShaderFailed = true;
            std::fprintf(stderr, "[gsr] HD movie shader could not be created\n");
        }
    }
    return s.psMovie != nullptr;
}
inline bool movieImageLoaded(D3D11Backend::Impl& s) {
    return s.movieSrv != nullptr;
}
inline bool createMovieTexture(D3D11Backend::Impl& s, uint32_t width, uint32_t height) {
    // Mip chain so internal resolutions below the HD size (e.g. 2x for a 2048 frame) minify without aliasing.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 0;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    s.movieTex.Reset();
    s.movieSrv.Reset();
    if (FAILED(s.dev->CreateTexture2D(&td, nullptr, &s.movieTex)) ||
        FAILED(s.dev->CreateShaderResourceView(s.movieTex.Get(), nullptr, &s.movieSrv))) {
        s.movieTex.Reset();
        s.movieSrv.Reset();
        return false;
    }
    return true;
}
inline void writeMovieImage(D3D11Backend::Impl& s, const MovieImage& image) {
    s.ctx->UpdateSubresource(s.movieTex.Get(), 0, nullptr, image.rgba.data(), image.width * 4u, 0);
    s.ctx->GenerateMips(s.movieSrv.Get());
}
inline void drawMovieImage(D3D11Backend::Impl& s, RT& rt, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    auto& ctx = s.ctx;
    const uint32_t scale = s.scale;
    ID3D11ShaderResourceView* srv = s.movieSrv.Get();
    ID3D11SamplerState* smp = s.smpMovie.Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &smp);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(s.psMovie.Get(), nullptr, 0);
    ctx->RSSetState(s.rast.Get());
    D3D11_VIEWPORT vp{(float)(x * scale), (float)(y * scale), (float)(w * scale), (float)(h * scale), 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT clip{(LONG)(x * scale), (LONG)(y * scale), (LONG)((x + w) * scale), (LONG)((y + h) * scale)};
    ctx->RSSetScissorRects(1, &clip);
    ctx->OMSetBlendState(s.blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ID3D11RenderTargetView* rtv = rt.rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
}
inline bool createBackdropTexture(D3D11Backend::Impl& s, D3D11Backend::Impl::WideBackdrop& bd,
                                  const std::vector<uint32_t>& px, uint32_t w, uint32_t h) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{px.data(), w * 4u, 0};
    ComPtr<ID3D11Texture2D> tex;
    return SUCCEEDED(s.dev->CreateTexture2D(&td, &sd, &tex)) &&
           SUCCEEDED(s.dev->CreateShaderResourceView(tex.Get(), nullptr, &bd.srv));
}
inline bool backdropProgramReady(D3D11Backend::Impl& s) {
    if (!s.psWideBackdrop && !s.wideBackdropShaderFailed) {
        ComPtr<ID3DBlob> shader, errors;
        if (FAILED(shader_compile::compile(kWideBackdropPs, sizeof(kWideBackdropPs) - 1, "wide_backdrop", nullptr,
                                           "main", "ps_5_0", 0, &shader, &errors)) ||
            FAILED(s.dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr,
                                            &s.psWideBackdrop)))
            s.wideBackdropShaderFailed = true;
    }
    return s.psWideBackdrop != nullptr;
}
inline void drawBackdrop(D3D11Backend::Impl& s, RT& rt, const D3D11Backend::Impl::WideBackdrop& bd, const float* rect,
                         float W, float H) {
    auto& ctx = s.ctx;
    ID3D11ShaderResourceView* srv = bd.srv.Get();
    ID3D11SamplerState* smp = s.smpLin.Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &smp);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(s.vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(s.psWideBackdrop.Get(), nullptr, 0);
    ctx->RSSetState(s.rast.Get());
    D3D11_VIEWPORT vp{rect[0] * W, rect[1] * H, rect[2] * W, rect[3] * H, 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT clip{0, 0, (LONG)W, (LONG)H};
    ctx->RSSetScissorRects(1, &clip);
    ctx->OMSetBlendState(s.blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ID3D11RenderTargetView* rtv = rt.rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
}

} // namespace gsr::backend_hook

#define GSR_IMPL D3D11Backend::Impl
#include "gs_images.inl"
#undef GSR_IMPL
