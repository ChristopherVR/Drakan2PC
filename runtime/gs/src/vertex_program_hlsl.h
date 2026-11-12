// HLSL of the vertex-program draws (gsr/vertex_program.h; prototype): vp::evaluate() operation for operation.
// The VU rounds every result toward zero; a double holds the product of two floats exactly and their sum to within
// a residual whose sign two more sums give, so the float below the exact result is found with one comparison
// (vp::vu::roundZero). That needs double support in shaders (optional in Direct3D 11) and nothing else: no
// rounding-mode switch, no fused multiply-add, no reliance on the precision of float division.
//
// vpEvaluate returns what the program would have put into the GS packet. VSProgram (compiled after gs_shaders.h's
// kHlslMain) feeds it through the ordinary VSMain; VSProgramRaw returns it as integers for the test that compares the
// shader with the CPU evaluation (tests/test_vertex_program.cpp).
#pragma once
// clang-format off

namespace gsr
{

inline const char* kHlslVertexProgram = R"HLSL(
// ---------------------------------------------------------------- vertex programs
cbuffer CBProgram : register(b5)
{
    int4 vp_offset; // xy = XYOFFSET (12.4 fixed point)
};
Buffer<uint4> VpConstants : register(t8);
struct VpIn
{
    uint4 pos : VPPOS;
    uint4 st : VPST;
    int4 nrm : VPNRM;
    uint4 sel : VPSEL; // x = constants index, y = program
};
struct VpRaw
{
    uint4 place;  // x, y (16 bits each), z, fog lane (bits 4..11 fog, bit 15 skip)
    uint4 colour; // r g b a (0..255)
    uint4 stq;    // float bits of s, t, q
};
uint vuOperand(uint b)
{
    uint sign = b & 0x80000000u;
    uint mag = b & 0x7FFFFFFFu;
    if (mag <= 0x007FFFFFu) return sign;
    return sign | min(mag, 0x7F7FFFFFu);
}
uint vuRound(double value, double residual)
{
    float nearest = (float)value;
    uint bits = asuint(nearest);
    if (value == 0.0) return bits & 0x80000000u;
    precise double back = (double)nearest;
    if ((value > 0.0 && (back > value || (back == value && residual < 0.0))) ||
        (value < 0.0 && (back < value || (back == value && residual > 0.0))))
        bits -= 1u;
    if ((bits & 0x7F800000u) == 0u) bits &= 0x80000000u;
    return bits;
}
uint vuAdd(uint a, uint b)
{
    precise double x = (double)asfloat(a);
    precise double y = (double)asfloat(b);
    precise double sum = x + y;
    precise double back = sum - x;
    precise double residual = (x - (sum - back)) + (y - back);
    return vuRound(sum, residual);
}
uint vuMul(uint a, uint b)
{
    precise double p = (double)asfloat(a) * (double)asfloat(b);
    return vuRound(p, 0.0);
}
uint vuNeg(uint a) { return a ^ 0x80000000u; }
uint vuReciprocal(uint w)
{
    uint sign = w & 0x80000000u;
    uint mag = w & 0x7FFFFFFFu;
    if (mag == 0u) return sign | 0x7F7FFFFFu;
    precise double aw = (double)asfloat(mag);
    // any starting value with a few correct bits; two Newton steps in double leave it far inside one float step
    precise double r = (double)(1.0 / asfloat(mag));
    r = r * (2.0 - aw * r);
    r = r * (2.0 - aw * r);
    uint q = asuint((float)r);
    if ((double)asfloat(q) * aw > 1.0) q -= 1u;
    if ((double)asfloat(q) * aw > 1.0) q -= 1u;
    if ((double)asfloat(q + 1u) * aw <= 1.0) q += 1u;
    if ((double)asfloat(q + 1u) * aw <= 1.0) q += 1u;
    if ((q & 0x7F800000u) == 0u) q = 0u;
    return sign | q;
}
uint vuFromInt(int v)
{
    // exact in double: the upper 20 bits and the lower 12 bits are both exact floats
    precise double d = (double)((float)(v >> 12)) * 4096.0 + (double)((float)(v & 0xFFF));
    return vuRound(d, 0.0);
}
int vuFixed(uint v, float scale)
{
    return (int)asfloat(vuMul(v, asuint(scale)));
}
bool vuLess(uint a, uint b) { return asfloat(a) < asfloat(b); }

VpRaw vpEvaluate(VpIn i)
{
    const int base = (int)(i.sel.x * 16u);
    uint4 m0 = VpConstants.Load(base + 0), m1 = VpConstants.Load(base + 1);
    uint4 m2 = VpConstants.Load(base + 2), m3 = VpConstants.Load(base + 3);
    uint4 kScale = VpConstants.Load(base + 4), kOffset = VpConstants.Load(base + 5);
    uint4 kFog = VpConstants.Load(base + 6);
    int4 kColour = (int4)VpConstants.Load(base + 7);
    uint p[4], clip[4];
    int fixedPoint[4];
    int l;
    for (l = 0; l < 4; ++l) p[l] = vuOperand(i.pos[l]);
    for (l = 0; l < 4; ++l)
    {
        uint acc = vuMul(vuOperand(m0[l]), p[0]);
        acc = vuAdd(acc, vuMul(vuOperand(m1[l]), p[1]));
        acc = vuAdd(acc, vuMul(vuOperand(m2[l]), p[2]));
        clip[l] = vuAdd(acc, vuMul(vuOperand(m3[l]), p[3]));
    }
    const uint q = vuReciprocal(clip[3]);
    uint depth = vuLess(clip[3], vuOperand(kFog.z)) ? clip[3] : vuOperand(kFog.z);
    depth = vuLess(vuOperand(kFog.y), depth) ? depth : vuOperand(kFog.y);
    for (l = 0; l < 4; ++l)
    {
        uint acc = vuOperand(kOffset[l]);
        if (l == 3) acc = vuAdd(acc, vuNeg(vuMul(vuOperand(kFog.w), depth)));
        const uint scaled = vuMul(clip[l], vuOperand(kScale[l]));
        fixedPoint[l] = vuFixed(vuAdd(acc, vuMul(scaled, q)), 16.0);
    }
    VpRaw o;
    o.colour.a = (uint)(kColour.w & 0xFF);
    if (i.sel.y == 1u)
    {
        o.colour.rgb = uint3((uint)(kColour.x & 0xFF), (uint)(kColour.y & 0xFF), (uint)(kColour.z & 0xFF));
    }
    else
    {
        uint4 d0 = VpConstants.Load(base + 8), d1 = VpConstants.Load(base + 9);
        uint4 d2 = VpConstants.Load(base + 10), d3 = VpConstants.Load(base + 11);
        int4 c0 = (int4)VpConstants.Load(base + 12), c1 = (int4)VpConstants.Load(base + 13);
        int4 c2 = (int4)VpConstants.Load(base + 14), c3 = (int4)VpConstants.Load(base + 15);
        uint n[4], term[4];
        for (l = 0; l < 4; ++l) n[l] = vuFromInt(i.nrm[l]);
        for (l = 0; l < 4; ++l)
        {
            uint acc = vuMul(vuOperand(d0[l]), n[0]);
            acc = vuAdd(acc, vuMul(vuOperand(d1[l]), n[1]));
            acc = vuAdd(acc, vuMul(vuOperand(d2[l]), n[2]));
            acc = vuAdd(acc, vuMul(vuOperand(d3[l]), n[3]));
            term[l] = vuLess(0u, acc) ? acc : 0u;
        }
        for (l = 0; l < 3; ++l)
        {
            uint acc = vuMul(vuFromInt(c0[l]), term[0]);
            acc = vuAdd(acc, vuMul(vuFromInt(c1[l]), term[1]));
            acc = vuAdd(acc, vuMul(vuFromInt(c2[l]), term[2]));
            acc = vuAdd(acc, vuMul(vuFromInt(c3[l]), term[3]));
            acc = vuAdd(acc, vuFromInt(kColour[l]));
            const uint limit = asuint(200.0);
            acc = vuLess(acc, limit) ? acc : limit;
            o.colour[l] = (uint)(vuFixed(acc, 1.0) & 0xFF);
        }
    }
    o.place = uint4((uint)(fixedPoint[0] & 0xFFFF), (uint)(fixedPoint[1] & 0xFFFF), (uint)fixedPoint[2],
                    (uint)fixedPoint[3]);
    o.stq = uint4(vuMul(vuOperand(i.st.x), q), vuMul(vuOperand(i.st.y), q), vuMul(vuOperand(i.st.z), q), 0u);
    return o;
}

#ifdef VP_RAW_ONLY
struct VpRawOut
{
    uint4 place : RAWPLACE;
    uint4 colour : RAWCOLOUR;
    uint4 stq : RAWSTQ;
    float4 pos : SV_Position;
};
VpRawOut VSProgramRaw(VpIn i)
{
    VpRaw r = vpEvaluate(i);
    VpRawOut o;
    o.place = r.place;
    o.colour = r.colour;
    o.stq = r.stq;
    o.pos = float4(0, 0, 0, 1);
    return o;
}
#else
VSOut VSProgram(VpIn i)
{
    VpRaw r = vpEvaluate(i);
    VSIn o;
    o.pf.x = (float)((int)r.place.x - vp_offset.x) * (1.0 / 16.0);
    o.pf.y = (float)((int)r.place.y - vp_offset.y) * (1.0 / 16.0);
    o.pf.z = (float)r.place.z;
    o.pf.w = (float)((r.place.w >> 4) & 0xFFu);
    o.col = float4(r.colour);
    o.stq = float3(asfloat(r.stq.x), asfloat(r.stq.y), asfloat(r.stq.z));
    return VSMain(o);
}
#endif
)HLSL";

} // namespace gsr
