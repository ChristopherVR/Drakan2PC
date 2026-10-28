// Wide menu backdrops: optional pack images (<textureDir>/wide/<name>_<a>x<b>.png) drawn behind the centered native
// 4:3 menu canvas, so title/menu art can fill 16:9, 21:9 and wider outputs. See docs/reference/widescreen.md.
#include "d3d11_backend_impl.h"

#include <cstdio>

namespace gsr {

namespace {
constexpr char kWideBackdropPs[] = "Texture2D<float4> Src : register(t0);\n"
                                   "SamplerState Smp : register(s0);\n"
                                   "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
                                   "{ return float4(Src.SampleLevel(Smp, uv, 0).rgb, 128.0 / 255.0); }\n";
} // namespace

const std::vector<Backend::Impl::WideBackdrop>& Backend::Impl::wideBackdropSet(const std::string& name) {
    auto it = wideBackdrops.find(name);
    if (it != wideBackdrops.end())
        return it->second;
    std::vector<WideBackdrop>& set = wideBackdrops[name];
    const std::string dir = texDir + "/wide";
    const std::string prefix = name + "_";
    for (const std::string& file : texup::listDir(dir)) {
        unsigned a = 0, b = 0;
        int used = 0;
        if (file.compare(0, prefix.size(), prefix) != 0 ||
            std::sscanf(file.c_str() + prefix.size(), "%ux%u.png%n", &a, &b, &used) < 2 || !a || !b ||
            prefix.size() + used != file.size())
            continue;
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        if (!texup::readImageFile(dir + "/" + file, px, w, h) || !w || !h || w > 16384 || h > 16384) {
            std::fprintf(stderr, "[gsr] wide backdrop '%s' could not be loaded\n", file.c_str());
            continue;
        }
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
        WideBackdrop bd;
        if (FAILED(dev->CreateTexture2D(&td, &sd, &tex)) ||
            FAILED(dev->CreateShaderResourceView(tex.Get(), nullptr, &bd.srv)))
            continue;
        bd.aspect = (float)a / (float)b;
        set.push_back(std::move(bd));
    }
    if (!set.empty())
        std::fprintf(stderr, "[gsr] wide backdrop '%s': %zu image(s)\n", name.c_str(), set.size());
    return set;
}

// Draws the best-fitting wide image for this GUI screen (falling back to "default") over the whole canvas.
// The caller has already cleared the target; false leaves it black.
// The screen's first frames can draw its art before its own GUI packet (the title's handler tick) reaches the renderer,
// so `guiScreen` may still name the previous screen (the logo); the screen the host saw when it switched the native
// menu canvas on is tried next, so the title gets its own image from its first frame instead of one "default" frame.
bool Backend::Impl::drawWideBackdrop(RT& rt, int guiScreen) {
    if (!(hudScale > 0 && hudScale < 1))
        return false;
    const float target = (4.0f / 3) / hudScale;
    auto screenName = [](int id) { return id >= 0 ? "screen" + std::to_string(id) : std::string(); };
    const std::string names[3] = {
        screenName(guiScreen), nativeMenuScreen != guiScreen ? screenName(nativeMenuScreen) : std::string(), "default"};
    for (const std::string& name : names) {
        if (name.empty())
            continue;
        const auto& set = wideBackdropSet(name);
        std::vector<float> aspects;
        for (const auto& bd : set)
            aspects.push_back(bd.aspect);
        const int pick = hud::pickWideAspect(aspects.data(), (unsigned)aspects.size(), target);
        if (pick < 0)
            continue;
        if (!psWideBackdrop && !wideBackdropShaderFailed) {
            ComPtr<ID3DBlob> shader, errors;
            if (FAILED(D3DCompile(kWideBackdropPs, sizeof(kWideBackdropPs) - 1, "wide_backdrop", nullptr, nullptr,
                                  "main", "ps_5_0", 0, 0, &shader, &errors)) ||
                FAILED(dev->CreatePixelShader(shader->GetBufferPointer(), shader->GetBufferSize(), nullptr,
                                              &psWideBackdrop)))
                wideBackdropShaderFailed = true;
        }
        if (!psWideBackdrop)
            return false;
        const float W = (float)(rt.w * scale), H = (float)(std::min(rt.h, 512u) * scale);
        const auto place = hud::widePlacement(set[pick].aspect, hudScale);
        ID3D11ShaderResourceView* srv = set[pick].srv.Get();
        ID3D11SamplerState* smp = smpLin.Get();
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, &smp);
        ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsFull.Get(), nullptr, 0);
        ctx->PSSetShader(psWideBackdrop.Get(), nullptr, 0);
        ctx->RSSetState(rast.Get());
        D3D11_VIEWPORT vp{place.left * W, 0, place.width * W, H, 0, 1};
        ctx->RSSetViewports(1, &vp);
        D3D11_RECT clip{0, 0, (LONG)W, (LONG)H};
        ctx->RSSetScissorRects(1, &clip);
        ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xffffffffu);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ID3D11RenderTargetView* rtv = rt.rtv.Get();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(0, 1, &none);
        if (name != "default") {
            wideReplaceFrame = frame;
            wideReplaceRT = &rt;
        }
        return true;
    }
    return false;
}

} // namespace gsr
