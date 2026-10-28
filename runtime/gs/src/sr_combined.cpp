#include "sr_combined.h"
#include "temporal.h" // existing Upscaler interface and createDlssUpscaler factory
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace gsr::sr_combined {
namespace {
const char* kShader = R"HLSL(
cbuffer Params : register(b0) {
    uint4 sizes; // render xy, output zw
    float4 jitterFlags; // current raster jitter xy; z=SR succeeded; w=alphaFull available
    uint4 flags; // x=history reset; y=AlphaRender available
};
Texture2D<float4> InputColor : register(t0);
Texture2D<float> InputDepth : register(t1);
Texture2D<float2> InputMotion : register(t2);
Texture2D<float4> Resolved : register(t3);
Texture2D<float4> AlphaFull : register(t4);
Texture2D<float4> AlphaRender : register(t5);
SamplerState LinearClamp : register(s0);
RWTexture2D<float4> FullColor : register(u0);
RWTexture2D<float> FullDepth : register(u1);
RWTexture2D<float2> FullMotion : register(u2);
float halfSafe(float x) { return isfinite(x)?clamp(x,-65504.0,65504.0):0; }
[numthreads(8,8,1)]
void CSConvert(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=sizes.zw)) return;
    float2 ratio=float2(sizes.zw)/float2(sizes.xy);
    // DLSS resolves onto an unjittered output grid. Pull depth AND full motion
    // from one nearest raster sample at the corresponding jittered input point.
    float2 source=(float2(id.xy)+.5)/ratio+jitterFlags.xy;
    int2 p=clamp(int2(floor(source)),int2(0,0),int2(sizes.xy)-1);
    float4 original=flags.y!=0 ? AlphaRender.Load(int3(p,0)) : InputColor.Load(int3(p,0));
    float4 c=jitterFlags.z!=0 ? Resolved.Load(int3(id.xy,0)) :
        InputColor.SampleLevel(LinearClamp,source/float2(sizes.xy),0);
    float a=jitterFlags.w!=0 ? AlphaFull.Load(int3(id.xy,0)).a : original.a;
    FullColor[id.xy]=float4(halfSafe(c.r),halfSafe(c.g),halfSafe(c.b),halfSafe(a));
    float d=InputDepth.Load(int3(p,0));
    bool valid=isfinite(d)&&d>0&&d<=1;
    FullDepth[id.xy]=valid?d:0;
    float2 mv=InputMotion.Load(int3(p,0))*ratio;
    if(!valid||flags.x!=0||!all(isfinite(mv))||any(abs(mv)>65504.0)) mv=0;
    FullMotion[id.xy]=mv;
}
struct VOut {float4 p:SV_Position;};
VOut VSCompose(uint id:SV_VertexID) {
    VOut o; float2 p=float2((id<<1)&2,id&2);
    o.p=float4(p*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 PSCompose(VOut i):SV_Target {
    int2 p=int2(i.p.xy), edge=int2(sizes.zw)-1;
    float4 c=InputColor.Load(int3(p,0));
    if(jitterFlags.x>0) {
        float3 n=InputColor.Load(int3(clamp(p+int2(0,-1),0,edge),0)).rgb;
        float3 s=InputColor.Load(int3(clamp(p+int2(0,1),0,edge),0)).rgb;
        float3 w=InputColor.Load(int3(clamp(p+int2(-1,0),0,edge),0)).rgb;
        float3 e=InputColor.Load(int3(clamp(p+int2(1,0),0,edge),0)).rgb;
        float3 lo=min(c.rgb,min(min(n,s),min(w,e)));
        float3 hi=max(c.rgb,max(max(n,s),max(w,e)));
        c.rgb=clamp(c.rgb+(c.rgb-(n+s+w+e)*.25)*jitterFlags.x,max(0,lo-.1),hi+.1);
    }
    return float4(halfSafe(c.r),halfSafe(c.g),halfSafe(c.b),0);
}
)HLSL";
struct alignas(16) Params {
    uint32_t sizes[4]{};
    float jitterFlags[4]{};
    uint32_t flags[4]{};
};
static_assert(sizeof(Params) == 48);
struct Target {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Texture borrow() const { return {tex.Get(), srv.Get(), uav.Get()}; }
};
struct Scope {
    ID3D11DeviceContext1* ctx;
    ComPtr<ID3DDeviceContextState> saved;
    Scope(ID3D11DeviceContext1* c, ID3DDeviceContextState* state) : ctx(c) {
        ctx->SwapDeviceContextState(state, &saved);
        ctx->ClearState();
    }
    ~Scope() {
        ctx->ClearState();
        ctx->SwapDeviceContextState(saved.Get(), nullptr);
    }
};
bool same(Size a, Size b) {
    return a.width == b.width && a.height == b.height;
}
bool same(const Config& a, const Config& b) {
    return same(a.render, b.render) && same(a.output, b.output) && a.mode == b.mode && a.preset == b.preset;
}
bool valid(const Config& c) {
    return c.render.width && c.render.height && c.output.width && c.output.height && c.render.width <= c.output.width &&
           c.render.height <= c.output.height && c.output.width <= 16384 && c.output.height <= 16384 && c.mode >= 1 &&
           c.mode <= 4;
}
} // namespace
const char* shaderSource() {
    return kShader;
}
struct Pipeline::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> ctx;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> rgbOnly;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11SamplerState> linear;
    Target rawSr, color, depth, motion, nrOutput;
    std::unique_ptr<Upscaler> up;
    Config config{};
    bool initialized = false, configured = false, hasOutput = false, forceReset = true;
    bool lastReset = true, lastSr = false, haveFrame = false;
    uint64_t lastFrame = 0;
    std::string status, featureStatus;
    bool fail(const char* text) {
        status = text;
        return false;
    }
    bool compile(const char* entry, const char* profile, ID3DBlob** blob) {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr =
            D3DCompile(kShader, std::strlen(kShader), "sr_combined", nullptr, nullptr, entry, profile,
                       D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                       0, blob, &errors);
        if (FAILED(hr)) {
            status = std::string(entry) + ": " +
                     (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed");
            return false;
        }
        return true;
    }
    bool make(Target& target, Size size, DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = size.width;
        d.Height = size.height;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = format;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        return SUCCEEDED(device->CreateTexture2D(&d, nullptr, &target.tex)) &&
               SUCCEEDED(device->CreateShaderResourceView(target.tex.Get(), nullptr, &target.srv)) &&
               SUCCEEDED(device->CreateUnorderedAccessView(target.tex.Get(), nullptr, &target.uav));
    }
    bool source(ID3D11ShaderResourceView* srv, Size size, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& tex,
                bool alpha = false) {
        if (!srv)
            return false;
        ComPtr<ID3D11Device> owner;
        srv->GetDevice(&owner);
        if (owner.Get() != device.Get())
            return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        srv->GetDesc(&v);
        if (v.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || v.Texture2D.MostDetailedMip != 0)
            return false;
        ComPtr<ID3D11Resource> r;
        srv->GetResource(&r);
        if (FAILED(r.As(&tex)))
            return false;
        for (Target* t : {&rawSr, &color, &depth, &motion, &nrOutput})
            if (tex.Get() == t->tex.Get())
                return false;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        const bool fmt = alpha
                             ? (v.Format == DXGI_FORMAT_R8G8B8A8_UNORM || v.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                                v.Format == DXGI_FORMAT_R32G32B32A32_FLOAT)
                             : (v.Format == format && d.Format == format);
        return fmt && d.Width == size.width && d.Height == size.height && d.MipLevels == 1 && d.ArraySize == 1 &&
               d.SampleDesc.Count == 1;
    }
    void clear() {
        if (!ctx || !state)
            return;
        Scope scope(ctx.Get(), state.Get());
        float zero[4]{};
        for (Target* t : {&rawSr, &color, &depth, &motion, &nrOutput})
            if (t->uav)
                ctx->ClearUnorderedAccessViewFloat(t->uav.Get(), zero);
    }
};
Pipeline::Pipeline(ID3D11Device* device, ID3D11DeviceContext* context) : impl(new Impl) {
    Impl& s = *impl;
    s.device = device;
    if (!device || !context || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        s.fail("SR combined requires FL11 immediate context");
        return;
    }
    ComPtr<ID3D11Device> owner;
    context->GetDevice(&owner);
    ComPtr<ID3D11Device1> d1;
    if (owner.Get() != device || FAILED(context->QueryInterface(IID_PPV_ARGS(&s.ctx))) ||
        FAILED(device->QueryInterface(IID_PPV_ARGS(&d1)))) {
        s.fail("D3D11.1 state isolation unavailable");
        return;
    }
    const D3D_FEATURE_LEVEL levels[] = {device->GetFeatureLevel()};
    D3D_FEATURE_LEVEL chosen{};
    if (FAILED(
            d1->CreateDeviceContextState(0, levels, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &chosen, &s.state))) {
        s.fail("SR combined context-state creation failed");
        return;
    }
    ComPtr<ID3DBlob> blob;
    if (!s.compile("CSConvert", "cs_5_0", &blob) ||
        FAILED(device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &s.cs)))
        return;
    blob.Reset();
    if (!s.compile("VSCompose", "vs_5_0", &blob) ||
        FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &s.vs)))
        return;
    blob.Reset();
    if (!s.compile("PSCompose", "ps_5_0", &blob) ||
        FAILED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &s.ps)))
        return;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Params);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_DEPTH_STENCIL_DESC ds{};
    D3D11_SAMPLER_DESC sm{};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &s.cb)) || FAILED(device->CreateRasterizerState(&rd, &s.raster)) ||
        FAILED(device->CreateBlendState(&blend, &s.rgbOnly)) ||
        FAILED(device->CreateDepthStencilState(&ds, &s.noDepth)) ||
        FAILED(device->CreateSamplerState(&sm, &s.linear))) {
        s.fail("SR combined states creation failed");
        return;
    }
    s.initialized = true;
    s.status = "SR combined shader pipeline ready; NGX not yet created";
}
Pipeline::~Pipeline() {
    if (impl->ctx && impl->state) {
        Scope scope(impl->ctx.Get(), impl->state.Get());
        impl->up.reset();
    }
}
bool Pipeline::ready() const {
    return impl->initialized;
}
const std::string& Pipeline::status() const {
    return impl->status;
}
void Pipeline::reset() {
    impl->forceReset = true;
    impl->hasOutput = false;
    impl->haveFrame = false;
    impl->clear();
}
void Pipeline::jitter(uint64_t index, const Config& config, float& x, float& y) {
    auto halton = [](uint32_t n, uint32_t base) {
        float r = 0, f = 1;
        while (n) {
            f /= static_cast<float>(base);
            r += f * (n % base);
            n /= base;
        }
        return r;
    };
    const float ratio = std::max(static_cast<float>(config.output.width) / std::max(1u, config.render.width),
                                 static_cast<float>(config.output.height) / std::max(1u, config.render.height));
    const uint32_t phases = std::clamp(static_cast<uint32_t>(std::ceil(8 * ratio * ratio)), 8u, 64u);
    const uint32_t n = static_cast<uint32_t>(index % phases) + 1;
    x = halton(n, 2) - .5f;
    y = halton(n, 3) - .5f;
}
bool Pipeline::configure(const Config& config) {
    Impl& s = *impl;
    if (!s.initialized)
        return false;
    if (!valid(config)) {
        s.configured = false;
        reset();
        return s.fail("invalid SR dimensions or quality mode");
    }
    if (s.configured && s.up && same(config, s.config))
        return true;
    s.hasOutput = false;
    s.forceReset = true;
    s.haveFrame = false;
    s.configured = false;
    Scope scope(s.ctx.Get(), s.state.Get());
    s.up.reset();
    Upscaler::Desc d;
    d.renderW = config.render.width;
    d.renderH = config.render.height;
    d.outW = config.output.width;
    d.outH = config.output.height;
    d.mode = config.mode;
    d.preset = config.preset;
    d.depthInverted = true;
    auto up = createDlssUpscaler(s.device.Get(), s.ctx.Get(), d, s.status);
    if (!up)
        return false;
    Target raw, color, depth, motion, nr;
    if (!s.make(raw, config.output, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !s.make(color, config.output, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !s.make(depth, config.output, DXGI_FORMAT_R32_FLOAT) ||
        !s.make(motion, config.output, DXGI_FORMAT_R16G16_FLOAT) ||
        !s.make(nr, config.output, DXGI_FORMAT_R16G16B16A16_FLOAT))
        return s.fail("SR combined output allocation failed");
    s.up = std::move(up);
    s.rawSr = std::move(raw);
    s.color = std::move(color);
    s.depth = std::move(depth);
    s.motion = std::move(motion);
    s.nrOutput = std::move(nr);
    s.config = config;
    s.configured = true;
    s.featureStatus = s.up->name();
    s.status = s.featureStatus + "; RGBA16F outputs allocated";
    return true;
}
bool Pipeline::resolve(const Inputs& in) {
    Impl& s = *impl;
    s.hasOutput = false;
    auto reject = [&](const char* text) {
        s.forceReset = true;
        s.clear();
        return s.fail(text);
    };
    if (!s.configured || !s.up)
        return s.fail("configure a real NGX feature before reduced rendering");
    ComPtr<ID3D11Texture2D> color, depth, motion, alpha, alphaRender;
    if (!s.source(in.color, s.config.render, DXGI_FORMAT_R16G16B16A16_FLOAT, color) ||
        !s.source(in.depth, s.config.render, DXGI_FORMAT_R32_FLOAT, depth) ||
        !s.source(in.motion, s.config.render, DXGI_FORMAT_R16G16_FLOAT, motion) ||
        (in.alphaFull && !s.source(in.alphaFull, s.config.output, DXGI_FORMAT_UNKNOWN, alpha, true)) ||
        (in.alphaRender && (in.alphaRender == in.color ||
                            !s.source(in.alphaRender, s.config.render, DXGI_FORMAT_UNKNOWN, alphaRender, true))) ||
        !std::isfinite(in.jitterPixels[0]) || !std::isfinite(in.jitterPixels[1]) || std::abs(in.jitterPixels[0]) > 1 ||
        std::abs(in.jitterPixels[1]) > 1)
        return reject("invalid supplied SR guide/alpha textures or raster jitter");
    const bool reset = in.reset || s.forceReset || !s.haveFrame || in.frameIndex != s.lastFrame + 1;
    bool evaluated = false, mapped = false;
    {
        Scope scope(s.ctx.Get(), s.state.Get());
        Upscaler::Inputs input;
        input.color = color.Get();
        input.depth = depth.Get();
        input.mv = motion.Get();
        input.out = s.rawSr.tex.Get();
        input.jitterX = in.jitterPixels[0];
        input.jitterY = in.jitterPixels[1];
        input.mvScaleX = input.mvScaleY = 1;
        input.reset = reset;
        evaluated = s.up->evaluate(input);
        // NGX may have changed all bindings. Clear only our isolated context.
        s.ctx->ClearState();
        Params p;
        p.sizes[0] = s.config.render.width;
        p.sizes[1] = s.config.render.height;
        p.sizes[2] = s.config.output.width;
        p.sizes[3] = s.config.output.height;
        p.jitterFlags[0] = in.jitterPixels[0];
        p.jitterFlags[1] = in.jitterPixels[1];
        p.jitterFlags[2] = evaluated ? 1.0f : 0.0f;
        p.jitterFlags[3] = in.alphaFull ? 1.0f : 0.0f;
        p.flags[0] = reset || !evaluated;
        p.flags[1] = in.alphaRender ? 1u : 0u;
        D3D11_MAPPED_SUBRESOURCE m{};
        mapped = SUCCEEDED(s.ctx->Map(s.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m));
        if (mapped) {
            std::memcpy(m.pData, &p, sizeof(p));
            s.ctx->Unmap(s.cb.Get(), 0);
            s.ctx->CSSetShader(s.cs.Get(), nullptr, 0);
            s.ctx->CSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
            ID3D11ShaderResourceView* srvs[] = {in.color,          in.depth,     in.motion,
                                                s.rawSr.srv.Get(), in.alphaFull, in.alphaRender};
            ID3D11UnorderedAccessView* uavs[] = {s.color.uav.Get(), s.depth.uav.Get(), s.motion.uav.Get()};
            s.ctx->CSSetShaderResources(0, 6, srvs);
            s.ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
            s.ctx->CSSetSamplers(0, 1, s.linear.GetAddressOf());
            s.ctx->Dispatch((s.config.output.width + 7) / 8, (s.config.output.height + 7) / 8, 1);
        }
    }
    if (!mapped)
        return reject("SR combined constants upload failed");
    s.lastReset = reset || !evaluated;
    s.lastSr = evaluated;
    s.forceReset = !evaluated;
    s.lastFrame = in.frameIndex;
    s.haveFrame = true;
    s.hasOutput = true;
    s.status = evaluated ? s.featureStatus + "; actual RGBA16F SR evaluated, full NR guides ready"
                         : "NGX SR evaluation FAILED; explicit spatial fallback, NR history reset";
    return true;
}
Outputs Pipeline::outputs() const {
    const Impl& s = *impl;
    if (!s.hasOutput)
        return {};
    Outputs out;
    out.color = s.color.borrow();
    out.depth = s.depth.borrow();
    out.motion = s.motion.borrow();
    out.nrOutput = s.nrOutput.borrow();
    out.size = s.config.output;
    out.motionScale[0] = 1.0f / out.size.width;
    out.motionScale[1] = 1.0f / out.size.height;
    out.historyReset = s.lastReset;
    out.srEvaluated = s.lastSr;
    return out;
}
bool Pipeline::composeRgb(ID3D11RenderTargetView* destination, bool useNrOutput, float sharpness) {
    Impl& s = *impl;
    if (!s.hasOutput || !destination)
        return s.fail("valid combined output and destination required");
    ComPtr<ID3D11Device> owner;
    destination->GetDevice(&owner);
    if (owner.Get() != s.device.Get())
        return s.fail("composition device differs");
    ComPtr<ID3D11Resource> resource;
    destination->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    destination->GetDesc(&view);
    if (view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || FAILED(resource.As(&tex)))
        return s.fail("composition requires Texture2D RTV");
    ID3D11ShaderResourceView* src = useNrOutput ? s.nrOutput.srv.Get() : s.color.srv.Get();
    if (tex.Get() == (useNrOutput ? s.nrOutput.tex.Get() : s.color.tex.Get()))
        return s.fail("composition source aliases destination");
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (d.SampleDesc.Count != 1 || view.Texture2D.MipSlice >= d.MipLevels ||
        std::max(1u, d.Width >> view.Texture2D.MipSlice) < s.config.output.width ||
        std::max(1u, d.Height >> view.Texture2D.MipSlice) < s.config.output.height)
        return s.fail("composition destination extent/sample mismatch");
    Scope scope(s.ctx.Get(), s.state.Get());
    Params params{};
    params.sizes[2] = s.config.output.width;
    params.sizes[3] = s.config.output.height;
    params.jitterFlags[0] = std::isfinite(sharpness) ? std::clamp(sharpness, 0.0f, 1.0f) : 0.0f;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(s.ctx->Map(s.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return s.fail("composition constants upload failed");
    std::memcpy(mapped.pData, &params, sizeof(params));
    s.ctx->Unmap(s.cb.Get(), 0);
    s.ctx->PSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
    s.ctx->IASetInputLayout(nullptr);
    s.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.ctx->VSSetShader(s.vs.Get(), nullptr, 0);
    s.ctx->PSSetShader(s.ps.Get(), nullptr, 0);
    s.ctx->PSSetShaderResources(0, 1, &src);
    s.ctx->RSSetState(s.raster.Get());
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(s.config.output.width), static_cast<float>(s.config.output.height),
                            0, 1};
    s.ctx->RSSetViewports(1, &viewport);
    s.ctx->OMSetBlendState(s.rgbOnly.Get(), nullptr, 0xffffffff);
    s.ctx->OMSetDepthStencilState(s.noDepth.Get(), 0);
    s.ctx->OMSetRenderTargets(1, &destination, nullptr);
    s.ctx->Draw(3, 0);
    return true;
}
} // namespace gsr::sr_combined
