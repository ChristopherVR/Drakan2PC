#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>

namespace gsr {
// Select the exact center texel used by GS readback before crossing PCIe.
// Do not filter: alpha bits and packed 16/24-bit framebuffer values matter.
class NativeReadback {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> shader;
    Ptr<ID3D11Buffer> constants;
    Ptr<ID3D11Texture2D> texture;
    Ptr<ID3D11UnorderedAccessView> output;
    uint32_t width = 0, height = 0;

public:
    ID3D11Texture2D* gather(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* source, uint32_t w,
                            uint32_t rows, uint32_t scale, uint32_t y0) {
        if (!shader) {
            static constexpr char code[] = R"(
Texture2D<float4> source : register(t0);
RWTexture2D<unorm float4> output : register(u0);
cbuffer Constants : register(b0) { uint width, rows, scale, y0; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    if(p.x<width && p.y<rows)
        output[p.xy]=source.Load(int3(p.x*scale+scale/2,(p.y+y0)*scale+scale/2,0));
})";
            Ptr<ID3DBlob> binary, errors;
            if (FAILED(D3DCompile(code, sizeof(code) - 1, "native_readback", nullptr, nullptr, "main", "cs_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &binary, &errors)))
                return nullptr;
            if (FAILED(dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr, &shader)))
                return nullptr;
            D3D11_BUFFER_DESC cb{};
            cb.ByteWidth = 16;
            cb.Usage = D3D11_USAGE_DEFAULT;
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(dev->CreateBuffer(&cb, nullptr, &constants))) {
                shader.Reset();
                return nullptr;
            }
        }
        if (!texture || width < w || height < rows) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = w;
            d.Height = rows;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            output.Reset();
            texture.Reset();
            width = height = 0;
            if (FAILED(dev->CreateTexture2D(&d, nullptr, &texture)) ||
                FAILED(dev->CreateUnorderedAccessView(texture.Get(), nullptr, &output))) {
                texture.Reset();
                return nullptr;
            }
            width = w;
            height = rows;
        }
        // Readback can interrupt a draw sequence; preserve both pipelines.
        ID3D11RenderTargetView* targets[8]{};
        ID3D11DepthStencilView* depth = nullptr;
        ID3D11ComputeShader* oldShader = nullptr;
        ID3D11Buffer* oldConstants = nullptr;
        ID3D11ShaderResourceView* oldSource = nullptr;
        ID3D11UnorderedAccessView* oldOutput = nullptr;
        ID3D11ClassInstance* classes[256]{};
        UINT classCount = 256;
        ctx->OMGetRenderTargets(8, targets, &depth);
        ctx->CSGetShader(&oldShader, classes, &classCount);
        ctx->CSGetConstantBuffers(0, 1, &oldConstants);
        ctx->CSGetShaderResources(0, 1, &oldSource);
        ctx->CSGetUnorderedAccessViews(0, 1, &oldOutput);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        const uint32_t values[] = {w, rows, scale, y0};
        ctx->UpdateSubresource(constants.Get(), 0, nullptr, values, 0, 0);
        auto* cb = constants.Get();
        auto* uav = output.Get();
        ctx->CSSetShader(shader.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &cb);
        ctx->CSSetShaderResources(0, 1, &source);
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ctx->Dispatch((w + 7) / 8, (rows + 7) / 8, 1);
        ID3D11ShaderResourceView* nullSource = nullptr;
        ID3D11UnorderedAccessView* nullOutput = nullptr;
        ctx->CSSetShaderResources(0, 1, &nullSource);
        ctx->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
        ctx->CSSetShader(oldShader, classes, classCount);
        ctx->CSSetConstantBuffers(0, 1, &oldConstants);
        ctx->CSSetShaderResources(0, 1, &oldSource);
        ctx->CSSetUnorderedAccessViews(0, 1, &oldOutput, nullptr);
        ctx->OMSetRenderTargets(8, targets, depth);
        for (auto* t : targets)
            if (t)
                t->Release();
        if (depth)
            depth->Release();
        if (oldShader)
            oldShader->Release();
        if (oldConstants)
            oldConstants->Release();
        if (oldSource)
            oldSource->Release();
        if (oldOutput)
            oldOutput->Release();
        for (UINT i = 0; i < classCount; ++i)
            if (classes[i])
                classes[i]->Release();
        return texture.Get();
    }
};
} // namespace gsr
