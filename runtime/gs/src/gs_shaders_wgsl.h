// WGSL sources of the WebGPU renderer (wgpu_*.cpp). A port of gs_shaders.h / gs_rt_sampling.inc: the same GS pixel
// pipeline, display circuit composition and present filter, with identical constant layouts (PSConsts, DispConsts).
// Bindings follow gsr::rhi: uniforms at 0.., textures at 8.., point / linear samplers at 16 / 17.
#pragma once
// clang-format off

namespace gsr {

// Shared GS draw source. wgpu_device.cpp prepends either "const HW_BLEND = true;" plus `enable dual_source_blending;`
// (fixed-function GS blend, one texture) or "const HW_BLEND = false;" (shader blend, destination copy at binding 9).
inline const char* kWgslGsCommon = R"WGSL(
struct CB {
    rt: vec4f,      // x=rtW y=rtH (native px)  z=zMax  w=scale
    a: vec4u,       // x=flags  y=aref  z=alpha ABCD  w=FIX
    b: vec4u,       // x=fbmask  y=texW  z=texH  w=wms|wmt<<2
    c: vec4u,       // minu maxu minv maxv
    fog: vec4f,     // rgb fog colour  w=TA0
    d: vec4u,       // x=mxl|lcm<<4|mmag<<5|mmin<<6|L<<9|AEM<<12  y=passmode  z,w=dimx
    e: vec4f,       // x=K  y=filter override  z=RT alpha mode  w=TA1
    src: vec4f,     // source RT: xy=origin px, zw=1/dims
    f: vec4f,       // x=upscale factor N  y=mip bias  z=max anisotropy  w=log2(N)
    g: vec4f,       // x=footprint mip bias  yz=NDC jitter
    h: vec4f,       // xy=raster jitter  z=max motion  w=motion valid
    spriteBounds: vec4f,
}
@group(0) @binding(0) var<uniform> cb: CB;
@group(0) @binding(8) var TexA: texture_2d<f32>;
@group(0) @binding(16) var SmpPt: sampler;
@group(0) @binding(17) var SmpLin: sampler;

const F_TME = 1u;
const F_FGE = 2u;
const F_FST = 4u;
const F_TCC = 8u;
const F_ATE = 64u;
const F_DATE = 4096u;
const F_DATM = 8192u;
const F_ABE = 16384u;
const F_PABE = 32768u;
const F_DTHE = 65536u;
const F_CCLAMP = 131072u;
const F_FBA = 262144u;
const F_FB16 = 524288u;
const F_TEXRT = 4194304u;

struct VSIn {
    @location(0) pf: vec4f,
    @location(1) col: vec4f,
    @location(2) stq: vec3f,
}
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) @interpolate(linear) col: vec4f,
    @location(1) @interpolate(linear) stq: vec3f,
    @location(2) @interpolate(linear) fog: f32,
}

@vertex fn vs(i: VSIn) -> VSOut {
    var o: VSOut;
    o.pos = vec4f(i.pf.x / cb.rt.x * 2.0 - 1.0, 1.0 - i.pf.y / cb.rt.y * 2.0, saturate(i.pf.z / cb.rt.z), 1.0);
    o.pos = vec4f(o.pos.xy + cb.g.yz, o.pos.zw);
    o.col = i.col;
    o.stq = i.stq;
    o.fog = i.pf.w;
    return o;
}

// ---------------------------------------------------------------- texture sampling
fn wrapC(c: i32, dim: i32, mode: u32, mn: i32, mx: i32) -> i32 {
    if (mode == 0u) { return c & (dim - 1); }
    if (mode == 1u) { return clamp(c, 0, dim - 1); }
    if (mode == 2u) { return clamp(c, mn, mx); }
    return (c & mn) | mx;
}

fn wrapCN(c: i32, dim: i32, mode: u32, mn: i32, mx: i32) -> i32 {
    let N = i32(cb.f.x);
    if (N == 1) { return wrapC(c, dim, mode, mn, mx); }
    if (mode == 1u) { return clamp(c, 0, dim * N - 1); }
    if (mode == 2u) { return clamp(c, mn * N, (mx + 1) * N - 1); }
    var cell = c >> u32(cb.f.w);
    let sub = c & (N - 1);
    if (mode == 0u) { cell = cell & (dim - 1); } else { cell = (cell & mn) | mx; }
    return cell * N + sub;
}

fn fetchTexel(p: vec2i, lvl: i32, dim: vec2i) -> vec4f {
    let wms = cb.b.w & 3u;
    let wmt = (cb.b.w >> 2u) & 3u;
    let x = wrapCN(p.x, dim.x, wms, i32(cb.c.x) >> u32(lvl), i32(cb.c.y) >> u32(lvl));
    let y = wrapCN(p.y, dim.y, wmt, i32(cb.c.z) >> u32(lvl), i32(cb.c.w) >> u32(lvl));
    return textureLoad(TexA, vec2i(x, y), lvl);
}

fn sampleLvl(uv: vec2f, lvl: i32, lin: bool) -> vec4f {
    let dim = vec2i(max(1, i32(cb.b.y) >> u32(lvl)), max(1, i32(cb.b.z) >> u32(lvl)));
    var p = uv * (cb.f.x / f32(1 << u32(lvl)));
    if (lin) {
        p = p - 0.5;
        let fl = floor(p);
        let i0 = vec2i(fl);
        let f = p - fl;
        let a = fetchTexel(i0, lvl, dim);
        let b = fetchTexel(i0 + vec2i(1, 0), lvl, dim);
        let c = fetchTexel(i0 + vec2i(0, 1), lvl, dim);
        let d = fetchTexel(i0 + vec2i(1, 1), lvl, dim);
        return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    }
    return fetchTexel(vec2i(floor(p)), lvl, dim);
}

// Point sampling keeps the original continuous address calculation.
fn wrapRTPoint(c: f32, dim: f32, mode: u32, mn: f32, mx: f32) -> f32 {
    if (mode == 0u) { return c - dim * floor(c / dim); }
    if (mode == 1u) { return clamp(c, 0.0, dim - 0.001); }
    if (mode == 2u) { return clamp(c, mn, mx + 0.999); }
    return f32(wrapC(i32(floor(c)), i32(dim), mode, i32(mn), i32(mx))) + fract(c);
}

fn wrapRTTap(c: f32, dim: f32, mode: u32, mn: f32, mx: f32, scale: f32) -> f32 {
    let halfTap = 0.5 / scale;
    if (mode == 1u) { return clamp(c, halfTap, dim - halfTap); }
    if (mode == 2u) { return clamp(c, mn + halfTap, mx + 1.0 - halfTap); }
    let cell = i32(floor(c));
    let wrapped = wrapC(cell, i32(dim), mode, i32(mn), i32(mx));
    return f32(wrapped) + (c - floor(c));
}

fn convertRTAlpha(t0: vec4f) -> vec4f {
    var t = t0;
    let am = u32(cb.e.z);
    if (am != 0u) {
        let high16 = am == 2u && t.a * 255.0 >= 64.0;
        var a = select(cb.fog.w, cb.e.w, high16);
        if (((cb.d.x >> 12u) & 1u) != 0u && !high16 && all(floor(t.rgb * 255.0 + 0.5) == vec3f(0.0))) { a = 0.0; }
        t.a = a / 255.0;
    }
    return t;
}

// Point sampling of TexA (clamp to edge) as a load: one sampler per texture keeps GLSL backends working.
fn loadNearest(uvn: vec2f) -> vec4f {
    let size = vec2i(textureDimensions(TexA));
    let p = clamp(vec2i(floor(uvn * vec2f(size))), vec2i(0), size - vec2i(1));
    return textureLoad(TexA, p, 0);
}

fn fetchRTTap(physical: vec2f, scale: vec2f) -> vec4f {
    var p = (physical + 0.5) / scale - cb.src.xy;
    p.x = wrapRTTap(p.x, f32(cb.b.y), cb.b.w & 3u, f32(cb.c.x), f32(cb.c.y), scale.x);
    p.y = wrapRTTap(p.y, f32(cb.b.z), (cb.b.w >> 2u) & 3u, f32(cb.c.z), f32(cb.c.w), scale.y);
    return convertRTAlpha(loadNearest((cb.src.xy + p) * cb.src.zw));
}

fn sampleRT(uv: vec2f, lin: bool) -> vec4f {
    let wms = cb.b.w & 3u;
    let wmt = (cb.b.w >> 2u) & 3u;
    if (!lin) {
        var p: vec2f;
        p.x = wrapRTPoint(uv.x, f32(cb.b.y), wms, f32(cb.c.x), f32(cb.c.y));
        p.y = wrapRTPoint(uv.y, f32(cb.b.z), wmt, f32(cb.c.z), f32(cb.c.w));
        return convertRTAlpha(loadNearest((cb.src.xy + p) * cb.src.zw));
    }
    if (u32(cb.e.z) == 0u && wms == 1u && wmt == 1u) {
        let p = clamp(uv, vec2f(0.5), vec2f(f32(cb.b.y), f32(cb.b.z)) - 0.5);
        return textureSampleLevel(TexA, SmpLin, (cb.src.xy + p) * cb.src.zw, 0.0);
    }
    let size = vec2f(textureDimensions(TexA));
    let scale = size * cb.src.zw;
    var p = (cb.src.xy + uv) * scale;
    p = p - 0.5;
    let base = floor(p);
    let f = p - base;
    let a = fetchRTTap(base, scale);
    let b = fetchRTTap(base + vec2f(1.0, 0.0), scale);
    let c = fetchRTTap(base + vec2f(0.0, 1.0), scale);
    let d = fetchRTTap(base + vec2f(1.0, 1.0), scale);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

fn sampleMip(uv: vec2f, l0in: f32, mag: bool, mxl: u32, mmag: u32, mmin: u32, lin: bool) -> vec4f {
    let useMip = (!mag) && mmin >= 2u && mxl > 0u;
    let mipLin = (!mag) && (mmin == 3u || mmin == 5u) && mxl > 0u;
    let l = clamp(l0in, 0.0, f32(mxl));
    var l0 = 0;
    if (useMip) { l0 = select(i32(floor(l + 0.5)), i32(floor(l)), mipLin); }
    var c0 = sampleLvl(uv, l0, lin);
    if (mipLin) {
        let l1 = min(l0 + 1, i32(mxl));
        let c1 = sampleLvl(uv, l1, lin);
        c0 = mix(c0, c1, fract(l));
    }
    return c0;
}

fn sampleTexture(uv: vec2f, q: f32, flags: u32, duvdx: vec2f, duvdy: vec2f) -> vec4f {
    let fst = (flags & F_FST) != 0u;
    let mxl = cb.d.x & 15u;
    let lcm = (cb.d.x >> 4u) & 1u;
    let mmag = (cb.d.x >> 5u) & 1u;
    let mmin = (cb.d.x >> 6u) & 7u;
    let L = (cb.d.x >> 9u) & 3u;
    var lod = cb.e.x;
    if (!(lcm != 0u || fst)) { lod = -log2(max(abs(q), 1e-20)) * f32(1u << L) + cb.e.x; }
    let lodB = lod + cb.f.y;
    let mag = lodB < 0.0;
    var lin = select(mmin == 1u || mmin >= 4u, mmag == 1u, mag);
    if (cb.e.y > 0.5) { lin = cb.e.y > 1.5; }
    if ((flags & F_TEXRT) != 0u) { return sampleRT(uv, lin); }

    if (cb.f.z > 1.5 && mxl > 0u && lcm == 0u && !fst && mmin >= 2u) {
        let lx = length(duvdx);
        let ly = length(duvdy);
        let major = max(lx, ly);
        let minor = max(min(lx, ly), 1e-6);
        if (major > 1.0) {
            let ratio = clamp(major / minor, 1.0, cb.f.z);
            let taps = max(1.0, ceil(ratio - 0.02));
            let lodA = log2(major / taps) + cb.g.x;
            let axis = select(duvdy, duvdx, lx >= ly);
            let magA = lodA < 0.0;
            var linA = select(mmin == 1u || mmin >= 4u, mmag == 1u, magA);
            if (cb.e.y > 0.5) { linA = cb.e.y > 1.5; }
            var acc = vec4f(0.0);
            let n = i32(taps);
            for (var i = 0; i < n; i++) {
                let t = (f32(i) + 0.5) / taps - 0.5;
                acc += sampleMip(uv + axis * t, lodA, magA, mxl, mmag, mmin, linA);
            }
            return acc / taps;
        }
    }
    return sampleMip(uv, lodB, mag, mxl, mmag, mmin, lin);
}

// ---------------------------------------------------------------- pixel pipeline
fn sourcePixel(i: VSOut, duvdx: vec2f, duvdy: vec2f, uvIn: vec2f) -> vec4f {
    let fl = cb.a.x;
    let cv = floor(i.col + 0.5);
    var c = cv.rgb;
    var a = cv.a;
    if ((fl & F_TME) != 0u) {
        var uv = uvIn;
        var q = 1.0;
        if ((fl & F_FST) == 0u) { q = i.stq.z; }
        if ((fl & (1u << 24u)) != 0u) { uv = clamp(uv, cb.spriteBounds.xy, cb.spriteBounds.zw); }
        let t = floor(sampleTexture(uv, q, fl, duvdx, duvdy) * 255.0 + 0.5);
        let tfx = (fl >> 4u) & 3u;
        let tcc = (fl & F_TCC) != 0u;
        if (tfx == 0u) {
            c = floor(t.rgb * cv.rgb / 128.0);
            a = select(cv.a, floor(t.a * cv.a / 128.0), tcc);
        } else if (tfx == 1u) {
            c = t.rgb;
            a = select(cv.a, t.a, tcc);
        } else if (tfx == 2u) {
            c = floor(t.rgb * cv.rgb / 128.0) + cv.a;
            a = select(cv.a, t.a + cv.a, tcc);
        } else {
            c = floor(t.rgb * cv.rgb / 128.0) + cv.a;
            a = select(cv.a, t.a, tcc);
        }
        c = clamp(c, vec3f(0.0), vec3f(255.0));
        a = clamp(a, 0.0, 255.0);
    }
    if ((fl & F_FGE) != 0u) {
        let f = clamp(floor(i.fog + 0.5), 0.0, 255.0);
        c = clamp(floor((c * f + cb.fog.rgb * (256.0 - f)) / 256.0), vec3f(0.0), vec3f(255.0));
    }
    return vec4f(c, a);
}

// The texture coordinate in texels, before sprite clamping; derivatives are taken from it in uniform control flow.
fn texelUV(i: VSOut) -> vec2f {
    let fl = cb.a.x;
    if ((fl & F_FST) != 0u) { return i.stq.xy; }
    let q = i.stq.z;
    var iq = 0.0;
    if (q != 0.0) { iq = 1.0 / q; }
    return vec2f(i.stq.x * iq * f32(cb.b.y), i.stq.y * iq * f32(cb.b.z));
}

fn alphaPasses(a: f32) -> bool {
    let fl = cb.a.x;
    var pass1 = true;
    if ((fl & F_ATE) != 0u) {
        let atst = (fl >> 7u) & 7u;
        let ai = i32(a);
        let ar = i32(cb.a.y);
        if (atst == 0u) { pass1 = false; }
        else if (atst == 1u) { pass1 = true; }
        else if (atst == 2u) { pass1 = ai < ar; }
        else if (atst == 3u) { pass1 = ai <= ar; }
        else if (atst == 4u) { pass1 = ai == ar; }
        else if (atst == 5u) { pass1 = ai >= ar; }
        else if (atst == 6u) { pass1 = ai > ar; }
        else { pass1 = ai != ar; }
    }
    // pass mode 0 keeps passing pixels, 1 (the alpha-fail pass) keeps failing ones
    return select(!pass1, pass1, cb.d.y == 0u);
}

fn dither16(pn: vec2i, cin: vec3f, ain: f32, fl: u32) -> vec4f {
    var e = 0;
    if ((fl & F_DTHE) != 0u) {
        let idx = u32((pn.y & 3) * 4 + (pn.x & 3));
        let dm = select(cb.d.w, cb.d.z, idx < 8u);
        e = i32((dm >> (4u * (idx & 7u))) & 7u);
        if (e >= 4) { e -= 8; }
    }
    let outc = floor(clamp(cin + f32(e), vec3f(0.0), vec3f(255.0)) / 8.0) * 8.0;
    let outa = select(0.0, 128.0, ain >= 128.0);
    return vec4f(outc, outa);
}
)WGSL";

// Fixed-function blend variant: colour + blend factor (dual source).
inline const char* kWgslGsHw = R"WGSL(
struct PSOut {
    @location(0) @blend_src(0) c0: vec4f,
    @location(0) @blend_src(1) c1: vec4f,
}
@fragment fn fs(i: VSOut) -> PSOut {
    let fl = cb.a.x;
    let uv = texelUV(i);
    let duvdx = dpdx(uv);
    let duvdy = dpdy(uv);
    let source = sourcePixel(i, duvdx, duvdy, uv);
    var c = source.rgb;
    let a = source.a;
    if (!alphaPasses(a)) { discard; }
    var writeAlpha = a;
    if ((fl & F_FBA) != 0u) { writeAlpha = f32(u32(a) | 0x80u); }
    let abcd = cb.a.z;
    let ctype = (abcd >> 4u) & 3u;
    let coef = select(f32(cb.a.w), a, ctype == 0u);
    let cc = saturate(coef / 128.0);
    var outc = c;
    var outa = writeAlpha;
    if ((fl & F_FB16) != 0u) {
        let r = dither16(vec2i(i.pos.xy / cb.rt.w), outc, outa, fl);
        outc = r.rgb;
        outa = r.a;
    }
    var o: PSOut;
    o.c0 = vec4f(outc / 255.0, outa / 255.0);
    o.c1 = vec4f(cc);
    return o;
}
)WGSL";

// Shader blend variant: the destination comes from a copy of the target (binding 9).
inline const char* kWgslGsSb = R"WGSL(
@group(0) @binding(9) var DestTex: texture_2d<f32>;
@fragment fn fs(i: VSOut) -> @location(0) vec4f {
    let fl = cb.a.x;
    let uv = texelUV(i);
    let duvdx = dpdx(uv);
    let duvdy = dpdy(uv);
    let source = sourcePixel(i, duvdx, duvdy, uv);
    let c = source.rgb;
    let a = source.a;
    if (!alphaPasses(a)) { discard; }
    var writeAlpha = a;
    if ((fl & F_FBA) != 0u) { writeAlpha = f32(u32(a) | 0x80u); }
    let pp = vec2i(i.pos.xy);
    let pn = vec2i(i.pos.xy / cb.rt.w);
    let dst = floor(textureLoad(DestTex, pp, 0) * 255.0 + 0.5);
    if ((fl & F_DATE) != 0u) {
        let bit = (u32(dst.a) & 0x80u) != 0u;
        let datm = (fl & F_DATM) != 0u;
        if (bit != datm) { discard; }
    }
    var outc = c;
    if ((fl & F_ABE) != 0u && ((fl & F_PABE) == 0u || a >= 128.0)) {
        let abcd = cb.a.z;
        let sa = abcd & 3u;
        let sb = (abcd >> 2u) & 3u;
        let sc = (abcd >> 4u) & 3u;
        let sd = (abcd >> 6u) & 3u;
        let A = select(select(vec3f(0.0), dst.rgb, sa == 1u), c, sa == 0u);
        let B = select(select(vec3f(0.0), dst.rgb, sb == 1u), c, sb == 0u);
        let D = select(select(vec3f(0.0), dst.rgb, sd == 1u), c, sd == 0u);
        let C = select(select(f32(cb.a.w), dst.a, sc == 1u), a, sc == 0u);
        var r = floor((A - B) * C / 128.0) + D;
        if ((fl & F_CCLAMP) != 0u) { r = clamp(r, vec3f(0.0), vec3f(255.0)); }
        else { r = vec3f(vec3i(r) & vec3i(255)); }
        outc = r;
    }
    var outa = writeAlpha;
    if ((fl & F_FB16) != 0u) {
        let r = dither16(pn, outc, outa, fl);
        outc = r.rgb;
        outa = r.a;
    }
    let fm = cb.b.x;
    if (fm != 0u) {
        let s4 = vec4u(u32(outc.r), u32(outc.g), u32(outc.b), u32(outa));
        let d4 = vec4u(u32(dst.r), u32(dst.g), u32(dst.b), u32(dst.a));
        let m4 = vec4u(fm & 255u, (fm >> 8u) & 255u, (fm >> 16u) & 255u, (fm >> 24u) & 255u);
        let r4 = (s4 & ~m4) | (d4 & m4);
        outc = vec3f(r4.rgb);
        outa = f32(r4.a);
    }
    return vec4f(outc / 255.0, outa / 255.0);
}
)WGSL";

// Fullscreen triangle shared by the display / present programs.
inline const char* kWgslFull = R"WGSL(
struct FSOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
}
@vertex fn vs(@builtin(vertex_index) id: u32) -> FSOut {
    var o: FSOut;
    let p = vec2f(f32((id << 1u) & 2u), f32(id & 2u));
    o.uv = p;
    o.pos = vec4f(p * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
    return o;
}
)WGSL";

// PMODE composition of the two read circuits into the display image.
inline const char* kWgslDisplay = R"WGSL(
struct CBD {
    r1: vec4f,
    r2: vec4f,
    bg: vec4f,
    m: vec4u, // x=en1 y=en2 z=mmod|amod<<1|slbg<<2 w=alp
}
@group(0) @binding(0) var<uniform> d: CBD;
@group(0) @binding(8) var D1: texture_2d<f32>;
@group(0) @binding(9) var D2: texture_2d<f32>;
@group(0) @binding(16) var SmpPt: sampler;
@group(0) @binding(17) var SmpLin: sampler;
@fragment fn fs(i: FSOut) -> @location(0) vec4f {
    let uv1 = mix(d.r1.xy, d.r1.zw, i.uv);
    let uv2 = mix(d.r2.xy, d.r2.zw, i.uv);
    let s1 = textureSampleLevel(D1, SmpLin, uv1, 0.0);
    let s2 = textureSampleLevel(D2, SmpLin, uv2, 0.0);
    let c1 = select(vec4f(0.0), s1, d.m.x != 0u);
    let c2 = select(vec4f(0.0), s2, d.m.y != 0u);
    let mm = d.m.z;
    let mmod = (mm & 1u) != 0u;
    let amod = (mm & 2u) != 0u;
    let slbg = (mm & 4u) != 0u;
    var outc: vec3f;
    if (d.m.x != 0u && d.m.y != 0u) {
        var al = f32(d.m.w) / 128.0;
        if (!mmod) { al = select(c1.a, c2.a, amod) * 255.0 / 128.0; }
        let cd = select(c1.rgb, d.bg.rgb, slbg);
        outc = mix(cd, c2.rgb, saturate(al));
    } else if (d.m.x != 0u) { outc = c1.rgb; }
    else if (d.m.y != 0u) { outc = c2.rgb; }
    else { outc = d.bg.rgb; }
    return vec4f(outc, 1.0);
}
)WGSL";

// Final present: area-average downscale / sharp-bilinear upscale, optional field handling.
inline const char* kWgslPresent = R"WGSL(
struct CBP {
    a: vec4f, // xy = source size (px), zw = viewport size (px)
    m: vec4u, // x = deinterlace (0 weave, 1 bob, 2 blend)  y = field  z = sharp  w = internal scale
}
@group(0) @binding(0) var<uniform> p: CBP;
@group(0) @binding(8) var BlitSrc: texture_2d<f32>;
@group(0) @binding(16) var SmpPt: sampler;
@group(0) @binding(17) var SmpLin: sampler;

struct Taps {
    i0: i32,
    n: i32,
    w: array<f32, 8>,
}
fn presentTaps(pos: f32, s: f32) -> Taps {
    var t: Taps;
    for (var k = 0; k < 8; k++) { t.w[k] = 0.0; }
    if (s >= 1.0) {
        let q = pos - 0.5;
        let fl = floor(q);
        let f = saturate((q - fl - 0.5) * s + 0.5);
        t.i0 = i32(fl);
        t.n = 2;
        t.w[0] = 1.0 - f;
        t.w[1] = f;
    } else {
        let h = 0.5 / s;
        let a = pos - h;
        let b = pos + h;
        t.i0 = i32(floor(a));
        let i1 = i32(ceil(b)) - 1;
        t.n = min(i1 - t.i0 + 1, 8);
        var tot = 0.0;
        for (var k = 0; k < t.n; k++) {
            let lo = max(f32(t.i0 + k), a);
            let hi = min(f32(t.i0 + k + 1), b);
            t.w[k] = max(hi - lo, 0.0);
            tot += t.w[k];
        }
        for (var k2 = 0; k2 < t.n; k2++) { t.w[k2] /= max(tot, 1e-6); }
    }
    return t;
}

fn presentFetch(xin: i32, yin: i32, dim: vec2i) -> vec3f {
    let x = clamp(xin, 0, dim.x - 1);
    var y = clamp(yin, 0, dim.y - 1);
    let sc = max(p.m.w, 1u);
    if (p.m.x == 1u) {
        let ln = u32(y) / sc;
        y = i32(((ln & ~1u) | (p.m.y & 1u)) * sc + (u32(y) % sc));
        y = min(y, dim.y - 1);
    }
    var c = textureLoad(BlitSrc, vec2i(x, y), 0).rgb;
    if (p.m.x == 2u) {
        c = 0.5 * (c + textureLoad(BlitSrc, vec2i(x, min(y + i32(sc), dim.y - 1)), 0).rgb);
    }
    return c;
}

@fragment fn fs(i: FSOut) -> @location(0) vec4f {
    let linear = textureSampleLevel(BlitSrc, SmpLin, i.uv, 0.0).rgb;
    if (p.m.z == 0u && p.m.x == 0u) { return vec4f(linear, 1.0); }
    let src = p.a.xy;
    var s = p.a.zw / src;
    let pos = i.uv * src;
    let dim = vec2i(src);
    if (p.m.z == 0u) { s = vec2f(1.0); }
    let tx = presentTaps(pos.x, s.x);
    let ty = presentTaps(pos.y, s.y);
    var acc = vec3f(0.0);
    for (var y = 0; y < ty.n; y++) {
        for (var x = 0; x < tx.n; x++) {
            acc += presentFetch(tx.i0 + x, ty.i0 + y, dim) * (tx.w[x] * ty.w[y]);
        }
    }
    return vec4f(acc, 1.0);
}
)WGSL";

// The exact centre sample of every native pixel of a scaled target (GS readback must not filter: alpha bits and packed
// 16 / 24-bit values matter). Output pixel (x, y) reads source ((x * s + s / 2), ((y + y0) * s + s / 2)).
inline const char* kWgslGather = R"WGSL(
struct G {
    p: vec4u, // x = scale, y = first native row
}
@group(0) @binding(0) var<uniform> g: G;
@group(0) @binding(8) var Src: texture_2d<f32>;
@fragment fn fs(i: FSOut) -> @location(0) vec4f {
    let p = vec2u(i.pos.xy);
    let s = g.p.x;
    return textureLoad(Src, vec2i(i32(p.x * s + s / 2u), i32((p.y + g.p.y) * s + s / 2u)), 0);
}
)WGSL";

// A textured or solid rectangle placed by a uniform (target pixels), so placements may extend past the target
// (WebGPU viewports must stay inside it). mode 0 = linear sample rgb with the given alpha, 1 = solid colour.
inline const char* kWgslRect = R"WGSL(
struct R {
    rect: vec4f,   // x0 y0 x1 y1 in target pixels
    size: vec4f,   // xy = target size
    color: vec4f,  // solid colour (mode 1); w = output alpha (mode 0)
    mode: vec4u,
}
@group(0) @binding(0) var<uniform> r: R;
@group(0) @binding(8) var Src: texture_2d<f32>;
@group(0) @binding(16) var SmpPt: sampler;
@group(0) @binding(17) var SmpLin: sampler;
struct V {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
}
@vertex fn vs(@builtin(vertex_index) id: u32) -> V {
    let corner = array(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0),
                       vec2f(0.0, 1.0));
    let c = corner[id];
    let px = mix(r.rect.xy, r.rect.zw, c);
    var o: V;
    o.pos = vec4f(px.x / r.size.x * 2.0 - 1.0, 1.0 - px.y / r.size.y * 2.0, 0.0, 1.0);
    o.uv = c;
    return o;
}
@fragment fn fs(i: V) -> @location(0) vec4f {
    let s = textureSampleLevel(Src, SmpLin, i.uv, 0.0);
    if (r.mode.x == 1u) { return r.color; }
    return vec4f(s.rgb, r.color.w);
}
)WGSL";

} // namespace gsr
