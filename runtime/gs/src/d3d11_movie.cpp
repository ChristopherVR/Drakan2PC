// HD movie frames: the host's HD image of a movie frame drawn over the native frame the game stamped into a frame
// buffer (gs_core.cpp movie tracking, docs/reference/fmv.md section 9).
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
} // namespace

void D3D11Backend::Impl::drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                        const MovieImage& image, uint64_t imageId) {
    if (dbp % 32u || !w || !h || image.rgba.size() < (size_t)image.width * image.height || image.width > 16384 ||
        image.height > 16384)
        return;
    if (!psMovie && !movieShaderFailed) {
        ComPtr<ID3DBlob> shader, errors;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(D3DCompile(kMoviePs, sizeof(kMoviePs) - 1, "movie_frame", nullptr, nullptr, "main", "ps_5_0", 0, 0,
                              &shader, &errors)) ||
            FAILED(dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr, &psMovie)) ||
            FAILED(dev->CreateSamplerState(&sd, &smpMovie))) {
            movieShaderFailed = true;
            std::fprintf(stderr, "[gsr] HD movie shader could not be created\n");
        }
    }
    if (!psMovie)
        return;
    if (movieTexId != imageId || !movieSrv) {
        if (!movieTex || movieTexW != image.width || movieTexH != image.height) {
            // Mip chain so internal resolutions below the HD size (e.g. 2x for a 2048 frame) minify without aliasing.
            D3D11_TEXTURE2D_DESC td{};
            td.Width = image.width;
            td.Height = image.height;
            td.MipLevels = 0;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            movieTex.Reset();
            movieSrv.Reset();
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &movieTex)) ||
                FAILED(dev->CreateShaderResourceView(movieTex.Get(), nullptr, &movieSrv))) {
                movieTex.Reset();
                movieSrv.Reset();
                return;
            }
            movieTexW = image.width;
            movieTexH = image.height;
        }
        ctx->UpdateSubresource(movieTex.Get(), 0, nullptr, image.rgba.data(), image.width * 4u, 0);
        ctx->GenerateMips(movieSrv.Get());
        cpuProfile.uploaded(uint64_t(image.width) * image.height * 4);
        movieTexId = imageId;
    }

    RT* rt = getRT(dbp / 32u, dbw, PSM_CT32, y + h);
    if (!rt || rt->wstate == 1)
        return;
    syncRT(
        *rt); // native rows that memory still leads reach the target first, so they cannot overwrite the HD frame later

    ID3D11ShaderResourceView* srv = movieSrv.Get();
    ID3D11SamplerState* smp = smpMovie.Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &smp);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0);
    ctx->PSSetShader(psMovie.Get(), nullptr, 0);
    ctx->RSSetState(rast.Get());
    D3D11_VIEWPORT vp{(float)(x * scale), (float)(y * scale), (float)(w * scale), (float)(h * scale), 0, 1};
    ctx->RSSetViewports(1, &vp);
    D3D11_RECT clip{(LONG)(x * scale), (LONG)(y * scale), (LONG)((x + w) * scale), (LONG)((y + h) * scale)};
    ctx->RSSetScissorRects(1, &clip);
    ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ID3D11RenderTargetView* rtv = rt->rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    rt->copyValid = false;
    ++rt->writes;
    ++st.movieFrames;
}

} // namespace gsr
