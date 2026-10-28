// Synthetic copy upscaler tests composition only: no NVIDIA SDK or DLSS claims.
#include "sr_combined.h"
#include "temporal.h"
#include "upscale_controls.h"
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace gsr {
class CopyUpscaler final : public Upscaler {
    ID3D11DeviceContext* context;

public:
    explicit CopyUpscaler(ID3D11DeviceContext* c) : context(c) {}
    bool evaluate(const Inputs& in) override {
        context->CopyResource(in.out, in.color);
        return true;
    }
    std::string name() const override { return "synthetic copy, no NGX"; }
};
std::unique_ptr<Upscaler> createDlssUpscaler(ID3D11Device*, ID3D11DeviceContext* c, const Upscaler::Desc&,
                                             std::string& status) {
    status = "synthetic copy, no NGX";
    return std::make_unique<CopyUpscaler>(c);
}
} // namespace gsr
static void require(bool b, const char* text) {
    if (!b)
        throw std::runtime_error(text);
}
static uint16_t half(float v) {
    return DirectX::PackedVector::XMConvertFloatToHalf(v);
}
static float number(uint16_t v) {
    return DirectX::PackedVector::XMConvertHalfToFloat(v);
}
int main() {
    try {
        for (unsigned mode = 0; mode <= 4; ++mode)
            for (unsigned preset : {0u, 10u, 11u, 12u, 13u}) {
                require(!gsr::upscaleNeedsReset(mode, preset, mode, preset, false),
                        "unchanged settings preserve history");
                require(gsr::upscaleNeedsReset(mode, preset, mode, preset, true), "fault permits explicit retry");
            }
        require(!gsr::upscaleNeedsReset(0, 0, 0, 12, false), "inactive SR preset leaves NR history alone");
        require(gsr::upscaleNeedsReset(2, 10, 2, 11, false), "active preset resets");
        require(gsr::upscaleNeedsReset(2, 10, 3, 10, false), "quality mode resets");
        constexpr unsigned W = 16, H = 16;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                            &dev, nullptr, &ctx)),
                "WARP device");
        auto texture = [&](DXGI_FORMAT format, UINT bindings, const void* values, UINT pitch) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = W;
            d.Height = H;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = format;
            d.BindFlags = bindings;
            D3D11_SUBRESOURCE_DATA data{values, pitch, 0};
            ComPtr<ID3D11Texture2D> t;
            require(SUCCEEDED(dev->CreateTexture2D(&d, values ? &data : nullptr, &t)), "test texture");
            return t;
        };
        auto view = [&](ID3D11Texture2D* t) {
            ComPtr<ID3D11ShaderResourceView> v;
            require(SUCCEEDED(dev->CreateShaderResourceView(t, nullptr, &v)), "SRV");
            return v;
        };
        std::vector<uint16_t> rgba(W * H * 4), initial(W * H * 4), motion(W * H * 2);
        std::vector<float> depth(W * H, .5f);
        for (unsigned i = 0; i < W * H; ++i) {
            const float stripe[] = {.1f, .4f, .2f, .1f};
            rgba[i * 4] = half(stripe[i % 4]);
            rgba[i * 4 + 1] = half(.3f);
            rgba[i * 4 + 2] = half(1.5f);
            rgba[i * 4 + 3] = half(1 - float(i) / 255);
            initial[i * 4 + 3] = half(float(i) / 255);
        }
        auto color = texture(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, rgba.data(), W * 8);
        auto z = texture(DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, depth.data(), W * 4);
        auto mv = texture(DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, motion.data(), W * 4);
        auto dest = texture(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET, initial.data(), W * 8);
        auto cs = view(color.Get()), zs = view(z.Get()), ms = view(mv.Get());
        ComPtr<ID3D11RenderTargetView> rtv;
        require(SUCCEEDED(dev->CreateRenderTargetView(dest.Get(), nullptr, &rtv)), "RTV");
        auto read = [&]() {
            D3D11_TEXTURE2D_DESC d{};
            dest->GetDesc(&d);
            d.BindFlags = 0;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> t;
            require(SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t)), "staging");
            ctx->CopyResource(t.Get(), dest.Get());
            D3D11_MAPPED_SUBRESOURCE map{};
            require(SUCCEEDED(ctx->Map(t.Get(), 0, D3D11_MAP_READ, 0, &map)), "read pixels");
            std::vector<uint16_t> out(W * H * 4);
            for (unsigned y = 0; y < H; ++y)
                std::memcpy(out.data() + y * W * 4, static_cast<const char*>(map.pData) + y * map.RowPitch, W * 8);
            ctx->Unmap(t.Get(), 0);
            return out;
        };
        gsr::sr_combined::Pipeline p(dev.Get(), ctx.Get());
        require(p.ready(), p.status().c_str());
        require(p.configure({{W, H}, {W, H}, 1, 0}), p.status().c_str());
        gsr::sr_combined::Inputs in;
        in.color = cs.Get();
        in.depth = zs.Get();
        in.motion = ms.Get();
        require(p.resolve(in), p.status().c_str());
        unsigned cases = 0;
        for (bool neural : {false, true})
            for (float strength : {0.f, .5f, 1.f, -1.f, 2.f, std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity()}) {
                if (neural)
                    ctx->CopyResource(p.outputs().nrOutput.texture, color.Get());
                ID3D11RenderTargetView* sentinel = rtv.Get();
                ctx->OMSetRenderTargets(1, &sentinel, nullptr);
                require(p.composeRgb(rtv.Get(), neural, strength), p.status().c_str());
                ComPtr<ID3D11RenderTargetView> restored;
                ctx->OMGetRenderTargets(1, &restored, nullptr);
                require(restored.Get() == rtv.Get(), "caller RTV restored");
                ctx->ClearState();
                auto out = read();
                const float s = std::isfinite(strength) ? std::clamp(strength, 0.f, 1.f) : 0;
                for (unsigned y = 0; y < H; ++y)
                    for (unsigned x = 0; x < W; ++x) {
                        const unsigned i = y * W + x;
                        require(out[i * 4 + 3] == initial[i * 4 + 3], "all 256 destination alpha values preserved");
                        for (unsigned c = 0; c < 3; ++c) {
                            auto at = [&](int xx, int yy) {
                                return number(
                                    rgba[(std::clamp(yy, 0, int(H) - 1) * W + std::clamp(xx, 0, int(W) - 1)) * 4 + c]);
                            };
                            const float center = at(x, y), n = at(x, y - 1), south = at(x, y + 1), w = at(x - 1, y),
                                        e = at(x + 1, y);
                            const float lo = std::min({center, n, south, w, e}),
                                        hi = std::max({center, n, south, w, e});
                            const float expected = s ? std::clamp(center + (center - (n + south + w + e) * .25f) * s,
                                                                  std::max(0.f, lo - .1f), hi + .1f)
                                                     : center;
                            require(std::isfinite(number(out[i * 4 + c])) &&
                                        std::abs(number(out[i * 4 + c]) - expected) < .002f,
                                    "RGB sharpening, edge clamp, HDR and finite setting policy");
                        }
                    }
                ++cases;
            }
        in.reset = false;
        in.frameIndex = 1;
        require(p.resolve(in), p.status().c_str());
        require(p.composeRgb(rtv.Get(), false, 0), p.status().c_str());
        ctx->ClearState();
        auto out = read();
        for (unsigned i = 0; i < W * H; ++i)
            require(out[i * 4] == rgba[i * 4], "resolve after composition restores compute constants");
        // Render-resolution NR: SR consumes the NR image but keeps the GS alpha from a separate surface.
        std::vector<uint16_t> enhanced(W * H * 4);
        for (unsigned i = 0; i < W * H; ++i) {
            enhanced[i * 4] = half(.7f);
            enhanced[i * 4 + 1] = half(.6f);
            enhanced[i * 4 + 2] = half(.5f);
            enhanced[i * 4 + 3] = half(.25f); // vendor alpha, must not survive
        }
        auto nrColor = texture(DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, enhanced.data(), W * 8);
        auto nrs = view(nrColor.Get());
        in.alphaRender = cs.Get();
        in.color = cs.Get();
        require(!p.resolve(in), "alpha source must differ from the color input");
        in.color = nrs.Get();
        in.frameIndex = 2;
        require(p.resolve(in), p.status().c_str());
        {
            D3D11_TEXTURE2D_DESC d{};
            p.outputs().color.texture->GetDesc(&d);
            d.BindFlags = 0;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> t;
            require(SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t)), "staging SR color");
            ctx->CopyResource(t.Get(), p.outputs().color.texture);
            D3D11_MAPPED_SUBRESOURCE map{};
            require(SUCCEEDED(ctx->Map(t.Get(), 0, D3D11_MAP_READ, 0, &map)), "read SR color");
            for (unsigned y = 0; y < H; ++y)
                for (unsigned x = 0; x < W; ++x) {
                    const auto* px =
                        reinterpret_cast<const uint16_t*>(static_cast<const char*>(map.pData) + y * map.RowPitch) +
                        x * 4;
                    const unsigned i = y * W + x;
                    require(px[0] == enhanced[i * 4] && px[1] == enhanced[i * 4 + 1] && px[2] == enhanced[i * 4 + 2],
                            "SR resolves the NR image");
                    require(px[3] == rgba[i * 4 + 3], "SR keeps the GS alpha, not the NR output alpha");
                }
            ctx->Unmap(t.Get(), 0);
        }
        ++cases;
        std::printf("PASS %u SR/NR synthetic composition cases: sharpness, HDR, boundaries, all256 alpha values, "
                    "context restore and repeated resolve; no NGX evaluation\n",
                    cases);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
