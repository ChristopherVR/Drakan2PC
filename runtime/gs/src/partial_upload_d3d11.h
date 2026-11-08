#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include "shader_compile_d3d.h"
#include <wrl/client.h>
#include <cstdint>

namespace gsr {
class PartialUploadGpu {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> shader;
    Ptr<ID3D11Buffer> constants, values;
    Ptr<ID3D11ShaderResourceView> input;
    Ptr<ID3D11Texture2D> texture;
    Ptr<ID3D11UnorderedAccessView> output;
    uint32_t scale = 0;

    static constexpr char kCode[] = R"(
Texture2D<float4> source : register(t0);
StructuredBuffer<uint2> patches : register(t1);
RWTexture2D<unorm float4> output : register(u0);
cbuffer Constants : register(b0) { uint x0, y0, scale, unused; };
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID) {
    if(any(p.xy>=8*scale))return;
    uint2 entry=patches[(p.y/scale)*8+p.x/scale];
    uint4 rgba=uint4(round(saturate(source.Load(int3(uint2(x0,y0)*scale+p.xy,0)))*255));
    uint raw=rgba.x|(rgba.y<<8)|(rgba.z<<16)|(rgba.w<<24);
    raw=(raw&~entry.x)|(entry.y&entry.x);
    output[p.xy]=float4(raw&255,(raw>>8)&255,(raw>>16)&255,raw>>24)/255.;
})";

public:
    // Compile and create the shader once, device only (start-up prewarm thread).
    static void prewarm(ID3D11Device* dev) {
        Ptr<ID3DBlob> binary;
        Ptr<ID3D11ComputeShader> made;
        if (SUCCEEDED(shader_compile::compile(kCode, sizeof(kCode) - 1, "partial_upload", nullptr, "main", "cs_5_0",
                                              D3DCOMPILE_OPTIMIZATION_LEVEL3, &binary, nullptr)))
            dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr, &made);
    }
    bool prepare(ID3D11Device* dev, uint32_t newScale) {
        if (!shader) {
            Ptr<ID3DBlob> binary, errors;
            if (FAILED(shader_compile::compile(kCode, sizeof(kCode) - 1, "partial_upload", nullptr, "main", "cs_5_0",
                                               D3DCOMPILE_OPTIMIZATION_LEVEL3, &binary, &errors)) ||
                FAILED(dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr, &shader)))
                return false;
            D3D11_BUFFER_DESC d{};
            d.ByteWidth = 16;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(dev->CreateBuffer(&d, nullptr, &constants))) {
                shader.Reset();
                return false;
            }
            d.ByteWidth = 64 * 8;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            d.StructureByteStride = 8;
            if (FAILED(dev->CreateBuffer(&d, nullptr, &values)) ||
                FAILED(dev->CreateShaderResourceView(values.Get(), nullptr, &input))) {
                shader.Reset();
                return false;
            }
        }
        if (!texture || newScale != scale) {
            output.Reset();
            texture.Reset();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = d.Height = 8 * newScale;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(dev->CreateTexture2D(&d, nullptr, &texture)) ||
                FAILED(dev->CreateUnorderedAccessView(texture.Get(), nullptr, &output))) {
                texture.Reset();
                return false;
            }
            scale = newScale;
        }
        return true;
    }
    // prepare() must succeed before the transfer is accepted. Preserve context
    // state because an image upload may interrupt an ordinary draw sequence.
    void patch(ID3D11DeviceContext* ctx, ID3D11Texture2D* target, ID3D11ShaderResourceView* source, uint32_t x,
               uint32_t y, const uint32_t* patches) {
        ID3D11RenderTargetView* targets[8]{};
        ID3D11DepthStencilView* depth = nullptr;
        ID3D11ComputeShader* oldShader = nullptr;
        ID3D11Buffer* oldConstants = nullptr;
        constexpr UINT kSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        ID3D11ShaderResourceView *oldSources[kSlots]{}, *oldPixelSources[kSlots]{}, *nulls[kSlots]{};
        ID3D11UnorderedAccessView* oldOutput = nullptr;
        ID3D11ClassInstance* classes[256]{};
        UINT classCount = 256;
        ctx->OMGetRenderTargets(8, targets, &depth);
        ctx->CSGetShader(&oldShader, classes, &classCount);
        ctx->CSGetConstantBuffers(0, 1, &oldConstants);
        ctx->CSGetShaderResources(0, kSlots, oldSources);
        ctx->PSGetShaderResources(0, kSlots, oldPixelSources);
        ctx->CSGetUnorderedAccessViews(0, 1, &oldOutput);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ctx->CSSetShaderResources(0, kSlots, nulls);
        ctx->PSSetShaderResources(0, kSlots, nulls);
        const uint32_t cb[] = {x, y, scale, 0};
        ctx->UpdateSubresource(constants.Get(), 0, nullptr, cb, 0, 0);
        ctx->UpdateSubresource(values.Get(), 0, nullptr, patches, 0, 0);
        auto* buffer = constants.Get();
        ID3D11ShaderResourceView* srvs[] = {source, input.Get()};
        auto* uav = output.Get();
        ctx->CSSetShader(shader.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(0, 1, &buffer);
        ctx->CSSetShaderResources(0, 2, srvs);
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ctx->Dispatch(scale, scale, 1);
        ID3D11UnorderedAccessView* nullOutput = nullptr;
        ctx->CSSetShaderResources(0, 2, nulls);
        ctx->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
        const D3D11_BOX box{0, 0, 0, 8 * scale, 8 * scale, 1};
        ctx->CopySubresourceRegion(target, 0, x * scale, y * scale, 0, texture.Get(), 0, &box);
        ctx->CSSetShader(oldShader, classes, classCount);
        ctx->CSSetConstantBuffers(0, 1, &oldConstants);
        ctx->CSSetShaderResources(0, kSlots, oldSources);
        ctx->PSSetShaderResources(0, kSlots, oldPixelSources);
        ctx->CSSetUnorderedAccessViews(0, 1, &oldOutput, nullptr);
        ctx->OMSetRenderTargets(8, targets, depth);
        for (auto* p : targets)
            if (p)
                p->Release();
        for (auto* p : oldSources)
            if (p)
                p->Release();
        for (auto* p : oldPixelSources)
            if (p)
                p->Release();
        for (UINT i = 0; i < classCount; ++i)
            if (classes[i])
                classes[i]->Release();
        if (depth)
            depth->Release();
        if (oldShader)
            oldShader->Release();
        if (oldConstants)
            oldConstants->Release();
        if (oldOutput)
            oldOutput->Release();
    }
};
} // namespace gsr
