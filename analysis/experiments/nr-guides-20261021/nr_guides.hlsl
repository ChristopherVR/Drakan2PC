
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
