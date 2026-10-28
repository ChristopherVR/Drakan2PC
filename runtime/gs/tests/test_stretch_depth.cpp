#define NOMINMAX
#include "gs_shaders.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void must(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s: HRESULT 0x%08lx\n", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(operation);
    }
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
// Only fixture setup/observation shaders are local. The shader under test and
// fullscreen vertex shader are compiled directly from the production source.
constexpr const char* fixtureHlsl = R"(
float PSClear() : SV_Depth { return 0.0; }
struct Probe { float4 color : SV_Target; float depth : SV_Depth; };
Probe PSProbe() { Probe o; o.color = 1.0; o.depth = 0.5; return o; }
)";
ComPtr<ID3DBlob> compile(const char* source, const char* entry, const char* target) {
    const D3D_SHADER_MACRO macros[] = {{"HW_BLEND", "0"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), "stretch_depth_test", macros, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr) && errors)
        std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
    must(hr, entry);
    return code;
}
struct Size {
    UINT w, h;
};
struct Params {
    float sizes[4];
    float jitter[2];
    UINT unused[2];
};
static_assert(sizeof(Params) == 32);
struct Fixture {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> stretch, clear, probe;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11Texture2D> depth, snapshot, color;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilState> always, test;
    ComPtr<ID3D11RasterizerState> raster;
    Size full;

    Fixture(Size size, ID3DBlob* vsCode, ID3DBlob* stretchCode, ID3DBlob* clearCode, ID3DBlob* probeCode) : full(size) {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        must(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &dev, nullptr,
                               &ctx),
             "WARP device");
        must(dev->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "VSFull");
        auto pixel = [&](ID3DBlob* code, ComPtr<ID3D11PixelShader>& output) {
            must(dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &output),
                 "pixel shader");
        };
        pixel(stretchCode, stretch);
        pixel(clearCode, clear);
        pixel(probeCode, probe);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(Params);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        must(dev->CreateBuffer(&bd, nullptr, &cb), "CBP");
        D3D11_TEXTURE2D_DESC td{};
        td.Width = full.w;
        td.Height = full.h;
        td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R32_TYPELESS;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        must(dev->CreateTexture2D(&td, nullptr, &depth), "depth allocation");
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        must(dev->CreateDepthStencilView(depth.Get(), &dd, &dsv), "depth DSV");
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        must(dev->CreateTexture2D(&td, nullptr, &snapshot), "depth snapshot");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        must(dev->CreateShaderResourceView(snapshot.Get(), &sd, &srv), "snapshot SRV");
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        must(dev->CreateTexture2D(&td, nullptr, &color), "visibility target");
        must(dev->CreateRenderTargetView(color.Get(), nullptr, &rtv), "visibility RTV");
        D3D11_DEPTH_STENCIL_DESC zd{};
        zd.DepthEnable = TRUE;
        zd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        zd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        must(dev->CreateDepthStencilState(&zd, &always), "always write depth");
        // GS depth is reversed: the synthetic world at 0.5 passes a cleared
        // preview (0), but fails against retained UI depth (at least 0.75).
        zd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        must(dev->CreateDepthStencilState(&zd, &test), "world depth test");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        rd.ScissorEnable = TRUE;
        must(dev->CreateRasterizerState(&rd, &raster), "rasterizer");
        ctx->RSSetState(raster.Get());
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
    }
    void viewport(Size size, D3D11_RECT rect) {
        const D3D11_VIEWPORT vp{0, 0, float(size.w), float(size.h), 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetScissorRects(1, &rect);
    }
    void viewport(Size size) { viewport(size, {0, 0, LONG(size.w), LONG(size.h)}); }
    void unbind() {
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(0, 1, &none);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    void upload(const std::vector<float>& values) {
        // UpdateSubresource cannot target a depth/stencil resource. Upload to
        // the compatible typeless snapshot, then copy into the depth texture.
        unbind();
        ctx->UpdateSubresource(snapshot.Get(), 0, nullptr, values.data(), full.w * sizeof(float), 0);
        ctx->CopyResource(depth.Get(), snapshot.Get());
    }
    std::vector<float> read(ID3D11Texture2D* texture) {
        D3D11_TEXTURE2D_DESC td{};
        texture->GetDesc(&td);
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        must(dev->CreateTexture2D(&td, nullptr, &staging), "readback texture");
        ctx->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE map{};
        must(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "readback map");
        std::vector<float> result(size_t(full.w) * full.h);
        for (UINT y = 0; y < full.h; ++y)
            std::memcpy(result.data() + size_t(y) * full.w,
                        static_cast<const char*>(map.pData) + size_t(y) * map.RowPitch, full.w * sizeof(float));
        ctx->Unmap(staging.Get(), 0);
        return result;
    }
    void stretchDepth(Size source, Size dest, float jx = 0, float jy = 0) {
        unbind();
        ctx->CopyResource(snapshot.Get(), depth.Get());
        const Params params{{float(source.w), float(source.h), float(dest.w), float(dest.h)}, {jx, jy}, {0, 0}};
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &params, 0, 0);
        ctx->PSSetConstantBuffers(2, 1, cb.GetAddressOf());
        ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
        ctx->OMSetRenderTargets(0, nullptr, dsv.Get());
        ctx->OMSetDepthStencilState(always.Get(), 0);
        viewport(dest);
        ctx->PSSetShader(stretch.Get(), nullptr, 0);
        ctx->Draw(3, 0);
        unbind();
    }
    std::vector<float> drawWorld(Size size) {
        const float black[4]{};
        ctx->ClearRenderTargetView(rtv.Get(), black);
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
        ctx->OMSetDepthStencilState(test.Get(), 0);
        viewport(size);
        ctx->PSSetShader(probe.Get(), nullptr, 0);
        ctx->Draw(3, 0);
        unbind();
        return read(color.Get());
    }
};
// Independent CPU nearest oracle, including physical allocation stride and
// preservation outside the reduced viewport. Double arithmetic avoids copying
// the shader's float implementation; chosen cases stay away from tap ties.
std::vector<float> nearest(const std::vector<float>& input, Size allocation, Size source, Size dest, float jx,
                           float jy) {
    auto result = input;
    for (UINT y = 0; y < dest.h; ++y)
        for (UINT x = 0; x < dest.w; ++x) {
            const int sx = std::clamp(int(std::floor((double(x) + .5) * source.w / dest.w + jx)), 0, int(source.w) - 1);
            const int sy = std::clamp(int(std::floor((double(y) + .5) * source.h / dest.h + jy)), 0, int(source.h) - 1);
            result[size_t(y) * allocation.w + x] = input[size_t(sy) * allocation.w + sx];
        }
    return result;
}
void equal(const std::vector<float>& actual, const std::vector<float>& expected, Size allocation, const char* label) {
    for (size_t i = 0; i < expected.size(); ++i)
        if (actual[i] != expected[i]) {
            std::fprintf(stderr, "%s at (%zu,%zu): %.9g expected %.9g\n", label, i % allocation.w, i / allocation.w,
                         actual[i], expected[i]);
            throw std::runtime_error(label);
        }
}
void run(Size full, Size reduced, float jx, float jy, ID3DBlob* vs, ID3DBlob* ps, ID3DBlob* clear, ID3DBlob* probe) {
    Fixture f(full, vs, ps, clear, probe);
    std::vector<float> initial(size_t(full.w) * full.h);
    for (UINT y = 0; y < full.h; ++y)
        for (UINT x = 0; x < full.w; ++x)
            initial[size_t(y) * full.w + x] = .75f + float((x + 3 * y) % 32) / 256;
    f.upload(initial);
    const D3D11_RECT preview{LONG(full.w * 5 / 8), LONG(full.h / 4), LONG(full.w), LONG(full.h * 3 / 4)};
    f.ctx->OMSetRenderTargets(0, nullptr, f.dsv.Get());
    f.ctx->OMSetDepthStencilState(f.always.Get(), 0);
    f.viewport(full, preview);
    f.ctx->PSSetShader(f.clear.Get(), nullptr, 0);
    f.ctx->Draw(3, 0);
    f.unbind();
    for (LONG y = preview.top; y < preview.bottom; ++y)
        for (LONG x = preview.left; x < preview.right; ++x)
            initial[size_t(y) * full.w + x] = 0;
    equal(f.read(f.depth.Get()), initial, full, "partial right-side clear preserves UI depth");

    const auto reducedDepth = nearest(initial, full, full, reduced, 0, 0);
    std::vector<float> visible(initial.size());
    unsigned previewPixels = 0, staleHoles = 0;
    for (UINT y = 0; y < reduced.h; ++y)
        for (UINT x = 0; x < reduced.w; ++x) {
            const size_t i = size_t(y) * full.w + x;
            visible[i] = reducedDepth[i] <= .5f ? 1.0f : 0.0f;
            if (visible[i] != 0) {
                ++previewPixels;
                if (initial[i] > .5f)
                    ++staleHoles;
            }
        }
    require(previewPixels > 0 && staleHoles > 0, "fixture exposes stale UI depth inside reduced preview");
    const auto broken = f.drawWorld(reduced);
    unsigned observedHoles = 0;
    for (size_t i = 0; i < visible.size(); ++i)
        if (visible[i] != 0 && broken[i] == 0)
            ++observedHoles;
    require(observedHoles == staleHoles, "no-stretch control reproduces preview occlusion");
    // Discard control draws so the real transition starts with the exact clear.
    f.upload(initial);
    f.stretchDepth(full, reduced);
    equal(f.read(f.depth.Get()), reducedDepth, full, "full to reduced nearest depth and untouched exterior");
    equal(f.read(f.snapshot.Get()), initial, full, "full source snapshot unchanged after reduction");
    equal(f.drawWorld(reduced), visible, full, "preview world visible with no stale left UI depth");
    auto worldDepth = reducedDepth;
    for (UINT y = 0; y < reduced.h; ++y)
        for (UINT x = 0; x < reduced.w; ++x) {
            const size_t i = size_t(y) * full.w + x;
            if (visible[i] != 0)
                worldDepth[i] = .5f;
        }
    equal(f.read(f.depth.Get()), worldDepth, full, "world writes only cleared reduced preview");
    equal(f.read(f.snapshot.Get()), initial, full, "world draw leaves full source snapshot unchanged");
    const auto restored = nearest(worldDepth, full, reduced, full, jx, jy);
    require(restored != nearest(worldDepth, full, reduced, full, 0, 0) || (jx == 0 && jy == 0),
            "jitter case changes nearest taps");
    f.stretchDepth(reduced, full, jx, jy);
    equal(f.read(f.depth.Get()), restored, full, "reduced to full nearest depth with jitter and edge clamp");
    equal(f.read(f.snapshot.Get()), worldDepth, full, "reduced source snapshot unchanged after restoration");
    for (size_t i = 0; i < visible.size(); ++i)
        visible[i] = restored[i] <= .5f ? 1.0f : 0.0f;
    equal(f.drawWorld(full), visible, full, "restored preview visibility follows jittered depth");
    equal(f.read(f.snapshot.Get()), worldDepth, full, "restored world draw leaves source snapshot unchanged");
    std::printf("PASS %ux%u -> %ux%u -> full jitter (%g,%g), %u preview pixels, %u stale holes prevented\n", full.w,
                full.h, reduced.w, reduced.h, jx, jy, previewPixels, staleHoles);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 1 || (argc == 2 && std::strcmp(argv[1], "--compile-only") == 0),
                "usage: gsr_test_stretch_depth [--compile-only]");
        const auto vs = compile(gsr::kHlslMain, "VSFull", "vs_5_0");
        const auto ps = compile(gsr::kHlslMain, "PSStretchDepth", "ps_5_0");
        const auto clear = compile(fixtureHlsl, "PSClear", "ps_5_0");
        const auto probe = compile(fixtureHlsl, "PSProbe", "ps_5_0");
        if (argc == 2) {
            std::puts("PASS stretch depth shader compilation only; no D3D device created");
            return 0;
        }
        run({32, 24}, {16, 12}, 0, 0, vs.Get(), ps.Get(), clear.Get(), probe.Get());
        run({32, 24}, {16, 12}, .625f, -.625f, vs.Get(), ps.Get(), clear.Get(), probe.Get());
        run({31, 23}, {19, 13}, -.625f, .625f, vs.Get(), ps.Get(), clear.Get(), probe.Get());
        std::puts("PASS stretch depth: 3 bounded, asset-free WARP transition cases");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
}
