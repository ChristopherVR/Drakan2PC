#define NOMINMAX
#include "gs_shaders.h"
#include "motion_history.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
void must(HRESULT hr, const char* operation) {
    if (FAILED(hr))
        throw std::runtime_error(operation);
}
ComPtr<ID3DBlob> shader(const char* entry, const char* target, const char* hw = "0") {
    const D3D_SHADER_MACRO macros[] = {{"HW_BLEND", hw}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, error;
    const HRESULT hr = D3DCompile(gsr::kHlslMain, std::strlen(gsr::kHlslMain), "gsr", macros, nullptr, entry, target,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(hr) && error)
        std::fwrite(error->GetBufferPointer(), 1, error->GetBufferSize(), stderr);
    must(hr, entry);
    return code;
}
struct CB {
    float rt[4];
    uint32_t a[4], b[4], c[4];
    float fog[4];
    uint32_t d[4];
    float e[4], src[4], f[4], g[4], h[4], spriteBounds[4];
};
struct Vertex {
    float x, y, z, fog, r, g, b, a, s, t, q;
};
} // namespace
int main() {
    try {
        shader("VSMain", "vs_5_0");
        shader("PSMain", "ps_5_0");
        shader("PSMain", "ps_5_0", "1");
        auto vsCode = shader("VSMotion", "vs_5_0"), psCode = shader("PSMotion", "ps_5_0");
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        must(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr,
                               &ctx),
             "device");
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        must(dev->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "VS");
        must(dev->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps), "PS");
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        ComPtr<ID3D11InputLayout> layout;
        must(dev->CreateInputLayout(elements, 4, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout),
             "layout");
        Vertex vertices[] = {{2, 2, 100, 0, 128, 128, 128, 128, 0, 0, 1},
                             {24, 2, 100, 0, 128, 128, 128, 128, 1, 0, 1},
                             {2, 24, 100, 0, 128, 128, 128, 128, 0, 1, 1}};
        gsr::motion::Previous history[] = {{5, 0, 1, 1}, {27, 0, 1, 1}, {5, 22, 1, 1}};
        auto buffer = [&](const void* data, UINT size, UINT bind) {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = size;
            desc.BindFlags = bind;
            desc.Usage = D3D11_USAGE_DEFAULT;
            D3D11_SUBRESOURCE_DATA input{data, 0, 0};
            ComPtr<ID3D11Buffer> result;
            must(dev->CreateBuffer(&desc, &input, &result), "buffer");
            return result;
        };
        auto vb = buffer(vertices, sizeof vertices, D3D11_BIND_VERTEX_BUFFER),
             pb = buffer(history, sizeof history, D3D11_BIND_VERTEX_BUFFER);
        CB constants{};
        constants.rt[0] = constants.rt[1] = 32;
        constants.rt[2] = 1000;
        constants.rt[3] = 1;
        constants.h[2] = 16384;
        constants.h[3] = 1;
        auto cb = buffer(&constants, sizeof constants, D3D11_BIND_CONSTANT_BUFFER);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = 32;
        td.MipLevels = td.ArraySize = 1;
        td.SampleDesc.Count = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture, readback, depth;
        must(dev->CreateTexture2D(&td, nullptr, &texture), "target");
        td.BindFlags = 0;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        must(dev->CreateTexture2D(&td, nullptr, &readback), "readback");
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        td.Format = DXGI_FORMAT_D32_FLOAT;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.CPUAccessFlags = 0;
        must(dev->CreateTexture2D(&td, nullptr, &depth), "depth");
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11DepthStencilView> dsv;
        must(dev->CreateRenderTargetView(texture.Get(), nullptr, &rtv), "RTV");
        must(dev->CreateDepthStencilView(depth.Get(), nullptr, &dsv), "DSV");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = FALSE;
        ComPtr<ID3D11RasterizerState> raster;
        must(dev->CreateRasterizerState(&rd, &raster), "raster");
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        ComPtr<ID3D11DepthStencilState> z;
        must(dev->CreateDepthStencilState(&dd, &z), "zstate");
        D3D11_VIEWPORT vp{0, 0, 32, 32, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(raster.Get());
        ctx->IASetInputLayout(layout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* buffers[] = {vb.Get(), pb.Get()};
        UINT strides[] = {sizeof(Vertex), sizeof(gsr::motion::Previous)}, offsets[] = {0, 0};
        ctx->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ctx->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ctx->OMSetDepthStencilState(z.Get(), 0);
        unsigned checks = 0, failures = 0;
        auto check = [&](bool v, const char* message) {
            ++checks;
            if (!v) {
                ++failures;
                std::printf("FAIL %s\n", message);
            }
        };
        auto draw = [&](float depthClear) {
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            const float clear[] = {-99, -99, 0, 0};
            ctx->ClearRenderTargetView(rtv.Get(), clear);
            ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, depthClear, 0);
            ctx->UpdateSubresource(cb.Get(), 0, nullptr, &constants, 0, 0);
            ctx->UpdateSubresource(pb.Get(), 0, nullptr, history, 0, 0);
            ctx->UpdateSubresource(vb.Get(), 0, nullptr, vertices, 0, 0);
            ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
            ctx->Draw(3, 0);
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->CopyResource(readback.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE m{};
            must(ctx->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &m), "map");
            std::vector<float> pixel(4);
            std::memcpy(pixel.data(), static_cast<const char*>(m.pData) + 5 * m.RowPitch + 5 * 16, 16);
            ctx->Unmap(readback.Get(), 0);
            return pixel;
        };
        auto pixel = draw(0);
        check(std::abs(pixel[0] - 3) < 1e-5 && std::abs(pixel[1] + 2) < 1e-5 && pixel[2] == 1,
              "independent foreground velocity +3,-2");
        check(std::abs(pixel[3] - .1f) < 1e-6, "velocity carries actual GS depth");
        pixel = draw(.7f);
        check(pixel[0] == -99 && pixel[2] == 0, "occluded foreground cannot overwrite motion");
        constants.a[0] = (1u << 6) | (6u << 7);
        constants.a[1] = 128;
        pixel = draw(0);
        check(pixel[0] == -99, "GS alpha-test discard matches colour coverage");
        constants.a[0] = 0;
        for (auto& item : history)
            item.valid = 0;
        pixel = draw(0);
        check(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0,
              "unmatched foreground invalidates prior motion coverage");
        for (auto& item : history)
            item.valid = 1;
        constants.a[0] = 1u << 14; // additive colour retains background: no single-object velocity
        constants.a[2] = 0u | (2u << 2) | (2u << 4) | (1u << 6);
        constants.a[3] = 128;
        pixel = draw(0);
        check(pixel[2] == 0, "additive transparency uses conservative fallback");
        constants.a[0] = 0;
        vertices[0].z = 100;
        vertices[1].z = 200;
        vertices[2].z = 400;
        history[0] = {2.5f, 0, .5f, 1};
        history[1] = {54, 0, 2, 1};
        history[2] = {10, 44, 2, 1};
        pixel = draw(0);
        const float b1 = 3.5f / 22, b0 = 1 - 2 * b1, ratio = b0 * .5f + 4 * b1;
        const float expectedX = (b0 * 2.5f + b1 * 54 + b1 * 10) / ratio - 5.5f;
        const float expectedY = (b1 * 44) / ratio - 5.5f;
        check(std::abs(pixel[0] - expectedX) < 1e-4f && std::abs(pixel[1] - expectedY) < 1e-4f,
              "GPU perspective motion matches independent barycentric oracle");
        vertices[0].z = vertices[1].z = vertices[2].z = 100;
        history[0] = {2, 2, 1, 1};
        history[1] = {24, 2, 1, 1};
        history[2] = {2, 24, 1, 1};
        constants.g[1] = 2 * .25f / 32;
        constants.g[2] = 2 * .25f / 32;
        constants.h[0] = .25f;
        constants.h[1] = -.25f;
        pixel = draw(0);
        check(std::abs(pixel[0]) < 1e-5 && std::abs(pixel[1]) < 1e-5,
              "current raster jitter excluded from stationary object motion");
        std::printf("motion shader %u checks, %u failures; all GS colour/motion entries compile\n", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
