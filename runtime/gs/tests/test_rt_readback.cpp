#include "rt_readback.h"
#include <cassert>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                   nullptr, &ctx);
    if (FAILED(hr))
        return 1;
    const char keepCode[] = "[numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID) {}";
    ComPtr<ID3DBlob> keepBinary;
    ComPtr<ID3D11ComputeShader> keepShader;
    assert(SUCCEEDED(D3DCompile(keepCode, sizeof(keepCode) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0,
                                &keepBinary, nullptr)));
    assert(SUCCEEDED(
        dev->CreateComputeShader(keepBinary->GetBufferPointer(), keepBinary->GetBufferSize(), nullptr, &keepShader)));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> keepConstants;
    assert(SUCCEEDED(dev->CreateBuffer(&cb, nullptr, &keepConstants)));
    auto* keepBuffer = keepConstants.Get();
    ctx->CSSetShader(keepShader.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &keepBuffer);
    gsr::NativeReadback readback;
    size_t checked = 0;
    for (unsigned scale = 1; scale <= 8; ++scale)
        for (unsigned y0 : {0u, 3u}) {
            const unsigned w = 37, h = 19, rows = h - y0, W = w * scale, H = h * scale;
            std::vector<unsigned> pixels(size_t(W) * H);
            for (size_t i = 0; i < pixels.size(); ++i)
                pixels[i] = unsigned(i * 2654435761u);
            D3D11_TEXTURE2D_DESC d{};
            d.Width = W;
            d.Height = H;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA init{pixels.data(), W * 4, 0};
            ComPtr<ID3D11Texture2D> source;
            ComPtr<ID3D11ShaderResourceView> srv;
            ComPtr<ID3D11RenderTargetView> rtv;
            assert(SUCCEEDED(dev->CreateTexture2D(&d, &init, &source)));
            assert(SUCCEEDED(dev->CreateShaderResourceView(source.Get(), nullptr, &srv)));
            assert(SUCCEEDED(dev->CreateRenderTargetView(source.Get(), nullptr, &rtv)));
            auto* target = rtv.Get();
            ctx->OMSetRenderTargets(1, &target, nullptr);
            auto* result = readback.gather(dev.Get(), ctx.Get(), srv.Get(), w, rows, scale, y0);
            assert(result);
            ComPtr<ID3D11ComputeShader> restoredShader;
            ComPtr<ID3D11Buffer> restoredConstants;
            ctx->CSGetShader(&restoredShader, nullptr, nullptr);
            ctx->CSGetConstantBuffers(0, 1, &restoredConstants);
            assert(restoredShader.Get() == keepShader.Get() && restoredConstants.Get() == keepConstants.Get());
            ComPtr<ID3D11RenderTargetView> restored;
            ctx->OMGetRenderTargets(1, &restored, nullptr);
            assert(restored.Get() == target);
            d.Width = w;
            d.Height = rows;
            d.BindFlags = 0;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            assert(SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &staging)));
            D3D11_BOX box{0, 0, 0, w, rows, 1};
            ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, result, 0, &box);
            D3D11_MAPPED_SUBRESOURCE map{};
            assert(SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)));
            for (unsigned y = 0; y < rows; ++y)
                for (unsigned x = 0; x < w; ++x) {
                    const auto got = reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(map.pData) +
                                                                       y * map.RowPitch)[x];
                    const auto expected = pixels[size_t((y + y0) * scale + scale / 2) * W + x * scale + scale / 2];
                    assert(got == expected);
                    ++checked;
                }
            ctx->Unmap(staging.Get(), 0);
        }
    std::printf(
        "Native GPU readback: %zu exact RGBA texels; scales 1..8, dirty-row offsets and RTV restoration passed\n",
        checked);
}
