#pragma once
// Direct3D 11 side of surface_gpu.h: compiles the kernel (surface_gpu_hlsl.h) and dispatches it over the streamed
// draws of one world into a typed R32_UINT UAV laid out as GpuSurfaceLayout (the buffer the ray pass shares).
#include "shader_compile_d3d.h"
#include "surface_gpu.h"
#include "surface_gpu_hlsl.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gsr::surface {

class SurfaceGpuKernel {
    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Constants {
        float origin[3];
        uint32_t textures;
        float packedMaterial;
        uint32_t cellCount, drawCount, attributeOffset;
        uint32_t observeCapacity, pad[3];
    };
    static_assert(sizeof(Constants) == 48);
    static constexpr uint32_t kObserveCapacity = 8192;
    struct Structured {
        Ptr<ID3D11Buffer> buffer;
        Ptr<ID3D11ShaderResourceView> srv;
        uint32_t capacity = 0; // elements
    };
    Ptr<ID3D11ComputeShader> shader, alphaShader;
    Ptr<ID3D11Buffer> drawAlpha;
    Ptr<ID3D11UnorderedAccessView> drawAlphaUav;
    std::vector<std::array<uint32_t, 2>> chunkList;
    Ptr<ID3D11Buffer> constants, counters, counterStaging[4], observes, observeStaging[4];
    Ptr<ID3D11UnorderedAccessView> counterUav, observeUav;
    Structured draws, cameras, cells, entries, chunks;
    uint64_t matchSerial = ~0ull;
    unsigned counterNext = 0;
    bool counterPending[4]{};
    std::string error;
    bool failed = false;

    static bool compileShader(ID3D11Device* dev, Ptr<ID3DBlob>& binary, std::string* why, bool fastDivide,
                              const char* entry = "main") {
        Ptr<ID3DBlob> errors;
        const D3D_SHADER_MACRO macros[] = {{"SURFACE_FAST_DIV", fastDivide ? "1" : "0"}, {nullptr, nullptr}};
        const HRESULT hr = shader_compile::compile(
            kSurfaceGpuHlsl, sizeof(kSurfaceGpuHlsl) - 1, "surface_gpu", macros, entry, "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS, &binary, &errors);
        if (FAILED(hr) && why)
            *why = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                          : "compile failed";
        (void)dev;
        return SUCCEEDED(hr);
    }
    template <class T>
    bool upload(ID3D11Device* dev, ID3D11DeviceContext* ctx, Structured& s, const T* data, size_t count) {
        const uint32_t want = uint32_t(std::max<size_t>(count, 1));
        if (!s.buffer || s.capacity < want) {
            s = {};
            const uint32_t capacity = want + want / 2 + 16;
            D3D11_BUFFER_DESC d{};
            d.ByteWidth = UINT(capacity * sizeof(T));
            d.Usage = D3D11_USAGE_DYNAMIC;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            d.StructureByteStride = sizeof(T);
            if (FAILED(dev->CreateBuffer(&d, nullptr, &s.buffer)) ||
                FAILED(dev->CreateShaderResourceView(s.buffer.Get(), nullptr, &s.srv)))
                return false;
            s.capacity = capacity;
        }
        if (!count)
            return true;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(s.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            return false;
        std::memcpy(m.pData, data, count * sizeof(T));
        ctx->Unmap(s.buffer.Get(), 0);
        return true;
    }

public:
    // The kernel needs the double-precision division of Direct3D 11.1 (ExtendedDoublesShaderInstructions).
    static bool supported(ID3D11Device* dev) {
        D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
        D3D11_FEATURE_DATA_DOUBLES d{};
        return dev && SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &d, sizeof(d))) &&
               d.DoublePrecisionFloatShaderOps &&
               SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof(o))) &&
               o.ExtendedDoublesShaderInstructions;
    }
    // Compile into the blob cache only (start-up prewarm thread).
    static void prewarm(ID3D11Device* dev, bool fastDivide) {
        Ptr<ID3DBlob> binary, alpha;
        if (supported(dev)) {
            compileShader(dev, binary, nullptr, fastDivide);
            compileShader(dev, alpha, nullptr, fastDivide, "drawAlpha");
        }
    }
    // GSR_SURFACE_GPU_FAST_DIV: set before init.
    bool fastDivide = false;
    bool ready() const { return shader && !failed; }
    const std::string& why() const { return error; }
    bool init(ID3D11Device* dev) {
        if (shader || failed)
            return ready();
        if (!supported(dev)) {
            failed = true;
            error = "the adapter has no double-precision division (Direct3D 11.1 extended doubles)";
            return false;
        }
        Ptr<ID3DBlob> binary, alpha;
        if (!compileShader(dev, binary, &error, fastDivide) ||
            !compileShader(dev, alpha, &error, fastDivide, "drawAlpha") ||
            FAILED(dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr, &shader)) ||
            FAILED(
                dev->CreateComputeShader(alpha->GetBufferPointer(), alpha->GetBufferSize(), nullptr, &alphaShader))) {
            failed = true;
            if (error.empty())
                error = "the compute shader could not be created";
            shader.Reset();
            return false;
        }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(Constants);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_BUFFER_DESC counter{};
        counter.ByteWidth = 16;
        counter.Usage = D3D11_USAGE_DEFAULT;
        counter.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_R32_UINT;
        uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 4;
        D3D11_BUFFER_DESC staging = counter;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        bool ok = SUCCEEDED(dev->CreateBuffer(&cb, nullptr, &constants)) &&
                  SUCCEEDED(dev->CreateBuffer(&counter, nullptr, &counters)) &&
                  SUCCEEDED(dev->CreateUnorderedAccessView(counters.Get(), &uav, &counterUav));
        for (auto& s : counterStaging)
            ok = ok && SUCCEEDED(dev->CreateBuffer(&staging, nullptr, &s));
        D3D11_BUFFER_DESC records = counter;
        records.ByteWidth = kObserveCapacity * sizeof(GpuObserve);
        D3D11_UNORDERED_ACCESS_VIEW_DESC recordView = uav;
        recordView.Buffer.NumElements = records.ByteWidth / 4;
        ok = ok && SUCCEEDED(dev->CreateBuffer(&records, nullptr, &observes)) &&
             SUCCEEDED(dev->CreateUnorderedAccessView(observes.Get(), &recordView, &observeUav));
        records.Usage = D3D11_USAGE_STAGING;
        records.BindFlags = 0;
        records.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& s : observeStaging)
            ok = ok && SUCCEEDED(dev->CreateBuffer(&records, nullptr, &s));
        D3D11_BUFFER_DESC alphas = counter;
        alphas.ByteWidth = 65536 * 4;
        D3D11_UNORDERED_ACCESS_VIEW_DESC alphaView = uav;
        alphaView.Buffer.NumElements = 65536;
        ok = ok && SUCCEEDED(dev->CreateBuffer(&alphas, nullptr, &drawAlpha)) &&
             SUCCEEDED(dev->CreateUnorderedAccessView(drawAlpha.Get(), &alphaView, &drawAlphaUav));
        if (!ok) {
            failed = true;
            error = "the kernel's buffers could not be created";
            shader.Reset();
        }
        return ok;
    }
    // One world: `slots` output triangles of `draws` (their slot fields set), `stream` = the raw vertex stream as a
    // typed R32_UINT view, `out` = an R32_UINT texture view of at least layout.rows() rows (createOutputTexture).
    bool dispatch(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* stream,
                  const std::vector<GpuDraw>& drawList, const std::vector<GpuCamera>& cameraList,
                  const GpuMatchTable& match, ID3D11UnorderedAccessView* out, const GpuSurfaceLayout& layout,
                  Point origin, uint32_t textures, float packedMaterial) {
        if (!ready() || drawList.empty() || drawList.size() > 65535)
            return false;
        // 64 triangles of one draw per group of the second pass (one thread per triangle).
        chunkList.clear();
        for (uint32_t k = 0; k < drawList.size(); ++k)
            for (uint32_t t = 0; t * 3 < drawList[k].count; t += 64)
                chunkList.push_back({k, t});
        if (chunkList.empty() || chunkList.size() > 65535u)
            return false;
        if (!upload(dev, ctx, draws, drawList.data(), drawList.size()) ||
            !upload(dev, ctx, cameras, cameraList.data(), cameraList.size()) ||
            !upload(dev, ctx, chunks, chunkList.data(), chunkList.size()))
            return false;
        if (match.serial != matchSerial) {
            if (!upload(dev, ctx, cells, match.cells.data(), match.cells.size()) ||
                !upload(dev, ctx, entries, match.entries.data(), match.entries.size()))
                return false;
            matchSerial = match.serial;
        }
        const Constants k{{origin.x, origin.y, origin.z},
                          textures,
                          packedMaterial,
                          uint32_t(match.cells.size()),
                          uint32_t(drawList.size()),
                          uint32_t(layout.attributeOffset() / 4),
                          kObserveCapacity,
                          {0, 0, 0}};
        ctx->UpdateSubresource(constants.Get(), 0, nullptr, &k, 0, 0);
        const UINT zero[4] = {0, 0, 0, 0};
        ctx->ClearUnorderedAccessViewUint(counterUav.Get(), zero);
        ID3D11ShaderResourceView* srvs[6] = {stream,          draws.srv.Get(),   cameras.srv.Get(),
                                             cells.srv.Get(), entries.srv.Get(), chunks.srv.Get()};
        ID3D11UnorderedAccessView* uavs[4] = {out, counterUav.Get(), observeUav.Get(), drawAlphaUav.Get()};
        ctx->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
        ctx->CSSetShaderResources(0, 6, srvs);
        ctx->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
        ctx->CSSetShader(alphaShader.Get(), nullptr, 0); // per draw: its minimum vertex alpha
        ctx->Dispatch(UINT(drawList.size()), 1, 1);
        ctx->CSSetShader(shader.Get(), nullptr, 0); // per triangle
        ctx->Dispatch(UINT(chunkList.size()), 1, 1);
        ID3D11ShaderResourceView* noSrvs[6]{};
        ID3D11UnorderedAccessView* noUavs[4]{};
        ctx->CSSetShaderResources(0, 6, noSrvs);
        ctx->CSSetUnorderedAccessViews(0, 4, noUavs, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
        // The observe records and counters of this world, read some worlds later without waiting (countersLate). The
        // records are copied first: once the counters' copy is done, theirs is too.
        ctx->CopyResource(observeStaging[counterNext].Get(), observes.Get());
        auto& staging = counterStaging[counterNext];
        ctx->CopyResource(staging.Get(), counters.Get());
        counterPending[counterNext] = true;
        counterNext = (counterNext + 1) % 4;
        return true;
    }
    // The results of an earlier world whose copy the GPU has finished: {lost observes, emitted triangles} and the
    // observe records (sorted by slot, the CPU path's order; `overflow` counts those that did not fit). Never waits.
    bool countersLate(ID3D11DeviceContext* ctx, uint32_t& lost, uint32_t& emitted,
                      std::vector<GpuObserve>* records = nullptr, uint32_t* overflow = nullptr) {
        for (unsigned k = 1; k <= 4; ++k) {
            const unsigned i = (counterNext + k) % 4; // the oldest first
            if (!counterPending[i])
                continue;
            return readResults(ctx, i, D3D11_MAP_FLAG_DO_NOT_WAIT, lost, emitted, records, overflow);
        }
        return false;
    }
    // GSR_SURFACE_GPU_VERIFY and the tests: the results of the last dispatch, waiting for the GPU.
    bool countersNow(ID3D11DeviceContext* ctx, uint32_t& lost, uint32_t& emitted,
                     std::vector<GpuObserve>* records = nullptr, uint32_t* overflow = nullptr) {
        return readResults(ctx, (counterNext + 3) % 4, 0, lost, emitted, records, overflow);
    }

    // The tests: fdiv of `pairs` (a, b as float bits) on the GPU, waiting for it. `out` = an output texture view.
    bool divide(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* pairs, uint32_t count,
                ID3D11UnorderedAccessView* out) {
        if (!ready())
            return false;
        if (!divisionShader) {
            Ptr<ID3DBlob> binary;
            if (!compileShader(dev, binary, &error, fastDivide, "divisionTest") ||
                FAILED(dev->CreateComputeShader(binary->GetBufferPointer(), binary->GetBufferSize(), nullptr,
                                                &divisionShader)))
                return false;
        }
        const Constants k{{0, 0, 0}, 0, 0, 0, count, 0, 0, {0, 0, 0}};
        ctx->UpdateSubresource(constants.Get(), 0, nullptr, &k, 0, 0);
        ctx->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
        ctx->CSSetShaderResources(0, 1, &pairs);
        ctx->CSSetUnorderedAccessViews(0, 1, &out, nullptr);
        ctx->CSSetShader(divisionShader.Get(), nullptr, 0);
        ctx->Dispatch((count + 63) / 64, 1, 1);
        ID3D11ShaderResourceView* noSrv = nullptr;
        ID3D11UnorderedAccessView* noUav = nullptr;
        ctx->CSSetShaderResources(0, 1, &noSrv);
        ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
        return true;
    }

private:
    Ptr<ID3D11ComputeShader> divisionShader;
    bool readResults(ID3D11DeviceContext* ctx, unsigned i, UINT flags, uint32_t& lost, uint32_t& emitted,
                     std::vector<GpuObserve>* records, uint32_t* overflow) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (ctx->Map(counterStaging[i].Get(), 0, D3D11_MAP_READ, flags, &m) != S_OK)
            return false;
        const auto* v = static_cast<const uint32_t*>(m.pData);
        lost = v[0];
        emitted = v[1];
        const uint32_t found = v[2];
        ctx->Unmap(counterStaging[i].Get(), 0);
        counterPending[i] = false;
        const uint32_t kept = std::min(found, kObserveCapacity);
        if (overflow)
            *overflow = found - kept;
        if (records) {
            records->clear();
            if (kept && SUCCEEDED(ctx->Map(observeStaging[i].Get(), 0, D3D11_MAP_READ, 0, &m))) {
                const auto* r = static_cast<const GpuObserve*>(m.pData);
                records->assign(r, r + kept);
                ctx->Unmap(observeStaging[i].Get(), 0);
                std::stable_sort(records->begin(), records->end(),
                                 [](const GpuObserve& a, const GpuObserve& b) { return a.slot < b.slot; });
            }
        }
        return true;
    }

public:
};

// The output texture of a layout (R32_UINT, kRowWords wide), for the kernel's typed UAV.
inline bool createOutputTexture(ID3D11Device* dev, const GpuSurfaceLayout& layout, UINT miscFlags,
                                Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
                                Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& view) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = GpuSurfaceLayout::kRowWords;
    d.Height = std::max<UINT>(layout.rows(), 1);
    d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R32_UINT;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = miscFlags;
    return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &texture)) &&
           SUCCEEDED(dev->CreateUnorderedAccessView(texture.Get(), nullptr, &view));
}

// Reads the first `words` words of an output texture back (verification and tests only: waits for the GPU).
inline bool readWordsNow(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* source, uint64_t words,
                         std::vector<uint32_t>& out) {
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    const uint64_t rows = (words + GpuSurfaceLayout::kRowWords - 1) / GpuSurfaceLayout::kRowWords;
    if (!words || d.Width != GpuSurfaceLayout::kRowWords || rows > d.Height)
        return false;
    d.Height = UINT(rows);
    d.BindFlags = d.MiscFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &staging)))
        return false;
    D3D11_BOX box{0, 0, 0, d.Width, d.Height, 1};
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, 0, &box);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m)))
        return false;
    out.assign(size_t(rows) * GpuSurfaceLayout::kRowWords, 0);
    for (uint64_t r = 0; r < rows; ++r)
        std::memcpy(out.data() + r * GpuSurfaceLayout::kRowWords, static_cast<const uint8_t*>(m.pData) + r * m.RowPitch,
                    GpuSurfaceLayout::kRowWords * 4);
    ctx->Unmap(staging.Get(), 0);
    out.resize(size_t(words));
    return true;
}

// Compares the kernel's output slots with the CPU path's triangles. `expected[slot]` is the triangle the CPU would
// have appended for that slot, or null when it appends none. Returns the number of slots that differ.
inline uint64_t compareSlots(const std::vector<uint32_t>& words, const GpuSurfaceLayout& layout,
                             const std::vector<const Triangle*>& expected, Point origin, size_t textures,
                             uint64_t* active = nullptr, std::string* first = nullptr) {
    uint64_t differ = 0;
    const size_t attributeWord = size_t(layout.attributeOffset() / 4);
    for (uint32_t slot = 0; slot < layout.slots && slot < expected.size(); ++slot) {
        const uint32_t* position = words.data() + size_t(slot) * 9;
        const uint32_t* attribute = words.data() + attributeWord + size_t(slot) * kTriangleAttributes * 4;
        bool same = true;
        if (!expected[slot]) {
            float x;
            std::memcpy(&x, position, 4);
            same = std::isnan(x);
        } else {
            if (active)
                ++*active;
            float positions[9];
            Attribute attributes[kTriangleAttributes];
            packFrameTriangle(*expected[slot], origin, textures, positions, attributes);
            same = std::memcmp(position, positions, sizeof(positions)) == 0 &&
                   std::memcmp(attribute, attributes, sizeof(attributes)) == 0;
        }
        if (!same) {
            if (!differ && first) {
                char b[512];
                std::snprintf(b, sizeof(b), "slot %u: expected %s, GPU x0=%08x y0=%08x z0=%08x a0=%08x %08x %08x %08x",
                              slot, expected[slot] ? "a triangle" : "none", position[0], position[1], position[2],
                              attribute[0], attribute[1], attribute[2], attribute[3]);
                *first = b;
                if (expected[slot]) {
                    float positions[9];
                    Attribute attributes[kTriangleAttributes];
                    packFrameTriangle(*expected[slot], origin, textures, positions, attributes);
                    uint32_t w[9 + 28];
                    std::memcpy(w, positions, sizeof(positions));
                    std::memcpy(w + 9, attributes, sizeof(attributes));
                    std::string detail = "; differing words:";
                    for (unsigned i = 0; i < 37; ++i) {
                        const uint32_t got = i < 9 ? position[i] : attribute[i - 9];
                        if (got != w[i]) {
                            char e[64];
                            std::snprintf(e, sizeof(e), " [%u] cpu %08x gpu %08x", i, w[i], got);
                            detail += e;
                        }
                    }
                    *first += detail;
                }
            }
            ++differ;
        }
    }
    return differ;
}

} // namespace gsr::surface
