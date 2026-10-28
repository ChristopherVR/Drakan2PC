// Adjacent C++ string literal, included inside the main HLSL initializer.
R"HLSL(
// Point sampling keeps the original continuous address calculation. Wrapping
// a physical centre instead can change its logical cell at fractional scales.
float wrapRTPoint(float c, float dim, uint mode, float mn, float mx)
{
    if (mode == 0u) return c - dim * floor(c / dim);
    if (mode == 1u) return clamp(c, 0.0, dim - 0.001);
    if (mode == 2u) return clamp(c, mn, mx + 0.999);
    return (float)wrapC((int)floor(c), (int)dim, mode, (int)mn, (int)mx) + frac(c);
}

float wrapRTTap(float c, float dim, uint mode, float mn, float mx, float scale)
{
    float halfTap = 0.5 / scale;
    if (mode == 1u) return clamp(c, halfTap, dim - halfTap);
    if (mode == 2u) return clamp(c, mn + halfTap, mx + 1.0 - halfTap);
    int cell = (int)floor(c);
    int wrapped = wrapC(cell, (int)dim, mode, (int)mn, (int)mx);
    return (float)wrapped + (c - floor(c));
}

float4 convertRTAlpha(float4 t)
{
    uint am = (uint)cb_e.z;
    if (am != 0u)
    {
        bool high16 = am == 2u && t.a * 255.0 >= 64.0;
        float a = high16 ? cb_e.w : cb_fog.w;
        if (((cb_d.x >> 12) & 1u) != 0u && !high16 &&
            all(floor(t.rgb * 255.0 + 0.5) == 0.0)) a = 0.0;
        t.a = a / 255.0;
    }
    return t;
}

float4 fetchRTTap(float2 physical, float2 scale)
{
    // Address every physical tap independently in logical GS coordinates.
    // cb_src includes the reduced-world scale as well as the RT allocation.
    float2 p = (physical + 0.5) / scale - cb_src.xy;
    p.x = wrapRTTap(p.x, (float)cb_b.y, cb_b.w & 3u,
                   (float)cb_c.x, (float)cb_c.y, scale.x);
    p.y = wrapRTTap(p.y, (float)cb_b.z, (cb_b.w >> 2) & 3u,
                   (float)cb_c.z, (float)cb_c.w, scale.y);
    return convertRTAlpha(TexA.SampleLevel(SmpPt, (cb_src.xy + p) * cb_src.zw, 0));
}

float4 sampleRT(float2 uv, bool lin)
{
    uint wms = cb_b.w & 3u, wmt = (cb_b.w >> 2) & 3u;
    if (!lin)
    {
        float2 p;
        p.x = wrapRTPoint(uv.x, (float)cb_b.y, wms, (float)cb_c.x, (float)cb_c.y);
        p.y = wrapRTPoint(uv.y, (float)cb_b.z, wmt, (float)cb_c.z, (float)cb_c.w);
        return convertRTAlpha(TexA.SampleLevel(SmpPt, (cb_src.xy + p) * cb_src.zw, 0));
    }
    // Observed shadow/CT32 CLAMP consumers need neither nonlinear alpha nor
    // discontinuous wrapping. Preserve their existing hardware-filter path
    // and native-edge policy, and avoid four fetches for framebuffer copies.
    if ((uint)cb_e.z == 0u && wms == 1u && wmt == 1u)
    {
        float2 p = clamp(uv, float2(0.5, 0.5), float2(cb_b.y, cb_b.z) - 0.5);
        return TexA.SampleLevel(SmpLin, (cb_src.xy + p) * cb_src.zw, 0);
    }
    uint width, height;
    TexA.GetDimensions(width, height);
    float2 scale = float2(width, height) * cb_src.zw;
    float2 p = (cb_src.xy + uv) * scale;
    p -= 0.5;
    float2 base = floor(p), f = p - base;
    float4 a = fetchRTTap(base, scale);
    float4 b = fetchRTTap(base + float2(1, 0), scale);
    float4 c = fetchRTTap(base + float2(0, 1), scale);
    float4 d = fetchRTTap(base + float2(1, 1), scale);
    // TEXA/AEM is nonlinear: expand individual texels before interpolation.
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
)HLSL"
