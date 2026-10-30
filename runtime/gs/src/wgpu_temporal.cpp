// Portable temporal anti-aliasing for the WebGPU backend: the substitute for DLSS / DLAA on the shared renderer (every
// upscaler mode runs as full-resolution TAA).
//
// The world is opened at the first full-frame, depth-tested world draw (as the D3D11 temporal path does) and every
// world draw is offset by a Halton sub-pixel jitter. At the engine's EndWorld (finalizeNeuralWorld, before the native
// HUD) one fragment pass reprojects the history through the scene camera (guide_camera.h: current clip ->
// previous clip), clamps it to the current 3x3 neighbourhood in YCoCg and blends it with the current pixel; the result
// is written back into the target and becomes the next history.
#include "guide_camera.h"
#include "wgpu_backend_impl.h"

#include <algorithm>
#include <cmath>

namespace gsr {

namespace {

const char* kWgslTaa = R"WGSL(
struct T {
    clipToPrev: mat4x4f, // row-major row-vector matrix uploaded as is: prev = M * clip (see wgpu_temporal.cpp)
    dims: vec4u,         // region w, h; z = history valid
    zmap: vec4f,         // far, 1 / (near - far), current weight, history texel scale (1 / history texture width)
    jitter: vec4f,       // current raster jitter (pixels); zw = 1 / history texture size
}
@group(0) @binding(0) var<uniform> t: T;
@group(0) @binding(8) var Color: texture_2d<f32>;
@group(0) @binding(9) var Depth: texture_2d<f32>;
@group(0) @binding(10) var History: texture_2d<f32>;
@group(0) @binding(16) var SmpPt: sampler;
@group(0) @binding(17) var SmpLin: sampler;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(p[i], 0.0, 1.0);
}
fn ycocg(c: vec3f) -> vec3f {
    return vec3f(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
fn rgb(y: vec3f) -> vec3f {
    return vec3f(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    let p = vec2i(pos.xy);
    let size = vec2i(t.dims.xy);
    let cur = textureLoad(Color, p, 0);
    var lo = ycocg(cur.rgb);
    var hi = lo;
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            let q = clamp(p + vec2i(dx, dy), vec2i(0), size - 1);
            let c = ycocg(textureLoad(Color, q, 0).rgb);
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }
    if (t.dims.z == 0u) {
        return cur;
    }
    // Depth is GS-normalised (reverse Z): map it onto [0, 1] over [far, near] like the guide matrix expects. Sky
    // (depth 0) reprojects as a point at infinity: rotation only, which is right for the sky.
    let d = clamp((textureLoad(Depth, p, 0).r - t.zmap.x) * t.zmap.y, 0.0, 1.0);
    let c = pos.xy - t.jitter.xy;
    let clip = vec4f(c / vec2f(size) * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), d, 1.0);
    let prev = t.clipToPrev * clip;
    if (!(abs(prev.w) > 1e-6)) {
        return cur;
    }
    let ndc = prev.xy / prev.w;
    let pp = (ndc * vec2f(0.5, -0.5) + 0.5) * vec2f(size);
    if (any(pp < vec2f(0.0)) || any(pp >= vec2f(size))) {
        return cur;
    }
    let h = textureSampleLevel(History, SmpLin, pp * t.jitter.zw, 0.0).rgb;
    let clamped = rgb(clamp(ycocg(h), lo, hi));
    return vec4f(mix(clamped, cur.rgb, t.zmap.z), cur.a); // GS alpha stays the current frame's
}
)WGSL";

struct TaaConstants {
    float clipToPrev[16];
    uint32_t dims[4];
    float zmap[4];
    float jitter[4];
};

float halton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    for (uint32_t i = index; i > 0; i /= base) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
    }
    return r;
}

guide::View viewOf(const SceneGuideCamera& c, const float* xy) {
    guide::View v;
    std::copy(std::begin(c.worldToClipVU), std::end(c.worldToClipVU), v.vp);
    std::copy(c.viewportScale, c.viewportScale + 3, v.scale);
    std::copy(c.viewportOffset, c.viewportOffset + 3, v.offset);
    v.xyOffset[0] = xy[0];
    v.xyOffset[1] = xy[1];
    v.valid = (c.flags & SceneGuideCamera::Valid) != 0;
    return v;
}

} // namespace

void WgpuBackend::Impl::enterTemporalWorld(RT& rt, uint32_t zbp, float zmax, uint64_t xyoffset) {
    rt.neuralState = 1;
    rt.temporalEpoch = motionEpoch;
    rt.temporalSR = false;
    rt.wjx = rt.wjy = 0;
    // One history (and one ray-traced world): the first full world target of an epoch owns it.
    if ((!upMode && !modernWater && !rayShadows) || (taaOwnerEpoch == motionEpoch && taaOwner != &rt)) {
        rt.neuralState = 2;
        return;
    }
    DepthBuf* depth = getDepth(zbp, rt.w, rt.h);
    if (!depth || !depth->tex) {
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "[temporal] world without a depth buffer (zbp %u, %ux%u): no TAA or ray tracing\n",
                         zbp, rt.w, rt.h);
            warned = true;
        }
        rt.neuralState = 2;
        taaReset = true;
        return;
    }
    taaOwner = &rt;
    taaOwnerEpoch = motionEpoch;
    rt.taaCamera = sceneCamera;
    rt.taaDepth = zbp;
    rt.taaZmax = zmax;
    rt.taaXY[0] = static_cast<float>(bitsOf(xyoffset, 0, 16));
    rt.taaXY[1] = static_cast<float>(bitsOf(xyoffset, 32, 16));
    if (!upMode)
        return; // ray tracing only: an unjittered world
    // 8 Halton (2, 3) phases, in render pixels: the history converges within a few frames.
    const uint32_t phase = static_cast<uint32_t>(taaFrames % 8) + 1;
    rt.wjx = halton(phase, 2) - 0.5f;
    rt.wjy = halton(phase, 3) - 0.5f;
    if (hud::frozenWorld(sceneCamera.guiScreen, nativeMenu, nativeMenuScreen))
        rt.wjx = rt.wjy = 0; // a still world under the pause menu must not be resampled (hud_page.h)
    rt.temporalSR = true;    // world draws carry the jitter
}

void WgpuBackend::Impl::finalizeNeuralWorld(RT& rt) {
    if (rt.neuralState != 1)
        return;
    rt.neuralState = 2;
    const bool jittered = rt.temporalSR;
    rt.temporalSR = false;
    // The ray pass works on this frame's (jittered) world; the temporal AA then accumulates its result.
    if (modernWater || rayShadows)
        traceWorld(rt, jittered);
    if (!jittered || !upMode)
        return;
    DepthBuf* depth = getDepth(rt.taaDepth, rt.w, rt.h);
    const SceneGuideCamera& cam = rt.taaCamera;
    const uint32_t frameW = cam.frameWidth, frameH = cam.frameHeight;
    if (!depth || !depth->tex || !progTaa || !frameW || !frameH || frameW > rt.w || frameH > rt.h)
        return;
    const uint32_t w = frameW * scale, h = frameH * scale;
    // Ping-pong: the resolve reads one history and writes the other, which then becomes the history.
    for (auto& tex : taaTex)
        if (!tex || tex->width() != w || tex->height() != h) {
            tex = createTexture(w, h, rhi::RenderTarget | rhi::Sampled | rhi::CopySrc, rhi::Format::RGBA8, 1,
                                "temporal AA history");
            taaReset = true;
        }
    if (!taaTex[0] || !taaTex[1])
        return;

    guide::Mapping map;
    map.frameW = w; // pixels of this resolve (the guide matrix maps the frame onto them)
    map.frameH = h;
    map.zmax = rt.taaZmax;
    map.farGs = 16.0f * cam.viewportOffset[2] / map.zmax;
    map.nearGs = 16.0f * (cam.viewportOffset[2] + cam.viewportScale[2]) / map.zmax;
    // The mapping is per native pixel: scale the frame (and XYOFFSET) to render pixels.
    guide::View cur = viewOf(cam, rt.taaXY), prev = viewOf(taaPrevCamera, taaPrevXY);
    for (guide::View* v : {&cur, &prev}) {
        for (int i = 0; i < 2; ++i) {
            v->scale[i] *= static_cast<float>(scale);
            v->offset[i] *= static_cast<float>(scale);
            v->xyOffset[i] *= static_cast<float>(scale);
        }
    }
    guide::Matrix curM{}, invCur{}, prevM{}, transform{};
    bool history = !taaReset && taaPrevValid && (cam.flags & SceneGuideCamera::Reset) == 0 &&
                   taaLastFrame + 12 >= frame && taaPrevSize[0] == w && taaPrevSize[1] == h &&
                   map.nearGs > map.farGs + 1e-8f;
    if (history)
        history = guide::cameraMatrix(map, cur, curM) && guide::inverse(curM, invCur) &&
                  guide::cameraMatrix(map, prev, prevM);
    if (history)
        guide::multiply(invCur, prevM, transform);
    else
        guide::identity(transform);

    TaaConstants k{};
    if (!guide::toFloats(transform, k.clipToPrev)) {
        guide::Matrix id{};
        guide::identity(id);
        guide::toFloats(id, k.clipToPrev);
        history = false;
    }
    k.dims[0] = w;
    k.dims[1] = h;
    k.dims[2] = history ? 1u : 0u;
    k.zmap[0] = map.farGs;
    k.zmap[1] = 1.0f / std::max(map.nearGs - map.farGs, 1e-8f);
    k.zmap[2] = 0.1f; // current frame weight
    k.jitter[0] = rt.wjx;
    k.jitter[1] = rt.wjy;
    rhi::Texture& historyTex = *taaTex[taaCurrent];
    rhi::Texture& out = *taaTex[taaCurrent ^ 1];
    k.jitter[2] = 1.0f / static_cast<float>(historyTex.width());
    k.jitter[3] = 1.0f / static_cast<float>(historyTex.height());

    gpu->setTargets(&out, nullptr, 0);
    gpu->setProgram(progTaa.get());
    gpu->setBlend({});
    gpu->setDepth({});
    gpu->setTopology(rhi::Topology::TriangleList);
    gpu->setViewport(0, 0, static_cast<float>(w), static_cast<float>(h));
    gpu->setScissor({0, 0, w, h});
    gpu->setUniforms(0, &k, sizeof k);
    gpu->setTexture(0, rt.tex.get());
    gpu->setTexture(1, depth->tex.get());
    gpu->setTexture(2, &historyTex);
    gpu->drawNoInput(3);
    gpu->copyTexture(out, 0, 0, 0, *rt.tex, 0, 0, 0, w, h);
    taaCurrent ^= 1;

    taaPrevCamera = cam;
    taaPrevXY[0] = rt.taaXY[0];
    taaPrevXY[1] = rt.taaXY[1];
    taaPrevValid = (cam.flags & SceneGuideCamera::Valid) != 0;
    taaPrevSize[0] = w;
    taaPrevSize[1] = h;
    taaLastFrame = frame;
    taaReset = false;
    ++taaFrames;
    rt.dirty = true;
    ++rt.writes;
    rt.dy0 = 0;
    rt.dy1 = std::min(rt.h, frameH);
    rt.copyValid = false;
}

// Modern water, reflections and ray-traced shadows over the closed world (wgpu_ray.cpp), written back into the target
// before the HUD. The D3D11 renderer runs the same scene through DXR (finalizeNeuralWorld in d3d11_world.cpp).
void WgpuBackend::Impl::traceWorld(RT& rt, bool jittered) {
    surfaceBuilder->drain(); // the reconstructed triangles and resident material projections are complete
    const bool current = rt.surface.epoch == motionEpoch;
    const bool wantWater =
        modernWater && current &&
        std::any_of(rt.surface.triangles.begin(), rt.surface.triangles.end(), [](const auto& t) { return t.water; });
    const bool wantShadows = rayShadows && nativeModels && !nativeModels->lighting.lights.empty();
    if (!wantWater && !wantShadows)
        return;
    DepthBuf* depth = getDepth(rt.taaDepth, rt.w, rt.h);
    const SceneGuideCamera& cam = rt.taaCamera;
    const uint32_t frameW = cam.frameWidth, frameH = cam.frameHeight;
    if (!depth || !depth->tex || !frameW || !frameH || frameW > rt.w || frameH > rt.h ||
        !(cam.flags & SceneGuideCamera::Valid)) {
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "[modern-water] bypass: %s (frame %ux%u, target %ux%u)\n",
                         !depth || !depth->tex ? "no world depth" : "no valid scene camera", frameW, frameH, rt.w,
                         rt.h);
            warned = true;
        }
        return;
    }
    // World -> clip over the native frame (as nr::guides prepares it), then the camera basis.
    guide::Mapping map;
    map.frameW = frameW;
    map.frameH = frameH;
    map.zmax = rt.taaZmax;
    map.farGs = 16.0f * cam.viewportOffset[2] / map.zmax;
    map.nearGs = 16.0f * (cam.viewportOffset[2] + cam.viewportScale[2]) / map.zmax;
    guide::Matrix worldToClip{};
    ray::PassFrame f;
    const char* why = "invalid camera";
    if (!(map.nearGs > map.farGs + 1e-8f) || !guide::cameraMatrix(map, viewOf(cam, rt.taaXY), worldToClip) ||
        !guide::frameCamera(worldToClip, cam.worldToView, f.camera, &why)) {
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "[modern-water] bypass: %s\n", why);
            warned = true;
        }
        return;
    }
    if (!rayPass) {
        rayPass = std::make_unique<ray::WgpuRay>(*gpu);
        std::fprintf(stderr, "[modern-water] %s\n", rayPass->status().c_str());
    }
    if (!rayPass->ready())
        return;
    rayPass->shareStaticWorld(residentScene.projectedShared(), residentScene.geometryVersion,
                              residentScene.attributeVersion());
    f.color = rt.tex.get();
    f.depth = depth->tex.get();
    f.width = frameW * scale;
    f.height = frameH * scale;
    f.zFar = map.farGs;
    f.zInvRange = 1.0f / (map.nearGs - map.farGs);
    if (jittered) {
        f.jitter[0] = rt.wjx;
        f.jitter[1] = rt.wjy;
    }
    f.seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - waterStartTime).count();
    if (runtimeFlags().waterTime && std::isfinite(*runtimeFlags().waterTime) &&
        std::abs(*runtimeFlags().waterTime) < 1000000)
        f.seconds = *runtimeFlags().waterTime;
    f.reflections = rayReflections;
    f.coverageDebug = runtimeFlags().waterCoverageDebug;
    // Compute tracing has no ray-tracing hardware: land shadows at the native resolution (GSR_RAY_SHADOW_SCALE
    // overrides the render pixels per shadow pixel) and fewer rays when the temporal AA integrates their noise.
    f.shadowScale = scale;
    if (const char* e = std::getenv("GSR_RAY_SHADOW_SCALE"))
        f.shadowScale = std::clamp(std::atoi(e), 1, 16);
    f.sunSamples = jittered ? 1 : 2;
    f.occlusionRays = jittered ? 2 : 4;
    f.reflectionRays = jittered ? 2 : 4;
    static const std::vector<surface::Triangle> noTriangles;
    const auto& scene = current ? rt.surface.triangles : noTriangles;
    if (!rayPass->render(f, scene, reflectionTextures, nativeModels.get(),
                         surface::RayOptions{wantWater, wantShadows, nullptr, true})) {
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "[modern-water] bypass: %s\n", rayPass->status().c_str());
            warned = true;
        }
        return;
    }
    gpu->copyTexture(*rayPass->output(), 0, 0, 0, *rt.tex, 0, 0, 0, f.width, f.height);
    if (wantWater)
        rt.waterOutputEpoch = motionEpoch;
    rt.rayOutputEpoch = motionEpoch;
    // The pass may have been created by the settings overlay's status query, so say once that it traces.
    static bool announced = false;
    if (!announced) {
        std::fprintf(stderr, "[modern-water] first ray-traced world: %s\n", rayPass->status().c_str());
        announced = true;
    }
    if (runtimeFlags().waterLog)
        std::fprintf(stderr, "[modern-water] epoch=%llu %s\n", (unsigned long long)motionEpoch,
                     rayPass->status().c_str());
    rt.dirty = true;
    ++rt.writes;
    rt.dy0 = 0;
    rt.dy1 = std::max(rt.dy1, std::min(rt.h, frameH));
    rt.copyValid = false;
}

void WgpuBackend::Impl::finalizeWorld(RT& rt) {
    if (rt.wstate == 1)
        rt.wstate = 2;
}
void WgpuBackend::Impl::finalizeAllWorlds() {}

void WgpuBackend::Impl::discardTemporalWorlds() {
    pendingTemporalEnd = false;
    for (auto& target : rts)
        if (target->neuralState == 1) {
            target->neuralState = 2;
            target->temporalSR = false;
        }
    taaOwner = nullptr;
    taaOwnerEpoch = 0;
}

void WgpuBackend::Impl::setUpscale(uint32_t mode, uint32_t preset, float sharp) {
    mode = mode <= 4 ? mode : 0;
    const bool changed = (mode != 0) != (upMode != 0);
    upPreset = preset;
    upSharp = std::isfinite(sharp) ? std::clamp(sharp, 0.0f, 1.0f) : 0.0f;
    if (changed) {
        discardTemporalWorlds();
        taaReset = true;
        taaPrevValid = false;
    }
    upMode = mode;
    if (mode && !progTaa) {
        rhi::ProgramDesc d;
        d.wgsl = kWgslTaa;
        d.uniformSlots = 1;
        d.textureSlots = 3;
        d.unfilterableMask = 0b011; // the target and depth are read with textureLoad; the history is filtered
        d.samplers = true;
        d.vertexInput = false;
        d.label = "temporal AA";
        std::string error;
        progTaa = gpu->createProgram(d, &error);
        if (!progTaa)
            std::fprintf(stderr, "[gsr-webgpu] temporal AA program: %s\n", error.c_str());
    }
    upStatusText = !mode     ? "Temporal AA off"
                   : progTaa ? "Temporal AA (portable): every mode renders at full internal resolution"
                             : "Temporal AA unavailable: its shader did not compile";
    if (!progTaa)
        upMode = 0;
    if (changed)
        std::fprintf(stderr, "[gsr-webgpu] %s\n", upStatusText.c_str());
}

} // namespace gsr
