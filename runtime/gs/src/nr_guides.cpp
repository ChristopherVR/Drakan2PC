#include "nr_guides.h"
#include "guide_camera.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

using Microsoft::WRL::ComPtr;
namespace gsr::nr::guides {
namespace {
const char* kHlsl = R"HLSL(
cbuffer Params : register(b0) {
    row_major float4x4 clipToPrevious;
    uint4 dims;       // guide width,height,color origin x,y
    uint4 depthInfo;  // depth origin x,y, history usable, reserved
    float4 zmap;      // source far, 1/(near-far), sky epsilon, max motion pixels
    float4 jitter;    // current raster jitter in guide pixels
};
Texture2D<float4> Color : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float4> ObjectMotion : register(t2);
Texture2DArray<float> ModelAlpha : register(t3);
SamplerState ModelSampler : register(s0);
RWTexture2D<float4> GuideColor : register(u0);
RWTexture2D<float> GuideDepth : register(u1);
RWTexture2D<float2> Motion : register(u2);
float finiteHalf(float v) { return isfinite(v) ? clamp(v, -65504.0, 65504.0) : 0.0; }
[numthreads(8,8,1)]
void CSPrepare(uint3 tid : SV_DispatchThreadID) {
    uint2 p = tid.xy;
    if (any(p >= dims.xy)) return;
    float4 color = Color.Load(int3(p + dims.zw, 0));
    GuideColor[p] = float4(finiteHalf(color.r), finiteHalf(color.g), finiteHalf(color.b), finiteHalf(color.a));
    float source = Depth.Load(int3(p + depthInfo.xy, 0));
    float d = (source - zmap.x) * zmap.y;
    bool valid = isfinite(source) && isfinite(d) && d > zmap.z && d <= 1.0;
    GuideDepth[p] = valid ? d : 0.0;
    float2 mv = 0;
    // Sky, saturated near depth, missing history and cuts never yield motion.
    if (valid && d < 1.0 - zmap.z && depthInfo.z != 0) {
        float2 cur = float2(p) + .5 - jitter.xy;
        float4 clip = float4(cur / float2(dims.xy) * float2(2,-2) + float2(-1,1), d, 1);
        float4 prev = mul(clip, clipToPrevious);
        if (all(isfinite(prev)) && prev.w > 1e-6) {
            float3 ndc = prev.xyz / prev.w;
            float2 pp = (ndc.xy * float2(.5,-.5) + .5) * float2(dims.xy);
            float2 delta = pp - cur;
            if (all(isfinite(ndc)) && all(isfinite(delta)) && ndc.z > 0 && ndc.z <= 1 &&
                all(pp >= 0) && all(pp < float2(dims.xy)) && all(abs(delta) <= zmap.w)) mv = delta;
        }
    }
    if (valid && d < 1.0 - zmap.z && depthInfo.w != 0) {
        const float4 object=ObjectMotion.Load(int3(p+depthInfo.xy,0));
        // Both passes store the same GS-normalized D32 depth. Allow a few
        // interpolation ULPs, not an absolute epsilon: with reverse Z that
        // epsilon admits unrelated surfaces far from the camera.
        const uint sourceBits=asuint(source), objectBits=asuint(object.w);
        if (object.z>0.5 && all(isfinite(object)) && all(abs(object.xy)<=zmap.w) &&
            object.w>=0 && object.w<=1 && max(sourceBits,objectBits)-min(sourceBits,objectBits)<=4) mv=object.xy;
    }
    Motion[p] = mv;
}
struct VOut { float4 pos : SV_Position; };
VOut VSCompose(uint id : SV_VertexID) {
    VOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1);
    return o;
}
// No sampling/scaling: NR output and destination region are exactly guide-sized.
float4 PSCompose(VOut i) : SV_Target {
    float4 c = Color.Load(int3(uint2(i.pos.xy), 0));
    return float4(finiteHalf(c.r), finiteHalf(c.g), finiteHalf(c.b), 0);
}
struct ModelIn {float4 current : POSITION;float4 previous : TEXCOORD0;float2 uv : TEXCOORD1;uint textureIndex : TEXCOORD2;};
struct ModelOut {float4 position : SV_Position;float4 previous : TEXCOORD0;float2 uv : TEXCOORD1;nointerpolation uint textureIndex : TEXCOORD2;};
ModelOut VSModel(ModelIn i){
    ModelOut o;o.position=i.current;o.previous=i.previous;o.uv=i.uv;o.textureIndex=i.textureIndex;
    o.position.xy+=jitter.xy/float2(dims.xy)*float2(2,-2)*o.position.w;return o;
}
float2 PSModel(ModelOut i) : SV_Target {
    float z=Color.Load(int3(int2(i.position.xy),0)).x;
    // Pose geometry must agree with the actually displayed opaque surface.
    // This rejects hidden actors and preserves camera motion on disocclusions.
    if(!isfinite(z) || z<=zmap.z || z>=1-zmap.z || abs(z-i.position.z)>max(abs(z)*.002,1e-10))discard;
    if(i.textureIndex!=0xffffffff && ModelAlpha.SampleLevel(ModelSampler,float3(i.uv,i.textureIndex),0)*255<1)discard;
    if(!all(isfinite(i.previous)) || i.previous.w<=1e-6)discard;
    float3 previous=i.previous.xyz/i.previous.w;
    if(previous.z<=0 || previous.z>=1)discard;
    float2 pixel=(previous.xy*float2(.5,-.5)+.5)*float2(dims.xy);
    float2 flow=pixel-(i.position.xy-jitter.xy);
    if(!all(isfinite(flow)) || any(abs(flow)>zmap.w))discard;
    return flow;
}
)HLSL";
bool finite(const float* a, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(a[i]))
            return false;
    return true;
}
using guide::identity;
using guide::inverse;
using guide::Matrix; // mathematical row-major, ROW-VECTOR
using guide::multiply;
bool floats(const double* in, float* out) {
    return guide::toFloats(in, out);
}
bool cameraMatrix(const Frame& f, const Camera& c, double* out) {
    guide::View v;
    std::copy(c.vp, c.vp + 16, v.vp);
    std::copy(c.scale, c.scale + 3, v.scale);
    std::copy(c.offset, c.offset + 3, v.offset);
    v.xyOffset[0] = c.xyOffset[0];
    v.xyOffset[1] = c.xyOffset[1];
    v.valid = c.valid;
    return guide::cameraMatrix({f.frame.width, f.frame.height, f.zmax, f.nearGs, f.farGs}, v, out);
}
bool validFrame(const Frame& f) {
    auto size = [](Size s) { return s.width > 0 && s.height > 0 && s.width <= 16384 && s.height <= 16384; };
    auto fits = [](Size tex, Size region, uint32_t x, uint32_t y) {
        return region.width <= tex.width && region.height <= tex.height && x <= tex.width - region.width &&
               y <= tex.height - region.height;
    };
    return size(f.frame) && size(f.guide) && size(f.colorTexture) && size(f.depthTexture) &&
           fits(f.colorTexture, f.guide, f.colorX, f.colorY) && fits(f.depthTexture, f.guide, f.depthX, f.depthY) &&
           std::isfinite(f.zmax) && f.zmax > 0 && std::isfinite(f.nearGs) && std::isfinite(f.farGs) && f.farGs >= 0 &&
           f.nearGs <= 1 && f.nearGs > f.farGs + 1e-8f && std::isfinite(f.skyEpsilon) && f.skyEpsilon >= 0 &&
           f.skyEpsilon < .01f && std::isfinite(f.maxMotionPixels) && f.maxMotionPixels > 0 &&
           f.maxMotionPixels <= 65504 && finite(f.jitterPixels, 2);
}
struct Target {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Texture borrow() const { return {tex.Get(), srv.Get(), uav.Get()}; }
};
bool same(Size a, Size b) {
    return a.width == b.width && a.height == b.height;
}
struct alignas(16) Params {
    float clipToPrevious[16];
    uint32_t dims[4], depthInfo[4];
    float zmap[4], jitter[4];
};
static_assert(sizeof(Params) == 128);
// An isolated context-state object prevents source RTV/DSV conflicts and
// preserves ALL caller bindings, including shader class instances/predication.
struct Scope {
    ID3D11DeviceContext1* ctx;
    ComPtr<ID3DDeviceContextState> previous;
    Scope(ID3D11DeviceContext1* c, ID3DDeviceContextState* state) : ctx(c) {
        ctx->SwapDeviceContextState(state, &previous);
        ctx->ClearState();
    }
    ~Scope() {
        ctx->ClearState();
        ctx->SwapDeviceContextState(previous.Get(), nullptr);
    }
};
} // namespace

const char* shaderSource() {
    return kHlsl;
}
bool compileShaders(Shaders& out, std::string& status) {
    Shaders shaders;
    auto compile = [&](const char* entry, const char* target, std::vector<uint8_t>& bytes) {
        ComPtr<ID3DBlob> blob, error;
        const HRESULT hr =
            D3DCompile(kHlsl, std::strlen(kHlsl), "nr_guides", nullptr, nullptr, entry, target,
                       D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                       0, &blob, &error);
        if (FAILED(hr)) {
            status = std::string(entry) + ": " +
                     (error ? static_cast<const char*>(error->GetBufferPointer()) : "D3DCompile failed");
            return false;
        }
        const auto* p = static_cast<const uint8_t*>(blob->GetBufferPointer());
        bytes.assign(p, p + blob->GetBufferSize());
        return true;
    };
    if (!compile("CSPrepare", "cs_5_0", shaders.prepare) || !compile("VSCompose", "vs_5_0", shaders.vertex) ||
        !compile("PSCompose", "ps_5_0", shaders.compose) || !compile("VSModel", "vs_5_0", shaders.modelVertex) ||
        !compile("PSModel", "ps_5_0", shaders.modelPixel))
        return false;
    out = std::move(shaders);
    status = "all three shaders compiled offline";
    return true;
}

struct Guides::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11VertexShader> modelVs;
    ComPtr<ID3D11PixelShader> modelPs;
    ComPtr<ID3D11InputLayout> modelLayout;
    ComPtr<ID3D11Buffer> modelVertices;
    ComPtr<ID3D11RenderTargetView> modelRtv;
    ComPtr<ID3D11Texture2D> modelAlpha;
    ComPtr<ID3D11ShaderResourceView> modelAlphaSrv;
    ComPtr<ID3D11SamplerState> modelSampler;
    std::vector<uint64_t> modelTextureIds;
    uint32_t modelCapacity = 0, modelOffset = 0, modelLayers = 0;
    uint64_t modelAlphaAllocationCount = 0;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> rgbOnly;
    ComPtr<ID3D11DepthStencilState> noDepth;
    Target color, depth, motion, output;
    Size size{}, lastFrame{};
    Frame last{};
    Matrix cur{}, invCur{}, prev{}, invPrev{};
    bool initialized = false, prepared = false, forceReset = true, historyReset = true;
    std::string status;
    bool fail(const char* message) {
        status = message;
        return false;
    }
    bool make(Target& t, DXGI_FORMAT format, Size sz) {
        UINT support = 0;
        if (FAILED(device->CheckFormatSupport(format, &support)) ||
            (support & (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_LOAD |
                        D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)) !=
                (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_LOAD |
                 D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))
            return false;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = sz.width;
        d.Height = sz.height;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = format;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (format == DXGI_FORMAT_R16G16_FLOAT)
            d.BindFlags |= D3D11_BIND_RENDER_TARGET;
        return SUCCEEDED(device->CreateTexture2D(&d, nullptr, &t.tex)) &&
               SUCCEEDED(device->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv)) &&
               SUCCEEDED(device->CreateUnorderedAccessView(t.tex.Get(), nullptr, &t.uav));
    }
    void clear() {
        if (!context || !state)
            return;
        Scope scope(context.Get(), state.Get());
        float zero[4]{};
        for (Target* t : {&color, &depth, &motion, &output})
            if (t->uav)
                context->ClearUnorderedAccessViewFloat(t->uav.Get(), zero);
    }
    bool source(ID3D11ShaderResourceView* srv, Size expected, bool isDepth) {
        if (!srv)
            return false;
        ComPtr<ID3D11Device> owner;
        srv->GetDevice(&owner);
        if (owner.Get() != device.Get())
            return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        srv->GetDesc(&view);
        if (view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || (isDepth && view.Format != DXGI_FORMAT_R32_FLOAT))
            return false;
        if (!isDepth && view.Format != DXGI_FORMAT_R8G8B8A8_UNORM && view.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
            view.Format != DXGI_FORMAT_B8G8R8A8_UNORM && view.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
            view.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && view.Format != DXGI_FORMAT_R32G32B32A32_FLOAT)
            return false;
        ComPtr<ID3D11Resource> resource;
        srv->GetResource(&resource);
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(resource.As(&tex)))
            return false;
        for (Target* t : {&color, &depth, &motion, &output})
            if (tex.Get() == t->tex.Get())
                return false;
        D3D11_TEXTURE2D_DESC d{};
        tex->GetDesc(&d);
        const uint32_t mip = view.Texture2D.MostDetailedMip;
        if (mip >= d.MipLevels || d.SampleDesc.Count != 1 || d.ArraySize != 1)
            return false;
        return std::max(1u, d.Width >> mip) == expected.width && std::max(1u, d.Height >> mip) == expected.height;
    }
};

Guides::Guides(ID3D11Device* device, ID3D11DeviceContext* context, const Shaders* provided) : impl(new Impl) {
    Impl& s = *impl;
    s.device = device;
    if (!device || !context || device->GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0 ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        s.fail("FL11 immediate device/context required");
        return;
    }
    ComPtr<ID3D11Device> owner;
    context->GetDevice(&owner);
    ComPtr<ID3D11Device1> d1;
    if (owner.Get() != device || FAILED(context->QueryInterface(IID_PPV_ARGS(&s.context))) ||
        FAILED(device->QueryInterface(IID_PPV_ARGS(&d1)))) {
        s.fail("matching D3D11.1 device/context required");
        return;
    }
    D3D_FEATURE_LEVEL selected{};
    const D3D_FEATURE_LEVEL levels[] = {device->GetFeatureLevel()};
    if (FAILED(d1->CreateDeviceContextState(0, levels, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &selected,
                                            &s.state))) {
        s.fail("context-state isolation unavailable");
        return;
    }
    Shaders local;
    if (!provided) {
        if (!compileShaders(local, s.status))
            return;
        provided = &local;
    }
    if (provided->prepare.empty() || provided->vertex.empty() || provided->compose.empty() ||
        FAILED(device->CreateComputeShader(provided->prepare.data(), provided->prepare.size(), nullptr, &s.cs)) ||
        FAILED(device->CreateVertexShader(provided->vertex.data(), provided->vertex.size(), nullptr, &s.vs)) ||
        FAILED(device->CreatePixelShader(provided->compose.data(), provided->compose.size(), nullptr, &s.ps))) {
        s.fail("guide shader creation failed");
        return;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Params);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_DEPTH_STENCIL_DESC ds{};
    if (FAILED(device->CreateBuffer(&bd, nullptr, &s.cb)) || FAILED(device->CreateRasterizerState(&rd, &s.raster)) ||
        FAILED(device->CreateBlendState(&blend, &s.rgbOnly)) ||
        FAILED(device->CreateDepthStencilState(&ds, &s.noDepth))) {
        s.fail("guide state allocation failed");
        return;
    }
    s.initialized = true;
    s.status = "guide helper ready (camera motion only)";
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_UINT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (provided->modelVertex.empty() || provided->modelPixel.empty() ||
        FAILED(device->CreateVertexShader(provided->modelVertex.data(), provided->modelVertex.size(), nullptr,
                                          &s.modelVs)) ||
        FAILED(
            device->CreatePixelShader(provided->modelPixel.data(), provided->modelPixel.size(), nullptr, &s.modelPs)) ||
        FAILED(device->CreateInputLayout(layout, 4, provided->modelVertex.data(), provided->modelVertex.size(),
                                         &s.modelLayout)) ||
        FAILED(device->CreateSamplerState(&sampler, &s.modelSampler))) {
        s.initialized = false;
        s.fail("native model motion shader allocation failed");
    }
}
Guides::~Guides() = default;
bool Guides::ready() const {
    return impl->initialized;
}
const std::string& Guides::status() const {
    return impl->status;
}
Textures Guides::textures() const {
    const Impl& s = *impl;
    if (!s.prepared)
        return {};
    return {s.color.borrow(), s.depth.borrow(), s.motion.borrow(), s.output.borrow(), s.size, s.historyReset};
}
void Guides::reset() {
    impl->prepared = false;
    impl->forceReset = true;
    impl->historyReset = true;
    impl->clear();
}

bool Guides::prepare(const Frame& f) {
    Impl& s = *impl;
    s.prepared = false;
    auto reject = [&](const char* error) {
        s.forceReset = true;
        s.historyReset = true;
        s.clear();
        return s.fail(error);
    };
    if (!s.initialized)
        return false;
    if (!validFrame(f) || !s.source(f.sourceColor, f.colorTexture, false) ||
        !s.source(f.sourceDepth, f.depthTexture, true))
        return reject("invalid source, dimensions, region, device, depth mapping or jitter");
    if (f.sourceObjectMotion) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        f.sourceObjectMotion->GetDesc(&view);
        if (view.Format != DXGI_FORMAT_R32G32B32A32_FLOAT || !s.source(f.sourceObjectMotion, f.depthTexture, false))
            return reject("object motion requires a matching RGBA32F source on this device");
    }
    if (!cameraMatrix(f, f.current, s.cur) || !inverse(s.cur, s.invCur))
        return reject("invalid/singular current camera");
    const bool resized = !same(s.size, f.guide) || !s.color.tex;
    if (resized) {
        Target c, d, m, o;
        if (!s.make(c, DXGI_FORMAT_R16G16B16A16_FLOAT, f.guide) || !s.make(d, DXGI_FORMAT_R32_FLOAT, f.guide) ||
            !s.make(m, DXGI_FORMAT_R16G16_FLOAT, f.guide) || !s.make(o, DXGI_FORMAT_R16G16B16A16_FLOAT, f.guide))
            return reject("exact-size guide allocation failed");
        s.color = std::move(c);
        s.depth = std::move(d);
        s.motion = std::move(m);
        s.output = std::move(o);
        s.size = f.guide;
        s.clear();
        s.modelRtv.Reset();
        if (FAILED(s.device->CreateRenderTargetView(s.motion.tex.Get(), nullptr, &s.modelRtv)))
            return reject("model motion render target allocation failed");
    }
    // Caller supplies the previous CAMERA for this frame. Detect mapping/size
    // changes too: they invalidate the meaning of prior clip coordinates.
    s.historyReset = f.reset || s.forceReset || resized || !same(f.frame, s.lastFrame) || f.zmax != s.last.zmax ||
                     f.nearGs != s.last.nearGs || f.farGs != s.last.farGs ||
                     f.frameIndex != uint32_t(s.last.frameIndex + 1u);
    const bool validPrevious = cameraMatrix(f, f.previous, s.prev) && inverse(s.prev, s.invPrev);
    if (!validPrevious)
        s.historyReset = true;
    if (s.historyReset) {
        std::copy(s.cur, s.cur + 16, s.prev);
        std::copy(s.invCur, s.invCur + 16, s.invPrev);
    }
    const bool stationary = std::equal(s.cur, s.cur + 16, s.prev);
    Matrix transform{};
    if (stationary)
        identity(transform);
    else
        multiply(s.invCur, s.prev, transform);
    Params p{};
    if (!floats(transform, p.clipToPrevious))
        return reject("non-finite motion transform");
    p.dims[0] = f.guide.width;
    p.dims[1] = f.guide.height;
    p.dims[2] = f.colorX;
    p.dims[3] = f.colorY;
    p.depthInfo[0] = f.depthX;
    p.depthInfo[1] = f.depthY;
    p.depthInfo[2] = !s.historyReset && !stationary;
    p.depthInfo[3] = f.sourceObjectMotion && !s.historyReset;
    p.zmap[0] = f.farGs;
    p.zmap[1] = 1.0f / (f.nearGs - f.farGs);
    p.zmap[2] = f.skyEpsilon;
    p.zmap[3] = f.maxMotionPixels;
    p.jitter[0] = f.jitterPixels[0];
    p.jitter[1] = f.jitterPixels[1];
    bool mapped = false;
    {
        Scope scope(s.context.Get(), s.state.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        mapped = SUCCEEDED(s.context->Map(s.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map));
        if (mapped) {
            std::memcpy(map.pData, &p, sizeof(p));
            s.context->Unmap(s.cb.Get(), 0);
            s.context->CSSetShader(s.cs.Get(), nullptr, 0);
            s.context->CSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
            ID3D11ShaderResourceView* srvs[] = {f.sourceColor, f.sourceDepth, f.sourceObjectMotion};
            ID3D11UnorderedAccessView* uavs[] = {s.color.uav.Get(), s.depth.uav.Get(), s.motion.uav.Get()};
            s.context->CSSetShaderResources(0, 3, srvs);
            s.context->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
            s.context->Dispatch((f.guide.width + 7) / 8, (f.guide.height + 7) / 8, 1);
        }
    }
    if (!mapped)
        return reject("guide constant upload failed");
    s.last = f;
    s.lastFrame = f.frame;
    s.prepared = true;
    s.forceReset = false;
    s.status = s.historyReset         ? "guides ready; history reset; zero motion"
               : f.sourceObjectMotion ? "guides ready; camera and tracked geometry motion"
                                      : "guides ready; camera-only pixel motion";
    return true;
}

namespace detail {
namespace {
bool finiteBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return (u & 0x7f800000u) != 0x7f800000u;
}
} // namespace
// Projects the model vertices through the current and previous world-to-clip matrices into out, in double precision
// with the accumulation order of the original scalar loop (m[12 + c] + x * m[c] + y * m[4 + c] + z * m[8 + c]), so
// the results are identical. Returns nullptr or the failure status.
const char* projectModelVertices(std::span<const ModelMotionVertex> vertices,
                                 std::span<const ModelMotionTexture> textures, const double* cur, const double* prev,
                                 ModelGpuVertex* out) {
#if defined(_M_X64) || defined(__SSE2__)
    const __m128d curT01 = _mm_loadu_pd(cur + 12), curT23 = _mm_loadu_pd(cur + 14);
    const __m128d prevT01 = _mm_loadu_pd(prev + 12), prevT23 = _mm_loadu_pd(prev + 14);
    __m128d curM01[3], curM23[3], prevM01[3], prevM23[3];
    for (unsigned r = 0; r < 3; ++r) {
        curM01[r] = _mm_loadu_pd(cur + r * 4);
        curM23[r] = _mm_loadu_pd(cur + r * 4 + 2);
        prevM01[r] = _mm_loadu_pd(prev + r * 4);
        prevM23[r] = _mm_loadu_pd(prev + r * 4 + 2);
    }
    const __m128d absMask = _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffffll));
    const __m128d limit = _mm_set1_pd(1e30);
#endif
    for (size_t n = 0; n < vertices.size(); ++n) {
        const auto& v = vertices[n];
        ModelGpuVertex p;
        p.texture = v.alphaTexture;
        if (!finiteBits(v.current[0]) || !finiteBits(v.current[1]) || !finiteBits(v.current[2]) ||
            !finiteBits(v.previous[0]) || !finiteBits(v.previous[1]) || !finiteBits(v.previous[2]) ||
            !finiteBits(v.uv[0]) || !finiteBits(v.uv[1]) ||
            (p.texture != ~0u && (p.texture >= textures.size() || textures[p.texture].pixels.size() != 128 * 128)))
            return "model motion vertices/material invalid";
#if defined(_M_X64) || defined(__SSE2__)
        __m128d a01 = curT01, a23 = curT23, b01 = prevT01, b23 = prevT23;
        for (unsigned r = 0; r < 3; ++r) {
            const __m128d x = _mm_set1_pd(double(v.current[r])), y = _mm_set1_pd(double(v.previous[r]));
            a01 = _mm_add_pd(a01, _mm_mul_pd(x, curM01[r]));
            a23 = _mm_add_pd(a23, _mm_mul_pd(x, curM23[r]));
            b01 = _mm_add_pd(b01, _mm_mul_pd(y, prevM01[r]));
            b23 = _mm_add_pd(b23, _mm_mul_pd(y, prevM23[r]));
        }
        // |value| <= 1e30 is false for NaN and infinities: the original isfinite/abs test.
        const int ok = _mm_movemask_pd(_mm_cmple_pd(_mm_and_pd(a01, absMask), limit)) &
                       _mm_movemask_pd(_mm_cmple_pd(_mm_and_pd(a23, absMask), limit)) &
                       _mm_movemask_pd(_mm_cmple_pd(_mm_and_pd(b01, absMask), limit)) &
                       _mm_movemask_pd(_mm_cmple_pd(_mm_and_pd(b23, absMask), limit));
        if (ok != 3)
            return "model motion projection invalid";
        _mm_storeu_ps(p.current, _mm_movelh_ps(_mm_cvtpd_ps(a01), _mm_cvtpd_ps(a23)));
        _mm_storeu_ps(p.previous, _mm_movelh_ps(_mm_cvtpd_ps(b01), _mm_cvtpd_ps(b23)));
#else
        for (unsigned c = 0; c < 4; ++c) {
            double a = cur[12 + c], b = prev[12 + c];
            for (unsigned r = 0; r < 3; ++r) {
                a += v.current[r] * cur[r * 4 + c];
                b += v.previous[r] * prev[r * 4 + c];
            }
            if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 1e30 || std::abs(b) > 1e30)
                return "model motion projection invalid";
            p.current[c] = float(a);
            p.previous[c] = float(b);
        }
#endif
        p.uv[0] = v.uv[0];
        p.uv[1] = v.uv[1];
        out[n] = p;
    }
    return nullptr;
}
const char* projectModelVerticesReference(std::span<const ModelMotionVertex> vertices,
                                          std::span<const ModelMotionTexture> textures, const double* cur,
                                          const double* prev, ModelGpuVertex* out) {
    for (size_t n = 0; n < vertices.size(); ++n) {
        const auto& v = vertices[n];
        ModelGpuVertex p{};
        p.texture = v.alphaTexture;
        if (!finite(v.current, 3) || !finite(v.previous, 3) || !finite(v.uv, 2) ||
            (p.texture != ~0u && (p.texture >= textures.size() || textures[p.texture].pixels.size() != 128 * 128)))
            return "model motion vertices/material invalid";
        for (unsigned c = 0; c < 4; ++c) {
            double a = cur[12 + c], b = prev[12 + c];
            for (unsigned r = 0; r < 3; ++r) {
                a += v.current[r] * cur[r * 4 + c];
                b += v.previous[r] * prev[r * 4 + c];
            }
            if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 1e30 || std::abs(b) > 1e30)
                return "model motion projection invalid";
            p.current[c] = float(a);
            p.previous[c] = float(b);
        }
        std::copy(v.uv, v.uv + 2, p.uv);
        out[n] = p;
    }
    return nullptr;
}
} // namespace detail

uint64_t Guides::modelAlphaAllocations() const {
    return impl->modelAlphaAllocationCount;
}
bool Guides::modelMotion(std::span<const ModelMotionVertex> vertices, std::span<const ModelMotionTexture> textures) {
    auto& s = *impl;
    if (!s.prepared)
        return s.fail("prepared guides required for model motion");
    if (s.historyReset || vertices.empty())
        return true;
    if (vertices.size() % 3 || vertices.size() > 1500000 || textures.size() > 768)
        return s.fail("model motion input budget/topology invalid");
    using Vertex = detail::ModelGpuVertex;
    const auto bytes = UINT(vertices.size() * sizeof(Vertex));
    // Ring buffer: append with NO_OVERWRITE, DISCARD only on wrap. A DISCARD every frame blocked Map for several
    // milliseconds once the GPU ran a frame behind (the renamed instance was still waiting for the ray pass).
    if (bytes > s.modelCapacity / 4) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = std::max(bytes * 4, 8u << 20);
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> buffer;
        if (FAILED(s.device->CreateBuffer(&d, nullptr, &buffer)))
            return s.fail("model motion vertex allocation failed");
        s.modelVertices = std::move(buffer);
        s.modelCapacity = d.ByteWidth;
        s.modelOffset = s.modelCapacity; // the first map discards
    }
    const auto layers = std::max(1u, UINT(textures.size()));
    if (!s.modelAlpha || s.modelLayers < layers) {
        // Material visibility changes every frame. Retain capacity on shrink
        // so returning actors do not recreate the array and reupload all slots.
        const auto capacity = std::max(layers, std::min(768u, s.modelLayers * 2));
        D3D11_TEXTURE2D_DESC d{};
        d.Width = d.Height = 128;
        d.MipLevels = 1;
        d.ArraySize = capacity;
        d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8_UNORM;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = d.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.MipLevels = 1;
        view.Texture2DArray.ArraySize = capacity;
        if (FAILED(s.device->CreateTexture2D(&d, nullptr, &texture)) ||
            FAILED(s.device->CreateShaderResourceView(texture.Get(), &view, &srv)))
            return s.fail("model motion alpha allocation failed");
        s.modelAlpha = std::move(texture);
        s.modelAlphaSrv = std::move(srv);
        s.modelLayers = capacity;
        s.modelTextureIds.assign(capacity, 0);
        ++s.modelAlphaAllocationCount;
    }
    // The vertices are projected straight into the ring (no staging copy); a rejected input leaves the mapped range
    // unused and nothing is drawn.
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (bytes > s.modelCapacity - s.modelOffset) {
        mode = D3D11_MAP_WRITE_DISCARD;
        s.modelOffset = 0;
    }
    const UINT offset = s.modelOffset;
    s.modelOffset += bytes;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(s.context->Map(s.modelVertices.Get(), 0, mode, 0, &map)))
        return s.fail("model motion vertex upload failed");
    const char* invalid = detail::projectModelVertices(
        vertices, textures, s.cur, s.prev, reinterpret_cast<Vertex*>(static_cast<uint8_t*>(map.pData) + offset));
    s.context->Unmap(s.modelVertices.Get(), 0);
    if (invalid)
        return s.fail(invalid);
    Scope scope(s.context.Get(), s.state.Get());
    std::vector<uint8_t> alpha;
    for (size_t i = 0; i < textures.size(); ++i) {
        const auto& t = textures[i];
        uint64_t identity = t.identity;
        if (!identity) {
            identity = 14695981039346656037ull;
            for (auto pixel : t.pixels) {
                identity ^= pixel;
                identity *= 1099511628211ull;
            }
        }
        if (s.modelTextureIds[i] == identity)
            continue;
        if (alpha.empty())
            alpha.resize(128 * 128);
        if (t.pixels.size() == alpha.size())
            for (size_t n = 0; n < alpha.size(); ++n)
                alpha[n] = uint8_t(t.pixels[n] >> 24);
        else
            std::fill(alpha.begin(), alpha.end(), 128);
        s.context->UpdateSubresource(s.modelAlpha.Get(), UINT(i), nullptr, alpha.data(), 128, 0);
        s.modelTextureIds[i] = identity;
    }
    const UINT stride = sizeof(Vertex);
    s.context->IASetInputLayout(s.modelLayout.Get());
    s.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.context->IASetVertexBuffers(0, 1, s.modelVertices.GetAddressOf(), &stride, &offset);
    s.context->VSSetShader(s.modelVs.Get(), nullptr, 0);
    s.context->PSSetShader(s.modelPs.Get(), nullptr, 0);
    s.context->VSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
    s.context->PSSetConstantBuffers(0, 1, s.cb.GetAddressOf());
    s.context->PSSetShaderResources(0, 1, s.depth.srv.GetAddressOf());
    s.context->PSSetShaderResources(3, 1, s.modelAlphaSrv.GetAddressOf());
    s.context->PSSetSamplers(0, 1, s.modelSampler.GetAddressOf());
    s.context->RSSetState(s.raster.Get());
    const D3D11_VIEWPORT vp{0, 0, float(s.size.width), float(s.size.height), 0, 1};
    s.context->RSSetViewports(1, &vp);
    s.context->OMSetDepthStencilState(s.noDepth.Get(), 0);
    s.context->OMSetRenderTargets(1, s.modelRtv.GetAddressOf(), nullptr);
    s.context->Draw(UINT(vertices.size()), 0);
    s.status = "guides ready; camera, tracked draw and native model motion";
    return true;
}

bool Guides::frameConstants(const float worldToView[16], FrameConstants& out) {
    Impl& s = *impl;
    out = {};
    if (!s.prepared || !worldToView || !finite(worldToView, 16))
        return s.fail("verified worldToView and prepared guides required");
    Matrix view{}, invView{}, projection{}, invProjection{}, forward{}, back{};
    for (int i = 0; i < 16; ++i)
        view[i] = worldToView[i];
    // Require a rigid affine world-to-view matrix; do not mistake an MVP for it.
    if (std::abs(view[3]) + std::abs(view[7]) + std::abs(view[11]) + std::abs(view[15] - 1) > 1e-5 ||
        !inverse(view, invView))
        return s.fail("worldToView is not affine/invertible");
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double dot = 0;
            for (int k = 0; k < 3; ++k)
                dot += invView[i * 4 + k] * invView[j * 4 + k];
            if (std::abs(dot - (i == j ? 1 : 0)) > 1e-3)
                return s.fail("worldToView basis is not rigid");
        }
    multiply(invView, s.cur, projection);
    if (!inverse(projection, invProjection))
        return s.fail("projection inverse failed");
    if (std::equal(s.cur, s.cur + 16, s.prev)) {
        identity(forward);
        identity(back);
    } else {
        multiply(s.invCur, s.prev, forward);
        multiply(s.invPrev, s.cur, back);
    }
    FrameConstants c{};
    if (!floats(projection, c.currentViewToClip) || !floats(invProjection, c.clipToCameraView) ||
        !floats(forward, c.clipToPrevClip) || !floats(back, c.prevClipToClip))
        return s.fail("camera constants overflow");
    // Unproject normalized D3D z=1 (near) / z=0 (far). Infinite reverse-Z is
    // retained exactly in the projection, with FLT_MAX as metadata sentinel.
    const double nw = invProjection[11] + invProjection[15];
    const double nz = invProjection[10] + invProjection[14];
    const double fw = invProjection[15], fz = invProjection[14];
    if (std::abs(nw) < 1e-20)
        return s.fail("invalid near endpoint");
    const double nearZ = nz / nw, nearPlane = std::abs(nearZ);
    c.infiniteFar = std::abs(fw) < 1e-20;
    const double farZ = c.infiniteFar ? nearZ : fz / fw;
    const double farPlane = c.infiniteFar ? std::numeric_limits<float>::max() : std::abs(farZ);
    if (!std::isfinite(nearPlane) || nearPlane <= 0 || !std::isfinite(farPlane) || farPlane <= nearPlane ||
        (!c.infiniteFar && nearZ * farZ <= 0) || nearPlane > std::numeric_limits<float>::max())
        return s.fail("projection is not a supported reverse-Z camera");
    c.nearPlane = static_cast<float>(nearPlane);
    c.farPlane = static_cast<float>(farPlane);
    c.orthographic = std::abs(projection[11]) < 1e-12;
    // This helper accepts the engine's centered/off-center perspective with a
    // Z-only clip W, or an orthographic matrix. Oblique/skew cameras need review.
    if (std::abs(projection[3]) + std::abs(projection[7]) > 1e-7 ||
        std::abs(projection[1]) + std::abs(projection[4]) > 1e-5 || std::abs(projection[0]) < 1e-12 ||
        std::abs(projection[5]) < 1e-12)
        return s.fail("unsupported oblique/skew projection");
    for (int i = 0; i < 3; ++i) {
        c.cameraPosition[i] = static_cast<float>(invView[12 + i]);
        c.cameraRight[i] = static_cast<float>(invView[i]);
        c.cameraUp[i] = static_cast<float>(invView[4 + i]);
        c.cameraForward[i] = static_cast<float>(invView[8 + i] * (nearZ < 0 ? -1 : 1));
    }
    const double top = (invProjection[5] + invProjection[9] + invProjection[13]) / nw;
    const double bottom = (-invProjection[5] + invProjection[9] + invProjection[13]) / nw;
    c.verticalFov = c.orthographic
                        ? 1.0f
                        : static_cast<float>(std::abs(std::atan2(top, nearPlane) - std::atan2(bottom, nearPlane)));
    // Streamline cameraAspectRatio is VIEW-SPACE width/height. For the
    // supported projection this is |Pyy/Pxx|, including anamorphic cameras;
    // neither guide texture dimensions nor presentation aspect defines it.
    c.aspectRatio = static_cast<float>(std::abs(projection[5] / projection[0]));
    c.motionScale[0] = 1.0f / s.size.width;
    c.motionScale[1] = 1.0f / s.size.height;
    std::copy(s.last.jitterPixels, s.last.jitterPixels + 2, c.jitterPixels);
    c.reset = s.historyReset;
    c.frameIndex = s.last.frameIndex;
    if (!finite(c.cameraPosition, 3) || !std::isfinite(c.verticalFov) || c.verticalFov <= 0 ||
        !std::isfinite(c.aspectRatio) || c.aspectRatio <= 0)
        return s.fail("invalid camera metadata");
    out = c;
    return true;
}

bool Guides::composeRgb(ID3D11RenderTargetView* destination, bool useOutput) {
    Impl& s = *impl;
    if (!s.prepared || !destination)
        return s.fail("prepared guides and destination required");
    ComPtr<ID3D11Device> owner;
    destination->GetDevice(&owner);
    if (owner.Get() != s.device.Get())
        return s.fail("destination device differs");
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    destination->GetDesc(&view);
    ComPtr<ID3D11Resource> resource;
    destination->GetResource(&resource);
    ComPtr<ID3D11Texture2D> tex;
    if (view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || FAILED(resource.As(&tex)) ||
        tex.Get() == s.output.tex.Get())
        return s.fail("destination must be a distinct Texture2D RTV");
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    if (d.SampleDesc.Count != 1 || std::max(1u, d.Width >> view.Texture2D.MipSlice) < s.size.width ||
        std::max(1u, d.Height >> view.Texture2D.MipSlice) < s.size.height)
        return s.fail("destination is smaller than guide region or multisampled");
    Scope scope(s.context.Get(), s.state.Get());
    s.context->IASetInputLayout(nullptr);
    s.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.context->VSSetShader(s.vs.Get(), nullptr, 0);
    s.context->PSSetShader(s.ps.Get(), nullptr, 0);
    s.context->PSSetShaderResources(0, 1, useOutput ? s.output.srv.GetAddressOf() : s.color.srv.GetAddressOf());
    s.context->RSSetState(s.raster.Get());
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(s.size.width), static_cast<float>(s.size.height), 0, 1};
    s.context->RSSetViewports(1, &vp);
    s.context->OMSetDepthStencilState(s.noDepth.Get(), 0);
    s.context->OMSetBlendState(s.rgbOnly.Get(), nullptr, 0xffffffff);
    s.context->OMSetRenderTargets(1, &destination, nullptr);
    s.context->Draw(3, 0);
    s.status = "NR RGB composed; destination alpha preserved";
    return true;
}
} // namespace gsr::nr::guides
