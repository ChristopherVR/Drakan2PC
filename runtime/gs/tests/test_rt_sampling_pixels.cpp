// Opt-in device test. Authoring/building this file does not validate pixels.
// Execute only during an assigned GPU/WARP slot; uses WARP exclusively.
#define NOMINMAX
#include "gs_shaders.h"
#include "sprite_footprint.h"
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT outputSize = 4, sourceSize = 64;
constexpr uint32_t textureFlags = (1u << 0) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 17) | (1u << 22);
// FST, TCC, DECAL, COLCLAMP, RT texture; no blend, alpha test, footprint flag24.
struct CB {
    float rt[4];
    uint32_t a[4], b[4], c[4];
    float fog[4];
    uint32_t d[4];
    float e[4], src[4], f[4], g[4], h[4], spriteBounds[4];
};
static_assert(sizeof(CB) == 192 && offsetof(CB, spriteBounds) == 176);
struct Vertex {
    float x, y, z, fog, r, g, b, a, s, t, q;
};
static_assert(sizeof(Vertex) == 44);
using Pixel = std::array<float, 4>;
void must(HRESULT hr, const char* operation) {
    if (FAILED(hr))
        throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(hr));
}
ComPtr<ID3DBlob> compile(const char* entry, const char* target, const char* hw) {
    D3D_SHADER_MACRO macros[] = {{"HW_BLEND", hw}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, error;
    const HRESULT hr = D3DCompile(gsr::kHlslMain, std::strlen(gsr::kHlslMain), "integrated_rt_pixels", macros, nullptr,
                                  entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(hr) && error)
        std::fwrite(error->GetBufferPointer(), 1, error->GetBufferSize(), stderr);
    must(hr, entry);
    return code;
}
void checkLayout(ID3DBlob* code) {
    ComPtr<ID3D11ShaderReflection> reflection;
    must(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ID3D11ShaderReflection,
                    reinterpret_cast<void**>(reflection.GetAddressOf())),
         "reflect integrated shader");
    auto* buffer = reflection->GetConstantBufferByName("CB");
    D3D11_SHADER_BUFFER_DESC desc{};
    must(buffer->GetDesc(&desc), "reflect CB");
    D3D11_SHADER_VARIABLE_DESC bounds{};
    must(buffer->GetVariableByName("cb_spriteBounds")->GetDesc(&bounds), "reflect spriteBounds");
    if (desc.Size != sizeof(CB) || bounds.StartOffset != offsetof(CB, spriteBounds) || bounds.Size != 16)
        throw std::runtime_error("CB layout changed: update pixel test before running");
}

class Harness {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps[2];
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb, cb;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> destination;
    ComPtr<ID3D11SamplerState> point, linear;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11Buffer> buffer(UINT bytes, UINT bind) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = bytes;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind;
        ComPtr<ID3D11Buffer> result;
        must(dev->CreateBuffer(&d, nullptr, &result), "buffer");
        return result;
    }

public:
    Harness() {
        auto vsCode = compile("VSMain", "vs_5_0", "0");
        auto p0 = compile("PSMain", "ps_5_0", "0"), p1 = compile("PSMain", "ps_5_0", "1");
        checkLayout(p0.Get());
        checkLayout(p1.Get());
        compile("VSMain", "vs_5_0", "1");
        for (const char* hw : {"0", "1"}) {
            compile("VSMotion", "vs_5_0", hw);
            compile("PSMotion", "ps_5_0", hw);
        }
        D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0, actual{};
        must(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &requested, 1, D3D11_SDK_VERSION, &dev,
                               &actual, &ctx),
             "WARP device (no hardware fallback)");
        if (actual != requested)
            throw std::runtime_error("feature level11 required");
        must(dev->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "VSMain");
        must(dev->CreatePixelShader(p0->GetBufferPointer(), p0->GetBufferSize(), nullptr, &ps[0]), "PSMain SW");
        must(dev->CreatePixelShader(p1->GetBufferPointer(), p1->GetBufferSize(), nullptr, &ps[1]), "PSMain HW");
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        must(dev->CreateInputLayout(elements, 3, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout),
             "input layout");
        vb = buffer(UINT(6 * sizeof(Vertex)), D3D11_BIND_VERTEX_BUFFER);
        cb = buffer(UINT(sizeof(CB)), D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = outputSize;
        td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        must(dev->CreateTexture2D(&td, nullptr, &target), "target");
        must(dev->CreateRenderTargetView(target.Get(), nullptr, &rtv), "RTV");
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        must(dev->CreateTexture2D(&td, nullptr, &staging), "staging");
        // Software PSMain reads DestTex even with ABE disabled; bind a separate,
        // initialized destination so no unbound-resource behavior is involved.
        destination = source(std::vector<Pixel>(outputSize * outputSize, Pixel{0, 0, 200.f / 255, 1}), outputSize);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        must(dev->CreateSamplerState(&sd, &point), "point sampler");
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        must(dev->CreateSamplerState(&sd, &linear), "linear sampler");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        must(dev->CreateRasterizerState(&rd, &raster), "raster state");
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = FALSE;
        dd.StencilEnable = FALSE;
        must(dev->CreateDepthStencilState(&dd, &depth), "depth disabled");
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        must(dev->CreateBlendState(&bd, &blend), "blend disabled");
    }
    ComPtr<ID3D11ShaderResourceView> source(const std::vector<Pixel>& pixels, UINT size = sourceSize) {
        // Match the renderer's byte RT storage; float output below avoids
        // introducing an additional UNORM conversion in the pixel oracle.
        std::vector<std::array<uint8_t, 4>> bytes(pixels.size());
        for (size_t i = 0; i < pixels.size(); ++i)
            for (unsigned c = 0; c < 4; ++c)
                bytes[i][c] = static_cast<uint8_t>(std::lround(pixels[i][c] * 255));
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = size;
        td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{bytes.data(), size * 4, 0};
        ComPtr<ID3D11Texture2D> tex;
        must(dev->CreateTexture2D(&td, &data, &tex), "source texture");
        ComPtr<ID3D11ShaderResourceView> srv;
        must(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv), "source SRV");
        return srv;
    }
    std::vector<Pixel> draw(const CB& constants, ID3D11ShaderResourceView* texture, float u, float v, unsigned hw,
                            bool footprintProbe = false) {
        if ((constants.a[0] & (1u << 24)) && !footprintProbe)
            throw std::runtime_error("RT cases must keep sprite footprint off");
        const auto vertex = [&](float x, float y) { return Vertex{x, y, 1, 0, 128, 128, 128, 128, u, v, 1}; };
        const Vertex vertices[] = {vertex(0, 0), vertex(4, 0), vertex(0, 4), vertex(4, 0), vertex(4, 4), vertex(0, 4)};
        ctx->UpdateSubresource(vb.Get(), 0, nullptr, vertices, 0, 0);
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &constants, 0, 0);
        const float clear[] = {-1, -1, -1, -1};
        ctx->ClearRenderTargetView(rtv.Get(), clear);
        D3D11_VIEWPORT viewport{0, 0, float(outputSize), float(outputSize), 0, 1};
        ctx->RSSetViewports(1, &viewport);
        ctx->RSSetState(raster.Get());
        ctx->IASetInputLayout(layout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* buffers[] = {vb.Get()};
        UINT stride = sizeof(Vertex), offset = 0;
        ctx->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps[hw].Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ID3D11ShaderResourceView* resources[] = {texture, destination.Get()};
        ctx->PSSetShaderResources(0, 2, resources);
        ID3D11SamplerState* samplers[] = {point.Get(), linear.Get()};
        ctx->PSSetSamplers(0, 2, samplers);
        ctx->OMSetDepthStencilState(depth.Get(), 0);
        ctx->OMSetBlendState(blend.Get(), nullptr, 0xffffffffu);
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        ctx->Draw(6, 0);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* empty[] = {nullptr, nullptr};
        ctx->PSSetShaderResources(0, 2, empty);
        ctx->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        must(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "read pixels");
        std::vector<Pixel> result(outputSize * outputSize);
        for (UINT y = 0; y < outputSize; ++y)
            std::memcpy(result.data() + y * outputSize, static_cast<const char*>(map.pData) + y * map.RowPitch,
                        outputSize * sizeof(Pixel));
        ctx->Unmap(staging.Get(), 0);
        return result;
    }
};

struct Scale {
    float x, y;
};
enum class Pattern { X, Y, Corner };
std::vector<Pixel> pattern(Scale scale, Pattern kind, bool ct24) {
    // Logical source [4,8)x[4,8) lies within a larger allocation. Its outer
    // texels are green sentinels: physical allocation clamp is not logical wrap.
    std::vector<Pixel> pixels(sourceSize * sourceSize, Pixel{0, 1, 0, 1});
    const int x0 = int(4 * scale.x), y0 = int(4 * scale.y), w = int(4 * scale.x), h = int(4 * scale.y);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool red = kind == Pattern::X ? x == 0 : kind == Pattern::Y ? y == 0 : x == 0 && y == 0;
            // CT24 must ignore these deliberately inverted stored alpha values.
            const float a = ct24 ? (red ? 17.f : 231.f) : (red ? 128.f : 0.f);
            pixels[size_t(y0 + y) * sourceSize + size_t(x0 + x)] = {red ? 1.f : 0.f, 0, 0, a / 255};
        }
    return pixels;
}
CB constants(Scale scale, bool ct24, bool repeat, bool linear = true) {
    CB k{};
    k.rt[0] = k.rt[1] = float(outputSize);
    k.rt[2] = k.rt[3] = 1;
    k.a[0] = textureFlags;
    k.b[1] = k.b[2] = 4;
    k.b[3] = repeat ? 0 : 5;
    k.d[0] = (1u << 4) | (1u << 5) | (1u << 6) | (ct24 ? 1u << 12 : 0); // fixedLOD, GS linear, AEM
    k.e[1] = linear ? 2.f : 1.f;
    k.e[2] = ct24 ? 1.f : 0.f;
    k.fog[3] = 128;
    k.src[0] = k.src[1] = 4;
    k.src[2] = scale.x / sourceSize;
    k.src[3] = scale.y / sourceSize;
    k.f[0] = 1;
    k.f[2] = 1; // texture upscale N=1; no anisotropy/mips/jitter
    return k;
}
} // namespace

int main(int argc, char** argv) {
    // A direct invocation must also make device use explicit.
    if (argc != 2 || std::strcmp(argv[1], "--run-warp")) {
        std::fprintf(stderr, "Device test not run. Use --run-warp only during an assigned slot.\n");
        return 2;
    }
    try {
        Harness h;
        unsigned cases = 0, checks = 0, failures = 0;
        const auto run = [&](const char* name, Scale scale, Pattern p, bool ct24, bool repeat, float u, float v,
                             Pixel expected, bool lin = true, bool discard = false) {
            auto texture = h.source(pattern(scale, p, ct24));
            CB k = constants(scale, ct24, repeat, lin);
            if (discard) {
                k.a[0] |= (1u << 6) | (5u << 7);
                k.a[1] = 96;
            } // GEQUAL96
            for (unsigned hw = 0; hw < 2; ++hw) {
                ++cases;
                const auto pixels = h.draw(k, texture.Get(), u, v, hw);
                for (size_t n = 0; n < pixels.size(); ++n)
                    for (unsigned channel = 0; channel < 4; ++channel) {
                        ++checks;
                        const float actual = pixels[n][channel];
                        // Expected channels are in GS byte units. One-unit tolerance
                        // covers half-step float/filter quantization, not alpha128 vs64.
                        const float want = discard ? -1.f : expected[channel] / 255.f;
                        const float tolerance = discard ? 0.f : 1.01f / 255;
                        if (!std::isfinite(actual) || std::fabs(actual - want) > tolerance) {
                            if (failures++ < 20)
                                std::fprintf(
                                    stderr, "FAIL %s scale=%g,%g HW_BLEND=%u pixel=%zu channel=%u got=%g expected=%g\n",
                                    name, scale.x, scale.y, hw, n, channel, actual, want);
                        }
                    }
            }
        };
        for (Scale scale :
             {Scale{1, 1}, Scale{4, 4}, Scale{1.5f, 1.5f}, Scale{.75f, .75f}, Scale{1.5f, .75f}, Scale{.75f, 1.5f}}) {
            const float xCentre = .5f / scale.x, yCentre = .5f / scale.y;
            // Each boundary is chosen in physical texel coordinates; expected
            // weights are independently fixed at1/2 or1/4, not a sampler oracle.
            run("CT24 AEM horizontal prefilter", scale, Pattern::X, true, false, 1 / scale.x, yCentre, {128, 0, 0, 64});
            run("CT24 AEM vertical prefilter", scale, Pattern::Y, true, false, xCentre, 1 / scale.y, {128, 0, 0, 64});
            run("CT24 AEM four-tap prefilter", scale, Pattern::Corner, true, false, 1 / scale.x, 1 / scale.y,
                {64, 0, 0, 32});
            run("CT24 point red ignores stored alpha17", scale, Pattern::X, true, false, xCentre, yCentre,
                {255, 0, 0, 128}, false);
            run("CT24 point black AEM ignores stored alpha231", scale, Pattern::X, true, false, 1.5f / scale.x, yCentre,
                {0, 0, 0, 0}, false);
            run("CT32 horizontal repeat seam", scale, Pattern::X, false, true, 0, yCentre, {128, 0, 0, 64});
            run("CT32 positive endpoint seam", scale, Pattern::X, false, true, 4, yCentre, {128, 0, 0, 64});
            run("CT32 negative period seam", scale, Pattern::X, false, true, -4, yCentre, {128, 0, 0, 64});
            run("CT32 vertical repeat seam", scale, Pattern::Y, false, true, xCentre, 0, {128, 0, 0, 64});
            run("CT32 four-tap repeat corner", scale, Pattern::Corner, false, true, 0, 0, {64, 0, 0, 32});
            run("CT24 AEM plus repeat seam", scale, Pattern::X, true, true, 0, yCentre, {128, 0, 0, 64});
            run("CT24 alpha64 fails GEQUAL96", scale, Pattern::X, true, false, 1 / scale.x, yCentre, {0, 0, 0, 0}, true,
                true);
        }
        // Ordinary CT32 footprint probes: use bounds from actual original
        // endpoint quads, then probe constant UVs so failures isolate sampling
        // from interpolation/raster origin. This does not exercise backend
        // batch splitting, HUD transforms, or parent live-scene integration.
        const auto endpoints = [](bool intentional, bool fst) {
            const float end = intentional ? 3.f : 2.f, uvEnd = intentional ? 4.5f : 3.5f;
            const auto v = [](float x, float y, float u, float t) {
                return Vertex{x, y, 1, 0, 128, 128, 128, 128, u, t, 1};
            };
            std::array<Vertex, 6> quad = {v(0, 0, 1.5f, 1.5f),    v(end, 0, uvEnd, 1.5f),    v(0, end, 1.5f, uvEnd),
                                          v(end, 0, uvEnd, 1.5f), v(end, end, uvEnd, uvEnd), v(0, end, 1.5f, uvEnd)};
            if (!fst)
                for (auto& item : quad) {
                    item.s /= 8;
                    item.t /= 8;
                }
            return quad;
        };
        for (unsigned factor : {1u, 2u, 4u, 8u})
            for (bool fst : {false, true})
                for (bool vertical : {false, true}) {
                    const UINT size = 8 * factor;
                    std::vector<Pixel> atlas(size * size, Pixel{0, 0, 1, 128.f / 255});
                    for (UINT y = 0; y < size; ++y)
                        for (UINT x = 0; x < size; ++x) {
                            const UINT p = vertical ? y : x, cell = p / factor, sub = p % factor;
                            if (cell == 1)
                                atlas[size_t(y) * size + x] = {0, 60.f / 255, 0, 128.f / 255};
                            if (cell == 2)
                                atlas[size_t(y) * size + x] = {float(20 + 20 * sub) / 255, 0, 0, 128.f / 255};
                            if (cell == 3)
                                atlas[size_t(y) * size + x] = {1, 1, 1, 127.f / 255};
                        }
                    auto texture = h.source(atlas, size);
                    const auto basic = endpoints(false, fst), intentional = endpoints(true, fst);
                    const auto excluded = gsr::sprite_footprint::bounds(basic.data(), 6, fst, 8, 8, factor);
                    const auto included = gsr::sprite_footprint::bounds(intentional.data(), 6, fst, 8, 8, factor);
                    if (!excluded.valid || !included.valid)
                        throw std::runtime_error("footprint endpoint fixture rejected");
                    CB k{};
                    k.rt[0] = k.rt[1] = float(outputSize);
                    k.rt[2] = k.rt[3] = 1;
                    k.a[0] = (1u << 0) | (1u << 3) | (1u << 4) | (1u << 17) | (fst ? 1u << 2 : 0);
                    k.b[1] = k.b[2] = 8;
                    k.b[3] = 5;
                    k.d[0] = 1u << 4; // CLAMP, fixedLOD, point
                    k.e[1] = 1;
                    k.f[0] = float(factor);
                    k.f[2] = 1;
                    k.f[3] = std::log2(float(factor));
                    const auto probe = [&](const char* name, float along, Pixel expected, bool enabled,
                                           const gsr::sprite_footprint::Bounds& bounds) {
                        k.a[0] = enabled ? (k.a[0] | (1u << 24)) : (k.a[0] & ~(1u << 24));
                        k.spriteBounds[0] = bounds.u0;
                        k.spriteBounds[1] = bounds.v0;
                        k.spriteBounds[2] = bounds.u1;
                        k.spriteBounds[3] = bounds.v1;
                        float u = vertical ? 1.5f : along, v = vertical ? along : 1.5f;
                        if (!fst) {
                            u /= 8;
                            v /= 8;
                        }
                        for (unsigned hw = 0; hw < 2; ++hw) {
                            ++cases;
                            const auto pixels = h.draw(k, texture.Get(), u, v, hw, true);
                            for (size_t n = 0; n < pixels.size(); ++n)
                                for (unsigned c = 0; c < 4; ++c) {
                                    ++checks;
                                    const float actual = pixels[n][c], want = expected[c] / 255;
                                    if (!std::isfinite(actual) || std::fabs(actual - want) > 1.01f / 255) {
                                        if (failures++ < 20)
                                            std::fprintf(stderr,
                                                         "FAIL footprint %s N=%u FST=%d vertical=%d HW=%u pixel=%zu "
                                                         "channel=%u got=%g expected=%g\n",
                                                         name, factor, int(fst), int(vertical), hw, n, c, actual, want);
                                    }
                                }
                        }
                    };
                    const float last = float(20 + 20 * (factor - 1));
                    probe("flag off leaves adjacent white visible", 3.25f, {255, 255, 255, 127}, false, excluded);
                    probe("flag on excludes unsampled white cell", 3.25f, {last, 0, 0, 128}, true, excluded);
                    probe("intentional white outline and alpha127 preserved", 3.25f, {255, 255, 255, 127}, true,
                          included);
                    probe("first HD subtexel retained", 2 + .5f / factor, {20, 0, 0, 128}, true, excluded);
                    probe("last HD subtexel retained", 3 - .5f / factor, {last, 0, 0, 128}, true, excluded);
                    probe("lower footprint excludes adjacent blue cell", .75f, {0, 60, 0, 128}, true, excluded);
                    probe("flag off leaves lower blue cell visible", .75f, {0, 0, 255, 128}, false, excluded);
                }
        std::printf("RT/footprint sampling pixels: %u draws, %u channel checks, %u failures; WARP only\n", cases,
                    checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
