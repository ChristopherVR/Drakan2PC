// Adjacent C++ string literal, included inside the main HLSL initializer (gs_shaders.h).
// Terrain detail: the shader side of terrain_detail.h (constants, weights and amplitudes are defined there and the
// CPU reference in that header mirrors this code). Runs only for draws flagged F_TERRAIN / F_TERRAIN_SIDE.
R"HLSL(
// ---------------------------------------------------------------- terrain detail
cbuffer CBT : register(b3)
{
    float4 t_base;      // xyz camera-relative base, w = strength
    float4 t_d0;        // w = detail on
    float4 t_dx;        // w = side projection on
    float4 t_dy;        // w = debug mode
    float4 t_octave[3]; // xyz = anchor offset in noise tiles, w = 1 / period
    float4 t_amp;       // fine, medium, slow, relief
    float4 t_sampling;  // w = gradient scale
    float4 t_side;      // xyz = anchor modulo the side projection period
    float4 t_relief;    // x = fine weight, y = medium weight, z = fade start, w = 1 / fade range
    float4 t_reliefFar; // x = relief strength at the far end of the fade
    float4 t_grid;      // xy = anchor in grid cells relative to the grid origin, zw = grid size (0 = no grid)
    float4 t_gridY;     // x = anchor height, y = height tolerance, z = cliff start (normal y), w = 1 / cliff range
};
Texture3D<float2> TerrainNoise : register(t2); // r = noise, g = the noise one relief step towards the light
Texture2DArray<float4> TerrainGrid : register(t4); // smooth normals of the height field: nx, nz, height, ny
SamplerState SmpNoise : register(s2);

#define F_TERRAIN      (1u<<26)
#define F_TERRAIN_SIDE (1u<<27)

static const float3x3 kTerrainRot1 = float3x3(0.8657570, -0.2803366, 0.4145796, 0.4145796, 0.8657570, -0.2803366, -0.2803366, 0.4145796, 0.8657570);
static const float3x3 kTerrainRot2 = float3x3(0.8288755, -0.4966948, -0.2574096, -0.0066126, 0.4513950, -0.8922997, 0.5593940, 0.7413075, 0.3708658);

struct TerrainPoint
{
    float3 rel;        // camera-relative world position
    float3 dx, dy;     // its screen derivatives
    float3 n;          // unit face normal
};

// pos: SV_Position.xy; q and its screen derivatives (q is linear in screen space, so they are exact per triangle)
TerrainPoint terrainPoint(float2 pos, float q, float2 dq)
{
    TerrainPoint p;
    float w = 1.0 / q;
    float3 dir = t_d0.xyz + pos.x * t_dx.xyz + pos.y * t_dy.xyz;
    p.rel = w * dir + t_base.xyz;
    float w2 = w * w;
    p.dx = (-dq.x * w2) * dir + w * t_dx.xyz;
    p.dy = (-dq.y * w2) * dir + w * t_dy.xyz;
    float3 c = cross(p.dx, p.dy);
    p.n = c * rsqrt(max(dot(c, c), 1e-20));
    return p;
}

float2 terrainOctave(float3 r, float3 rdx, float3 rdy, int o)
{
    float f = t_octave[o].w;
    float g = f * t_sampling.w;
    return TerrainNoise.SampleGrad(SmpNoise, r * f + t_octave[o].xyz, rdx * g, rdy * g);
}

// Factor for the layer colour (before fog), and the medium octave for dithering the side projection.
float terrainDetail(TerrainPoint p, out float medium)
{
    float2 n0 = terrainOctave(p.rel, p.dx, p.dy, 0);
    float2 n1 = terrainOctave(mul(kTerrainRot1, p.rel), mul(kTerrainRot1, p.dx), mul(kTerrainRot1, p.dy), 1);
    float2 n2 = terrainOctave(mul(kTerrainRot2, p.rel), mul(kTerrainRot2, p.dx), mul(kTerrainRot2, p.dy), 2);
    medium = n1.x - 0.5;
    // the fine and medium octaves and the relief are close-range detail and fade with distance; the slow octave
    // (against the visible tile repeat) does not
    float fade = lerp(1.0, t_reliefFar.x, saturate((length(p.rel) - t_relief.z) * t_relief.w));
    float m = t_amp.x * (n0.x - 0.5) * 2.0 + t_amp.y * (n1.x - 0.5) * 2.0;
    m += t_amp.w * ((n0.x - n0.y) * t_relief.x + (n1.x - n1.y) * t_relief.y);
    m = m * fade + t_amp.z * (n2.x - 0.5) * 2.0;
    return max(0.0, 1.0 + t_base.w * m);
}

float terrainMedium(TerrainPoint p)
{
    return terrainOctave(mul(kTerrainRot1, p.rel), mul(kTerrainRot1, p.dx), mul(kTerrainRot1, p.dy), 1).x - 0.5;
}

// The normal of the height field itself at the pixel (terrain_detail.h, smoothNormal): bilinear over the four grid
// vertices of its cell, each taking the stacked entry nearest the height the triangle of the pixel predicts there,
// else the face normal. Points up (y >= 0).
float3 terrainSmoothNormal(TerrainPoint p)
{
    float3 up = p.n.y < 0.0 ? -p.n : p.n;
    if (t_grid.z < 0.5) return up;
    float2 c = p.rel.xz * (1.0 / 2048.0) + t_grid.xy;
    float2 fl = floor(c);
    float2 f = c - fl;
    int2 i0 = int2(fl);
    float worldY = p.rel.y + t_gridY.x;
    float3 acc = float3(0, 0, 0);
    [loop] for (int corner = 0; corner < 4; ++corner)
    {
        int2 o = int2(corner & 1, corner >> 1);
        float2 wv = lerp(1.0 - f, f, float2(o));
        int2 g = i0 + o;
        float3 n = up;
        if (g.x >= 0 && g.y >= 0 && g.x < (int)t_grid.z && g.y < (int)t_grid.w)
        {
            float2 d = (float2(o) - f) * 2048.0;
            float expected = up.y > 0.02 ? worldY - (up.x * d.x + up.z * d.y) / up.y : worldY;
            float best = t_gridY.y;
            [unroll] for (int slot = 0; slot < 4; ++slot)
            {
                float4 texel = TerrainGrid.Load(int4(g, slot, 0));
                float dist = abs(texel.z - expected);
                if (dist < best) { best = dist; n = float3(texel.x, texel.w, texel.y); }
            }
        }
        acc += wv.x * wv.y * n;
    }
    return acc * rsqrt(max(dot(acc, acc), 1e-12));
}

// Steep faces: the layer texture sampled from the side (world y against x or z) at the density of the mapping from
// above (cb_g.w, logical texels per world unit). Fills projections 1 (plane x) and 2 (plane z) and returns the weight
// left for the sample from above; the caller samples every projection with a weight in one loop (a second and third
// inlined copy of sampleTexture made the shader compile ten times slower).
float terrainSide(TerrainPoint p, float dither, out float2 uv1, out float2 dx1, out float2 dy1, out float w1,
                  out float2 uv2, out float2 dx2, out float2 dy2, out float w2)
{
    float3 n = terrainSmoothNormal(p);
    float ny = n.y;
    float side = saturate((t_gridY.z - ny) * t_gridY.w + dither * 0.6);
    float ax = n.x * n.x, az = n.z * n.z;
    float planeZ = saturate((az / (ax + az + 1e-6) - 0.5) * 4.0 + 0.5 + dither * 0.5);
    float d = cb_g.w;
    float3 pm = p.rel + t_side.xyz;
    uv1 = float2(pm.z, -pm.y) * d; dx1 = float2(p.dx.z, -p.dx.y) * d; dy1 = float2(p.dy.z, -p.dy.y) * d;
    uv2 = float2(pm.x, -pm.y) * d; dx2 = float2(p.dx.x, -p.dx.y) * d; dy2 = float2(p.dy.x, -p.dy.y) * d;
    w1 = side * (1.0 - planeZ);
    w2 = side * planeZ;
    return 1.0 - side;
}
)HLSL"
