// TemporalPipeline: jitter, camera motion vectors from depth, resolve (Upscaler or bilinear fallback). See temporal.h
// and docs/reference/dlss.md.
#include "temporal.h"
#include "runtime_flags.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace gsr {
namespace {
const char* kTemporalHlsl = R"HLSL(
cbuffer CBM : register(b0)
{
    float4x4 invVP;     // inverse of the current view-projection (row-major: clip_i = sum_j M[i][j] * v_j)
    float4x4 prevVP;
    float4 sc;          // current VU screen scale (rows 6), offset (row 5) in .xyz
    float4 off;
    float4 psc;
    float4 poff;
    float4 p0;          // x = renderW  y = renderH  z = frameW  w = frameH
    float4 p1;          // x = jitterX  y = jitterY  z = zmax    w = 1/16 (vu xy unit)
    float4 p2;          // x = flags (bit0: prev valid)
};
Texture2D<float> Depth : register(t0);
RWTexture2D<float2> Mv : register(u0);

[numthreads(8, 8, 1)]
void CSMotion(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)p0.x || id.y >= (uint)p0.y) return;
    float d = Depth.Load(int3(id.xy, 0));
    float2 c = float2(id.xy) + 0.5 - p1.xy;                // unjittered render pixel
    float2 fr = p0.zw;
    float2 g = c / p0.xy * fr;                             // frame (native GS) pixels
    float zg = d * p1.z;
    float3 ndc;
    ndc.x = (g.x - fr.x * 0.5) / (sc.x * p1.w);
    ndc.y = (g.y - fr.y * 0.5) / (sc.y * p1.w);
    ndc.z = (zg - off.z) / sc.z;
    float4 wp = mul(invVP, float4(ndc, 1.0));
    float2 mv = float2(0, 0);
    if (abs(wp.w) > 1e-12)
    {
        wp.xyz /= wp.w;
        float4 pc = mul(prevVP, float4(wp.xyz, 1.0));
        if (pc.w > 1e-6 || pc.w < -1e-6)
        {
            float3 pn = pc.xyz / pc.w;
            float2 gp = fr * 0.5 + float2(pn.x * psc.x * p1.w, pn.y * psc.y * p1.w);
            float2 pp = gp / fr * p0.xy;
            mv = pp - c;
            if (!all(isfinite(mv))) mv = float2(0, 0);
        }
    }
    if (((uint)p2.x & 1u) == 0u) mv = float2(0, 0);
    Mv[id.xy] = mv;
}

struct FSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FSOut VSFull(uint id : SV_VertexID)
{
    FSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
cbuffer CBS : register(b1)
{
    float4 s_a;   // xy = uv scale (region / texture size), zw = 1 / texture size (texels)
    float4 s_b;   // x = sharpness 0..1, y = alpha written
};
Texture2D<float4> Src : register(t0);
SamplerState SmpL : register(s0);
float4 PSStretch(FSOut i) : SV_Target
{
    float2 uv = clamp(i.uv * s_a.xy, s_a.zw * 0.5, s_a.xy - s_a.zw * 0.5);
    float3 c = Src.SampleLevel(SmpL, uv, 0).rgb;
    if (s_b.x > 0.001)
    {
        float2 t = s_a.zw;
        float3 n = Src.SampleLevel(SmpL, uv + float2(0, -t.y), 0).rgb + Src.SampleLevel(SmpL, uv + float2(0, t.y), 0).rgb +
                   Src.SampleLevel(SmpL, uv + float2(-t.x, 0), 0).rgb + Src.SampleLevel(SmpL, uv + float2(t.x, 0), 0).rgb;
        float3 lo = min(c, n * 0.25), hi = max(c, n * 0.25);
        c = clamp(c + (c - n * 0.25) * s_b.x, saturate(lo - 0.1), saturate(hi + 0.1));
    }
    return float4(c, s_b.z > 0.5 ? Src.SampleLevel(SmpL, uv, 0).a : s_b.y);
}
float4 PSMvDebug(FSOut i) : SV_Target
{
    float2 mv = Src.SampleLevel(SmpL, i.uv * s_a.xy, 0).rg;
    return float4(0.5 + mv.x * 0.05, 0.5 + mv.y * 0.05, 0.5, 1.0);
}
)HLSL";

struct MotionCB {
    float invVP[16];
    float prevVP[16];
    float sc[4], off[4], psc[4], poff[4];
    float p0[4], p1[4], p2[4];
};
struct StretchCB {
    float a[4], b[4];
};

// row-major 4x4 from the VU qwords (qword i = column i)
void vuToRowMajor(const float* q, double m[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            m[r * 4 + c] = q[c * 4 + r];
}

bool invert4(const double m[16], double out[16]) {
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!std::isfinite(det) || std::fabs(det) < 1e-30)
        return false;
    const double id = 1.0 / det;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] * id;
    return true;
}
} // namespace

struct TemporalPipeline::Impl {
    ID3D11Device* dev;
    ID3D11DeviceContext* ctx;
    ComPtr<ID3D11ComputeShader> csMotion;
    ComPtr<ID3D11VertexShader> vsFull;
    ComPtr<ID3D11PixelShader> psStretch, psMvDebug;
    ComPtr<ID3D11Buffer> cbMotion, cbStretch;
    ComPtr<ID3D11SamplerState> smpLin;
    ComPtr<ID3D11RasterizerState> rast;
    ComPtr<ID3D11BlendState> blendOff;
    ComPtr<ID3D11DepthStencilState> dsOff;

    ComPtr<ID3D11Texture2D> mvTex, outTex;
    ComPtr<ID3D11ShaderResourceView> mvSrv, outSrv;
    ComPtr<ID3D11UnorderedAccessView> mvUav;
    uint32_t mvW = 0, mvH = 0, outW = 0, outH = 0;

    std::unique_ptr<Upscaler> up;
    Upscaler::Desc upDesc;
    std::string upStatus;
    bool upFailed = false, historyReset = true;
    uint32_t upFailKey = 0;

    // GPU timing ring
    struct Q {
        ComPtr<ID3D11Query> dj, t0, t1;
        bool pending = false;
    };
    Q q[4];
    uint32_t qi = 0;

    bool ok = false;

    bool compile(const char* entry, const char* target, ID3DBlob** out) {
        ComPtr<ID3DBlob> err;
        HRESULT hr = D3DCompile(kTemporalHlsl, std::strlen(kTemporalHlsl), "temporal", nullptr, nullptr, entry, target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0, out, &err);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[temporal] shader %s failed: %s\n", entry,
                         err ? (const char*)err->GetBufferPointer() : "?");
            return false;
        }
        return true;
    }

    bool init() {
        ComPtr<ID3DBlob> b;
        if (!compile("CSMotion", "cs_5_0", &b) ||
            FAILED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &csMotion)))
            return false;
        b.Reset();
        if (!compile("VSFull", "vs_5_0", &b) ||
            FAILED(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsFull)))
            return false;
        b.Reset();
        if (!compile("PSStretch", "ps_5_0", &b) ||
            FAILED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psStretch)))
            return false;
        b.Reset();
        if (!compile("PSMvDebug", "ps_5_0", &b) ||
            FAILED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psMvDebug)))
            return false;
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = (sizeof(MotionCB) + 15) / 16 * 16;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cbMotion)))
            return false;
        bd.ByteWidth = (sizeof(StretchCB) + 15) / 16 * 16;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cbStretch)))
            return false;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &smpLin)))
            return false;
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        if (FAILED(dev->CreateRasterizerState(&rd, &rast)))
            return false;
        D3D11_BLEND_DESC bld{};
        bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bld, &blendOff)))
            return false;
        D3D11_DEPTH_STENCIL_DESC dsd{};
        if (FAILED(dev->CreateDepthStencilState(&dsd, &dsOff)))
            return false;
        for (Q& k : q) {
            D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            dev->CreateQuery(&qd, &k.dj);
            qd.Query = D3D11_QUERY_TIMESTAMP;
            dev->CreateQuery(&qd, &k.t0);
            dev->CreateQuery(&qd, &k.t1);
        }
        return ok = true;
    }

    ComPtr<ID3D11Texture2D> makeTex(uint32_t w, uint32_t h, DXGI_FORMAT fmt, UINT bind) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = fmt;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind;
        ComPtr<ID3D11Texture2D> t;
        dev->CreateTexture2D(&d, nullptr, &t);
        return t;
    }

    bool ensureTargets(uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh) {
        if (!mvTex || !mvSrv || !mvUav || mvW != rw || mvH != rh) {
            mvW = rw;
            mvH = rh;
            mvSrv.Reset();
            mvUav.Reset();
            mvTex = makeTex(rw, rh, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
            if (!mvTex || FAILED(dev->CreateShaderResourceView(mvTex.Get(), nullptr, &mvSrv)) ||
                FAILED(dev->CreateUnorderedAccessView(mvTex.Get(), nullptr, &mvUav)))
                return false;
        }
        if (!outTex || !outSrv || outW != ow || outH != oh) {
            outW = ow;
            outH = oh;
            outSrv.Reset();
            outTex =
                makeTex(ow, oh, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
            if (!outTex || FAILED(dev->CreateShaderResourceView(outTex.Get(), nullptr, &outSrv)))
                return false;
        }
        return true;
    }

    bool fullscreen(ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, ID3D11RenderTargetView* rtv, uint32_t vw,
                    uint32_t vh, const StretchCB& cb) {
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (!src || !rtv || FAILED(ctx->Map(cbStretch.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
            return false;
        std::memcpy(ms.pData, &cb, sizeof(cb));
        ctx->Unmap(cbStretch.Get(), 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsFull.Get(), nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->PSSetConstantBuffers(1, 1, cbStretch.GetAddressOf());
        ctx->PSSetShaderResources(0, 1, &src);
        ID3D11SamplerState* s = smpLin.Get();
        ctx->PSSetSamplers(0, 1, &s);
        ctx->RSSetState(rast.Get());
        D3D11_VIEWPORT vp{0, 0, (float)vw, (float)vh, 0, 1};
        ctx->RSSetViewports(1, &vp);
        D3D11_RECT sr{0, 0, (LONG)vw, (LONG)vh};
        ctx->RSSetScissorRects(1, &sr);
        ctx->OMSetBlendState(blendOff.Get(), nullptr, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(dsOff.Get(), 0);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* nul = nullptr;
        ctx->PSSetShaderResources(0, 1, &nul);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        return true;
    }
};

TemporalPipeline::TemporalPipeline(ID3D11Device* dev, ID3D11DeviceContext* ctx) : m_impl(new Impl) {
    m_impl->dev = dev;
    m_impl->ctx = ctx;
    if (!m_impl->init()) {
        m_status = "temporal: shader / state creation failed";
        return;
    }
    std::string st;
    m_supported = dlssAvailable(dev, st);
    m_status = st;
    if (runtimeFlags().dlssDebugMode)
        debugMode = *runtimeFlags().dlssDebugMode;
}

TemporalPipeline::~TemporalPipeline() {
    m_impl->up.reset();
    // Renderer owner shuts down NGX after all legacy/combined features die.
}

float TemporalPipeline::ratioForMode(uint32_t mode) {
    switch (mode) {
        case 1:
            return 1.0f;
        case 2:
            return 1.0f / 1.5f;
        case 3:
            return 1.0f / 1.724f;
        case 4:
            return 0.5f;
        default:
            return 1.0f;
    }
}

TemporalPipeline::Geom TemporalPipeline::geometry(uint32_t fw, uint32_t fh, uint32_t scale, uint32_t mode) const {
    Geom g;
    g.frameW = fw;
    g.frameH = fh;
    g.outW = fw * scale;
    g.outH = fh * scale;
    const float r = ratioForMode(mode);
    auto even = [](float v) { return std::max(16u, (uint32_t)(v + 0.5f) & ~1u); };
    g.renderW = mode == 1 ? g.outW : std::min(g.outW, even(g.outW * r));
    g.renderH = mode == 1 ? g.outH : std::min(g.outH, even(g.outH * r));
    return g;
}

void TemporalPipeline::jitter(uint32_t idx, const Geom& g, float& jx, float& jy) const {
    auto halton = [](uint32_t i, uint32_t b) {
        float f = 1.0f, r = 0.0f;
        while (i > 0) {
            f /= (float)b;
            r += f * (float)(i % b);
            i /= b;
        }
        return r;
    };
    const float ratio = (float)g.outW / (float)std::max(1u, g.renderW);
    const uint32_t phases = std::clamp<uint32_t>((uint32_t)std::ceil(8.0f * ratio * ratio), 8u, 64u);
    const uint32_t i = idx % phases + 1;
    jx = halton(i, 2) - 0.5f;
    jy = halton(i, 3) - 0.5f;
}

bool TemporalPipeline::stretch(ID3D11ShaderResourceView* srcSrv, uint32_t texW, uint32_t texH, uint32_t srcW,
                               uint32_t srcH, ID3D11RenderTargetView* dstRtv, uint32_t dstW, uint32_t dstH, bool,
                               bool preserveAlpha) {
    StretchCB cb{};
    cb.a[0] = (float)srcW / (float)texW;
    cb.a[1] = (float)srcH / (float)texH;
    cb.a[2] = 1.0f / (float)texW;
    cb.a[3] = 1.0f / (float)texH;
    cb.b[0] = 0.0f;
    cb.b[1] = 0.5f;
    cb.b[2] = preserveAlpha ? 1.0f : 0.0f;
    return m_impl->fullscreen(m_impl->psStretch.Get(), srcSrv, dstRtv, dstW, dstH, cb);
}

std::string TemporalPipeline::upscalerName() const {
    return m_impl->up ? m_impl->up->name() : std::string("none");
}

void TemporalPipeline::reset() {
    m_impl->up.reset();
    m_impl->upFailed = false;
    m_impl->historyReset = true;
    m_impl->upFailKey = 0;
    m_gpuMs = 0;
}

bool TemporalPipeline::prepare(const Geom& g, uint32_t mode, uint32_t preset, const SceneCamera& cam) {
    Impl& s = *m_impl;
    if (!m_supported || !g.renderW || !g.renderH || !g.outW || !g.outH)
        return false;
    double matrix[16], inverse[16];
    vuToRowMajor(cam.vp, matrix);
    if (!cam.haveVP || !cam.haveSO || !invert4(matrix, inverse)) {
        m_status = "DLSS SR camera matrix is invalid";
        return false;
    }
    if (!s.ensureTargets(g.renderW, g.renderH, g.outW, g.outH)) {
        m_status = "DLSS SR target allocation failed";
        return false;
    }
    Upscaler::Desc d;
    d.renderW = g.renderW;
    d.renderH = g.renderH;
    d.outW = g.outW;
    d.outH = g.outH;
    d.mode = mode;
    d.preset = preset;
    const bool same = s.upDesc.renderW == d.renderW && s.upDesc.renderH == d.renderH && s.upDesc.outW == d.outW &&
                      s.upDesc.outH == d.outH && s.upDesc.mode == d.mode && s.upDesc.preset == d.preset;
    if (!same) {
        s.up.reset();
        s.upFailed = false;
    }
    if (!s.up && !s.upFailed) {
        s.upDesc = d;
        s.up = createDlssUpscaler(s.dev, s.ctx, d, s.upStatus);
        s.upFailed = !s.up;
        s.historyReset = true;
        std::fprintf(stderr, "[temporal] %s\n", s.upStatus.c_str());
    }
    m_status = s.upStatus;
    return bool(s.up);
}

bool TemporalPipeline::resolve(const ResolveIn& in) {
    Impl& s = *m_impl;
    ID3D11DeviceContext* ctx = s.ctx;
    const Geom& g = in.g;
    // --- GPU timing begin
    Impl::Q* q = nullptr;
    if (m_timing) {
        q = &s.q[s.qi++ & 3];
        if (!q->pending && q->dj && q->t0 && q->t1) {
            ctx->Begin(q->dj.Get());
            ctx->End(q->t0.Get());
        } else
            q = nullptr;
    }
    // collect an older result
    if (m_timing)
        for (auto& k : s.q)
            if (k.pending) {
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
                UINT64 a = 0, b = 0;
                if (ctx->GetData(k.dj.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                    ctx->GetData(k.t0.Get(), &a, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                    ctx->GetData(k.t1.Get(), &b, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
                    if (!dj.Disjoint)
                        m_gpuMs = (double)(b - a) / (double)dj.Frequency * 1000.0;
                    k.pending = false;
                }
            }

    const bool haveCam = in.cam && in.cam->haveVP && in.cam->haveSO;
    bool upscaled = false;
    if (haveCam && in.depthSrv) {
        // prepare() allocated these targets before the first reduced-resolution draw.
        // --- motion vectors
        MotionCB cb{};
        double m[16], im[16] = {}, pm[16];
        vuToRowMajor(in.cam->vp, m);
        bool ok = invert4(m, im);
        const bool prevOk = in.prev && in.prev->haveVP && in.prev->haveSO && !in.reset;
        vuToRowMajor((prevOk ? in.prev : in.cam)->vp, pm);
        for (int i = 0; i < 16; ++i) {
            cb.invVP[i] = (float)im[i];
            cb.prevVP[i] = (float)pm[i];
        }
        std::memcpy(cb.sc, in.cam->scale, 16);
        std::memcpy(cb.off, in.cam->offset, 16);
        std::memcpy(cb.psc, (prevOk ? in.prev : in.cam)->scale, 16);
        std::memcpy(cb.poff, (prevOk ? in.prev : in.cam)->offset, 16);
        cb.p0[0] = (float)g.renderW;
        cb.p0[1] = (float)g.renderH;
        cb.p0[2] = (float)g.frameW;
        cb.p0[3] = (float)g.frameH;
        cb.p1[0] = in.jx;
        cb.p1[1] = in.jy;
        cb.p1[2] = in.zmax;
        cb.p1[3] = 1.0f / 16.0f;
        cb.p2[0] = prevOk ? 1.0f : 0.0f;
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (ok && SUCCEEDED(ctx->Map(s.cbMotion.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            std::memcpy(ms.pData, &cb, sizeof(cb));
            ctx->Unmap(s.cbMotion.Get(), 0);
            ctx->CSSetShader(s.csMotion.Get(), nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, s.cbMotion.GetAddressOf());
            ID3D11ShaderResourceView* srv = in.depthSrv;
            ctx->CSSetShaderResources(0, 1, &srv);
            ID3D11UnorderedAccessView* uav = s.mvUav.Get();
            ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
            ctx->Dispatch((g.renderW + 7) / 8, (g.renderH + 7) / 8, 1);
            ID3D11ShaderResourceView* nsrv = nullptr;
            ID3D11UnorderedAccessView* nuav = nullptr;
            ctx->CSSetShaderResources(0, 1, &nsrv);
            ctx->CSSetUnorderedAccessViews(0, 1, &nuav, nullptr);
            ctx->CSSetShader(nullptr, nullptr, 0);

            if (s.up) {
                Upscaler::Inputs ui;
                ui.color = in.colorRT;
                ui.depth = in.depthTex;
                ui.mv = s.mvTex.Get();
                ui.out = s.outTex.Get();
                ui.jitterX = in.jx;
                ui.jitterY = in.jy;
                ui.reset = in.reset || !prevOk || s.historyReset;
                ui.mvScaleX = ui.mvScaleY = 1.0f;
                upscaled = s.up->evaluate(ui);
                s.historyReset = !upscaled;
            }
        }
    }
    if (!upscaled)
        m_status = "DLSS SR evaluation/camera failed; spatial fallback for this frame";
    if (upscaled) {
        StretchCB cb{};
        cb.a[0] = cb.a[1] = 1.0f;
        cb.a[2] = 1.0f / (float)g.outW;
        cb.a[3] = 1.0f / (float)g.outH;
        cb.b[0] = in.sharpness;
        cb.b[1] = 0.5f;
        upscaled = s.fullscreen(debugMode == 1 ? s.psMvDebug.Get() : s.psStretch.Get(),
                                debugMode == 1 ? s.mvSrv.Get() : s.outSrv.Get(), in.colorRtv, g.outW, g.outH, cb);
        if (!upscaled)
            m_status = "DLSS SR output blit failed; spatial fallback for this frame";
    }
    if (!upscaled) {
        // fallback: bilinear stretch of the render rectangle (colour copy is taken by the caller: in.colorSrv aliases
        // the target, so go through outTex) Targets remain valid from prepare(), including when NGX evaluation fails.
        ComPtr<ID3D11Resource> dummy;
        D3D11_BOX box{0, 0, 0, g.renderW, g.renderH, 1};
        // copy the render rectangle into the top-left of outTex (same format), then stretch from there into the target
        ctx->CopySubresourceRegion(s.outTex.Get(), 0, 0, 0, 0, in.colorRT, 0, &box);
        StretchCB cb{};
        cb.a[0] = (float)g.renderW / (float)g.outW;
        cb.a[1] = (float)g.renderH / (float)g.outH;
        cb.a[2] = 1.0f / (float)g.outW;
        cb.a[3] = 1.0f / (float)g.outH;
        cb.b[0] = 0.0f;
        cb.b[1] = 0.5f;
        s.fullscreen(s.psStretch.Get(), s.outSrv.Get(), in.colorRtv, g.outW, g.outH, cb);
    }
    if (q) {
        ctx->End(q->t1.Get());
        ctx->End(q->dj.Get());
        q->pending = true;
    }
    return upscaled;
}

} // namespace gsr
