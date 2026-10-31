#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>

// Isolated transport prototype. Caller owns Streamline and command-list lifetime.
// Both APIs see COMMON outside the D3D12 evaluation interval. Two fence timelines
// prevent one API's completion signal from satisfying the other's pending work.
class NrInterop {
public:
    NrInterop(ID3D12Device* device, UINT width, UINT height, ID3D11Device* rendererDevice = nullptr);
    ID3D12Resource* texture(unsigned index) const { return textures12.at(index).Get(); }
    ID3D11Device* device11() const { return d11.Get(); }
    ID3D11DeviceContext4* context11() const { return context.Get(); }
    void produce(ID3D12CommandQueue* queue, const std::array<ID3D11Texture2D*, 4>& inputs);
    void consume(ID3D12CommandQueue* queue, ID3D11Texture2D* output);
    // Split form for a D3D12 queue fed from another thread. The D3D11 halves run on the context's thread:
    // produce11 copies the inputs and returns the fence value the D3D12 queue must wait12 for before reading them;
    // reserve11 validates the output and returns the value the queue will signal12 after writing it; consume11 makes
    // later D3D11 work wait on the GPU for that value, then copies the output. Hand the reserved value to the thread
    // that signals it before calling consume11: some drivers block the CPU on a D3D11 wait for a shared fence.
    // Values are consumed in increasing order.
    UINT64 produce11(const std::array<ID3D11Texture2D*, 4>& inputs);
    void wait12(ID3D12CommandQueue* queue, UINT64 value);
    UINT64 reserve11(ID3D11Texture2D* output);
    void consume11(ID3D11Texture2D* output, UINT64 value);
    void signal12(ID3D12CommandQueue* queue, UINT64 value);
    // Recovery from a signal that never arrives: raises both D3D12 fences from the CPU to at least these values, which
    // releases every D3D11 and D3D12 wait on them. Touches only the D3D12 fences, so any thread may call it, as may
    // the completed-value queries.
    void release12(UINT64 produced, UINT64 consumed);
    UINT64 producedCompleted() const { return produced12->GetCompletedValue(); }
    UINT64 consumedCompleted() const { return consumed12->GetCompletedValue(); }

private:
    void validateOutput(ID3D11Texture2D* output) const;
    Microsoft::WRL::ComPtr<ID3D11Device5> d11;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 4> textures11;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 4> textures12;
    Microsoft::WRL::ComPtr<ID3D12Fence> produced12, consumed12;
    Microsoft::WRL::ComPtr<ID3D11Fence> produced11, consumed11;
    UINT64 produced = 0, consumed = 0;
};
