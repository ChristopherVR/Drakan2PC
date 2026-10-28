// HLSL sources (compiled at runtime with D3DCompile).
#pragma once
// clang-format off

namespace gsr
{

inline const char* kHlslMain = R"HLSL(
// ---------------------------------------------------------------- constants
cbuffer CB : register(b0)
{
    float4 cb_rt;   // x=rtW y=rtH (native px)  z=zMax  w=scale
    uint4  cb_a;    // x=flags  y=aref  z=alpha ABCD (A|B<<2|C<<4|D<<6)  w=FIX
    uint4  cb_b;    // x=fbmask(rgba bytes, target space)  y=texW  z=texH  w=wms|wmt<<2
    uint4  cb_c;    // minu maxu minv maxv
    float4 cb_fog;  // rgb fog colour (0..255)  w=TA0 (24-bit RT source alpha)
    uint4  cb_d;    // x=mxl|lcm<<4|mmag<<5|mmin<<6|L<<9  y=passmode  z=dimx lo  w=dimx hi
    float4 cb_e;    // x=K (lod bias)
    float4 cb_src;  // source RT: xy=origin px, zw=1/dims (native px)
    float4 cb_f;    // x=texture upscale factor N (power of two; the bound texture is N times the logical size)  y=mip bias  z=max anisotropy taps  w=log2(N)
    float4 cb_g;    // x=mip bias added to the footprint LOD of the anisotropic path (the internal-resolution part is already in the derivatives)
    float4 cb_h;    // xy = raster jitter in pixels; z = maximum accepted motion in pixels
    float4 cb_spriteBounds; // validated single-sprite native texel footprint
};

#define F_TME   (1u<<0)
#define F_FGE   (1u<<1)
#define F_FST   (1u<<2)
#define F_TCC   (1u<<3)
#define F_ATE   (1u<<6)
#define F_DATE  (1u<<12)
#define F_DATM  (1u<<13)
#define F_ABE   (1u<<14)
#define F_PABE  (1u<<15)
#define F_DTHE  (1u<<16)
#define F_CCLAMP (1u<<17)
#define F_FBA   (1u<<18)
#define F_FB16  (1u<<19)
#define F_TEXRT (1u<<22)
#define F_BILIN (1u<<23)

Texture2D<float4> TexA : register(t0);
Texture2D<float4> DestTex : register(t1);
SamplerState SmpPt : register(s0);
SamplerState SmpLin : register(s1);

struct VSIn
{
    float4 pf : POSITION;  // x y z fog
    float4 col : COLOR;
    float3 stq : TEXCOORD0;
};
struct VSOut
{
    float4 pos : SV_Position;
    noperspective float4 col : COLOR;
    noperspective float3 stq : TEXCOORD0;
    noperspective float fog : TEXCOORD1;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pf.x / cb_rt.x * 2.0 - 1.0, 1.0 - i.pf.y / cb_rt.y * 2.0, saturate(i.pf.z / cb_rt.z), 1.0);
    o.pos.xy += cb_g.yz;   // temporal upscaler: sub-pixel jitter (NDC), zero for everything but the 3D world
    o.col = i.col;
    o.stq = i.stq;
    o.fog = i.pf.w;
    return o;
}

// ---------------------------------------------------------------- texture sampling
// All coordinates handed to fetchTexel are in *physical* texels of the bound texture: logical texel * N (N = cb_f.x, 1 unless the texture was
// upscaled / replaced). Wrapping is decided in logical texels, so REPEAT / REGION_* semantics are identical for every N.
int wrapC(int c, int dim, uint mode, int mn, int mx)
{
    if (mode == 0u) return c & (dim - 1);
    if (mode == 1u) return clamp(c, 0, dim - 1);
    if (mode == 2u) return clamp(c, mn, mx);
    return (c & mn) | mx;
}

int wrapCN(int c, int dim, uint mode, int mn, int mx)
{
    int N = (int)cb_f.x;
    if (N == 1) return wrapC(c, dim, mode, mn, mx);
    if (mode == 1u) return clamp(c, 0, dim * N - 1);
    if (mode == 2u) return clamp(c, mn * N, (mx + 1) * N - 1);
    int cell = c >> (int)cb_f.w;
    int sub = c & (N - 1);
    cell = (mode == 0u) ? (cell & (dim - 1)) : ((cell & mn) | mx);
    return cell * N + sub;
}

float4 fetchTexel(int2 p, int lvl, int2 dim)
{
    uint wms = cb_b.w & 3u;
    uint wmt = (cb_b.w >> 2) & 3u;
    int x = wrapCN(p.x, dim.x, wms, (int)cb_c.x >> lvl, (int)cb_c.y >> lvl);
    int y = wrapCN(p.y, dim.y, wmt, (int)cb_c.z >> lvl, (int)cb_c.w >> lvl);
    return TexA.Load(int3(x, y, lvl));
}

float4 sampleLvl(float2 uv, int lvl, bool lin)
{
    int2 dim = int2(max(1, (int)cb_b.y >> lvl), max(1, (int)cb_b.z >> lvl));
    float2 p = uv * (cb_f.x / (float)(1 << lvl));
    if (lin)
    {
        p -= 0.5;
        float2 fl = floor(p);
        int2 i0 = (int2)fl;
        float2 f = p - fl;
        float4 a = fetchTexel(i0, lvl, dim);
        float4 b = fetchTexel(i0 + int2(1, 0), lvl, dim);
        float4 c = fetchTexel(i0 + int2(0, 1), lvl, dim);
        float4 d = fetchTexel(i0 + int2(1, 1), lvl, dim);
        return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
    }
    return fetchTexel((int2)floor(p), lvl, dim);
}

)HLSL"
#include "gs_rt_sampling.inc"
R"HLSL(
float4 sampleMip(float2 uv, float l, bool mag, uint mxl, uint mmag, uint mmin, bool lin)
{
    bool useMip = (!mag) && mmin >= 2u && mxl > 0u;
    bool mipLin = (!mag) && (mmin == 3u || mmin == 5u) && mxl > 0u;
    l = clamp(l, 0.0, (float)mxl);
    int l0 = useMip ? (int)(mipLin ? floor(l) : floor(l + 0.5)) : 0;
    float4 c0 = sampleLvl(uv, l0, lin);
    if (mipLin)
    {
        int l1 = min(l0 + 1, (int)mxl);
        float4 c1 = sampleLvl(uv, l1, lin);
        c0 = lerp(c0, c1, frac(l));
    }
    return c0;
}

// duvdx / duvdy: screen derivatives of uv (logical level-0 texels per internal-resolution pixel)
float4 sampleTexture(float2 uv, float q, uint flags, float2 duvdx, float2 duvdy)
{
    bool fst = (flags & F_FST) != 0u;
    uint mxl = cb_d.x & 15u;
    uint lcm = (cb_d.x >> 4) & 1u;
    uint mmag = (cb_d.x >> 5) & 1u;
    uint mmin = (cb_d.x >> 6) & 7u;
    uint L = (cb_d.x >> 9) & 3u;
    // GS LOD: from Q (K, L) unless LCM = 1 (fixed K). cb_f.y shifts it for the higher internal resolution / user preference (mip_bias).
    float lod = (lcm != 0u || fst) ? cb_e.x : (-log2(max(abs(q), 1e-20)) * (float)(1u << L) + cb_e.x);
    float lodB = lod + cb_f.y;
    bool mag = lodB < 0.0;
    bool lin = mag ? (mmag == 1u) : (mmin == 1u || mmin >= 4u);
    if (cb_e.y > 0.5) lin = cb_e.y > 1.5; // user override: 1 = point, 2 = bilinear
    if ((flags & F_TEXRT) != 0u) return sampleRT(uv, lin);

    // anisotropic filtering: replaces the (isotropic) GS LOD by the screen-space footprint of the pixel in minified, mip-mapped, Q-driven draws
    if (cb_f.z > 1.5 && mxl > 0u && lcm == 0u && !fst && mmin >= 2u)
    {
        float lx = length(duvdx), ly = length(duvdy);
        float major = max(lx, ly), minor = max(min(lx, ly), 1e-6);
        if (major > 1.0)
        {
            float ratio = clamp(major / minor, 1.0, cb_f.z);
            float taps = max(1.0, ceil(ratio - 0.02));
            float lodA = log2(major / taps) + cb_g.x;
            float2 axis = (lx >= ly) ? duvdx : duvdy;
            bool magA = lodA < 0.0;
            bool linA = magA ? (mmag == 1u) : (mmin == 1u || mmin >= 4u);
            if (cb_e.y > 0.5) linA = cb_e.y > 1.5;
            float4 acc = float4(0, 0, 0, 0);
            int n = (int)taps;
            for (int i = 0; i < n; ++i)
            {
                float t = ((float)i + 0.5) / taps - 0.5;
                acc += sampleMip(uv + axis * t, lodA, magA, mxl, mmag, mmin, linA);
            }
            return acc / taps;
        }
    }
    return sampleMip(uv, lodB, mag, mxl, mmag, mmin, lin);
}

)HLSL"
R"HLSL(
// ---------------------------------------------------------------- pixel shader
struct PSOut
{
    float4 c0 : SV_Target0;
#if HW_BLEND
    float4 c1 : SV_Target1;
#endif
};

float4 sourcePixel(VSOut i)
{
    uint fl = cb_a.x;
    float4 cv = floor(i.col + 0.5);
    float3 c = cv.rgb;
    float a = cv.a;

    if (fl & F_TME)
    {
        float2 uv;
        float q = 1.0;
        uint wd = cb_b.y, ht = cb_b.z;
        if (fl & F_FST) uv = i.stq.xy;
        else
        {
            q = i.stq.z;
            float iq = (q != 0.0) ? 1.0 / q : 0.0;
            uv = float2(i.stq.x * iq * (float)wd, i.stq.y * iq * (float)ht);
        }
        if (fl & (1u<<24)) uv=clamp(uv,cb_spriteBounds.xy,cb_spriteBounds.zw);
        float4 t = floor(sampleTexture(uv, q, fl, ddx(uv), ddy(uv)) * 255.0 + 0.5);
        uint tfx = (fl >> 4) & 3u;
        bool tcc = (fl & F_TCC) != 0u;
        if (tfx == 0u)
        {
            c = floor(t.rgb * cv.rgb / 128.0);
            a = tcc ? floor(t.a * cv.a / 128.0) : cv.a;
        }
        else if (tfx == 1u)
        {
            c = t.rgb;
            a = tcc ? t.a : cv.a;
        }
        else if (tfx == 2u)
        {
            c = floor(t.rgb * cv.rgb / 128.0) + cv.a;
            a = tcc ? (t.a + cv.a) : cv.a;
        }
        else
        {
            c = floor(t.rgb * cv.rgb / 128.0) + cv.a;
            a = tcc ? t.a : cv.a;
        }
        c = clamp(c, 0.0, 255.0);
        a = clamp(a, 0.0, 255.0);
    }

    if (fl & F_FGE)
    {
        // GS fog: Cout = (F*Cs + (256-F)*FOGCOL) >> 8. (A float f = F/255 can come out as 1.0000001 on the GPU, which made
        // c = floor(-tiny) = -1 and wrapped to 255 in COLCLAMP=0 blends: the "inverted loading screen".)
        float f = clamp(floor(i.fog + 0.5), 0.0, 255.0);
        c = clamp(floor((c * f + cb_fog.rgb * (256.0 - f)) / 256.0), 0.0, 255.0);
    }

    return float4(c, a);
}

void alphaCoverage(float a)
{
    uint fl = cb_a.x;
    // alpha test
    uint pass1 = 1u;
    if (fl & F_ATE)
    {
        uint atst = (fl >> 7) & 7u;
        int ai = (int)a;
        int ar = (int)cb_a.y;
        bool p = true;
        if (atst == 0u) p = false;
        else if (atst == 1u) p = true;
        else if (atst == 2u) p = ai < ar;
        else if (atst == 3u) p = ai <= ar;
        else if (atst == 4u) p = ai == ar;
        else if (atst == 5u) p = ai >= ar;
        else if (atst == 6u) p = ai > ar;
        else p = ai != ar;
        pass1 = p ? 1u : 0u;
    }
    if (cb_d.y == 0u) { if (pass1 == 0u) discard; }
    else { if (pass1 != 0u) discard; }

}

PSOut PSMain(VSOut i)
{
    uint fl = cb_a.x;
    float4 source = sourcePixel(i);
    float3 c = source.rgb;
    float a = source.a;
    alphaCoverage(a);
    // FBA affects stored alpha only; ALPHA/PABE consume original source alpha.
    float writeAlpha = (fl & F_FBA) ? (float)((uint)a | 0x80u) : a;

    PSOut o;
#if HW_BLEND
    uint abcd = cb_a.z;
    uint ctype = (abcd >> 4) & 3u;
    float coef = (ctype == 0u) ? a : (float)cb_a.w;
    float cc = saturate(coef / 128.0);
    float3 outc = c;
    float outa = writeAlpha;
    if (fl & F_FB16)
    {
        int2 pp = int2(i.pos.xy / cb_rt.w);
        int e = 0;
        if (fl & F_DTHE)
        {
            uint idx = (uint)((pp.y & 3) * 4 + (pp.x & 3));
            uint dm = (idx < 8u) ? cb_d.z : cb_d.w;
            e = (int)((dm >> (4u * (idx & 7u))) & 7u);
            if (e >= 4) e -= 8;
        }
        outc = floor(clamp(outc + (float)e, 0.0, 255.0) / 8.0) * 8.0;
        outa = (outa >= 128.0) ? 128.0 : 0.0;
    }
    o.c0 = float4(outc / 255.0, outa / 255.0);
    o.c1 = float4(cc, cc, cc, cc);
#else
    int2 pp = int2(i.pos.xy);
    int2 pn = int2(i.pos.xy / cb_rt.w);
    float4 dst = floor(DestTex.Load(int3(pp, 0)) * 255.0 + 0.5);
    if (fl & F_DATE)
    {
        bool bit = (((uint)dst.a) & 0x80u) != 0u;
        bool datm = (fl & F_DATM) != 0u;
        if (bit != datm) discard;
    }
    float3 outc = c;
    if ((fl & F_ABE) && (!(fl & F_PABE) || a >= 128.0))
    {
        uint abcd = cb_a.z;
        uint sa = abcd & 3u, sb = (abcd >> 2) & 3u, sc = (abcd >> 4) & 3u, sd = (abcd >> 6) & 3u;
        float3 A = (sa == 0u) ? c : ((sa == 1u) ? dst.rgb : float3(0, 0, 0));
        float3 B = (sb == 0u) ? c : ((sb == 1u) ? dst.rgb : float3(0, 0, 0));
        float3 D = (sd == 0u) ? c : ((sd == 1u) ? dst.rgb : float3(0, 0, 0));
        float C = (sc == 0u) ? a : ((sc == 1u) ? dst.a : (float)cb_a.w);
        float3 r = floor((A - B) * C / 128.0) + D;
        if (fl & F_CCLAMP) r = clamp(r, 0.0, 255.0);
        else r = (float3)(((int3)r) & 255);
        outc = r;
    }
    float outa = writeAlpha;
    if (fl & F_FB16)
    {
        int e = 0;
        if (fl & F_DTHE)
        {
            uint idx = (uint)((pn.y & 3) * 4 + (pn.x & 3));
            uint dm = (idx < 8u) ? cb_d.z : cb_d.w;
            e = (int)((dm >> (4u * (idx & 7u))) & 7u);
            if (e >= 4) e -= 8;
        }
        outc = floor(clamp(outc + (float)e, 0.0, 255.0) / 8.0) * 8.0;
        outa = (outa >= 128.0) ? 128.0 : 0.0;
    }
    // frame buffer mask
    uint fm = cb_b.x;
    if (fm != 0u)
    {
        uint4 s4 = uint4((uint)outc.r, (uint)outc.g, (uint)outc.b, (uint)outa);
        uint4 d4 = uint4((uint)dst.r, (uint)dst.g, (uint)dst.b, (uint)dst.a);
        uint4 m4 = uint4(fm & 255u, (fm >> 8) & 255u, (fm >> 16) & 255u, (fm >> 24) & 255u);
        uint4 r4 = (s4 & ~m4) | (d4 & m4);
        outc = float3(r4.rgb);
        outa = (float)r4.a;
    }
    o.c0 = float4(outc / 255.0, outa / 255.0);
#endif
    return o;
}

// Separate coverage/depth-matched velocity pass: GS dual-source colour
// blending cannot share its output merger with an additional motion MRT.
struct MotionIn {
    float4 pf : POSITION;
    float4 col : COLOR;
    float3 stq : TEXCOORD0;
    float4 history : TEXCOORD2;
};
struct MotionOut {
    float4 pos : SV_Position;
    noperspective float4 col : COLOR;
    noperspective float3 stq : TEXCOORD0;
    noperspective float fog : TEXCOORD1;
    noperspective float4 history : TEXCOORD2;
};
MotionOut VSMotion(MotionIn i) {
    VSIn base; base.pf=i.pf; base.col=i.col; base.stq=i.stq;
    VSOut v=VSMain(base);
    MotionOut o; o.pos=v.pos; o.col=v.col; o.stq=v.stq; o.fog=v.fog;
    o.history=i.history;
    return o;
}
float4 PSMotion(MotionOut i) : SV_Target {
    VSOut base; base.pos=i.pos; base.col=i.col; base.stq=i.stq; base.fog=i.fog;
    const float4 source=sourcePixel(base);
    alphaCoverage(source.a);
    const uint fl=cb_a.x;
    float4 dst=0;
    if ((fl & F_DATE) || ((fl & F_ABE) && ((cb_a.z>>4)&3u)==1u))
        dst=floor(DestTex.Load(int3(int2(i.pos.xy),0))*255.0+0.5);
    if (fl & F_DATE) {
        const bool bit=(((uint)dst.a)&0x80u)!=0u;
        if (bit!=((fl & F_DATM)!=0u)) discard;
    }
    bool opaque=true;
    if ((fl & F_ABE) && (!(fl & F_PABE) || source.a>=128.0)) {
        const uint A=cb_a.z&3u, B=(cb_a.z>>2)&3u, C=(cb_a.z>>4)&3u, D=(cb_a.z>>6)&3u;
        const float factor=(C==0u?source.a:(C==1u?dst.a:(float)cb_a.w))/128.0;
        const float sourceWeight=(D==0u?1.0:0.0)+((A==0u?1.0:0.0)-(B==0u?1.0:0.0))*factor;
        const float destWeight=(D==1u?1.0:0.0)+((A==1u?1.0:0.0)-(B==1u?1.0:0.0))*factor;
        if (abs(destWeight-1.0)<1e-4 && (abs(sourceWeight)<1e-4 || all(source.rgb==0))) discard;
        opaque=abs(sourceWeight-1.0)<1e-4 && abs(destWeight)<1e-4;
    }
    float2 motion=0;
    bool valid=opaque && cb_h.w>0.5 && i.history.w>0.999 && i.history.z>1e-8;
    if (valid) {
        const float2 previous=i.history.xy/i.history.z;
        motion=previous-(i.pos.xy-cb_h.xy);
        valid=all(isfinite(motion)) && all(abs(motion)<=cb_h.zz);
    }
    return float4(valid?motion:float2(0,0),valid?1.0:0.0,i.pos.z);
}

)HLSL"
R"HLSL(
// ---------------------------------------------------------------- display
struct FSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
FSOut VSFull(uint id : SV_VertexID)
{
    FSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

cbuffer CBD : register(b1)
{
    float4 d_r1;  // circuit 1 uv rect (x0 y0 x1 y1)
    float4 d_r2;
    float4 d_bg;
    uint4  d_m;   // x=en1 y=en2 z=mmod|amod<<1|slbg<<2 w=alp
};
Texture2D<float4> D1 : register(t0);
Texture2D<float4> D2 : register(t1);

float4 PSDisplay(FSOut i) : SV_Target
{
    float2 uv1 = lerp(d_r1.xy, d_r1.zw, i.uv);
    float2 uv2 = lerp(d_r2.xy, d_r2.zw, i.uv);
    float4 c1 = (d_m.x != 0u) ? D1.SampleLevel(SmpLin, uv1, 0) : float4(0, 0, 0, 0);
    float4 c2 = (d_m.y != 0u) ? D2.SampleLevel(SmpLin, uv2, 0) : float4(0, 0, 0, 0);
    uint mm = d_m.z;
    bool mmod = (mm & 1u) != 0u, amod = (mm & 2u) != 0u, slbg = (mm & 4u) != 0u;
    float3 outc;
    if (d_m.x != 0u && d_m.y != 0u)
    {
        float al = mmod ? ((float)d_m.w / 128.0) : ((amod ? c2.a : c1.a) * 255.0 / 128.0);
        float3 cd = slbg ? d_bg.rgb : c1.rgb;
        outc = lerp(cd, c2.rgb, saturate(al));
    }
    else if (d_m.x != 0u) outc = c1.rgb;
    else if (d_m.y != 0u) outc = c2.rgb;
    else outc = d_bg.rgb;
    return float4(outc, 1.0);
}

Texture2D<float4> BlitSrc : register(t0);
float4 PSBlit(FSOut i) : SV_Target
{
    return float4(BlitSrc.SampleLevel(SmpLin, i.uv, 0).rgb, 1.0);
}
float4 PSBlitPt(FSOut i) : SV_Target
{
    return float4(BlitSrc.SampleLevel(SmpPt, i.uv, 0).rgb, 1.0);
}
float4 PSCopyRGBA(FSOut i) : SV_Target { return BlitSrc.SampleLevel(SmpPt,i.uv,0); }

// ---- final present: area-average downscale / sharp-bilinear upscale, optional field handling ------------------
cbuffer CBP : register(b2)
{
    float4 p_a;  // xy = source size (px), zw = viewport size (px)
    uint4  p_m;  // x = deinterlace (0 weave, 1 bob, 2 blend)  y = field  z = sharp  w = internal scale
};

// Color and depth must use the same coordinates across reduced world draws.
// p_m.xy carries float jitter bits for this pass only.
float PSStretchDepth(FSOut i) : SV_Depth
{
    int2 p = clamp(int2(floor(i.pos.xy * p_a.xy / p_a.zw + asfloat(p_m.xy))),
                  int2(0,0), int2(p_a.xy)-1);
    return BlitSrc.Load(int3(p,0)).r;
}

void presentTaps(float p, float s, out int i0, out int n, out float w[8])
{
    [unroll] for (int k = 0; k < 8; ++k) w[k] = 0.0;
    if (s >= 1.0)
    {
        float q = p - 0.5;
        float fl = floor(q);
        float f = saturate((q - fl - 0.5) * s + 0.5);
        i0 = (int)fl;
        n = 2;
        w[0] = 1.0 - f;
        w[1] = f;
    }
    else
    {
        float h = 0.5 / s;
        float a = p - h, b = p + h;
        i0 = (int)floor(a);
        int i1 = (int)ceil(b) - 1;
        n = min(i1 - i0 + 1, 8);
        float tot = 0.0;
        for (int k = 0; k < n; ++k)
        {
            float lo = max((float)(i0 + k), a);
            float hi = min((float)(i0 + k + 1), b);
            w[k] = max(hi - lo, 0.0);
            tot += w[k];
        }
        for (int k2 = 0; k2 < n; ++k2) w[k2] /= max(tot, 1e-6);
    }
}

float3 presentFetch(int x, int y, int2 dim)
{
    x = clamp(x, 0, dim.x - 1);
    y = clamp(y, 0, dim.y - 1);
    uint sc = max(p_m.w, 1u);
    if (p_m.x == 1u)
    {
        // bob: only the lines of the current field, each shown twice
        uint ln = (uint)y / sc;
        y = (int)(((ln & ~1u) | (p_m.y & 1u)) * sc + ((uint)y % sc));
        y = min(y, dim.y - 1);
    }
    float3 c = BlitSrc.Load(int3(x, y, 0)).rgb;
    if (p_m.x == 2u)
        c = 0.5 * (c + BlitSrc.Load(int3(x, min(y + (int)sc, dim.y - 1), 0)).rgb);
    return c;
}

float4 PSPresent(FSOut i) : SV_Target
{
    if (p_m.z == 0u && p_m.x == 0u) return float4(BlitSrc.SampleLevel(SmpLin, i.uv, 0).rgb, 1.0);
    float2 src = p_a.xy;
    float2 s = p_a.zw / src;
    float2 p = i.uv * src;
    int2 dim = int2(src);
    int ix0, nx, iy0, ny;
    float wx[8], wy[8];
    if (p_m.z == 0u) { s = float2(1.0, 1.0); }
    presentTaps(p.x, s.x, ix0, nx, wx);
    presentTaps(p.y, s.y, iy0, ny, wy);
    float3 acc = 0;
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x)
            acc += presentFetch(ix0 + x, iy0 + y, dim) * (wx[x] * wy[y]);
    return float4(acc, 1.0);
}
)HLSL";

} // namespace gsr
