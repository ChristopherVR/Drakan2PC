cbuffer Frame : register(b0) {
    row_major float4x4 Projection;
    row_major float4x4 InverseProjection;
    float4 CameraRight, CameraUp, CameraForward;
    float4 SizeTime; // guide size, elapsed scene seconds, reflections
    float4 Jitter;
    float4 Limits; // near plane, opaque triangles, camera world x/z (world-anchored water detail)
    uint4 SceneInfo; // material texel offset, texture count, texture size
    float4 Ambient,LightVector[4],LightColor[4];
    uint4 Effects; // water, shadows, native light count
};
RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<float4> Materials : register(t1);
Texture2D<float4> Color : register(t2);
Texture2D<float> Depth : register(t3);
RWTexture2D<float4> Output : register(u0);
RWStructuredBuffer<float4> Sky : register(u1);
groupshared float4 SkySamples[7][256];
float3 skyAxis(uint sector) {
    uint axis=(sector-1)/2;float sign=(sector&1)?1:-1;
    return axis==0?float3(sign,0,0):axis==1?float3(0,sign,0):float3(0,0,sign);
}
float3 toLinear(float3 c) {return pow(max(c,0),2.2);}
float3 toDisplay(float3 c) {return pow(saturate(c),1.0/2.2);}
// World scale: Rynn's resident mesh is 835 units tall, so one metre is about 500 units.
static const float kMetre=500;
static const float kSunRadius=.006;              // angular radius (rad) of the soft directional-light disk
static const float kPointLightRadius=.06*kMetre; // native point lights are points; give them a small emitter
static const float kTerminatorOffset=.1*kMetre;  // extra normal offset at grazing light (low-poly self-shadow)
static const uint kOcclusionRays=4;
static const float kOcclusionRadius=kMetre;      // ambient occlusion reach: contact grounding and creases
static const float kOcclusionStrength=.6;        // full-strength AO inks every wall/ground contact like an outline
static const uint kNoInstance=0xffffffff;
static const uint kAttributes=7; // float4 material attributes per triangle (kTriangleAttributes)
static const float kWaterExtinction=.35/kMetre;  // Beer-Lambert extinction outside the water's own colour
static const float kFoamDepth=.06*kMetre;        // vertical depth over which shoreline foam forms
static const float kCausticStrength=.9;
static const float kRefraction=.12;              // screen-space refraction per unit of wave slope
// Directional wave spectrum: wavelength (m), slope amplitude, direction relative to the wind (rad).
// Deep-water dispersion sets each wave's speed, so longer waves travel faster than short chop.
static const float kWind=.6;
static const uint kWaveCount=8;
static const float3 kWaves[kWaveCount]={float3(4.3,.022,0),float3(2.7,.02,.55),float3(1.75,.018,-.4),
                                        float3(1.13,.016,1),float3(.71,.014,-.85),float3(.46,.012,.3),
                                        float3(.29,.01,-1.3),float3(.18,.008,1.6)};
float luminance(float3 c) {return dot(c,float3(.2126,.7152,.0722));}
// Interleaved gradient noise in jitter-compensated pixel coordinates. A whole-pixel jitter moves the pattern with
// the image; the subpixel phase decorrelates frames so a temporal resolve (DLSS) integrates the samples, and a
// still camera without jitter keeps one stable pattern.
float pixelNoise(uint2 id,uint salt) {
    float2 p=float2(id)-floor(Jitter.xy)+frac(Jitter.xy)*float2(113,71)+float(salt)*float2(47,17);
    return frac(52.9829189*frac(dot(p,float2(.06711056,.00583715))));
}
// Vogel (golden-angle) disk: even coverage for any sample count, rotated per pixel.
float2 diskSample(uint i,uint count,float rotation) {
    float r=sqrt((i+.5)/count),a=i*2.39996323+rotation*6.2831853;
    return r*float2(cos(a),sin(a));
}
void basis(float3 n,out float3 t,out float3 b) {
    t=normalize(cross(n,abs(n.y)>.95?float3(1,0,0):float3(0,1,0)));b=cross(n,t);
}
// Octahedral unit vector packed as an exact integer float, 12 bits per axis (packNormal in ray_normals.h).
float3 unpackNormal(float packed) {
    uint bits=uint(packed);float2 e=float2(bits&4095,bits>>12)/4095*2-1;
    float3 n=float3(e,1-abs(e.x)-abs(e.y));
    if(n.z<0)n.xy=(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1);
    return normalize(n);
}
float hashNoise(int2 p) {
    uint h=uint(p.x)*0x8da6b343u^uint(p.y)*0xd8163841u;h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;h*=0x297a2d39u;h^=h>>15;
    return h/4294967296.;
}
float valueNoise(float2 p) {
    int2 i=int2(floor(p));float2 f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(hashNoise(i),hashNoise(i+int2(1,0)),f.x),lerp(hashNoise(i+int2(0,1)),hashNoise(i+1),f.x),f.y);
}
// Reduce sky radiance once per frame. Sparse probes miss the narrow visible
// sky between canyon walls and incorrectly substitute the water's own color.
[numthreads(16,16,1)]
void skyMain(uint3 thread : SV_GroupThreadID) {
    uint index=thread.y*16+thread.x;float4 sums[7];
    [unroll]for(uint slot=0;slot<7;++slot)sums[slot]=0;
    // Observe the full viewport once. Seven separate reductions used to load
    // and linearise the same upper-half pixels seven times, and missed sky
    // visible only through a lower opening or with a strongly pitched camera.
    [unroll]for(uint y=0;y<4;++y)[unroll]for(uint x=0;x<4;++x){
        float2 uv=(float2(thread.xy)+float2(x*16,y*16)+.5)/float2(64,64);
        int2 pixel=min(int2(uv*SizeTime.xy),int2(SizeTime.xy)-1);
        if(Depth.Load(int3(pixel,0))<=0){float3 c=Color.Load(int3(pixel,0)).rgb;if(all(isfinite(c))){
            float3 radiance=toLinear(c);sums[0]+=float4(radiance,1);
            float2 ndc=((float2(pixel)+.5-Jitter.xy)/SizeTime.xy)*float2(2,-2)+float2(-1,1);
            float4 v=mul(float4(ndc,1,1),InverseProjection);
            float3 direction=normalize(v.x*CameraRight.xyz+v.y*CameraUp.xyz-v.z*CameraForward.xyz);
            [unroll]for(uint sector=1;sector<7;++sector){
                float weight=saturate(dot(direction,skyAxis(sector)));
                weight*=weight;weight*=weight;weight*=weight;
                sums[sector]+=float4(radiance*weight,weight);
            }
        }}
    }
    [unroll]for(uint sector=0;sector<7;++sector)SkySamples[sector][index]=sums[sector];
    GroupMemoryBarrierWithGroupSync();
    [unroll]for(uint stride=128;stride>0;stride>>=1){
        if(index<stride)[unroll]for(uint sector=0;sector<7;++sector)SkySamples[sector][index]+=SkySamples[sector][index+stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if(index==0)[unroll]for(uint sector=0;sector<7;++sector){
        float4 sum=SkySamples[sector][0];
        if(sum.w>(sector?.25:0))Sky[sector]=float4(sum.rgb/sum.w,1);else if(Effects.w&8)Sky[sector]=0;
    }
}
float3 viewPosition(float3 p) {return float3(dot(p,CameraRight.xyz),dot(p,CameraUp.xyz),-dot(p,CameraForward.xyz));}
float4 texel(uint texture,int2 p) {
    uint size=SceneInfo.z;p=(p%int(size)+int(size))%int(size);
    uint index=texture*size*size+uint(p.y)*size+uint(p.x);
    if(Effects.w&16) {
        // Four original RGBA8 texels occupy one float4-sized storage entry.
        // Reinterpret the bits before conversion; packed bytes are not floats.
        uint pixel=asuint(Materials[SceneInfo.x+(index>>2)])[index&3];
        return float4(pixel&255,(pixel>>8)&255,(pixel>>16)&255,pixel>>24)/255.0;
    }
    return Materials[SceneInfo.x+index];
}
float4 materialTexel(uint primitive,float2 bary) {
    uint i=primitive*kAttributes;float3 weights=float3(1-bary.x-bary.y,bary.x,bary.y);
    int texture=int(Materials[i+5].y);
    if(texture<0 || uint(texture)>=SceneInfo.y)return float4(1,1,1,128./255.);
    float2 uv=Materials[i+3].xy*weights.x+Materials[i+3].zw*weights.y+Materials[i+4].xy*weights.z;
    float2 xy=frac(uv)*SceneInfo.z-.5;int2 p=int2(floor(xy));float2 f=frac(xy);
    return lerp(lerp(texel(texture,p),texel(texture,p+int2(1,0)),f.x),lerp(texel(texture,p+int2(0,1)),texel(texture,p+1),f.x),f.y);
}
// The texel modulated by its GS vertex colour: display-referred, as the GS blends it.
float3 materialDisplay(uint primitive,float2 bary) {
    uint i=primitive*kAttributes;float3 weights=float3(1-bary.x-bary.y,bary.x,bary.y);
    float3 light=Materials[i].rgb*weights.x+Materials[i+1].rgb*weights.y+Materials[i+2].rgb*weights.z;
    return materialTexel(primitive,bary).rgb*light;
}
float3 materialColor(uint primitive,float2 bary) {return toLinear(materialDisplay(primitive,bary));}
float materialAlpha(uint primitive,float2 bary) {
    uint i=primitive*kAttributes,flags=uint(Materials[i+5].z),func=flags&7;
    float3 weights=float3(1-bary.x-bary.y,bary.x,bary.y);
    float vertex=floor(dot(weights,float3(Materials[i].a,Materials[i+1].a,Materials[i+2].a))*128+.5);
    float a=vertex;
    if(flags&16){float t=floor(materialTexel(primitive,bary).a*255+.5);uint tfx=(flags>>5)&3;
        a=tfx==0?floor(t*vertex/128):tfx==2?t+vertex:t;}
    a=clamp(a,0,255);float ref=Materials[i+5].w;
    bool pass=func==1 || (func==2 && a<ref) || (func==3 && a<=ref) || (func==4 && a==ref) ||
        (func==5 && a>=ref) || (func==6 && a>ref) || (func==7 && a!=ref);
    if(!pass)return -1;
    return (flags&8)?saturate(a/128):1;
}
// Faceted native geometry: a neighbouring face across a shallow crease is the same smoothly lit (GS Gouraud)
// surface, not an occluder. Hits on surfaces facing within 30 degrees of the receiver are ignored, fully counted
// beyond 60 degrees. Rays leaving the receiver can only meet such a face at grazing angles.
float creaseWeight(uint primitive,float3x3 worldToObject,float3 receiver,float3 direction) {
    float3 localNormal=float3(Materials[primitive*kAttributes+4].zw,Materials[primitive*kAttributes+5].x);
    float3 hitNormal=normalize(mul(localNormal,worldToObject));
    if(dot(hitNormal,direction)>0)hitNormal=-hitNormal;
    float facing=dot(hitNormal,receiver);
    return isfinite(facing)?1-smoothstep(.5,.87,facing):1;
}
// `self` is the receiver's own native model instance (kNoInstance for level geometry and water). Low-poly
// characters and props do not shadow or occlude themselves: their flat faces would print as dark facets over the
// native per-vertex lighting (a cel-shaded face).
// The GS lights terrain and models per vertex, so direct light from a flat face normal prints every facet (shadowed
// cliffs and hillsides in per-triangle steps). Shade with the interpolated welded vertex normal, oriented like the
// (camera-facing) geometric normal; rays still leave along the geometric normal.
float3 shadingNormal(uint primitive,float2 bary,float3x3 worldToObject,float3 geometric) {
    float3 w=float3(1-bary.x-bary.y,bary.x,bary.y);float4 packed=Materials[primitive*kAttributes+6];
    float3 local=unpackNormal(packed.x)*w.x+unpackNormal(packed.y)*w.y+unpackNormal(packed.z)*w.z;
    float3 face=float3(Materials[primitive*kAttributes+4].zw,Materials[primitive*kAttributes+5].x);
    float3 n=normalize(mul(local,worldToObject));
    if(dot(mul(face,worldToObject),geometric)<0)n=-n;
    return all(isfinite(n)) && dot(n,geometric)>.1?n:geometric;
}
float visibility(float3 p,float3 normal,float3 direction,float maximum,uint self) {
    // The normal offset grows towards the terminator: faceted native terrain otherwise shadows its own
    // neighbours in triangle-shaped stripes where the smooth GS lighting is still lit.
    float offset=4+kTerminatorOffset*(1-saturate(dot(normal,direction)));
    RayDesc ray;ray.Origin=p+normal*offset+direction*2;ray.Direction=direction;ray.TMin=2;ray.TMax=maximum;
    float transmission=1,travelled=0;
    [loop]for(uint layer=0;layer<8 && transmission>.01;++layer){
        RayQuery<RAY_FLAG_NONE> query;query.TraceRayInline(Scene,RAY_FLAG_NONE,1,ray);
        while(query.Proceed())if(query.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE && query.CandidateInstanceIndex()!=self){
            uint primitive=(query.CandidateInstanceID()&0x7ffff)+query.CandidatePrimitiveIndex();
            if(!(uint(Materials[primitive*kAttributes+5].z)&256) && materialAlpha(primitive,query.CandidateTriangleBarycentrics())>0)query.CommitNonOpaqueTriangleHit();
        }
        if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)break;
        uint primitive=(query.CommittedInstanceID()&0x7ffff)+query.CommittedPrimitiveIndex();
        float alpha=saturate(materialAlpha(primitive,query.CommittedTriangleBarycentrics()))*float(query.CommittedInstanceID()>>19)/31.;
        // A low sun over a shallow terrain crease shadows the neighbouring face in per-triangle bands ("stitched"
        // terrain). Those bands are local: a gently sloped hillside further away still casts its shadow.
        travelled+=query.CommittedRayT();
        alpha*=lerp(creaseWeight(primitive,(float3x3)query.CommittedWorldToObject3x4(),normal,direction),1,
                    smoothstep(2*kMetre,5*kMetre,travelled));
        transmission*=1-alpha;float advance=query.CommittedRayT()+2;ray.Origin+=direction*advance;ray.TMax-=advance;travelled+=2;
        if(ray.TMax<=ray.TMin)break;
    }
    return transmission;
}
// Ray-traced ambient occlusion. The native ambient term is a flat fill that also lights creases, the ground under
// props and characters' feet; occluding it (and only it) grounds the scene without changing open surfaces.
float ambientOcclusion(float3 p,float3 normal,float rotation,uint self) {
    float3 t,b;basis(normal,t,b);float occlusion=0;
    [unroll]for(uint sample=0;sample<kOcclusionRays;++sample){
        // A uniform disk point lifted onto the hemisphere is cosine distributed.
        float2 d=diskSample(sample,kOcclusionRays,rotation);
        RayDesc ray;ray.Origin=p+normal*4;ray.Direction=normalize(t*d.x+b*d.y+normal*sqrt(saturate(1-dot(d,d))));
        ray.TMin=2;ray.TMax=kOcclusionRadius;
        RayQuery<RAY_FLAG_NONE> query;query.TraceRayInline(Scene,RAY_FLAG_NONE,1,ray);
        while(query.Proceed())if(query.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE && query.CandidateInstanceIndex()!=self){
            uint primitive=(query.CandidateInstanceID()&0x7ffff)+query.CandidatePrimitiveIndex();
            if(!(uint(Materials[primitive*kAttributes+5].z)&256) && materialAlpha(primitive,query.CandidateTriangleBarycentrics())>0)query.CommitNonOpaqueTriangleHit();
        }
        if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)continue;
        uint primitive=(query.CommittedInstanceID()&0x7ffff)+query.CommittedPrimitiveIndex();
        float alpha=saturate(materialAlpha(primitive,query.CommittedTriangleBarycentrics()))*float(query.CommittedInstanceID()>>19)/31.;
        // Counting a shallow crease draws dark lines along terrain triangle edges and per-face bands.
        float crease=creaseWeight(primitive,(float3x3)query.CommittedWorldToObject3x4(),normal,ray.Direction);
        occlusion+=alpha*crease*(1-query.CommittedRayT()/kOcclusionRadius);
    }
    return 1-kOcclusionStrength*occlusion/kOcclusionRays;
}
struct Lighting {float3 lit,unshadowed,sun;}; // sun: the visible directional light alone
Lighting nativeLighting(float3 p,float3 normal,float3 shading,float rotation,float occlusion,uint self) {
    Lighting result;result.sun=0;
    if(!Effects.z){result.lit=result.unshadowed=1;return result;}
    result.unshadowed=Ambient.rgb;result.lit=Ambient.rgb*occlusion;
    [loop]for(uint light=0;light<Effects.z;++light){
        float3 l=LightVector[light].xyz;float maximum=1e7,attenuation=1;bool local=LightVector[light].w>0;
        if(local){l-=p;float distanceSquared=dot(l,l);maximum=sqrt(distanceSquared)-8;
            if(maximum<=2 || distanceSquared>=LightColor[light].w)continue;
            attenuation=saturate(1-distanceSquared/LightColor[light].w);}
        float3 centre=normalize(l);float ndl=saturate(dot(shading,centre));if(ndl<=0)continue;
        float3 direct=LightColor[light].rgb*ndl*attenuation;float visible=1;
        if(Effects.y){
            // Soft shadows from per-pixel rotated samples over the emitter: the penumbra widens with blocker
            // distance (contact hardening), and its noise resolves temporally instead of in fixed steps.
            float3 tangent,bitangent;basis(centre,tangent,bitangent);
            if(local){
                float sum=0;
                [unroll]for(uint sample=0;sample<2;++sample){
                    float2 d=diskSample(sample,2,rotation)*kPointLightRadius;
                    float3 toLight=l+tangent*d.x+bitangent*d.y;
                    sum+=visibility(p,normal,normalize(toLight),length(toLight)-8,self);
                }
                visible=sum*.5;
            } else {
                float sum=0;
                [unroll]for(uint sample=0;sample<4;++sample){
                    float2 d=diskSample(sample,4,rotation)*kSunRadius;
                    sum+=visibility(p,normal,normalize(centre+tangent*d.x+bitangent*d.y),maximum,self);
                }
                visible=sum*.25;
            }
        }
        result.unshadowed+=direct;result.lit+=direct*visible;
        if(!local)result.sun+=direct*visible;
    }
    return result;
}
float3 shadowRatio(Lighting light) {return saturate((light.lit+.0001)/(light.unshadowed+.0001));}
float3 worldPosition(float2 pixel,float depth) {
    float2 uv=(pixel+.5)/SizeTime.xy;
    float2 ndc=(uv-Jitter.xy/SizeTime.xy)*float2(2,-2)+float2(-1,1);
    float4 p=mul(float4(ndc,depth,1),InverseProjection);p.xyz/=p.w;
    return p.x*CameraRight.xyz+p.y*CameraUp.xyz-p.z*CameraForward.xyz;
}
static const float kLodReach=kMetre; // vertical search for a receiver drawn at a coarser terrain LOD
// Closest alpha-tested level or model triangle along `ray`. A vertical search passes levelOnly: it looks for the
// terrain under a drawn pixel, not a prop beside it.
struct SurfaceHit {bool hit;float t;uint primitive,instance;float2 bary;float3x3 worldToObject;};
SurfaceHit closestSurface(RayDesc ray,bool levelOnly) {
    SurfaceHit h;h.hit=false;h.t=0;h.primitive=0;h.instance=kNoInstance;h.bary=0;h.worldToObject=(float3x3)0;
    RayQuery<RAY_FLAG_NONE> query;query.TraceRayInline(Scene,RAY_FLAG_NONE,1,ray);
    while(query.Proceed())if(query.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE){
        uint primitive=(query.CandidateInstanceID()&0x7ffff)+query.CandidatePrimitiveIndex(),flags=uint(Materials[primitive*kAttributes+5].z);
        if(!(flags&256) && !(levelOnly && (flags&128)) && materialAlpha(primitive,query.CandidateTriangleBarycentrics())>0)query.CommitNonOpaqueTriangleHit();
    }
    if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return h;
    h.hit=true;h.t=query.CommittedRayT();h.primitive=(query.CommittedInstanceID()&0x7ffff)+query.CommittedPrimitiveIndex();
    h.instance=query.CommittedInstanceIndex();h.bary=query.CommittedTriangleBarycentrics();
    h.worldToObject=(float3x3)query.CommittedWorldToObject3x4();
    return h;
}
float3 faceNormal(SurfaceHit h) {
    return normalize(mul(float3(Materials[h.primitive*kAttributes+4].zw,Materials[h.primitive*kAttributes+5].x),h.worldToObject));
}
// GS depth is quantized and interpolated independently of DXR geometry. Anchor
// a shadow receiver to its actual triangle rather than launching from a point
// slightly inside a cliff (which creates diagonal self-shadow stripes).
bool primarySurface(inout float3 p,out float3 normal,out float3 shading,out float confidence,out uint self) {
    confidence=0;normal=0;shading=0;self=kNoInstance;float distance=length(p);if(distance<=0 || !isfinite(distance))return false;
    float tolerance=max(32,distance*.02);
    RayDesc ray;ray.Origin=0;ray.Direction=p/distance;
    ray.TMin=max(.5,distance-tolerance);ray.TMax=distance+tolerance;
    SurfaceHit surface=closestSurface(ray,false);float3 anchor=0;
    // Fade mismatched receivers before the search interval ends. A hard
    // hit/miss boundary stamps full-detail terrain triangles onto the raster LOD.
    if(surface.hit){confidence=1-smoothstep(.25,1,abs(surface.t-distance)/tolerance);anchor=ray.Direction*surface.t;}
    // Native terrain LOD morphing moves the drawn surface vertically, up to about half a metre from the resident
    // full-detail grid. At grazing views that is far along the view ray, outside its window, so distant terrain lost
    // its shadow tile by tile. Search the level geometry straight below and above the drawn point instead; walls
    // (steep hits) are not LOD error.
    if(confidence<1){
        RayDesc vertical;vertical.Origin=p;vertical.TMin=0;vertical.TMax=kLodReach;
        vertical.Direction=float3(0,-1,0);SurfaceHit below=closestSurface(vertical,true);
        vertical.Direction=float3(0,1,0);SurfaceHit above=closestSurface(vertical,true);
        bool up=above.hit && (!below.hit || above.t<below.t);SurfaceHit ground=below;if(up)ground=above;
        if(ground.hit){
            float grounded=(1-smoothstep(.25,1,ground.t/kLodReach))*smoothstep(.5,.7,abs(faceNormal(ground).y));
            if(grounded>confidence){confidence=grounded;surface=ground;anchor=p+float3(0,up?ground.t:-ground.t,0);}
        }
    }
    if(!surface.hit || confidence<=0)return false;
    normal=faceNormal(surface);
    if(dot(normal,anchor)>0)normal=-normal; // face the camera
    shading=shadingNormal(surface.primitive,surface.bary,surface.worldToObject,normal);
    if(uint(Materials[surface.primitive*kAttributes+5].z)&128)self=surface.instance;
    p=anchor;return all(isfinite(normal));
}
// Filter radiance, not depth: each tap must independently prove that it belongs
// to the ray hit (or sky). Invalid taps retain the physical fallback so a thin
// foreground model cannot bleed into the reflection as it crosses a pixel.
float3 screenRadiance(float2 uv,float expectedDepth,float3 fallback,float3 scale,bool sky) {
    float2 xy=uv*SizeTime.xy-.5;int2 origin=int2(floor(xy));float2 f=frac(xy);
    float3 result=0;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
        int2 pixel=origin+int2(x,y);float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        float3 value=fallback;
        if(all(pixel>=0) && all(pixel<int2(SizeTime.xy))){
            float d=Depth.Load(int3(pixel,0));float confidence=0;
            if(isfinite(d)){
                if(sky)confidence=d<=0?1:0;
                else if(d>0 && expectedDepth>0)confidence=1-smoothstep(.002,.01,abs(expectedDepth-d)/max(expectedDepth,d));
            }
            if(confidence>0){float3 sample=Color.Load(int3(pixel,0)).rgb;if(all(isfinite(sample)))value=lerp(fallback,toLinear(sample)*scale,confidence);}
        }
        result+=value*weight;
    }
    return result;
}
float3 hitColor(float3 p,uint primitive,float2 bary,float3 normal,float3 shading,float rotation) {
    Lighting light=nativeLighting(p,normal,shading,rotation,1,kNoInstance);
    float3 ratio=shadowRatio(light);
    // Native models carry no GS lighting; the native lights stand in for their vertex colour and, like a GS vertex
    // colour, modulate the texel before it is linearised. Scaling the linear texel instead lit an off-screen model
    // about 2.3 times too brightly in shadow, so a reflected wall stood out as flat bright polygons.
    float3 display=materialDisplay(primitive,bary);
    float3 material=(uint(Materials[primitive*kAttributes+5].z)&128)?toLinear(display*light.lit):toLinear(display)*ratio;
    float4 clip=mul(float4(viewPosition(p),1),Projection);
    if(clip.w>.001) {
        float3 ndc=clip.xyz/clip.w;float2 uv=ndc.xy*float2(.5,-.5)+.5+Jitter.xy/SizeTime.xy;
        if(all(uv>.002) && all(uv<.998)) {
            if(ndc.z>0) {
                float edge=min(min(uv.x,uv.y),min(1-uv.x,1-uv.y));
                // Relative reverse-Z error is also relative view-distance error.
                // An absolute epsilon accepts unrelated distant surfaces. Fade
                // uncertain matches instead of popping to the fallback material.
                float3 filtered=screenRadiance(uv,ndc.z,material,ratio,false);
                return lerp(material,filtered,smoothstep(.002,.04,edge));
            }
        }
    }
    return material;
}
float3 environment(float3 direction,float3 fallback) {
    float3 weather=Sky[0].w>0?Sky[0].rgb:fallback;
    // Retain world-oriented sky lobes when they leave the camera. This keeps
    // differently lit horizons distinct instead of reflecting one global tint.
    float3 directional=0;float total=0;
    [unroll]for(uint axis=0;axis<3;++axis){
        // Only one sign per axis can contribute: three loads and multiplies
        // replace six loads and general powers for every water pixel.
        float component=direction[axis];uint sector=axis*2+(component>=0?1:2);
        float4 lobe=Sky[sector];float weight=component*component;weight*=weight;
        weight*=lobe.w;directional+=lobe.rgb*weight;total+=weight;
    }
    if(total>0)weather=lerp(weather,directional/max(total,.0001),smoothstep(0,.05,total));
    float4 clip=mul(float4(viewPosition(direction),0),Projection);
    if(clip.w>.001) {
        float2 uv=clip.xy/clip.w*float2(.5,-.5)+.5+Jitter.xy/SizeTime.xy;
        if(all(uv>0) && all(uv<1)) {
            float edge=min(min(uv.x,uv.y),min(1-uv.x,1-uv.y));
            return lerp(weather,screenRadiance(uv,0,weather,1,true),smoothstep(0,.04,edge));
        }
    }
    // A low-frequency miss environment, never a substitute for a geometry hit.
    // Retain observed weather while it briefly leaves the camera view.
    return weather;
}
// Native water is several blended layers over one pixel: the surface itself (vertex alpha about 0.85) and overlay
// passes (a sheen near 0.38, a darkening pass of 0.1-0.5, ...). CPU-clipped layers carry different GS depth scales,
// so whichever layer the nearest hit lands on changes from triangle to triangle; its alpha then stamped hard-edged
// regions of half-faded water. The strongest layer covering the pixel is the surface's own opacity.
bool coverageOpacity(RayDesc ray,out float opacity) {
    opacity=0;bool covered=false;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> layers;layers.TraceRayInline(Scene,RAY_FLAG_NONE,2,ray);
    while(layers.Proceed()){
        uint i=((layers.CandidateInstanceID()&0x7ffff)+layers.CandidatePrimitiveIndex())*kAttributes;
        float2 bary=layers.CandidateTriangleBarycentrics();covered=true;
        opacity=max(opacity,saturate(dot(float3(1-bary.x-bary.y,bary),float3(Materials[i].a,Materials[i+1].a,Materials[i+2].a))));
    }
    return covered;
}
bool waterSurface(RayDesc ray,out float distance,out float3 normal,out float opacity) {
    distance=0;normal=0;opacity=0;
    if(!(Effects.w&3) || ((Effects.w&4) && !(Effects.w&2)))return false;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> water;
    water.TraceRayInline(Scene,RAY_FLAG_NONE,(Effects.w&1)?4:2,ray);while(water.Proceed()){}
    if(water.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return false;
    distance=water.CommittedRayT();
    uint wi=((water.CommittedInstanceID()&0x7ffff)+water.CommittedPrimitiveIndex())*kAttributes;
    normal=float3(Materials[wi+4].zw,Materials[wi+5].x);if(normal.y<0)normal=-normal;
    if(Effects.w&2){
        // CPU-clipped water uses a different GS depth scale. Its triangles
        // prove screen coverage and opacity only; native grids own world XYZ.
        return coverageOpacity(ray,opacity);
    }
    float2 bary=water.CommittedTriangleBarycentrics();
    opacity=saturate(dot(float3(1-bary.x-bary.y,bary),float3(Materials[wi].a,Materials[wi+1].a,Materials[wi+2].a)));
    return true;
}
struct WaterSurface {float3 normal;float a2,crest;};
// Sum of directional waves in world space. Waves shorter than a few pixels would alias; their slope variance
// becomes microfacet roughness instead, so distant water widens the sun path rather than sparkling per pixel.
WaterSurface waterWaves(float2 world,float3 geometric,float footprint) {
    float2 slope=0;float lost=0,height=0,range=0;
    [unroll]for(uint i=0;i<kWaveCount;++i){
        float lambda=kWaves[i].x*kMetre,k=6.2831853/lambda,omega=sqrt(9.81*kMetre*k),s=kWaves[i].y;
        float angle=kWind+kWaves[i].z;float2 d=float2(cos(angle),sin(angle));
        float keep=smoothstep(2,6,lambda/footprint),phase=k*dot(d,world)-omega*SizeTime.z;
        slope+=d*(s*keep*cos(phase));height+=s/k*keep*sin(phase);range+=s/k*keep;
        lost+=.5*s*s*(1-keep*keep);
    }
    WaterSurface w;
    w.normal=normalize(geometric+float3(-slope.x,0,-slope.y));
    w.a2=.0004+lost;
    w.crest=range>0?saturate(height/range*.5+.5):.5;
    return w;
}
// Water-scope passes the classifier does not mark as water (such as the dark pass drawn with the surface) are
// reconstructed as fallback reflection geometry. Their GS depth scale tilts them a few degrees through the water
// plane, so rays leaving the surface hit them within centimetres wherever they rise above it: hard-edged patches of a
// dark, half-transparent reflector. A reconstructed surface lying flat on the water, at its height, is the water.
bool waterLayer(uint primitive,float height,float3 geometric) {
    uint i=primitive*kAttributes;if(!(uint(Materials[i+5].z)&256))return false;
    return abs(dot(float3(Materials[i+4].zw,Materials[i+5].x),geometric))>.966 && height<.25*kMetre;
}
// Caustic web on the bed: bright lines along the zero crossings of two drifting wave fields.
float caustic(float2 p) {
    float t=SizeTime.z;
    float a=sin(p.x+t*.9)+sin(p.y*1.3-t*.7)+sin((p.x+p.y)*.7+t*1.1);
    float b=sin(p.x*.8-p.y*1.1-t*.8)+sin(p.y*.9+t*1.2)+sin((p.x-p.y)*1.2-t*.5);
    a=saturate(1-abs(a)*.5);b=saturate(1-abs(b)*.5);a*=a*a;b*=b*b;
    return a*a+b*b-.2; // about zero mean: caustics redistribute light rather than add it
}
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=uint2(SizeTime.xy)))return;
    float4 original=Color.Load(int3(id.xy,0));Output[id.xy]=original;float3 receiverShadow=1;
    float sceneDepth=Depth.Load(int3(id.xy,0));
    float2 uv=(float2(id.xy)+.5)/SizeTime.xy;
    float2 ndc=(uv-Jitter.xy/SizeTime.xy)*float2(2,-2)+float2(-1,1);
    float4 view=mul(float4(ndc,1,1),InverseProjection);
    if(!all(isfinite(view)) || abs(view.w)<1e-12)return;
    float3 v=view.xyz/view.w;
    float3 direction=normalize(v.x*CameraRight.xyz+v.y*CameraUp.xyz-v.z*CameraForward.xyz);
    RayDesc ray;ray.Origin=0;ray.Direction=direction;
    ray.TMin=max(.5,Limits.x/max(1e-5,dot(direction,CameraForward.xyz)));ray.TMax=1e7;
    float distance,waterOpacity;float3 geometric;
    bool haveWater=waterSurface(ray,distance,geometric,waterOpacity);float3 p=direction*distance;
    float4 waterClip=mul(float4(viewPosition(p),1),Projection);
    float waterDepth=haveWater?waterClip.z/waterClip.w:0;
    bool coveredWater=haveWater;
    haveWater=haveWater && !(sceneDepth>waterDepth+max(1e-6,abs(waterDepth)*.002));
    if(SceneInfo.w && SceneInfo.w<=2){float3 debug=float3(distance>0?1:0,coveredWater?1:0,haveWater?1:0);if(SceneInfo.w==2)debug=haveWater?waterOpacity.xxx:float3(0,0,0);Output[id.xy]=float4(debug,original.a);return;}
    float rotation=pixelNoise(id.xy,0);
    Lighting waterLight;waterLight.lit=waterLight.unshadowed=1;waterLight.sun=0;
    if(haveWater)waterLight=nativeLighting(p,geometric,geometric,rotation,1,kNoInstance);
    if(Effects.y && Effects.z && sceneDepth>0){
        if(haveWater)receiverShadow=shadowRatio(waterLight);
        else {
            float3 surface=worldPosition(id.xy,sceneDepth),normal,shading;float confidence;uint self;
            if(all(isfinite(surface)) && primarySurface(surface,normal,shading,confidence,self)){
                float occlusion=ambientOcclusion(surface,normal,pixelNoise(id.xy,1),self);
                receiverShadow=lerp(float3(1,1,1),shadowRatio(nativeLighting(surface,normal,shading,rotation,occlusion,self)),confidence);
            }
        }
        Output[id.xy]=float4(toDisplay(toLinear(original.rgb)*receiverShadow),original.a);
    }
    if(!Effects.x || !haveWater)return;
    float bottom=distance+8000;
    if(sceneDepth>0) {
        float4 back=mul(float4(ndc,sceneDepth,1),InverseProjection);
        if(all(isfinite(back)) && abs(back.w)>1e-12)bottom=length(back.xyz/back.w);
    }
    if(dot(geometric,-direction)<=0)return;
    // World units covered by one pixel at the water, stretched by the grazing view.
    float footprint=2*distance/max(1,SizeTime.y*abs(Projection[1][1]));
    float cosine=abs(dot(direction,geometric)),stretched=footprint/max(cosine,.03);
    float2 world=p.xz+Limits.zw;
    WaterSurface waves=waterWaves(world,geometric,stretched);float3 normal=waves.normal;
    float ndv=saturate(dot(-direction,normal));float fresnel=.0204+.9796*pow(1-ndv,5);
    float thickness=max(0,bottom-distance),depth=thickness*cosine; // along the view ray; vertical
    // Fade only the subpixel intersection. A fixed 96-world-unit band exposes
    // the old blue water along shallow banks even when coverage is proven.
    // Fresnel reflection still exists in shallow water; thickness controls
    // absorption below, not a broad fade back to the original water material.
    float coverage=waterOpacity*smoothstep(0,max(1,footprint),thickness);
    if(coverage<=0)return;
    float2 refracted=clamp(uv+(normal.xz-geometric.xz)*kRefraction*saturate(thickness/(.5*kMetre)),.001,.999);
    int2 rp=int2(refracted*SizeTime.xy);
    if(Depth.Load(int3(rp,0))>waterDepth*1.005+1e-6)rp=int2(id.xy);
    float3 base=toLinear(Color.Load(int3(rp,0)).rgb)*receiverShadow;
    float3 body=toLinear(original.rgb)*receiverShadow;
    // Caustics focus the visible sun onto the bed; they fade with depth and once a pixel covers several cells.
    float sunShare=saturate(luminance(waterLight.sun)/max(luminance(waterLight.unshadowed),1e-4));
    if(sunShare>0){
        float3 bed=direction*bottom;float bedFootprint=footprint*bottom/max(distance,1);
        float weight=sunShare*smoothstep(0,.05*kMetre,depth)*exp(-depth/(1.5*kMetre))*saturate(1-bedFootprint/(.04*kMetre));
        if(weight>0)base*=max(0,1+kCausticStrength*weight*caustic((bed.xz+Limits.zw)/(.06*kMetre)));
    }
    // Beer-Lambert absorption: light outside the native water colour is absorbed first, so depth converges
    // on the level's own water hue (scattered back by the water body) instead of a fixed blue.
    float3 hue=body/max(max(body.r,max(body.g,body.b)),1e-4);
    float3 transmittance=exp(-kWaterExtinction*(1.15-hue)*min(thickness,20*kMetre));
    base=base*transmittance+body*.9*(1-transmittance);
    if(SizeTime.w>0) {
        float3 central=reflect(direction,normal),sky=environment(central,toLinear(original.rgb));
        float3 tangent,bitangent;basis(central,tangent,bitangent);
        float spread=sqrt(waves.a2)*.7,reflectionRotation=pixelNoise(id.xy,2);float3 sum=0;
        [unroll]for(uint sample=0;sample<4;++sample) {
            // Glossy reflection: rotated samples over the GGX lobe width of the visible (and filtered) waves.
            float2 offset=diskSample(sample,4,reflectionRotation)*spread;
            float3 reflected=normalize(central+tangent*offset.x+bitangent*offset.y);
            float above=dot(reflected,geometric);if(above<.002)reflected=normalize(reflected+geometric*(.002-above));
            ray.Origin=p+geometric*4;ray.Direction=reflected;ray.TMin=2;
            float3 value=0;float transmission=1;
            [loop]for(uint layer=0;layer<4 && transmission>.01;++layer){
                RayQuery<RAY_FLAG_NONE> reflection;
                reflection.TraceRayInline(Scene,RAY_FLAG_NONE,1,ray);
                while(reflection.Proceed())if(reflection.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE){
                    uint primitive=(reflection.CandidateInstanceID()&0x7ffff)+reflection.CandidatePrimitiveIndex();
                    float height=dot(ray.Origin-p,geometric)+reflection.CandidateTriangleRayT()*dot(ray.Direction,geometric);
                    if(!waterLayer(primitive,height,geometric) && materialAlpha(primitive,reflection.CandidateTriangleBarycentrics())>0)reflection.CommitNonOpaqueTriangleHit();
                }
                if(reflection.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)break;
                uint primitive=(reflection.CommittedInstanceID()&0x7ffff)+reflection.CommittedPrimitiveIndex();float2 bary=reflection.CommittedTriangleBarycentrics();
                float alpha=saturate(materialAlpha(primitive,bary))*float(reflection.CommittedInstanceID()>>19)/31.;float3 hit=ray.Origin+ray.Direction*reflection.CommittedRayT();
                float3 localNormal=float3(Materials[primitive*kAttributes+4].zw,Materials[primitive*kAttributes+5].x);
                float3 hitNormal=normalize(mul(localNormal,(float3x3)reflection.CommittedWorldToObject3x4()));
                if(dot(hitNormal,-ray.Direction)<0)hitNormal=-hitNormal;
                float3 hitShading=shadingNormal(primitive,bary,(float3x3)reflection.CommittedWorldToObject3x4(),hitNormal);
                value+=transmission*alpha*hitColor(hit,primitive,bary,hitNormal,hitShading,rotation);transmission*=1-alpha;ray.Origin=hit+ray.Direction*2;
            }
            sum+=value+sky*transmission;
        }
        base=lerp(base,sum*.25,fresnel);
    }
    // GGX sun glints use the authored native directional light and the same
    // ray-traced visibility as land. No synthetic screen-space light source.
    [loop]for(uint light=0;light<Effects.z;++light)if(LightVector[light].w==0){
        float3 l=normalize(LightVector[light].xyz),halfVector=normalize(l-direction);
        float nl=saturate(dot(normal,l)),nh=saturate(dot(normal,halfVector)),vh=saturate(dot(-direction,halfVector));
        // The soft sun visibility already traced for this water surface (one exported sun).
        float lit=luminance(LightColor[light].rgb)*saturate(dot(geometric,l));
        float visible=!Effects.y?1:lit>0?saturate(luminance(waterLight.sun)/max(lit,1e-4)):0;
        if(nl>0 && nh>0 && ndv>0){
            float a2=waves.a2,denominator=nh*nh*(a2-1)+1;
            float distribution=a2/(3.14159265*denominator*denominator);
            float geometry=(2*ndv/(ndv+sqrt(a2+(1-a2)*ndv*ndv)))*(2*nl/(nl+sqrt(a2+(1-a2)*nl*nl)));
            float reflectance=.0204+.9796*pow(1-vh,5);
            float specular=distribution*geometry*reflectance/max(4*ndv,.0001);
            if(specular>.0001)base+=LightColor[light].rgb*specular*visible;
        }
        // Light scattered through backlit wave crests when looking towards a low sun.
        float2 view2=direction.xz,light2=l.xz;
        if(dot(view2,view2)>1e-6 && dot(light2,light2)>1e-6){
            float forward=saturate(dot(normalize(view2),normalize(light2)));forward*=forward;forward*=forward;
            float steep=saturate(length(normal.xz-geometric.xz)*12);
            base+=LightColor[light].rgb*visible*hue*.25*forward*waves.crest*waves.crest*steep*(1-fresnel)*(1-saturate(l.y));
        }
    }
    // Shoreline and intersection foam: thin water against the bed, broken up by drifting world-space noise.
    float band=1-smoothstep(0,kFoamDepth,depth);
    if(band>0){
        float2 f=world/(.18*kMetre);float drift=SizeTime.z*.15;
        float n=.65*valueNoise(f+drift)+.35*valueNoise(f*2.7-drift*1.7);
        // Patches thin out towards deeper water. Foam fades out once a pixel spans several noise
        // cells: on distant banks the band is subpixel and would only draw a bright outline.
        float threshold=1-.8*band,patches=smoothstep(threshold,threshold+.2,n)*band;
        float foam=patches*saturate(1-stretched/(.05*kMetre));
        base=lerp(base,.65*min(waterLight.lit,1.5),foam*.85);
    }
    // Coverage fades into the shadowed receiver, not the unlit input.
    // Otherwise nearly transparent water restores full brightness at banks.
    Output[id.xy]=float4(lerp(Output[id.xy].rgb,toDisplay(base),coverage),original.a);
    if(SceneInfo.w==3)Output[id.xy]=float4(waterOpacity,saturate(thickness/96),fresnel,original.a);
}
