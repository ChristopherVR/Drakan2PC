#pragma once
// The compute kernel of surface_gpu.h (cs_5_0, double-precision extensions of Direct3D 11.1). One thread group per
// streamed draw; every line mirrors reconstructDraw, submitDraw and packFrameAttributes. Float additions,
// subtractions and multiplications are IEEE operations (`precise`, no contraction); division and square root are
// rounded correctly through double precision, as the CPU does them.
namespace gsr::surface {
inline constexpr char kSurfaceGpuHlsl[] = R"HLSL(
cbuffer Constants : register(b0) {
    float3 gOrigin; uint gTextures;
    float gPackedMaterial; uint gCellCount; uint gDrawCount; uint gAttributeOffset;
    uint gObserveCapacity; uint3 gPad;
};
struct Draw { uint first, count, slot, camera; int ofx, ofy; uint flags, alphaTest; uint alphaRef, tfx, texMin, textureId; uint4 pad; };
struct Camera { uint4 inverse[8]; float4 xyOffset; float4 offsetScale; uint4 valid; uint4 pad; };
Buffer<uint> Stream : register(t0);
StructuredBuffer<Draw> Draws : register(t1);
StructuredBuffer<Camera> Cameras : register(t2);
StructuredBuffer<uint4> Cells : register(t3);
StructuredBuffer<float4> Entries : register(t4);
// The output words in rows of 16384 (a texture: Direct3D 11 and 12 share textures, not buffers); row r of the texture
// is words [r * 16384, (r + 1) * 16384) of GpuSurfaceLayout.
RWTexture2D<uint> Out : register(u0);
void store(uint word, uint value) { Out[uint2(word & 16383u, word >> 14)] = value; }
RWBuffer<uint> Counters : register(u1); // lost observes, emitted triangles, observe records
// The resident observes the CPU path would have made (ResidentScene::observe), for the renderer to apply a few worlds
// later: slot, resident index, texture, the three world positions and UVs (GpuObserve, 18 words each).
RWBuffer<uint> Observes : register(u2);

static const uint kIip = 1, kZmask = 2, kBlend = 4, kAlpha44 = 8, kAlphaTestOn = 16, kTextureAlpha = 32,
                  kMaterialEligible = 64, kHasTexture = 128;
groupshared uint sAlpha;

bool finiteD(double d) { uint lo, hi; asuint(d, lo, hi); return (hi & 0x7ff00000u) != 0x7ff00000u; }
float fdiv(float a, float b) { return (float)((double)a / (double)b); }
// The correctly rounded square root of a float (std::sqrt): the hardware's estimate moved to the neighbour whose
// rounding interval holds x. The interval bounds are midpoints of two floats (25 bits), squared exactly in double.
float fsqrt(float x) {
    float s = sqrt(x);
    if (!(x > 0) || !isfinite(x) || !(s > 0) || !isfinite(s)) return s;
    [unroll] for (uint i = 0; i < 2; ++i) {
        const float up = asfloat(asuint(s) + 1), down = asfloat(asuint(s) - 1);
        const double hi = ((double)s + (double)up) * 0.5L, lo = ((double)s + (double)down) * 0.5L;
        if ((double)x > hi * hi) s = up;
        else if ((double)x < lo * lo) s = down;
    }
    return s;
}
float3 fsub3(float3 a, float3 b) { precise float3 r = a - b; return r; }
float3 fcross(float3 a, float3 b) {
    precise float x = a.y * b.z - a.z * b.y;
    precise float y = a.z * b.x - a.x * b.z;
    precise float z = a.x * b.y - a.y * b.x;
    return float3(x, y, z);
}
float flength(float3 a) { precise float sum = a.x * a.x + a.y * a.y + a.z * a.z; return fsqrt(sum); }

double inverseAt(Camera c, uint k) {
    const uint4 v = c.inverse[k >> 1];
    return (k & 1) ? asdouble(v.z, v.w) : asdouble(v.x, v.y);
}
// RasterToWorld::point.
bool rasterToWorld(Camera c, float x, float y, float rawZ, out float3 o) {
    o = 0;
    if (c.valid.x == 0) return false;
    precise float ax = (x + c.xyOffset.x) - c.xyOffset.z;
    precise float ay = (y + c.xyOffset.y) - c.xyOffset.w;
    const double p0 = (double)fdiv(ax, c.offsetScale.y);
    const double p1 = (double)fdiv(ay, c.offsetScale.z);
    const double p2 = ((double)rawZ / 16.0L - (double)c.offsetScale.x) / (double)c.offsetScale.w;
    double q[4];
    [unroll] for (uint j = 0; j < 4; ++j) {
        double sum = 0.0L;
        sum = sum + p0 * inverseAt(c, 0 * 4 + j);
        sum = sum + p1 * inverseAt(c, 1 * 4 + j);
        sum = sum + p2 * inverseAt(c, 2 * 4 + j);
        sum = sum + 1.0L * inverseAt(c, 3 * 4 + j);
        q[j] = sum;
    }
    if (!finiteD(q[3]) || abs(q[3]) < 1e-12L) return false;
    o = float3((float)(q[0] / q[3]), (float)(q[1] / q[3]), (float)(q[2] / q[3]));
    return all(isfinite(o));
}
// int32_t(f) of a floored value as x64 converts it: out of range and NaN give INT_MIN.
uint cellIndex(float f) {
    const float floored = floor(f);
    return abs(floored) < 2147483648.0f ? asuint((int)floored) : 0x80000000u;
}
// ResidentScene::match(tri, false) >= 0.
int residentMatch(float3 a, float3 b, float3 c) {
    precise float px = fdiv((a.x + b.x) + c.x, 3.0f);
    precise float py = fdiv((a.y + b.y) + c.y, 3.0f);
    precise float pz = fdiv((a.z + b.z) + c.z, 3.0f);
    const uint keyHigh = cellIndex(px * 0.00048828125f), keyLow = cellIndex(pz * 0.00048828125f);
    uint low = 0, high = gCellCount;
    while (low < high) {
        const uint mid = (low + high) >> 1;
        const uint4 cell = Cells[mid];
        if (cell.x < keyHigh || (cell.x == keyHigh && cell.y < keyLow)) low = mid + 1;
        else high = mid;
    }
    if (low >= gCellCount) return -1;
    const uint4 cell = Cells[low];
    if (cell.x != keyHigh || cell.y != keyLow) return -1;
    const float3 q[3] = {a, b, c};
    for (uint e = cell.z; e < cell.z + cell.w; ++e) {
        const float4 g0 = Entries[e * 4 + 0], g1 = Entries[e * 4 + 1], g2 = Entries[e * 4 + 2], g3 = Entries[e * 4 + 3];
        // x0 y0 z0 ax | az bx bz det | rdet nx ny nz | length
        precise float nu = (px - g0.x) * g1.z - (pz - g0.z) * g1.y;
        precise float nv = g0.w * (pz - g0.z) - g1.x * (px - g0.x);
        precise float pu = nu * g2.x, pv = nv * g2.x;
        precise float bound = pu + pv - 1e-5f * (abs(pu) + abs(pv));
        if (pu < -.0101f || pv < -.0101f || bound > 1.0101f) continue;
        const float u = fdiv(nu, g1.w), v = fdiv(nv, g1.w);
        precise float uv = u + v;
        if (u < -.01f || v < -.01f || uv > 1.01f) continue;
        precise float limit = g3.x * 32.f;
        bool samePlane = true;
        [unroll] for (uint k = 0; k < 3; ++k) {
            precise float d = g2.y * (q[k].x - g0.x) + g2.z * (q[k].y - g0.y) + g2.w * (q[k].z - g0.z);
            if (abs(d) > limit) samePlane = false;
        }
        if (samePlane) return (int)asuint(g3.y); // the first in the cell's insertion order, as match()
    }
    return -1;
}
uint quantize(float x) {
    precise float scaled = (clamp(x, -1.f, 1.f) * .5f + .5f) * 4095;
    return (uint)((double)scaled + 0.5L);
}
float packNormal(float3 n) {
    precise float l1 = (abs(n.x) + abs(n.y)) + abs(n.z);
    if (!(l1 > 0) || !isfinite(l1)) n = float3(0, 0, 1);
    else n = float3(fdiv(n.x, l1), fdiv(n.y, l1), fdiv(n.z, l1));
    float u = n.x, v = n.y;
    if (n.z < 0) {
        precise float pu = (1 - abs(n.y)) * (n.x >= 0 ? 1.f : -1.f);
        precise float pv = (1 - abs(n.x)) * (n.y >= 0 ? 1.f : -1.f);
        u = pu; v = pv;
    }
    return (float)(quantize(u) | quantize(v) << 12);
}
struct Vertex { float x, y, z; uint4 colour; float s, t, q; };
Vertex loadVertex(uint index, int ofx, int ofy) {
    const uint b = index * 12;
    Vertex v;
    v.x = (float)((int)(Stream[b + 0] & 0xFFFF) - ofx) * (1.0f / 16.0f);
    v.y = (float)((int)(Stream[b + 1] & 0xFFFF) - ofy) * (1.0f / 16.0f);
    v.z = (float)(double)Stream[b + 2];
    v.colour = uint4(Stream[b + 4], Stream[b + 5], Stream[b + 6], Stream[b + 7]) & 0xFF;
    v.s = asfloat(Stream[b + 8]);
    v.t = asfloat(Stream[b + 9]);
    v.q = asfloat(Stream[b + 10]);
    return v;
}
void writePositions(uint slot, float3 p[3], bool emitted) {
    [unroll] for (uint j = 0; j < 3; ++j) {
        precise float3 r = p[j] - gOrigin;
        store(slot * 9 + j * 3 + 0, emitted ? asuint(r.x) : 0x7fc00000u);
        store(slot * 9 + j * 3 + 1, emitted ? asuint(r.y) : 0x7fc00000u);
        store(slot * 9 + j * 3 + 2, emitted ? asuint(r.z) : 0x7fc00000u);
    }
}
void writeAttribute(uint slot, uint index, float4 a) {
    const uint b = gAttributeOffset + (slot * 7 + index) * 4;
    store(b + 0, asuint(a.x)); store(b + 1, asuint(a.y)); store(b + 2, asuint(a.z)); store(b + 3, asuint(a.w));
}

[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    const Draw d = Draws[group.x];
    const bool iip = (d.flags & kIip) != 0;
    // submitDraw: the minimum vertex alpha of the draw as the core queued it (flat shading gives every vertex of a
    // triangle its last vertex's colour).
    if (thread == 0) sAlpha = 255;
    GroupMemoryBarrierWithGroupSync();
    uint alpha = 255;
    for (uint i = thread; i < d.count; i += 64) {
        const uint source = iip ? i : (i / 3) * 3 + 2;
        alpha = min(alpha, Stream[(d.first + source) * 12 + 7] & 0xFF);
    }
    InterlockedMin(sAlpha, alpha);
    GroupMemoryBarrierWithGroupSync();
    const uint vertexAlpha = d.count ? sAlpha : 255;
    const bool blend = (d.flags & kBlend) != 0, testOn = (d.flags & kAlphaTestOn) != 0;
    const bool textureAlpha = (d.flags & kTextureAlpha) != 0;
    const uint alphaSource = textureAlpha ? d.texMin : vertexAlpha;
    const bool constantAlphaPass =
        (!textureAlpha || (d.tfx == 0 && vertexAlpha >= 128)) &&
        ((d.alphaTest == 5 && alphaSource >= d.alphaRef) || (d.alphaTest == 6 && alphaSource > d.alphaRef));
    const bool opaqueBlend =
        !blend || ((d.flags & kAlpha44) && vertexAlpha >= 128 && (!textureAlpha || (d.tfx == 0 && d.texMin >= 128)));
    const bool opaqueEligible = !(d.flags & kZmask) && opaqueBlend && (!testOn || d.alphaTest == 1 || constantAlphaPass);
    const bool materialEligible = (d.flags & kMaterialEligible) != 0;
    const bool hasTexture = (d.flags & kHasTexture) != 0;
    const bool transparent = blend && !opaqueBlend;
    const uint alphaTest = testOn ? d.alphaTest : 1;
    const uint flags = alphaTest | (transparent ? 8u : 0u) | (textureAlpha ? 16u : 0u) | (d.tfx << 5) | 256u;
    const float textureIndex = hasTexture && d.textureId < gTextures ? (float)d.textureId : -1.f;
    const Camera camera = Cameras[d.camera];
    uint lost = 0, emittedCount = 0;
    for (uint t = thread; t * 3 < d.count; t += 64) {
        const uint slot = d.slot + t;
        Vertex v[3];
        [unroll] for (uint j = 0; j < 3; ++j) v[j] = loadVertex(d.first + t * 3 + j, d.ofx, d.ofy);
        if (!iip) { v[0].colour = v[2].colour; v[1].colour = v[2].colour; }
        float3 p[3];
        bool valid = opaqueEligible || materialEligible;
        [unroll] for (uint k = 0; k < 3; ++k) {
            float3 w = 0;
            valid = valid && v[k].z > 0 && rasterToWorld(camera, v[k].x, v[k].y, v[k].z, w);
            p[k] = w;
        }
        valid = valid && !(flength(fsub3(p[1], p[0])) > 2000000);
        const int residentIndex = valid ? residentMatch(p[0], p[1], p[2]) : -1;
        const bool resident = residentIndex >= 0;
        const bool emit = valid && !resident;
        if (resident && opaqueEligible && hasTexture && d.textureId != 0xffffffffu) {
            ++lost;
            uint at;
            InterlockedAdd(Counters[2], 1, at);
            if (at < gObserveCapacity) {
                const uint b = at * 18;
                Observes[b + 0] = slot;
                Observes[b + 1] = (uint)residentIndex;
                Observes[b + 2] = d.textureId;
                [unroll] for (uint k = 0; k < 3; ++k) {
                    Observes[b + 3 + k * 3 + 0] = asuint(p[k].x);
                    Observes[b + 3 + k * 3 + 1] = asuint(p[k].y);
                    Observes[b + 3 + k * 3 + 2] = asuint(p[k].z);
                    float2 o = float2(0, 0);
                    if ((double)abs(v[k].q) > 1e-12L) o = float2(fdiv(v[k].s, v[k].q), fdiv(v[k].t, v[k].q));
                    Observes[b + 12 + k * 2 + 0] = asuint(o.x);
                    Observes[b + 12 + k * 2 + 1] = asuint(o.y);
                }
            }
        }
        writePositions(slot, p, emit);
        if (!emit) {
            [unroll] for (uint a = 0; a < 7; ++a) writeAttribute(slot, a, float4(0, 0, 0, 0));
            continue;
        }
        ++emittedCount;
        float2 uv[3];
        [unroll] for (uint m = 0; m < 3; ++m) {
            uv[m] = float2(0, 0);
            if ((double)abs(v[m].q) > 1e-12L) uv[m] = float2(fdiv(v[m].s, v[m].q), fdiv(v[m].t, v[m].q));
            const float4 colour = (float4)v[m].colour;
            writeAttribute(slot, m, float4(colour.r * (1.0f / 128.0f), colour.g * (1.0f / 128.0f),
                                           colour.b * (1.0f / 128.0f), clamp(colour.a * (1.0f / 128.0f), 0.f, 1.f)));
        }
        writeAttribute(slot, 3, float4(uv[0], uv[1]));
        float3 normal = fcross(fsub3(p[1], p[0]), fsub3(p[2], p[0]));
        const float len = flength(normal);
        if (len > 0) normal = float3(fdiv(normal.x, len), fdiv(normal.y, len), fdiv(normal.z, len));
        writeAttribute(slot, 4, float4(uv[2], normal.x, normal.y));
        writeAttribute(slot, 5, float4(normal.z, textureIndex, (float)flags, (float)d.alphaRef));
        const float packed = packNormal(normal);
        writeAttribute(slot, 6, float4(packed, packed, packed, gPackedMaterial));
    }
    if (lost) InterlockedAdd(Counters[0], lost);
    if (emittedCount) InterlockedAdd(Counters[1], emittedCount);
}
)HLSL";
} // namespace gsr::surface
