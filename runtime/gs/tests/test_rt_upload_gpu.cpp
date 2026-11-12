// GPU expansion of native rows into a scaled render target (rt_upload_d3d11.h) against the CPU expansion it replaces
// (rt_upload.h): every texel of the target must be identical, the texels around the rectangle untouched, and the
// compute-stage bindings and the bound render target as they were.
#include "rt_upload.h"
#include "rt_upload_d3d11.h"

#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

static int g_fail = 0;
static void expectTrue(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what);
    }
}

int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        std::printf("no Direct3D 11 device\n");
        return 1;
    }
    // Bindings an upload must leave alone.
    const char keepCode[] = "[numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID) {}";
    ComPtr<ID3DBlob> keepBinary;
    ComPtr<ID3D11ComputeShader> keepShader;
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> keepConstants;
    if (FAILED(D3DCompile(keepCode, sizeof(keepCode) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0,
                          &keepBinary, nullptr)) ||
        FAILED(dev->CreateComputeShader(keepBinary->GetBufferPointer(), keepBinary->GetBufferSize(), nullptr,
                                        &keepShader)) ||
        FAILED(dev->CreateBuffer(&cb, nullptr, &keepConstants))) {
        std::printf("setup failed\n");
        return 1;
    }
    auto* keepBuffer = keepConstants.Get();
    ctx->CSSetShader(keepShader.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &keepBuffer);

    gsr::TargetUploadGpu upload;
    size_t checked = 0;
    struct Case {
        uint32_t targetW, targetH, x, y, w, h;
    };
    // a page row of a 128 wide scratch target, a block run inside a frame buffer, an odd rectangle, a full band
    const Case cases[] = {
        {128, 96, 0, 32, 128, 32}, {512, 64, 64, 8, 256, 8}, {70, 50, 3, 5, 37, 19}, {64, 64, 0, 0, 64, 64}};
    for (uint32_t scale = 2; scale <= 8; ++scale)
        for (const Case& c : cases) {
            const uint32_t W = c.targetW * scale, H = c.targetH * scale;
            std::vector<uint32_t> before(size_t(W) * H);
            for (size_t i = 0; i < before.size(); ++i)
                before[i] = uint32_t(i * 2246822519u) ^ 0x5a5a5a5au;
            std::vector<uint32_t> native(size_t(c.w) * c.h);
            for (size_t i = 0; i < native.size(); ++i)
                native[i] = uint32_t((i + scale) * 2654435761u); // every byte value, alpha included
            D3D11_TEXTURE2D_DESC d{};
            d.Width = W;
            d.Height = H;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
            d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA init{before.data(), W * 4, 0};
            ComPtr<ID3D11Texture2D> target, staging;
            ComPtr<ID3D11RenderTargetView> rtv;
            if (FAILED(dev->CreateTexture2D(&d, &init, &target)) ||
                FAILED(dev->CreateRenderTargetView(target.Get(), nullptr, &rtv))) {
                expectTrue(false, "target creation");
                continue;
            }
            auto* bound = rtv.Get();
            ctx->OMSetRenderTargets(1, &bound, nullptr); // an upload interrupts draws into the same target
            const bool done =
                upload.upload(dev.Get(), ctx.Get(), target.Get(), native.data(), c.x, c.y, c.w, c.h, scale);
            expectTrue(done, "upload accepted");
            ComPtr<ID3D11ComputeShader> shaderNow;
            ComPtr<ID3D11Buffer> constantsNow;
            ComPtr<ID3D11RenderTargetView> targetNow;
            ComPtr<ID3D11ShaderResourceView> sourceNow;
            ComPtr<ID3D11UnorderedAccessView> outputNow;
            ctx->CSGetShader(&shaderNow, nullptr, nullptr);
            ctx->CSGetConstantBuffers(0, 1, &constantsNow);
            ctx->CSGetShaderResources(0, 1, &sourceNow);
            ctx->CSGetUnorderedAccessViews(0, 1, &outputNow);
            ctx->OMGetRenderTargets(1, &targetNow, nullptr);
            expectTrue(shaderNow.Get() == keepShader.Get() && constantsNow.Get() == keepConstants.Get() && !sourceNow &&
                           !outputNow,
                       "compute bindings restored");
            expectTrue(targetNow.Get() == bound, "render target still bound");

            std::vector<uint32_t> expected = before, block(size_t(c.w) * scale * c.h * scale);
            gsr::expandRenderTargetRows(native.data(), block.data(), c.w, c.h, scale);
            for (uint32_t y = 0; y < c.h * scale; ++y)
                for (uint32_t x = 0; x < c.w * scale; ++x)
                    expected[size_t(c.y * scale + y) * W + c.x * scale + x] = block[size_t(y) * c.w * scale + x];
            d.BindFlags = 0;
            d.Usage = D3D11_USAGE_STAGING;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            D3D11_MAPPED_SUBRESOURCE map{};
            if (FAILED(dev->CreateTexture2D(&d, nullptr, &staging))) {
                expectTrue(false, "staging creation");
                continue;
            }
            ctx->CopyResource(staging.Get(), target.Get());
            if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) {
                expectTrue(false, "map");
                continue;
            }
            size_t wrong = 0;
            for (uint32_t y = 0; y < H; ++y) {
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map.pData) +
                                                                    size_t(y) * map.RowPitch);
                for (uint32_t x = 0; x < W; ++x, ++checked)
                    wrong += row[x] != expected[size_t(y) * W + x];
            }
            ctx->Unmap(staging.Get(), 0);
            if (wrong) {
                ++g_fail;
                std::printf("  [FAIL] scale %u target %ux%u rect %u,%u %ux%u: %zu texels differ\n", scale, c.targetW,
                            c.targetH, c.x, c.y, c.w, c.h, wrong);
            }
        }
    const uint32_t tooTall = 0;
    expectTrue(!upload.upload(dev.Get(), ctx.Get(), nullptr, &tooTall, 0, 0, 1, gsr::TargetUploadGpu::kBandRows + 1, 2),
               "more rows than one band are refused");
    expectTrue(!upload.upload(dev.Get(), ctx.Get(), nullptr, &tooTall, 0, 0, 1, 1, 1), "scale 1 is refused");
    std::printf("GPU target upload: %zu texels compared with the CPU expansion, scales 2..8, %d failed\n", checked,
                g_fail);
    return g_fail ? 1 : 0;
}
