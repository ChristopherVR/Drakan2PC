#include "nr_interop.h"
#include <dxgi1_4.h>
#include <stdexcept>
#include <cstdio>
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr) {
    if (FAILED(hr)) {
        std::printf("NR interop HRESULT=0x%08lx\n", (unsigned long)hr);
        throw std::runtime_error("D3D11/D3D12 interop operation failed");
    }
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
};
} // namespace
NrInterop::NrInterop(ID3D12Device* device, UINT width, UINT height, ID3D11Device* rendererDevice) {
    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;
    check(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D11Device> base;
    ComPtr<ID3D11DeviceContext> baseContext;
    if (rendererDevice) {
        base = rendererDevice;
        base->GetImmediateContext(&baseContext);
        ComPtr<IDXGIDevice> dxgi;
        check(base.As(&dxgi));
        ComPtr<IDXGIAdapter> actual;
        check(dxgi->GetAdapter(&actual));
        DXGI_ADAPTER_DESC desc{};
        check(actual->GetDesc(&desc));
        const LUID expected = device->GetAdapterLuid();
        if (desc.AdapterLuid.LowPart != expected.LowPart || desc.AdapterLuid.HighPart != expected.HighPart)
            throw std::runtime_error("NR requires the renderer's same adapter LUID");
    } else
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                &base, nullptr, &baseContext));
    check(base.As(&d11));
    check(baseContext.As(&context));
    const DXGI_FORMAT formats[]{DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_FLOAT};
    for (unsigned k = 0; k < 4; ++k) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = formats[k];
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(d11->CreateTexture2D(&desc, nullptr, &textures11[k]));
        ComPtr<IDXGIResource1> resource;
        check(textures11[k].As(&resource));
        Handle shared;
        check(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                           &shared.value));
        check(device->OpenSharedHandle(shared.value, IID_PPV_ARGS(&textures12[k])));
    }
    auto fence = [&](ComPtr<ID3D12Fence>& f12, ComPtr<ID3D11Fence>& f11) {
        check(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f12)));
        Handle shared;
        check(device->CreateSharedHandle(f12.Get(), nullptr, GENERIC_ALL, nullptr, &shared.value));
        check(d11->OpenSharedFence(shared.value, IID_PPV_ARGS(&f11)));
    };
    fence(produced12, produced11);
    fence(consumed12, consumed11);
    std::puts("NR interop: same-LUID D3D11Device5/Context4, four shared textures and two shared fences created");
}
void NrInterop::produce(ID3D12CommandQueue* queue, const std::array<ID3D11Texture2D*, 4>& inputs) {
    wait12(queue, produce11(inputs));
}
UINT64 NrInterop::produce11(const std::array<ID3D11Texture2D*, 4>& inputs) {
    for (unsigned k = 0; k < 4; ++k)
        if (inputs[k]) {
            ComPtr<ID3D11Device> owner;
            inputs[k]->GetDevice(&owner);
            ComPtr<ID3D11Device> expected;
            check(d11.As(&expected));
            if (owner.Get() != expected.Get())
                throw std::runtime_error("NR source belongs to another D3D11 device");
            D3D11_TEXTURE2D_DESC a{}, b{};
            inputs[k]->GetDesc(&a);
            textures11[k]->GetDesc(&b);
            if (a.Width != b.Width || a.Height != b.Height || a.Format != b.Format || a.SampleDesc.Count != 1 ||
                a.MipLevels != 1 || a.ArraySize != 1)
                throw std::runtime_error("NR transport requires matching single-subresource formats and dimensions");
            context->CopyResource(textures11[k].Get(), inputs[k]);
        }
    check(context->Signal(produced11.Get(), ++produced));
    context->Flush();
    return produced;
}
void NrInterop::wait12(ID3D12CommandQueue* queue, UINT64 value) {
    check(queue->Wait(produced12.Get(), value));
}
void NrInterop::signal12(ID3D12CommandQueue* queue, UINT64 value) {
    check(queue->Signal(consumed12.Get(), value));
}
UINT64 NrInterop::consume11(ID3D11Texture2D* output) {
    validateOutput(output);
    // The value may not be signaled yet: the GPU holds later D3D11 work until the D3D12 queue signals it. No Flush
    // here; the next fence signal or present submits it.
    check(context->Wait(consumed11.Get(), ++consumed));
    context->CopyResource(output, textures11[1].Get());
    return consumed;
}
void NrInterop::consume(ID3D12CommandQueue* queue, ID3D11Texture2D* output) {
    validateOutput(output);
    check(queue->Signal(consumed12.Get(), ++consumed));
    check(context->Wait(consumed11.Get(), consumed));
    context->CopyResource(output, textures11[1].Get());
    context->Flush();
}
void NrInterop::validateOutput(ID3D11Texture2D* output) const {
    ComPtr<ID3D11Device> owner;
    output->GetDevice(&owner);
    ComPtr<ID3D11Device> expected;
    check(d11.As(&expected));
    D3D11_TEXTURE2D_DESC a{}, b{};
    output->GetDesc(&a);
    textures11[1]->GetDesc(&b);
    if (owner.Get() != expected.Get() || a.Width != b.Width || a.Height != b.Height || a.Format != b.Format ||
        a.SampleDesc.Count != 1 || a.MipLevels != 1 || a.ArraySize != 1)
        throw std::runtime_error("NR output must match the renderer device, format and dimensions");
}
