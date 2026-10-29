#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "settings/OverlayD3D11.h"

#include <windows.h>
#include <d3d11.h>

#include <algorithm>
#include <cstring>

#include "backends/imgui_impl_dx11.h"

namespace dsettings {

namespace {

// Blend states for the screen layer's draw-list callbacks (one overlay per process; created with its device).
ID3D11BlendState* g_blendAdditive = nullptr;
ID3D11BlendState* g_blendOpaque = nullptr;
void setBlend(ID3D11BlendState* state, float k = 0) {
    auto* rs = static_cast<ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (rs && rs->DeviceContext && state) {
        const float factor[4] = {k, k, k, k};
        rs->DeviceContext->OMSetBlendState(state, factor, 0xffffffffu);
    }
}
void blendAdditive(const ImDrawList*, const ImDrawCmd*) {
    setBlend(g_blendAdditive);
}
// The frame copy has no usable alpha: "opaque" crops blend by the constant blend factor, the screen's fade (callback
// data, a float; 1 without), so they fade with everything else instead of turning into dark rectangles.
void blendOpaque(const ImDrawList*, const ImDrawCmd* cmd) {
    float k = 1.0f;
    if (cmd && cmd->UserCallbackData && cmd->UserCallbackDataSize == sizeof(float))
        std::memcpy(&k, cmd->UserCallbackData, sizeof k);
    setBlend(g_blendOpaque, std::clamp(k, 0.0f, 1.0f));
}
void createBlendStates(ID3D11Device* dev) {
    if (g_blendAdditive || !dev)
        return;
    D3D11_BLEND_DESC d{};
    d.RenderTarget[0].BlendEnable = TRUE;
    d.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    d.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    d.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&d, &g_blendAdditive);
    d.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
    d.RenderTarget[0].DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
    dev->CreateBlendState(&d, &g_blendOpaque);
}

ID3D11Texture2D* targetTexture(void* target) {
    auto* rtv = static_cast<ID3D11RenderTargetView*>(target);
    if (!rtv)
        return nullptr;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    if (!res)
        return nullptr;
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    res->Release();
    return tex;
}

class D3D11OverlayRenderer final : public OverlayRenderer {
public:
    D3D11OverlayRenderer(ID3D11Device* device, ID3D11DeviceContext* context) : device_(device), context_(context) {}
    ~D3D11OverlayRenderer() override {
        if (frameSrv_)
            frameSrv_->Release();
        if (frameTex_)
            frameTex_->Release();
    }

    bool init() override {
        if (!device_ || !context_ || !ImGui_ImplDX11_Init(device_, context_))
            return false;
        up_ = true;
        createBlendStates(device_);
        return true;
    }
    void shutdown() override {
        if (up_)
            ImGui_ImplDX11_Shutdown();
        up_ = false;
    }
    void newFrame() override { ImGui_ImplDX11_NewFrame(); }
    void invalidateFonts() override { ImGui_ImplDX11_InvalidateDeviceObjects(); }

    ImTextureID createTexture(const void* rgba, uint32_t w, uint32_t h) override {
        if (!rgba || !w || !h)
            return 0;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{rgba, w * 4, 0};
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device_->CreateTexture2D(&td, &sd, &tex)) || !tex)
            return 0;
        ID3D11ShaderResourceView* srv = nullptr;
        const HRESULT hr = device_->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        if (FAILED(hr) || !srv)
            return 0;
        return reinterpret_cast<ImTextureID>(srv);
    }
    void releaseTexture(ImTextureID texture) override {
        if (auto* srv = reinterpret_cast<ID3D11ShaderResourceView*>(texture))
            srv->Release();
    }

    ImTextureID copyTarget(void* target, uint32_t* w, uint32_t* h) override {
        ID3D11Texture2D* bb = targetTexture(target);
        if (!bb)
            return 0;
        D3D11_TEXTURE2D_DESC d{};
        bb->GetDesc(&d);
        if (!frameTex_ || frameDesc_.Width != d.Width || frameDesc_.Height != d.Height ||
            frameDesc_.Format != d.Format) {
            if (frameSrv_)
                frameSrv_->Release();
            if (frameTex_)
                frameTex_->Release();
            frameSrv_ = nullptr;
            frameTex_ = nullptr;
            D3D11_TEXTURE2D_DESC c = d;
            c.MipLevels = 1;
            c.ArraySize = 1;
            c.SampleDesc.Count = 1;
            c.SampleDesc.Quality = 0;
            c.Usage = D3D11_USAGE_DEFAULT;
            c.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            c.CPUAccessFlags = 0;
            c.MiscFlags = 0;
            if (FAILED(device_->CreateTexture2D(&c, nullptr, &frameTex_)) || !frameTex_ ||
                FAILED(device_->CreateShaderResourceView(frameTex_, nullptr, &frameSrv_))) {
                if (frameTex_)
                    frameTex_->Release();
                frameTex_ = nullptr;
                bb->Release();
                return 0;
            }
            frameDesc_ = d;
        }
        if (d.SampleDesc.Count > 1)
            context_->ResolveSubresource(frameTex_, 0, bb, 0, d.Format);
        else
            context_->CopyResource(frameTex_, bb);
        bb->Release();
        *w = frameDesc_.Width;
        *h = frameDesc_.Height;
        return reinterpret_cast<ImTextureID>(frameSrv_);
    }
    ImDrawCallback additiveCallback() override { return g_blendAdditive ? &blendAdditive : nullptr; }
    ImDrawCallback opaqueCallback() override { return g_blendOpaque ? &blendOpaque : nullptr; }

    void render(void* target, uint32_t w, uint32_t h, ImDrawData* data) override {
        auto* rtv = static_cast<ID3D11RenderTargetView*>(target);
        ID3D11RenderTargetView* oldRtv[1] = {};
        ID3D11DepthStencilView* oldDsv = nullptr;
        context_->OMGetRenderTargets(1, oldRtv, &oldDsv);
        UINT nvp = 1;
        D3D11_VIEWPORT oldVp{};
        context_->RSGetViewports(&nvp, &oldVp);
        context_->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        context_->RSSetViewports(1, &vp);
        ImGui_ImplDX11_RenderDrawData(data);
        context_->OMSetRenderTargets(1, oldRtv, oldDsv);
        if (nvp)
            context_->RSSetViewports(1, &oldVp);
        if (oldRtv[0])
            oldRtv[0]->Release();
        if (oldDsv)
            oldDsv->Release();
    }

    bool readTarget(void* target, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgba) override {
        ID3D11Texture2D* tex = targetTexture(target);
        if (!tex)
            return false;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        D3D11_TEXTURE2D_DESC sd = d;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        ID3D11Texture2D* st = nullptr;
        bool ok = false;
        if (SUCCEEDED(device_->CreateTexture2D(&sd, nullptr, &st)) && st) {
            context_->CopyResource(st, tex);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(context_->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
                const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                w = d.Width;
                h = d.Height;
                rgba.resize(size_t(w) * h * 4);
                for (uint32_t y = 0; y < h; ++y) {
                    const uint8_t* src = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
                    uint8_t* dst = rgba.data() + size_t(y) * w * 4;
                    for (uint32_t x = 0; x < w; ++x) {
                        dst[x * 4 + 0] = bgra ? src[x * 4 + 2] : src[x * 4 + 0];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = bgra ? src[x * 4 + 0] : src[x * 4 + 2];
                        dst[x * 4 + 3] = src[x * 4 + 3];
                    }
                }
                context_->Unmap(st, 0);
                ok = true;
            }
            st->Release();
        }
        tex->Release();
        return ok;
    }

private:
    ID3D11Device* device_;
    ID3D11DeviceContext* context_;
    bool up_ = false;
    ID3D11Texture2D* frameTex_ = nullptr;
    ID3D11ShaderResourceView* frameSrv_ = nullptr;
    D3D11_TEXTURE2D_DESC frameDesc_{};
};

} // namespace

std::unique_ptr<OverlayRenderer> createD3D11OverlayRenderer(ID3D11Device* device, ID3D11DeviceContext* context) {
    return std::make_unique<D3D11OverlayRenderer>(device, context);
}

} // namespace dsettings
