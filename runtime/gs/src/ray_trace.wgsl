// Ray-traced modern water, soft shadows and ambient occlusion for the WebGPU renderer (wgpu_ray.cpp): the port of the
// D3D11 renderer's DXR pass (ray_water.hlsl), with inline ray queries replaced by a two-level BVH walked in compute
// (ray_bvh.h). Keep the shading in step with ray_water.hlsl.
//
// Budget (no ray-tracing hardware): land shadows and ambient occlusion are traced at a reduced resolution (lowMain,
// C.quality.w render pixels per low pixel) and upsampled with depth-aware weights in main, and lowMain's water
// coverage mask limits main's water rays to pixels near water. The water's glossy reflection and its shadowed lighting
// are traced there too (lowMain's second output) and upsampled over water pixels; main keeps the per-pixel wave normal,
// refraction, Fresnel, glints, caustics and foam. The ray counts per pixel come from C.quality (fewer when the temporal
// AA accumulates the noise).
struct Camera {
    projection: mat4x4f,  // view -> clip (row-vector matrix uploaded as is: clip = projection * view)
    inverse: mat4x4f,     // clip -> view
    right: vec4f,
    up: vec4f,
    forward: vec4f,
    sizeTime: vec4f,      // guide size, elapsed scene seconds, reflections
    jitter: vec4f,
    limits: vec4f,        // near plane, opaque triangles, camera world x / z (world-anchored water detail)
    quality: vec4u,       // sun samples, occlusion rays, reflection rays, shadow scale (render pixels per shadow pixel)
}
struct Lights {
    scene: vec4u,         // texture count, texture size, coverage debug, top-level root node
    ambient: vec4f,
    lightVector: array<vec4f, 4>,
    lightColor: array<vec4f, 4>,
    effects: vec4u,       // water, shadows, native light count, flags (1 native water, 2 coverage, 4 require, 8 sky)
    zmap: vec4f,          // GS depth far, 1 / (near - far), sky epsilon, origin x | y << 16 of the frame (bits)
}
struct Node { lo: vec4f, hi: vec4f }   // xyz bounds; w: first / count (bits), see ray_bvh.h
struct Instance { r0: vec4f, r1: vec4f, r2: vec4f, info: vec4u } // world -> object rows; root, id, mask
@group(0) @binding(0) var<uniform> C: Camera;
@group(0) @binding(1) var<uniform> L: Lights;
@group(0) @binding(8) var Color: texture_2d<f32>;
@group(0) @binding(9) var Depth: texture_2d<f32>;
@group(0) @binding(10) var ShadowIn: texture_2d<f32>; // lowMain's result: shadow ratio, water coverage (main only)
@group(0) @binding(11) var ReflectIn: texture_2d<f32>; // lowMain's water reflection (display) and sun visibility
@group(0) @binding(20) var<storage, read> Nodes: array<Node>;
@group(0) @binding(21) var<storage, read> Tris: array<vec4f>;     // 3 per triangle; first.w = primitive (bits)
@group(0) @binding(22) var<storage, read> Instances: array<Instance>;
@group(0) @binding(23) var<storage, read> Materials: array<vec4f>;
@group(0) @binding(24) var<storage, read> Texels: array<u32>;
@group(0) @binding(25) var<storage, read_write> Sky: array<vec4f, 7>;
@group(0) @binding(28) var Output: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(29) var OutputB: texture_storage_2d<rgba8unorm, write>; // lowMain's ReflectIn

const kMetre = 500.0;
const kSunRadius = 0.006;
const kPointLightRadius = 0.06 * kMetre;
const kTerminatorOffset = 0.1 * kMetre;
const kOcclusionRadius = kMetre;
const kOcclusionStrength = 0.6;
const kNoInstance = 0xffffffffu;
const kAttributes = 7u;
const kWaterExtinction = 0.35 / kMetre;
const kFoamDepth = 0.06 * kMetre;
const kCausticStrength = 0.9;
const kRefraction = 0.12;
const kWind = 0.6;
const kLodReach = kMetre;

fn finite(x: f32) -> bool { return (bitcast<u32>(x) & 0x7f800000u) != 0x7f800000u; }
fn finite3(v: vec3f) -> bool { return finite(v.x) && finite(v.y) && finite(v.z); }
fn finite4(v: vec4f) -> bool { return finite3(v.xyz) && finite(v.w); }
fn toLinear(c: vec3f) -> vec3f { return pow(max(c, vec3f(0.0)), vec3f(2.2)); }
fn toDisplay(c: vec3f) -> vec3f { return pow(saturate(c), vec3f(1.0 / 2.2)); }
fn luminance(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }
fn size() -> vec2f { return C.sizeTime.xy; }

// ---- frame inputs: the GS target (display-referred RGBA8) and GS depth mapped to [0, 1] over [far, near], 0 = sky
fn origin() -> vec2i {
    let o = bitcast<u32>(L.zmap.w);
    return vec2i(i32(o & 0xffffu), i32(o >> 16u));
}
fn colorAt(p: vec2i) -> vec4f { return textureLoad(Color, p + origin(), 0); }
fn depthAt(p: vec2i) -> f32 {
    let source = textureLoad(Depth, p + origin(), 0).r;
    let d = (source - L.zmap.x) * L.zmap.y;
    return select(0.0, d, finite(source) && finite(d) && d > L.zmap.z && d <= 1.0);
}

fn skyAxis(sector: u32) -> vec3f {
    let axis = (sector - 1u) / 2u;
    let s = select(-1.0, 1.0, (sector & 1u) != 0u);
    return select(select(vec3f(0.0, 0.0, s), vec3f(0.0, s, 0.0), axis == 1u), vec3f(s, 0.0, 0.0), axis == 0u);
}

// Interleaved gradient noise in jitter-compensated pixel coordinates (see ray_water.hlsl).
fn pixelNoise(id: vec2u, salt: u32) -> f32 {
    let p = vec2f(id) - floor(C.jitter.xy) + fract(C.jitter.xy) * vec2f(113.0, 71.0) + f32(salt) * vec2f(47.0, 17.0);
    return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715))));
}
fn diskSample(i: u32, count: u32, rotation: f32) -> vec2f {
    let r = sqrt((f32(i) + 0.5) / f32(count));
    let a = f32(i) * 2.39996323 + rotation * 6.2831853;
    return r * vec2f(cos(a), sin(a));
}
fn basisT(n: vec3f) -> vec3f {
    return normalize(cross(n, select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(n.y) > 0.95)));
}
fn unpackNormal(packed: f32) -> vec3f {
    let bits = u32(packed);
    let e = vec2f(f32(bits & 4095u), f32(bits >> 12u)) / 4095.0 * 2.0 - 1.0;
    var n = vec3f(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n = vec3f((1.0 - abs(n.yx)) * select(vec2f(-1.0), vec2f(1.0), n.xy >= vec2f(0.0)), n.z);
    }
    return normalize(n);
}
fn hashNoise(p: vec2i) -> f32 {
    var h = (u32(p.x) * 0x8da6b343u) ^ (u32(p.y) * 0xd8163841u);
    h ^= h >> 15u;
    h *= 0x2c1b3c6du;
    h ^= h >> 12u;
    h *= 0x297a2d39u;
    h ^= h >> 15u;
    return f32(h) / 4294967296.0;
}
fn valueNoise(p: vec2f) -> f32 {
    let i = vec2i(floor(p));
    var f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hashNoise(i), hashNoise(i + vec2i(1, 0)), f.x), mix(hashNoise(i + vec2i(0, 1)), hashNoise(i + 1), f.x), f.y);
}

// ---- sky radiance, reduced once per frame (skyMain in ray_water.hlsl; 64 threads over a 64x64 grid)
var<workgroup> skySamples: array<array<vec4f, 64>, 7>;
@compute @workgroup_size(8, 8, 1)
fn skyMain(@builtin(local_invocation_id) thread: vec3u) {
    let index = thread.y * 8u + thread.x;
    var sums: array<vec4f, 7>;
    for (var y = 0u; y < 8u; y++) {
        for (var x = 0u; x < 8u; x++) {
            let uv = (vec2f(thread.xy) + vec2f(f32(x * 8u), f32(y * 8u)) + 0.5) / vec2f(64.0);
            let pixel = min(vec2i(uv * size()), vec2i(size()) - 1);
            if (depthAt(pixel) <= 0.0) {
                let c = colorAt(pixel).rgb;
                if (finite3(c)) {
                    let radiance = toLinear(c);
                    sums[0] += vec4f(radiance, 1.0);
                    let ndc = ((vec2f(pixel) + 0.5 - C.jitter.xy) / size()) * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0);
                    let v = C.inverse * vec4f(ndc, 1.0, 1.0);
                    let direction = normalize(v.x * C.right.xyz + v.y * C.up.xyz - v.z * C.forward.xyz);
                    for (var sector = 1u; sector < 7u; sector++) {
                        var weight = saturate(dot(direction, skyAxis(sector)));
                        weight *= weight;
                        weight *= weight;
                        weight *= weight;
                        sums[sector] += vec4f(radiance * weight, weight);
                    }
                }
            }
        }
    }
    for (var sector = 0u; sector < 7u; sector++) {
        skySamples[sector][index] = sums[sector];
    }
    workgroupBarrier();
    for (var stride = 32u; stride > 0u; stride >>= 1u) {
        if (index < stride) {
            for (var sector = 0u; sector < 7u; sector++) {
                skySamples[sector][index] += skySamples[sector][index + stride];
            }
        }
        workgroupBarrier();
    }
    if (index == 0u) {
        for (var sector = 0u; sector < 7u; sector++) {
            let sum = skySamples[sector][0];
            if (sum.w > select(0.0, 0.25, sector != 0u)) {
                Sky[sector] = vec4f(sum.rgb / sum.w, 1.0);
            } else if ((L.effects.w & 8u) != 0u) {
                Sky[sector] = vec4f(0.0);
            }
        }
    }
}

fn viewPosition(p: vec3f) -> vec3f { return vec3f(dot(p, C.right.xyz), dot(p, C.up.xyz), -dot(p, C.forward.xyz)); }
fn texel(texture: u32, q: vec2i) -> vec4f {
    let s = i32(L.scene.y);
    let p = (q % s + s) % s;
    let pixel = Texels[texture * u32(s * s) + u32(p.y * s + p.x)];
    return vec4f(f32(pixel & 255u), f32((pixel >> 8u) & 255u), f32((pixel >> 16u) & 255u), f32(pixel >> 24u)) / 255.0;
}
fn materialTexel(primitive: u32, bary: vec2f) -> vec4f {
    let i = primitive * kAttributes;
    let w = vec3f(1.0 - bary.x - bary.y, bary.x, bary.y);
    let texture = i32(Materials[i + 5u].y);
    if (texture < 0 || u32(texture) >= L.scene.x) {
        return vec4f(1.0, 1.0, 1.0, 128.0 / 255.0);
    }
    let uv = Materials[i + 3u].xy * w.x + Materials[i + 3u].zw * w.y + Materials[i + 4u].xy * w.z;
    let xy = fract(uv) * f32(L.scene.y) - 0.5;
    let p = vec2i(floor(xy));
    let f = fract(xy);
    let t = u32(texture);
    return mix(mix(texel(t, p), texel(t, p + vec2i(1, 0)), f.x), mix(texel(t, p + vec2i(0, 1)), texel(t, p + 1), f.x), f.y);
}
fn materialDisplay(primitive: u32, bary: vec2f) -> vec3f {
    let i = primitive * kAttributes;
    let w = vec3f(1.0 - bary.x - bary.y, bary.x, bary.y);
    let light = Materials[i].rgb * w.x + Materials[i + 1u].rgb * w.y + Materials[i + 2u].rgb * w.z;
    return materialTexel(primitive, bary).rgb * light;
}
fn materialFlags(primitive: u32) -> u32 { return u32(Materials[primitive * kAttributes + 5u].z); }
fn materialAlpha(primitive: u32, bary: vec2f) -> f32 {
    let i = primitive * kAttributes;
    let flags = u32(Materials[i + 5u].z);
    let func = flags & 7u;
    let w = vec3f(1.0 - bary.x - bary.y, bary.x, bary.y);
    let vertex = floor(dot(w, vec3f(Materials[i].a, Materials[i + 1u].a, Materials[i + 2u].a)) * 128.0 + 0.5);
    var a = vertex;
    if ((flags & 16u) != 0u) {
        let t = floor(materialTexel(primitive, bary).a * 255.0 + 0.5);
        let tfx = (flags >> 5u) & 3u;
        a = select(select(t, t + vertex, tfx == 2u), floor(t * vertex / 128.0), tfx == 0u);
    }
    a = clamp(a, 0.0, 255.0);
    let r = Materials[i + 5u].w;
    let passed = func == 1u || (func == 2u && a < r) || (func == 3u && a <= r) || (func == 4u && a == r) ||
               (func == 5u && a >= r) || (func == 6u && a > r) || (func == 7u && a != r);
    if (!passed) {
        return -1.0;
    }
    return select(1.0, saturate(a / 128.0), (flags & 8u) != 0u);
}
fn localFace(primitive: u32) -> vec3f {
    return vec3f(Materials[primitive * kAttributes + 4u].zw, Materials[primitive * kAttributes + 5u].x);
}
// mul(v, (float3x3)WorldToObject) in ray_water.hlsl: object-space normals to world space.
fn toWorld(n: vec3f, instance: u32) -> vec3f {
    let m = Instances[instance];
    return n.x * m.r0.xyz + n.y * m.r1.xyz + n.z * m.r2.xyz;
}
fn instanceOpacity(instance: u32) -> f32 { return f32(Instances[instance].info.y >> 19u) / 31.0; }

// ---- traversal: closest accepted hit along a ray (DXR inline ray query + candidate loop)
const kAlpha = 0u;     // alpha-tested occluders, not `self`, never non-shadow geometry (flag 256)
const kLevel = 1u;     // kAlpha on level geometry only (no model triangles, flag 128)
const kReflect = 2u;   // kAlpha (no self) except water-scope layers lying on the water (waterLayer)
const kOpaque = 3u;    // every triangle (RAY_FLAG_FORCE_OPAQUE)
const kCoverage = 4u;  // every candidate counted (max vertex opacity), none committed
const kShadow = 5u;    // kAlpha, ending the walk at a hit that alone blocks the light (traceBlocked)
struct Hit { t: f32, primitive: u32, instance: u32, bary: vec2f, hit: bool }
var<private> coverageOpacity: f32;
var<private> coverageSeen: bool;
var<private> traceBlocked: bool;
var<private> shadowTravelled: f32; // kShadow: the distance travelled before this segment, and the receiver normal
var<private> shadowReceiver: vec3f;
var<private> reflectOrigin: vec3f;  // kReflect: the water point and its plane normal
var<private> reflectNormal: vec3f;
var<private> reflectStart: vec3f;   // the ray origin of the current reflection segment

fn waterLayer(primitive: u32, height: f32, geometric: vec3f) -> bool {
    if ((materialFlags(primitive) & 256u) == 0u) {
        return false;
    }
    return abs(dot(localFace(primitive), geometric)) > 0.966 && height < 0.25 * kMetre;
}
fn accept(kind: u32, primitive: u32, instance: u32, bary: vec2f, t: f32, dir: vec3f, self_: u32) -> bool {
    if (kind == kOpaque) {
        return true;
    }
    if (kind == kCoverage) {
        let i = primitive * kAttributes;
        coverageSeen = true;
        coverageOpacity = max(coverageOpacity, saturate(dot(vec3f(1.0 - bary.x - bary.y, bary),
                                                            vec3f(Materials[i].a, Materials[i + 1u].a, Materials[i + 2u].a))));
        return false;
    }
    if (instance == self_) {
        return false;
    }
    let flags = materialFlags(primitive);
    if (kind == kReflect) {
        let height = dot(reflectStart - reflectOrigin, reflectNormal) + t * dot(dir, reflectNormal);
        if (waterLayer(primitive, height, reflectNormal)) {
            return false;
        }
    } else if ((flags & 256u) != 0u || (kind == kLevel && (flags & 128u) != 0u)) {
        return false;
    }
    let alpha = materialAlpha(primitive, bary);
    if (alpha <= 0.0) {
        return false;
    }
    if (kind == kShadow) {
        // visibility(): this layer's opacity, faded crease weight included; a full block ends the walk.
        var hitNormal = normalize(toWorld(localFace(primitive), instance));
        if (dot(hitNormal, dir) > 0.0) {
            hitNormal = -hitNormal;
        }
        let facing = dot(hitNormal, shadowReceiver);
        let crease = select(1.0, 1.0 - smoothstep(0.5, 0.87, facing), finite(facing));
        let block = saturate(alpha) * instanceOpacity(instance) *
                    mix(crease, 1.0, smoothstep(2.0 * kMetre, 5.0 * kMetre, shadowTravelled + t));
        traceBlocked = block >= 0.999;
    }
    return true;
}
fn slab(lo: vec3f, hi: vec3f, o: vec3f, inv: vec3f, tmin: f32, tmax: f32) -> f32 {
    let a = (lo - o) * inv;
    let b = (hi - o) * inv;
    let near = max(max(min(a.x, b.x), min(a.y, b.y)), max(min(a.z, b.z), tmin));
    let far = min(min(max(a.x, b.x), max(a.y, b.y)), min(max(a.z, b.z), tmax));
    return select(1e30, near, near <= far);
}
fn safeInverse(d: vec3f) -> vec3f {
    return 1.0 / select(d, vec3f(1e-30), abs(d) < vec3f(1e-30));
}
// Pushes the children of an interior node, the nearer one last (popped first); misses are not pushed.
struct Pair { near: u32, far: u32, tNear: f32, tFar: f32, count: u32 }
fn children(first: u32, o: vec3f, inv: vec3f, tmin: f32, tmax: f32) -> Pair {
    let a = Nodes[first];
    let b = Nodes[first + 1u];
    let ta = slab(a.lo.xyz, a.hi.xyz, o, inv, tmin, tmax);
    let tb = slab(b.lo.xyz, b.hi.xyz, o, inv, tmin, tmax);
    var r = Pair(first, first + 1u, ta, tb, u32(ta < 1e30) + u32(tb < 1e30));
    if (tb < ta) {
        r = Pair(first + 1u, first, tb, ta, r.count);
    }
    return r;
}
fn trace(o: vec3f, d: vec3f, tmin: f32, tmaxIn: f32, mask: u32, kind: u32, self_: u32) -> Hit {
    var best = Hit(tmaxIn, 0u, kNoInstance, vec2f(0.0), false);
    traceBlocked = false;
    let inv = safeInverse(d);
    var top: array<u32, 32>;
    var topT: array<f32, 32>;
    var topCount = 0u;
    let root = Nodes[L.scene.w];
    let rootT = slab(root.lo.xyz, root.hi.xyz, o, inv, tmin, best.t);
    if (rootT < 1e30) {
        top[0] = L.scene.w;
        topT[0] = rootT;
        topCount = 1u;
    }
    loop {
        if (topCount == 0u) {
            break;
        }
        topCount--;
        if (topT[topCount] > best.t) {
            continue;
        }
        let node = Nodes[top[topCount]];
        let first = bitcast<u32>(node.lo.w);
        let count = bitcast<u32>(node.hi.w);
        if (count == 0u) {
            let c = children(first, o, inv, tmin, best.t);
            if (c.count == 2u && topCount + 2u <= 32u) {
                top[topCount] = c.far;
                topT[topCount] = c.tFar;
                topCount++;
            }
            if (c.count >= 1u && topCount + 1u <= 32u) {
                top[topCount] = c.near;
                topT[topCount] = c.tNear;
                topCount++;
            }
            continue;
        }
        for (var k = 0u; k < count; k++) {
            let instance = first + k;
            let m = Instances[instance];
            if ((m.info.z & mask) == 0u) {
                continue;
            }
            // The ray in the instance's space; t stays the world ray's parameter.
            let oo = vec3f(dot(m.r0.xyz, o) + m.r0.w, dot(m.r1.xyz, o) + m.r1.w, dot(m.r2.xyz, o) + m.r2.w);
            let od = vec3f(dot(m.r0.xyz, d), dot(m.r1.xyz, d), dot(m.r2.xyz, d));
            let oinv = safeInverse(od);
            let base = m.info.y & 0x7ffffu;
            var stack: array<u32, 48>;
            var stackT: array<f32, 48>;
            var count2 = 0u;
            let r = Nodes[m.info.x];
            let rt = slab(r.lo.xyz, r.hi.xyz, oo, oinv, tmin, best.t);
            if (rt < 1e30) {
                stack[0] = m.info.x;
                stackT[0] = rt;
                count2 = 1u;
            }
            loop {
                if (count2 == 0u) {
                    break;
                }
                count2--;
                if (stackT[count2] > best.t) {
                    continue;
                }
                let n = Nodes[stack[count2]];
                let f = bitcast<u32>(n.lo.w);
                let c = bitcast<u32>(n.hi.w);
                if (c == 0u) {
                    let ch = children(f, oo, oinv, tmin, best.t);
                    if (ch.count == 2u && count2 + 2u <= 48u) {
                        stack[count2] = ch.far;
                        stackT[count2] = ch.tFar;
                        count2++;
                    }
                    if (ch.count >= 1u && count2 + 1u <= 48u) {
                        stack[count2] = ch.near;
                        stackT[count2] = ch.tNear;
                        count2++;
                    }
                    continue;
                }
                for (var j = 0u; j < c; j++) {
                    let tri = (f + j) * 3u;
                    let a = Tris[tri];
                    let e1 = Tris[tri + 1u].xyz - a.xyz;
                    let e2 = Tris[tri + 2u].xyz - a.xyz;
                    // Moller-Trumbore, both faces (no culling, like RAY_FLAG_NONE).
                    let pv = cross(od, e2);
                    let det = dot(e1, pv);
                    if (abs(det) < 1e-20) {
                        continue;
                    }
                    let id = 1.0 / det;
                    let tv = oo - a.xyz;
                    let u = dot(tv, pv) * id;
                    if (u < 0.0 || u > 1.0) {
                        continue;
                    }
                    let qv = cross(tv, e1);
                    let v = dot(od, qv) * id;
                    if (v < 0.0 || u + v > 1.0) {
                        continue;
                    }
                    let t = dot(e2, qv) * id;
                    if (t < tmin || t > best.t) {
                        continue;
                    }
                    let primitive = base + bitcast<u32>(a.w);
                    if (accept(kind, primitive, instance, vec2f(u, v), t, d, self_)) {
                        best = Hit(t, primitive, instance, vec2f(u, v), true);
                        if (traceBlocked) {
                            return best; // kShadow: this hit alone blocks the light
                        }
                    }
                }
            }
        }
    }
    return best;
}

// Faceted native geometry: a neighbouring face across a shallow crease is not an occluder (see ray_water.hlsl).
fn creaseWeight(h: Hit, receiver: vec3f, direction: vec3f) -> f32 {
    var hitNormal = normalize(toWorld(localFace(h.primitive), h.instance));
    if (dot(hitNormal, direction) > 0.0) {
        hitNormal = -hitNormal;
    }
    let facing = dot(hitNormal, receiver);
    return select(1.0, 1.0 - smoothstep(0.5, 0.87, facing), finite(facing));
}
fn shadingNormal(h: Hit, geometric: vec3f) -> vec3f {
    let w = vec3f(1.0 - h.bary.x - h.bary.y, h.bary.x, h.bary.y);
    let packed = Materials[h.primitive * kAttributes + 6u];
    let local = unpackNormal(packed.x) * w.x + unpackNormal(packed.y) * w.y + unpackNormal(packed.z) * w.z;
    var n = normalize(toWorld(local, h.instance));
    if (dot(toWorld(localFace(h.primitive), h.instance), geometric) < 0.0) {
        n = -n;
    }
    return select(geometric, n, finite3(n) && dot(n, geometric) > 0.1);
}
fn visibility(p: vec3f, normal: vec3f, direction: vec3f, maximum: f32, self_: u32) -> f32 {
    let offset = 4.0 + kTerminatorOffset * (1.0 - saturate(dot(normal, direction)));
    var o = p + normal * offset + direction * 2.0;
    var tmax = maximum;
    var transmission = 1.0;
    var travelled = 0.0;
    shadowReceiver = normal;
    for (var layer = 0u; layer < 8u && transmission > 0.01; layer++) {
        shadowTravelled = travelled;
        let h = trace(o, direction, 2.0, tmax, 1u, kShadow, self_);
        if (traceBlocked) {
            return 0.0;
        }
        if (!h.hit) {
            break;
        }
        var alpha = saturate(materialAlpha(h.primitive, h.bary)) * instanceOpacity(h.instance);
        travelled += h.t;
        alpha *= mix(creaseWeight(h, normal, direction), 1.0, smoothstep(2.0 * kMetre, 5.0 * kMetre, travelled));
        transmission *= 1.0 - alpha;
        let advance = h.t + 2.0;
        o += direction * advance;
        tmax -= advance;
        travelled += 2.0;
        if (tmax <= 2.0) {
            break;
        }
    }
    return transmission;
}
fn ambientOcclusion(p: vec3f, normal: vec3f, rotation: f32, self_: u32) -> f32 {
    let t = basisT(normal);
    let b = cross(normal, t);
    var occlusion = 0.0;
    let rays = max(1u, C.quality.y);
    for (var sample = 0u; sample < rays; sample++) {
        let d = diskSample(sample, rays, rotation);
        let dir = normalize(t * d.x + b * d.y + normal * sqrt(saturate(1.0 - dot(d, d))));
        let h = trace(p + normal * 4.0, dir, 2.0, kOcclusionRadius, 1u, kAlpha, self_);
        if (!h.hit) {
            continue;
        }
        let alpha = saturate(materialAlpha(h.primitive, h.bary)) * instanceOpacity(h.instance);
        occlusion += alpha * creaseWeight(h, normal, dir) * (1.0 - h.t / kOcclusionRadius);
    }
    return 1.0 - kOcclusionStrength * occlusion / f32(rays);
}
struct Lighting { lit: vec3f, unshadowed: vec3f, sun: vec3f }
fn nativeLighting(p: vec3f, normal: vec3f, shading: vec3f, rotation: f32, occlusion: f32, self_: u32,
                  samples: u32) -> Lighting {
    var r = Lighting(vec3f(1.0), vec3f(1.0), vec3f(0.0));
    if (L.effects.z == 0u) {
        return r;
    }
    r.unshadowed = L.ambient.rgb;
    r.lit = L.ambient.rgb * occlusion;
    for (var light = 0u; light < L.effects.z; light++) {
        var l = L.lightVector[light].xyz;
        var maximum = 1e7;
        var attenuation = 1.0;
        let local = L.lightVector[light].w > 0.0;
        if (local) {
            l -= p;
            let distanceSquared = dot(l, l);
            maximum = sqrt(distanceSquared) - 8.0;
            if (maximum <= 2.0 || distanceSquared >= L.lightColor[light].w) {
                continue;
            }
            attenuation = saturate(1.0 - distanceSquared / L.lightColor[light].w);
        }
        let centre = normalize(l);
        let ndl = saturate(dot(shading, centre));
        if (ndl <= 0.0) {
            continue;
        }
        let direct = L.lightColor[light].rgb * ndl * attenuation;
        var visible = 1.0;
        if (L.effects.y != 0u && samples > 0u) {
            let tangent = basisT(centre);
            let bitangent = cross(centre, tangent);
            var sum = 0.0;
            if (local) {
                let n = clamp(samples, 1u, 2u);
                for (var sample = 0u; sample < n; sample++) {
                    let d = diskSample(sample, n, rotation) * kPointLightRadius;
                    let toLight = l + tangent * d.x + bitangent * d.y;
                    sum += visibility(p, normal, normalize(toLight), length(toLight) - 8.0, self_);
                }
                visible = sum / f32(n);
            } else {
                let n = clamp(samples, 1u, 4u);
                for (var sample = 0u; sample < n; sample++) {
                    let d = diskSample(sample, n, rotation) * kSunRadius;
                    sum += visibility(p, normal, normalize(centre + tangent * d.x + bitangent * d.y), maximum, self_);
                }
                visible = sum / f32(n);
            }
        }
        r.unshadowed += direct;
        r.lit += direct * visible;
        if (!local) {
            r.sun += direct * visible;
        }
    }
    return r;
}
fn shadowRatio(l: Lighting) -> vec3f { return saturate((l.lit + 0.0001) / (l.unshadowed + 0.0001)); }
fn worldPosition(pixel: vec2f, depth: f32) -> vec3f {
    let uv = (pixel + 0.5) / size();
    let ndc = (uv - C.jitter.xy / size()) * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0);
    let q = C.inverse * vec4f(ndc, depth, 1.0);
    let p = q.xyz / q.w;
    return p.x * C.right.xyz + p.y * C.up.xyz - p.z * C.forward.xyz;
}
fn faceNormal(h: Hit) -> vec3f { return normalize(toWorld(localFace(h.primitive), h.instance)); }
struct Surface { p: vec3f, normal: vec3f, shading: vec3f, confidence: f32, self_: u32, ok: bool }
// Anchors a shadow receiver to its actual triangle (see primarySurface in ray_water.hlsl).
fn primarySurface(pIn: vec3f) -> Surface {
    var s = Surface(pIn, vec3f(0.0), vec3f(0.0), 0.0, kNoInstance, false);
    let distance = length(pIn);
    if (distance <= 0.0 || !finite(distance)) {
        return s;
    }
    let tolerance = max(32.0, distance * 0.02);
    let dir = pIn / distance;
    var surface = trace(vec3f(0.0), dir, max(0.5, distance - tolerance), distance + tolerance, 1u, kAlpha, kNoInstance);
    var anchor = vec3f(0.0);
    if (surface.hit) {
        s.confidence = 1.0 - smoothstep(0.25, 1.0, abs(surface.t - distance) / tolerance);
        anchor = dir * surface.t;
    }
    if (s.confidence < 1.0) {
        let below = trace(pIn, vec3f(0.0, -1.0, 0.0), 0.0, kLodReach, 1u, kLevel, kNoInstance);
        let above = trace(pIn, vec3f(0.0, 1.0, 0.0), 0.0, kLodReach, 1u, kLevel, kNoInstance);
        let up = above.hit && (!below.hit || above.t < below.t);
        var ground = below;
        if (up) {
            ground = above;
        }
        if (ground.hit) {
            let grounded = (1.0 - smoothstep(0.25, 1.0, ground.t / kLodReach)) * smoothstep(0.5, 0.7, abs(faceNormal(ground).y));
            if (grounded > s.confidence) {
                s.confidence = grounded;
                surface = ground;
                anchor = pIn + vec3f(0.0, select(-ground.t, ground.t, up), 0.0);
            }
        }
    }
    if (!surface.hit || s.confidence <= 0.0) {
        return s;
    }
    var normal = faceNormal(surface);
    if (dot(normal, anchor) > 0.0) {
        normal = -normal;
    }
    s.normal = normal;
    s.shading = shadingNormal(surface, normal);
    if ((materialFlags(surface.primitive) & 128u) != 0u) {
        s.self_ = surface.instance;
    }
    s.p = anchor;
    s.ok = finite3(normal);
    return s;
}
fn screenRadiance(uv: vec2f, expectedDepth: f32, fallback: vec3f, scale: vec3f, sky: bool) -> vec3f {
    let xy = uv * size() - 0.5;
    let o = vec2i(floor(xy));
    let f = fract(xy);
    var result = vec3f(0.0);
    for (var y = 0; y < 2; y++) {
        for (var x = 0; x < 2; x++) {
            let pixel = o + vec2i(x, y);
            let weight = select(1.0 - f.x, f.x, x == 1) * select(1.0 - f.y, f.y, y == 1);
            var value = fallback;
            if (all(pixel >= vec2i(0)) && all(pixel < vec2i(size()))) {
                let d = depthAt(pixel);
                var confidence = 0.0;
                if (sky) {
                    confidence = select(0.0, 1.0, d <= 0.0);
                } else if (d > 0.0 && expectedDepth > 0.0) {
                    confidence = 1.0 - smoothstep(0.002, 0.01, abs(expectedDepth - d) / max(expectedDepth, d));
                }
                if (confidence > 0.0) {
                    let c = colorAt(pixel).rgb;
                    if (finite3(c)) {
                        value = mix(fallback, toLinear(c) * scale, confidence);
                    }
                }
            }
            result += value * weight;
        }
    }
    return result;
}
fn hitColor(p: vec3f, h: Hit, normal: vec3f, shading: vec3f, rotation: f32) -> vec3f {
    let light = nativeLighting(p, normal, shading, rotation, 1.0, kNoInstance, 1u); // one sample: glossy anyway
    let ratio = shadowRatio(light);
    let display = materialDisplay(h.primitive, h.bary);
    let material = select(toLinear(display) * ratio, toLinear(display * light.lit), (materialFlags(h.primitive) & 128u) != 0u);
    let clip = C.projection * vec4f(viewPosition(p), 1.0);
    if (clip.w > 0.001) {
        let ndc = clip.xyz / clip.w;
        let uv = ndc.xy * vec2f(0.5, -0.5) + 0.5 + C.jitter.xy / size();
        if (all(uv > vec2f(0.002)) && all(uv < vec2f(0.998)) && ndc.z > 0.0) {
            let edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
            let filtered = screenRadiance(uv, ndc.z, material, ratio, false);
            return mix(material, filtered, smoothstep(0.002, 0.04, edge));
        }
    }
    return material;
}
fn environment(direction: vec3f, fallback: vec3f) -> vec3f {
    var weather = select(fallback, Sky[0].rgb, Sky[0].w > 0.0);
    var directional = vec3f(0.0);
    var total = 0.0;
    for (var axis = 0u; axis < 3u; axis++) {
        let component = direction[axis];
        let sector = axis * 2u + select(2u, 1u, component >= 0.0);
        let lobe = Sky[sector];
        var weight = component * component;
        weight *= weight;
        weight *= lobe.w;
        directional += lobe.rgb * weight;
        total += weight;
    }
    if (total > 0.0) {
        weather = mix(weather, directional / max(total, 0.0001), smoothstep(0.0, 0.05, total));
    }
    let clip = C.projection * vec4f(viewPosition(direction), 0.0);
    if (clip.w > 0.001) {
        let uv = clip.xy / clip.w * vec2f(0.5, -0.5) + 0.5 + C.jitter.xy / size();
        if (all(uv > vec2f(0.0)) && all(uv < vec2f(1.0))) {
            let edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
            return mix(weather, screenRadiance(uv, 0.0, weather, vec3f(1.0), true), smoothstep(0.0, 0.04, edge));
        }
    }
    return weather;
}
struct Water { distance: f32, normal: vec3f, opacity: f32, ok: bool }
fn waterSurface(o: vec3f, d: vec3f, tmin: f32) -> Water {
    var w = Water(0.0, vec3f(0.0), 0.0, false);
    let flags = L.effects.w;
    if ((flags & 3u) == 0u || ((flags & 4u) != 0u && (flags & 2u) == 0u)) {
        return w;
    }
    let h = trace(o, d, tmin, 1e7, select(2u, 4u, (flags & 1u) != 0u), kOpaque, kNoInstance);
    if (!h.hit) {
        return w;
    }
    w.distance = h.t;
    let wi = h.primitive * kAttributes;
    w.normal = vec3f(Materials[wi + 4u].zw, Materials[wi + 5u].x);
    if (w.normal.y < 0.0) {
        w.normal = -w.normal;
    }
    if ((flags & 2u) != 0u) {
        // CPU-clipped water layers prove screen coverage and opacity only: the strongest layer covering the pixel.
        coverageOpacity = 0.0;
        coverageSeen = false;
        _ = trace(o, d, tmin, 1e7, 2u, kCoverage, kNoInstance);
        w.opacity = coverageOpacity;
        w.ok = coverageSeen;
        return w;
    }
    w.opacity = saturate(dot(vec3f(1.0 - h.bary.x - h.bary.y, h.bary),
                             vec3f(Materials[wi].a, Materials[wi + 1u].a, Materials[wi + 2u].a)));
    w.ok = true;
    return w;
}
struct Waves { normal: vec3f, a2: f32, crest: f32 }
fn waterWaves(world: vec2f, geometric: vec3f, footprint: f32) -> Waves {
    var spectrum = array(vec3f(4.3, 0.022, 0.0), vec3f(2.7, 0.02, 0.55), vec3f(1.75, 0.018, -0.4), vec3f(1.13, 0.016, 1.0),
                      vec3f(0.71, 0.014, -0.85), vec3f(0.46, 0.012, 0.3), vec3f(0.29, 0.01, -1.3), vec3f(0.18, 0.008, 1.6));
    var slope = vec2f(0.0);
    var lost = 0.0;
    var height = 0.0;
    var range = 0.0;
    for (var i = 0u; i < 8u; i++) {
        let lambda = spectrum[i].x * kMetre;
        let k = 6.2831853 / lambda;
        let omega = sqrt(9.81 * kMetre * k);
        let s = spectrum[i].y;
        let angle = kWind + spectrum[i].z;
        let d = vec2f(cos(angle), sin(angle));
        let keep = smoothstep(2.0, 6.0, lambda / footprint);
        let phase = k * dot(d, world) - omega * C.sizeTime.z;
        slope += d * (s * keep * cos(phase));
        height += s / k * keep * sin(phase);
        range += s / k * keep;
        lost += 0.5 * s * s * (1.0 - keep * keep);
    }
    return Waves(normalize(geometric + vec3f(-slope.x, 0.0, -slope.y)), 0.0004 + lost,
                 select(0.5, saturate(height / range * 0.5 + 0.5), range > 0.0));
}
fn caustic(p: vec2f) -> f32 {
    let t = C.sizeTime.z;
    var a = sin(p.x + t * 0.9) + sin(p.y * 1.3 - t * 0.7) + sin((p.x + p.y) * 0.7 + t * 1.1);
    var b = sin(p.x * 0.8 - p.y * 1.1 - t * 0.8) + sin(p.y * 0.9 + t * 1.2) + sin((p.x - p.y) * 1.2 - t * 0.5);
    a = saturate(1.0 - abs(a) * 0.5);
    b = saturate(1.0 - abs(b) * 0.5);
    a *= a * a;
    b *= b * b;
    return a * a + b * b - 0.2;
}

// Glossy reflection of the water at p (geometric plane normal, wave normal and roughness), seen along direction.
fn waterReflection(p: vec3f, geometric: vec3f, normal: vec3f, a2: f32, direction: vec3f, fallback: vec3f, rotation: f32,
                   reflectionRotation: f32) -> vec3f {
    let central = reflect(direction, normal);
    let sky = environment(central, fallback);
    let tangent = basisT(central);
    let bitangent = cross(central, tangent);
    let spread = sqrt(a2) * 0.7;
    var sum = vec3f(0.0);
    reflectOrigin = p;
    reflectNormal = geometric;
    let rays = clamp(C.quality.z, 1u, 4u);
    for (var sample = 0u; sample < rays; sample++) {
        let offset = diskSample(sample, rays, reflectionRotation) * spread;
        var reflected = normalize(central + tangent * offset.x + bitangent * offset.y);
        let above = dot(reflected, geometric);
        if (above < 0.002) {
            reflected = normalize(reflected + geometric * (0.002 - above));
        }
        var o = p + geometric * 4.0;
        var value = vec3f(0.0);
        var transmission = 1.0;
        for (var layer = 0u; layer < 4u && transmission > 0.01; layer++) {
            reflectStart = o;
            let h = trace(o, reflected, 2.0, 1e7, 1u, kReflect, kNoInstance);
            if (!h.hit) {
                break;
            }
            let alpha = saturate(materialAlpha(h.primitive, h.bary)) * instanceOpacity(h.instance);
            let hit = o + reflected * h.t;
            var hitNormal = normalize(toWorld(localFace(h.primitive), h.instance));
            if (dot(hitNormal, -reflected) < 0.0) {
                hitNormal = -hitNormal;
            }
            let hitShading = shadingNormal(h, hitNormal);
            value += transmission * alpha * hitColor(hit, h, hitNormal, hitShading, rotation);
            transmission *= 1.0 - alpha;
            o = hit + reflected * 2.0;
        }
        sum += value + sky * transmission;
    }
    return sum / f32(rays);
}
// The receiver shadow of land (ray_water.hlsl main, the non-water branch) and whether a water layer covers the pixel,
// at a reduced resolution: one low pixel per C.quality.w render pixels, at the centre of its block.
fn shadowPixel(q: vec2u) -> vec2i {
    let k = max(1u, C.quality.w);
    return min(vec2i(q * k + k / 2u), vec2i(size()) - 1);
}
@compute @workgroup_size(8, 8, 1)
fn lowMain(@builtin(global_invocation_id) id: vec3u) {
    let k = max(1u, C.quality.w);
    if (any(id.xy * k >= vec2u(size()))) {
        return;
    }
    let pixel = shadowPixel(id.xy);
    var ratio = vec3f(1.0);
    var water = 0.0;
    var reflection = vec4f(0.0, 0.0, 0.0, 1.0); // display-referred reflection, sun visibility
    var shadedWater = false;
    let sceneDepth = depthAt(pixel);
    if (L.effects.x != 0u) {
        let ray = viewRay(vec2f(pixel));
        let w = waterSurface(vec3f(0.0), ray.xyz, ray.w);
        if (ray.w > 0.0 && w.ok) {
            water = 1.0;
            let p = ray.xyz * w.distance;
            let clip = C.projection * vec4f(viewPosition(p), 1.0);
            let waterDepth = clip.z / clip.w;
            if (!(sceneDepth > waterDepth + max(1e-6, abs(waterDepth) * 0.002)) && dot(w.normal, -ray.xyz) > 0.0) {
                shadedWater = true;
                let rotation = pixelNoise(vec2u(pixel), 0u);
                let lit = nativeLighting(p, w.normal, w.normal, rotation, 1.0, kNoInstance, C.quality.x);
                let open = nativeLighting(p, w.normal, w.normal, rotation, 1.0, kNoInstance, 0u);
                ratio = shadowRatio(lit);
                reflection.a = select(1.0, saturate(luminance(lit.sun) / luminance(open.sun)), luminance(open.sun) > 1e-6);
                if (C.sizeTime.w > 0.0) {
                    let footprint = 2.0 * w.distance / max(1.0, size().y * abs(C.projection[1][1]));
                    let stretched = footprint / max(abs(dot(ray.xyz, w.normal)), 0.03);
                    let waves = waterWaves(p.xz + C.limits.zw, w.normal, stretched);
                    let r = waterReflection(p, w.normal, waves.normal, waves.a2, ray.xyz, toLinear(colorAt(pixel).rgb),
                                            rotation, pixelNoise(vec2u(pixel), 2u));
                    reflection = vec4f(toDisplay(r), reflection.a);
                }
            }
        }
    }
    if (!shadedWater && L.effects.y != 0u && L.effects.z != 0u && sceneDepth > 0.0) {
        let surface = worldPosition(vec2f(pixel), sceneDepth);
        if (finite3(surface)) {
            let s = primarySurface(surface);
            if (s.ok) {
                let occlusion = ambientOcclusion(s.p, s.normal, pixelNoise(vec2u(pixel), 1u), s.self_);
                ratio = mix(vec3f(1.0), shadowRatio(nativeLighting(s.p, s.normal, s.shading, pixelNoise(vec2u(pixel), 0u),
                                                                   occlusion, s.self_, C.quality.x)), s.confidence);
            }
        }
    }
    textureStore(Output, id.xy, vec4f(ratio, water));
    textureStore(OutputB, id.xy, reflection);
}
// lowMain's water terms at a render pixel: bilinear over the neighbouring low pixels that saw water.
struct LowWater { shadow: vec3f, reflection: vec3f, visible: f32, ok: bool }
fn upsampledWater(pixel: vec2i) -> LowWater {
    let k = f32(max(1u, C.quality.w));
    let lowSize = vec2i(textureDimensions(ShadowIn));
    let xy = (vec2f(pixel) + 0.5) / k - 0.5;
    let o = vec2i(floor(xy));
    let f = fract(xy);
    var r = LowWater(vec3f(0.0), vec3f(0.0), 0.0, false);
    var total = 0.0;
    for (var y = 0; y < 2; y++) {
        for (var x = 0; x < 2; x++) {
            let q = clamp(o + vec2i(x, y), vec2i(0), lowSize - 1);
            let a = textureLoad(ShadowIn, q, 0);
            let b = textureLoad(ReflectIn, q, 0);
            let w = select(1.0 - f.x, f.x, x == 1) * select(1.0 - f.y, f.y, y == 1) * a.a + 1e-4 * a.a;
            r.shadow += a.rgb * w;
            r.reflection += toLinear(b.rgb) * w;
            r.visible += b.a * w;
            total += w;
        }
    }
    if (total <= 0.0) {
        return LowWater(vec3f(1.0), vec3f(0.0), 1.0, false);
    }
    return LowWater(r.shadow / total, r.reflection / total, r.visible / total, true);
}
// The camera ray through a render pixel: direction and the near-plane tmin (w = 0 when invalid).
fn viewRay(pixel: vec2f) -> vec4f {
    let uv = (pixel + 0.5) / size();
    let ndc = (uv - C.jitter.xy / size()) * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0);
    let view = C.inverse * vec4f(ndc, 1.0, 1.0);
    if (!finite4(view) || abs(view.w) < 1e-12) {
        return vec4f(0.0);
    }
    let v = view.xyz / view.w;
    let direction = normalize(v.x * C.right.xyz + v.y * C.up.xyz - v.z * C.forward.xyz);
    return vec4f(direction, max(0.5, C.limits.x / max(1e-5, dot(direction, C.forward.xyz))));
}
// Water within one low pixel of this render pixel (the dummy 1x1 white texture when lowMain did not run: always).
fn waterNear(pixel: vec2i) -> bool {
    let k = i32(max(1u, C.quality.w));
    let lowSize = vec2i(textureDimensions(ShadowIn));
    let q = pixel / k;
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            if (textureLoad(ShadowIn, clamp(q + vec2i(x, y), vec2i(0), lowSize - 1), 0).a > 0.5) {
                return true;
            }
        }
    }
    return false;
}
// Joint bilateral upsampling: the four nearest shadow pixels, weighted by distance and by how well the depth at their
// centre matches this pixel's (no shadow bleeding across silhouettes).
fn upsampledShadow(pixel: vec2i, depth: f32) -> vec3f {
    let k = f32(max(1u, C.quality.w));
    let lowSize = vec2i(textureDimensions(ShadowIn));
    let xy = (vec2f(pixel) + 0.5) / k - 0.5;
    let o = vec2i(floor(xy));
    let f = fract(xy);
    var sum = vec3f(0.0);
    var total = 0.0;
    var nearest = vec3f(1.0);
    var nearestError = 1e30;
    for (var y = 0; y < 2; y++) {
        for (var x = 0; x < 2; x++) {
            let q = clamp(o + vec2i(x, y), vec2i(0), lowSize - 1);
            let d = depthAt(shadowPixel(vec2u(q)));
            let error = abs(d - depth) / max(max(d, depth), 1e-6);
            let value = textureLoad(ShadowIn, q, 0).rgb;
            let w = select(1.0 - f.x, f.x, x == 1) * select(1.0 - f.y, f.y, y == 1) / (1e-3 + error * error * 1e4);
            sum += value * w;
            total += w;
            if (error < nearestError) {
                nearestError = error;
                nearest = value;
            }
        }
    }
    return select(nearest, sum / total, total > 1e-6);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (any(id.xy >= vec2u(size()))) {
        return;
    }
    let pixel = vec2i(id.xy);
    let original = colorAt(pixel);
    var result = original;
    var receiverShadow = vec3f(1.0);
    let sceneDepth = depthAt(pixel);
    let uv = (vec2f(id.xy) + 0.5) / size();
    let ndc = (uv - C.jitter.xy / size()) * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0);
    let view = C.inverse * vec4f(ndc, 1.0, 1.0);
    if (!finite4(view) || abs(view.w) < 1e-12) {
        textureStore(Output, id.xy, result);
        return;
    }
    let v = view.xyz / view.w;
    let direction = normalize(v.x * C.right.xyz + v.y * C.up.xyz - v.z * C.forward.xyz);
    let tmin = max(0.5, C.limits.x / max(1e-5, dot(direction, C.forward.xyz)));
    var water = Water(0.0, vec3f(0.0), 0.0, false);
    if (L.effects.x != 0u && waterNear(pixel)) {
        water = waterSurface(vec3f(0.0), direction, tmin);
    }
    var haveWater = water.ok;
    let distance = water.distance;
    let geometric = water.normal;
    let p = direction * distance;
    let waterClip = C.projection * vec4f(viewPosition(p), 1.0);
    let waterDepth = select(0.0, waterClip.z / waterClip.w, haveWater);
    let coveredWater = haveWater;
    haveWater = haveWater && !(sceneDepth > waterDepth + max(1e-6, abs(waterDepth) * 0.002));
    if (L.scene.z != 0u && L.scene.z <= 2u) {
        var debug = vec3f(select(0.0, 1.0, distance > 0.0), select(0.0, 1.0, coveredWater), select(0.0, 1.0, haveWater));
        if (L.scene.z == 2u) {
            debug = select(vec3f(0.0), vec3f(water.opacity), haveWater);
        }
        textureStore(Output, id.xy, vec4f(debug, original.a));
        return;
    }
    let rotation = pixelNoise(id.xy, 0u);
    var waterLight = Lighting(vec3f(1.0), vec3f(1.0), vec3f(0.0));
    var low = LowWater(vec3f(1.0), vec3f(0.0), 1.0, false);
    if (haveWater) {
        // Unshadowed lighting here, its shadowing and the reflection from lowMain.
        let open = nativeLighting(p, geometric, geometric, rotation, 1.0, kNoInstance, 0u);
        low = upsampledWater(pixel);
        if (!low.ok) {
            low.reflection = toLinear(original.rgb);
        }
        waterLight = Lighting(open.unshadowed * low.shadow, open.unshadowed, open.sun * low.visible);
    }
    if (L.effects.y != 0u && L.effects.z != 0u && sceneDepth > 0.0) {
        if (haveWater) {
            receiverShadow = shadowRatio(waterLight);
        } else {
            receiverShadow = upsampledShadow(pixel, sceneDepth);
        }
        result = vec4f(toDisplay(toLinear(original.rgb) * receiverShadow), original.a);
    }
    if (L.effects.x == 0u || !haveWater || dot(geometric, -direction) <= 0.0) {
        textureStore(Output, id.xy, result);
        return;
    }
    var bottom = distance + 8000.0;
    if (sceneDepth > 0.0) {
        let back = C.inverse * vec4f(ndc, sceneDepth, 1.0);
        if (finite4(back) && abs(back.w) > 1e-12) {
            bottom = length(back.xyz / back.w);
        }
    }
    let footprint = 2.0 * distance / max(1.0, size().y * abs(C.projection[1][1]));
    let cosine = abs(dot(direction, geometric));
    let stretched = footprint / max(cosine, 0.03);
    let world = p.xz + C.limits.zw;
    let waves = waterWaves(world, geometric, stretched);
    let normal = waves.normal;
    let ndv = saturate(dot(-direction, normal));
    let fresnel = 0.0204 + 0.9796 * pow(1.0 - ndv, 5.0);
    let thickness = max(0.0, bottom - distance);
    let depth = thickness * cosine;
    let coverage = water.opacity * smoothstep(0.0, max(1.0, footprint), thickness);
    if (coverage <= 0.0) {
        textureStore(Output, id.xy, result);
        return;
    }
    let refracted = clamp(uv + (normal.xz - geometric.xz) * kRefraction * saturate(thickness / (0.5 * kMetre)),
                          vec2f(0.001), vec2f(0.999));
    var rp = vec2i(refracted * size());
    if (depthAt(rp) > waterDepth * 1.005 + 1e-6) {
        rp = pixel;
    }
    var base = toLinear(colorAt(rp).rgb) * receiverShadow;
    let body = toLinear(original.rgb) * receiverShadow;
    let sunShare = saturate(luminance(waterLight.sun) / max(luminance(waterLight.unshadowed), 1e-4));
    if (sunShare > 0.0) {
        let bed = direction * bottom;
        let bedFootprint = footprint * bottom / max(distance, 1.0);
        let weight = sunShare * smoothstep(0.0, 0.05 * kMetre, depth) * exp(-depth / (1.5 * kMetre)) *
                     saturate(1.0 - bedFootprint / (0.04 * kMetre));
        if (weight > 0.0) {
            base *= max(0.0, 1.0 + kCausticStrength * weight * caustic((bed.xz + C.limits.zw) / (0.06 * kMetre)));
        }
    }
    let hue = body / max(max(body.r, max(body.g, body.b)), 1e-4);
    let transmittance = exp(-kWaterExtinction * (1.15 - hue) * min(thickness, 20.0 * kMetre));
    base = base * transmittance + body * 0.9 * (1.0 - transmittance);
    if (C.sizeTime.w > 0.0) {
        base = mix(base, low.reflection, fresnel);
    }
    for (var light = 0u; light < L.effects.z; light++) {
        if (L.lightVector[light].w != 0.0) {
            continue;
        }
        let l = normalize(L.lightVector[light].xyz);
        let halfVector = normalize(l - direction);
        let nl = saturate(dot(normal, l));
        let nh = saturate(dot(normal, halfVector));
        let vh = saturate(dot(-direction, halfVector));
        let lit = luminance(L.lightColor[light].rgb) * saturate(dot(geometric, l));
        var visible = 1.0;
        if (L.effects.y != 0u) {
            visible = select(0.0, saturate(luminance(waterLight.sun) / max(lit, 1e-4)), lit > 0.0);
        }
        if (nl > 0.0 && nh > 0.0 && ndv > 0.0) {
            let a2 = waves.a2;
            let denominator = nh * nh * (a2 - 1.0) + 1.0;
            let distribution = a2 / (3.14159265 * denominator * denominator);
            let geometry = (2.0 * ndv / (ndv + sqrt(a2 + (1.0 - a2) * ndv * ndv))) *
                           (2.0 * nl / (nl + sqrt(a2 + (1.0 - a2) * nl * nl)));
            let reflectance = 0.0204 + 0.9796 * pow(1.0 - vh, 5.0);
            let specular = distribution * geometry * reflectance / max(4.0 * ndv, 0.0001);
            if (specular > 0.0001) {
                base += L.lightColor[light].rgb * specular * visible;
            }
        }
        let view2 = direction.xz;
        let light2 = l.xz;
        if (dot(view2, view2) > 1e-6 && dot(light2, light2) > 1e-6) {
            var forward = saturate(dot(normalize(view2), normalize(light2)));
            forward *= forward;
            forward *= forward;
            let steep = saturate(length(normal.xz - geometric.xz) * 12.0);
            base += L.lightColor[light].rgb * visible * hue * 0.25 * forward * waves.crest * waves.crest * steep *
                    (1.0 - fresnel) * (1.0 - saturate(l.y));
        }
    }
    let band = 1.0 - smoothstep(0.0, kFoamDepth, depth);
    if (band > 0.0) {
        let f = world / (0.18 * kMetre);
        let drift = C.sizeTime.z * 0.15;
        let n = 0.65 * valueNoise(f + drift) + 0.35 * valueNoise(f * 2.7 - drift * 1.7);
        let threshold = 1.0 - 0.8 * band;
        let patches = smoothstep(threshold, threshold + 0.2, n) * band;
        let foam = patches * saturate(1.0 - stretched / (0.05 * kMetre));
        base = mix(base, 0.65 * min(waterLight.lit, vec3f(1.5)), foam * 0.85);
    }
    result = vec4f(mix(result.rgb, toDisplay(base), coverage), original.a);
    if (L.scene.z == 3u) {
        result = vec4f(water.opacity, saturate(thickness / 96.0), fresnel, original.a);
    }
    textureStore(Output, id.xy, result);
}
