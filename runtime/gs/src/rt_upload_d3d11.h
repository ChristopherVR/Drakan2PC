#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include "shader_compile_d3d.h"
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>

namespace gsr {
// Rows of GS memory copied up into a render target at an internal scale above 1. The CPU path replicates every
// pixel scale x scale times and sends the result (width * rows * scale^2 * 4 bytes: 4 MiB for one 512 pixel page row
// at scale 8); here the native pixels are sent (64 KiB for that row) and a compute pass writes each of them to its
// scale x scale block of a scratch texture, which is then copied into the target. Same values, bit for bit: an
// UNORM texel is loaded and stored unchanged (compare with GSR_RT_UPLOAD_GPU=0, the CPU path).
class TargetUploadGpu {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> shader;
    Ptr<ID3D11Buffer> constants;
    Ptr<ID3D11Texture2D> source, expanded;
    Ptr<ID3D11ShaderResourceView> sourceView;
    Ptr<ID3D11UnorderedAccessView> expandedView;
    uint32_t sourceW = 0, sourceH = 0, expandedW = 0, expandedH = 0;
    bool failed = false;

    static constexpr char kCode[] = R"(
Texture2D<float4> source : register(t0);
RWTexture2D<unorm float4> output : register(u0);
cbuffer Constants : register(b0) { uint width, rows, scale, unused; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    if(p.x<width*scale && p.y<rows*scale)
        output[p.xy]=source.Load(int3(p.xy/scale,0));
})";

    bool prepare(ID3D11Device* dev, uint32_t w, uint32_t h, uint32_t scale) {
        if (failed)
            return false;
        if (!shader) {
            Ptr<ID3DBlob> binary, errors;
            D3D11_BUFFER_DESC cb{};
            cb.ByteWidth = 16;
            cb.Usage = D3D11_USAGE_DEFAULT;
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(shader_compile::compile(kCode, sizeof(kCode) - 1, "target_upload", nullptr, "main", "cs_5_0",
                                               D3DCOMPILE_OPTIMIZATION_LEVEL3, &binary, &errors)) ||
                FAILED(
                    dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr, &shader)) ||
                FAILED(dev->CreateBuffer(&cb, nullptr, &constants))) {
                shader.Reset();
                failed = true;
                return false;
            }
        }
        D3D11_TEXTURE2D_DESC d{};
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_DEFAULT;
        if (!source || sourceW < w || sourceH < h) {
            d.Width = std::max(w, sourceW);
            d.Height = std::max(h, sourceH);
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            sourceView.Reset();
            source.Reset();
            sourceW = sourceH = 0;
            if (FAILED(dev->CreateTexture2D(&d, nullptr, &source)) ||
                FAILED(dev->CreateShaderResourceView(source.Get(), nullptr, &sourceView))) {
                source.Reset();
                return false;
            }
            sourceW = d.Width;
            sourceH = d.Height;
        }
        if (!expanded || expandedW < w * scale || expandedH < h * scale) {
            d.Width = std::max(w * scale, expandedW);
            d.Height = std::max(h * scale, expandedH);
            d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            expandedView.Reset();
            expanded.Reset();
            expandedW = expandedH = 0;
            if (FAILED(dev->CreateTexture2D(&d, nullptr, &expanded)) ||
                FAILED(dev->CreateUnorderedAccessView(expanded.Get(), nullptr, &expandedView))) {
                expanded.Reset();
                return false;
            }
            expandedW = d.Width;
            expandedH = d.Height;
        }
        return true;
    }

public:
    // Native rows per pass: bounds the scratch texture (a 512 pixel wide band at scale 8 is 4096 x 512 pixels).
    static constexpr uint32_t kBandRows = 64;

    // Compile and create the shader once, device only (start-up prewarm thread).
    static void prewarm(ID3D11Device* dev) {
        Ptr<ID3DBlob> binary;
        Ptr<ID3D11ComputeShader> made;
        if (SUCCEEDED(shader_compile::compile(kCode, sizeof(kCode) - 1, "target_upload", nullptr, "main", "cs_5_0",
                                              D3DCOMPILE_OPTIMIZATION_LEVEL3, &binary, nullptr)))
            dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr, &made);
    }
    // Write `pixels` (width x rows, RGBA8, native size) to the target rectangle at native (x, y), every pixel as a
    // scale x scale block. rows <= kBandRows. False = nothing was written (the caller takes the CPU path).
    // Only compute-stage bindings are touched and they are restored: an upload can interrupt a draw sequence.
    bool upload(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* target, const uint32_t* pixels,
                uint32_t x, uint32_t y, uint32_t width, uint32_t rows, uint32_t scale) {
        if (!width || !rows || rows > kBandRows || scale < 2 || !prepare(dev, width, rows, scale))
            return false;
        const D3D11_BOX native{0, 0, 0, width, rows, 1};
        ctx->UpdateSubresource(source.Get(), 0, &native, pixels, width * 4, 0);
        const uint32_t cb[] = {width, rows, scale, 0};
        ctx->UpdateSubresource(constants.Get(), 0, nullptr, cb, 0, 0);

        ID3D11ComputeShader* oldShader = nullptr;
        ID3D11Buffer* oldConstants = nullptr;
        ID3D11ShaderResourceView* oldSource = nullptr;
        ID3D11UnorderedAccessView* oldOutput = nullptr;
        ID3D11ClassInstance* classes[256]{};
        UINT classCount = 256;
        ctx->CSGetShader(&oldShader, classes, &classCount);
        ctx->CSGetConstantBuffers(0, 1, &oldConstants);
        ctx->CSGetShaderResources(0, 1, &oldSource);
        ctx->CSGetUnorderedAccessViews(0, 1, &oldOutput);

        auto* buffer = constants.Get();
        auto* srv = sourceView.Get();
        auto* uav = expandedView.Get();
        ctx->CSSetShader(shader.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &buffer);
        ctx->CSSetShaderResources(0, 1, &srv);
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ctx->Dispatch((width * scale + 7) / 8, (rows * scale + 7) / 8, 1);
        ID3D11UnorderedAccessView* nullOutput = nullptr;
        ctx->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
        const D3D11_BOX box{0, 0, 0, width * scale, rows * scale, 1};
        ctx->CopySubresourceRegion(target, 0, x * scale, y * scale, 0, expanded.Get(), 0, &box);

        ctx->CSSetShader(oldShader, classes, classCount);
        ctx->CSSetConstantBuffers(0, 1, &oldConstants);
        ctx->CSSetShaderResources(0, 1, &oldSource);
        ctx->CSSetUnorderedAccessViews(0, 1, &oldOutput, nullptr);
        for (UINT i = 0; i < classCount; ++i)
            if (classes[i])
                classes[i]->Release();
        if (oldShader)
            oldShader->Release();
        if (oldConstants)
            oldConstants->Release();
        if (oldSource)
            oldSource->Release();
        if (oldOutput)
            oldOutput->Release();
        return true;
    }
};
} // namespace gsr
