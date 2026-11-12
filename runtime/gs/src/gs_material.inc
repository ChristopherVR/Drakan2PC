// Adjacent C++ string literal, included inside the main HLSL initializer (gs_shaders.h) when MATERIAL is defined.
// Material detail shading: a per-pixel correction of the sampled texel from the texture's material map. It relights
// the relief inside a texture and leaves the scene's own lighting (the game's baked vertex colours, fog) alone: a flat
// normal returns the texel unchanged, bit for bit. The arithmetic is mirrored in material_detail.h (materialShade),
// which the unit test checks against known cases; a WGSL port must follow the same steps.
R"HLSL(
cbuffer CBM : register(b4)
{
    float4 m_proj;   // view ray through a pixel: (pos.x * m_proj.x + m_proj.y, pos.y * m_proj.z + m_proj.w, m_color.w)
    float4 m_light;  // xyz = unit direction to the light in view space, w = detail strength (0 = off)
    float4 m_param;  // x = specular strength, y = occlusion strength, z = shade given to faces turned from the light, w = debug view
    float4 m_tex;    // xy = 1 / logical texture size, z = view distance where the detail starts to fade, w = 1 / fade length
    float4 m_color;  // rgb = light colour for the specular term (0..1), w = z of the view ray (-1: the camera looks down -z)
};
Texture2D<float4> MatTex : register(t3);   // R, G = normal x, y (128 = 0); B = occlusion (200 = none); A = roughness
SamplerState SmpMat : register(s3);

float3 materialShade(float3 texel, float2 uv, float2 duvdx, float2 duvdy, float2 pos, float q)
{
    // View-space position from the pixel and 1/w: Q is linear in screen space, so this is exact per pixel.
    float w = 1.0 / max(abs(q), 1e-20);
    float3 P = float3(pos.x * m_proj.x + m_proj.y, pos.y * m_proj.z + m_proj.w, m_color.w) * w;
    float3 dp1 = ddx(P), dp2 = ddy(P);
    float3 G = cross(dp1, dp2);
    float gl = dot(G, G);
    if (!(gl > 1e-30)) return texel;
    G *= rsqrt(gl);
    // Gradients of u and v on the surface (exact solution of dP . T = du for both screen axes, T in the surface).
    float3 a = cross(dp2, G), b = cross(G, dp1);
    float3 T = a * duvdx.x + b * duvdy.x;
    float3 B = a * duvdx.y + b * duvdy.y;
    float tl = dot(T, T), bl = dot(B, B);
    if (!(tl > 1e-30) || !(bl > 1e-30)) return texel;
    T *= rsqrt(tl);
    B *= rsqrt(bl);
    float3 N = dot(G, P) > 0.0 ? -G : G;   // geometric normal facing the eye

    float4 m = MatTex.SampleGrad(SmpMat, uv * m_tex.xy, duvdx * m_tex.xy, duvdy * m_tex.xy);
    float2 nxy = (m.xy * 255.0 - 128.0) / 127.0;
    float fade = saturate(1.0 - (w - m_tex.z) * m_tex.w);
    float strength = m_light.w * fade;
    float3 L = m_light.xyz;
    float facing = lerp(m_param.z, 1.0, saturate(dot(N, L) * 2.0 + 0.5));
    // Change of the cosine to the light caused by the relief; the term of the flat normal itself is left out, so the
    // mean of a texture does not move.
    float delta = (nxy.x * dot(T, L) + nxy.y * dot(B, L)) * facing;
    float shade = clamp(1.0 + strength * delta, 0.5, 1.6);
    float occlusion = lerp(1.0, min(m.b * (255.0 / 200.0), 1.2), m_param.y * min(strength, 1.0));
    float3 result = texel * (shade * occlusion);
    if (m_param.x > 0.0)
    {
        float nz = sqrt(saturate(1.0 - dot(nxy, nxy)));
        float3 n = normalize(T * nxy.x + B * nxy.y + N * nz);
        float3 V = normalize(-P);
        float3 H = normalize(L + V);
        float rough = max(m.a, 0.08);
        float a2 = rough * rough;
        float shininess = 2.0 / (a2 * a2) - 2.0;
        float lobe = pow(saturate(dot(n, H)), max(shininess, 1.0));
        float gloss = (1.0 - rough) * (1.0 - rough);
        result += m_color.rgb * (255.0 * m_param.x * fade * gloss * lobe * saturate(dot(N, L) * 4.0));
    }
    if (m_param.w > 0.5)
    {
        if (m_param.w < 1.5) return (N * 0.5 + 0.5) * 255.0;                                   // 1: geometric normal
        if (m_param.w < 2.5) return float3(nxy * 0.5 + 0.5, 1.0) * 255.0;                      // 2: the map's normal
        if (m_param.w < 3.5) return saturate(0.5 + 0.5 * strength * delta) * float3(255, 255, 255); // 3: the detail term
        return (T * 0.5 + 0.5) * 255.0;                                                        // 4: u direction
    }
    return clamp(result, 0.0, 255.0);
}
)HLSL"
