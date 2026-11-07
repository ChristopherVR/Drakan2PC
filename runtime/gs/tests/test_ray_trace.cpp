// The WebGPU ray pass (wgpu_ray.cpp, ray_trace.wgsl) on a small scene: a native water plane in front of a red wall,
// seen by a camera just above the water. The shader must compile, leave the sky above the horizon untouched and
// reflect the wall in the water. Exit 77 = no hardware adapter (WARP takes minutes per ray dispatch). Shader or
// pipeline initialization failures on an available hardware adapter fail the test.
#include "guide_camera.h"
#include "ray_bvh.h"
#include "wgpu_ray.h"

#include <gpu/Gpu.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace gsr;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

surface::Triangle triangle(surface::Point a, surface::Point b, surface::Point c, float r, float g, float bl,
                           bool water) {
    surface::Triangle t;
    t.p[0] = a;
    t.p[1] = b;
    t.p[2] = c;
    t.color[0] = r;
    t.color[1] = g;
    t.color[2] = bl;
    t.water = water;
    t.opacity[0] = t.opacity[1] = t.opacity[2] = water ? 0.85f : 1.0f;
    return t;
}

void bvhChecks() {
    // Every item lands in exactly one leaf and every node bounds its subtree.
    std::vector<ray::Box> boxes;
    for (int i = 0; i < 1000; ++i) {
        const float x = float(i % 37) * 3.0f, y = float(i % 11) * 5.0f, z = float(i / 37);
        const float lo[3] = {x, y, z}, hi[3] = {x + 1.0f, y + 2.0f, z + 0.5f};
        ray::Box b;
        b.grow(lo);
        b.grow(hi);
        boxes.push_back(b);
    }
    const ray::Bvh bvh = ray::build(boxes, 4);
    std::vector<int> seen(boxes.size(), 0);
    bool bounded = true;
    for (const auto& n : bvh.nodes) {
        if (!n.count) {
            for (uint32_t c = n.first; c < n.first + 2; ++c)
                for (int a = 0; a < 3; ++a)
                    bounded &= bvh.nodes[c].min[a] >= n.min[a] && bvh.nodes[c].max[a] <= n.max[a];
            continue;
        }
        for (uint32_t i = n.first; i < n.first + n.count; ++i) {
            const auto& b = boxes[bvh.order[i]];
            ++seen[bvh.order[i]];
            for (int a = 0; a < 3; ++a)
                bounded &= b.min[a] >= n.min[a] && b.max[a] <= n.max[a];
        }
    }
    bool once = true;
    for (int s : seen)
        once &= s == 1;
    check(once, "every BVH item is in exactly one leaf");
    check(bounded, "BVH nodes bound their children and items");
}
} // namespace

int main() {
    check(surface::rayDiagnosticEffects("") == 0 && surface::rayDiagnosticEffects("off") == 0 &&
              surface::rayDiagnosticEffects("AO") == 0 && surface::rayDiagnosticEffects("direct,ao") == 0,
          "invalid/unset diagnostics preserve normal shader flags");
    check(surface::rayDiagnosticEffects("ao") == 64 && surface::rayDiagnosticEffects("direct") == 32 &&
              surface::rayDiagnosticEffects("receiver") == 128,
          "diagnostic masks match both shader Effects.w contracts");
    bvhChecks();
    drakan::gpu::ContextOptions options;
    options.allowSoftwareAdapter = true;
    std::string error;
    auto context = drakan::gpu::Context::create(options, &error);
    if (!context) {
        std::fprintf(stderr, "SKIP: %s\n", error.c_str());
        return g_failures ? 1 : 77;
    }
    if (context->softwareAdapter()) {
        std::fprintf(stderr, "SKIP: software adapter %s\n", context->adapterName().c_str());
        return g_failures ? 1 : 77;
    }
    auto device = rhi::Device::create(*context);
    ray::WgpuRay pass(*device);
    if (!pass.ready()) {
        std::fprintf(stderr, "FAIL: ray shaders must initialize on the available hardware adapter: %s\n",
                     pass.status().c_str());
        return 1;
    }
    constexpr uint32_t W = 64, H = 64;
    auto color =
        device->createTexture({W, H, 1, rhi::Format::RGBA8, rhi::Sampled | rhi::CopyDst | rhi::CopySrc, "color"});
    auto depth = device->createTexture({W, H, 1, rhi::Format::R32F, rhi::Sampled | rhi::CopyDst, "depth"});
    std::vector<uint32_t> gray(W * H, 0x80808080u);
    std::vector<float> sky(W * H, 0.0f);
    device->writeTexture(*color, 0, 0, 0, W, H, gray.data());
    device->writeTexture(*depth, 0, 0, 0, W, H, sky.data());

    // Camera at the origin looking down -z (90 degree field of view), reverse infinite Z with near plane 10.
    ray::PassFrame f;
    f.color = color.get();
    f.depth = depth.get();
    f.width = W;
    f.height = H;
    f.zFar = 0;
    f.zInvRange = 1;
    double p[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 10, 0}, ip[16];
    check(guide::inverse(p, ip), "projection inverts");
    guide::toFloats(p, f.camera.viewToClip);
    guide::toFloats(ip, f.camera.clipToView);
    f.camera.right[0] = 1;
    f.camera.up[1] = 1;
    f.camera.forward[2] = -1;
    f.camera.nearPlane = 10;
    f.seconds = 1;

    std::vector<surface::Triangle> scene;
    // Water at y = -100 from z = -10 to -5000; a red wall at z = -3000 rising from the water.
    scene.push_back(triangle({-5000, -100, -10}, {5000, -100, -10}, {5000, -100, -5000}, .2f, .3f, .5f, true));
    scene.push_back(triangle({-5000, -100, -10}, {5000, -100, -5000}, {-5000, -100, -5000}, .2f, .3f, .5f, true));
    scene.push_back(triangle({-2000, -100, -3000}, {2000, -100, -3000}, {2000, 3000, -3000}, 1, 0, 0, false));
    scene.push_back(triangle({-2000, -100, -3000}, {2000, 3000, -3000}, {-2000, 3000, -3000}, 1, 0, 0, false));
    surface::RayOptions ro;
    ro.water = true;
    f.shadowScale = 4; // the water mask at a quarter resolution
    check(pass.render(f, scene, {}, nullptr, ro), "the pass runs");
    std::fprintf(stderr, "%s\n", pass.status().c_str());
    std::vector<uint8_t> out;
    check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out) && out.size() == W * H * 4, "output readback");
    if (out.size() == W * H * 4) {
        auto px = [&](uint32_t x, uint32_t y) { return &out[(size_t(y) * W + x) * 4]; };
        const uint8_t* up = px(32, 10);
        check(up[0] == 128 && up[1] == 128 && up[2] == 128 && up[3] == 128, "the sky above the horizon is unchanged");
        // Ray at pixel row 34 meets the water about 1500 units out; its reflection rises into the wall.
        const uint8_t* water = px(32, 34);
        std::fprintf(stderr, "water pixel %u %u %u %u, sky pixel %u %u %u\n", water[0], water[1], water[2], water[3],
                     up[0], up[1], up[2]);
        check(water[0] > water[2] + 20 && water[0] > water[1] + 20, "the water reflects the red wall");
        check(water[3] == 128, "GS alpha is kept");
    }
    {
        const auto reflectionReference = out;
        auto reconstructed = scene;
        for (auto& tri : reconstructed)
            if (!tri.water)
                tri.shadowGeometry = false;
        check(pass.render(f, reconstructed, {}, nullptr, ro), "reconstructed reflector pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out), "reconstructed reflector readback");
        check(out == reflectionReference, "excluding an opaque batch from receiver queries preserves water reflection");
    }
    // Shadows: ground at y = -100 behind which a wall (x in [-300, 300], z = -1500) blocks a low sun from -z. The
    // ground's depth is in the depth texture; shadows are traced at half resolution and upsampled.
    {
        std::vector<float> ground(W * H, 0.0f);
        for (uint32_t y = 0; y < H; ++y) {
            const float ndcY = 1.0f - 2.0f * (float(y) + 0.5f) / float(H);
            if (ndcY < -1e-3f)
                for (uint32_t x = 0; x < W; ++x)
                    ground[y * W + x] = 10.0f / (100.0f / -ndcY); // near / view distance along -z
        }
        device->writeTexture(*depth, 0, 0, 0, W, H, ground.data());
        std::vector<surface::Triangle> world;
        world.push_back(triangle({-5000, -100, -10}, {5000, -100, -10}, {5000, -100, -5000}, .5f, .5f, .5f, false));
        world.push_back(triangle({-5000, -100, -10}, {5000, -100, -5000}, {-5000, -100, -5000}, .5f, .5f, .5f, false));
        world.push_back(triangle({-300, -100, -1500}, {300, -100, -1500}, {300, 400, -1500}, .5f, .5f, .5f, false));
        world.push_back(triangle({-300, -100, -1500}, {300, 400, -1500}, {-300, 400, -1500}, .5f, .5f, .5f, false));
        SceneModels lighting;
        lighting.lighting.ambient[0] = lighting.lighting.ambient[1] = lighting.lighting.ambient[2] = 0.3f;
        SceneLight sun;
        sun.vector[1] = 0.5f;
        sun.vector[2] = -1.0f;
        sun.color[0] = sun.color[1] = sun.color[2] = 1.0f;
        lighting.lighting.lights.push_back(sun);
        surface::RayOptions so;
        so.water = false;
        so.shadows = true;
        f.shadowScale = 2;
        check(pass.render(f, world, {}, &lighting, so), "the shadow pass runs");
        std::fprintf(stderr, "%s\n", pass.status().c_str());
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out) && out.size() == W * H * 4, "shadow readback");
        if (out.size() == W * H * 4) {
            const uint8_t* shade = &out[(size_t(34) * W + 32) * 4];
            const uint8_t* lit = &out[(size_t(34) * W + 56) * 4];
            std::fprintf(stderr, "shadowed ground %u, lit ground %u\n", shade[0], lit[0]);
            check(shade[0] + 30 < lit[0], "the wall shadows the ground behind it");
            check(lit[0] >= 120, "open ground stays lit");
        }
        // Native light colors multiply the display-referred GS image. At this receiver the blocker is beyond
        // AO reach: the known ambient/direct ratio must survive the linear-radiance composite unchanged.
        auto distant = world;
        for (size_t i = 2; i < distant.size(); ++i)
            for (auto& point : distant[i].p)
                point.z = -2000;
        f.shadowScale = 1;
        check(pass.render(f, distant, {}, &lighting, so), "display-lighting ratio pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out) && out.size() == W * H * 4,
              "display-lighting ratio readback");
        if (out.size() == W * H * 4) {
            const float expected = 128.0f * .3f / (.3f + .5f / std::sqrt(1.25f));
            check(std::abs(float(out[(34 * W + 32) * 4]) - expected) < 5,
                  "native display-lighting ratio is not gamma weakened");
        }
        // Sun occluders fade out by 16 m (8000 units) along the shadow ray: a huge cliff about 11000 units away does
        // not put the open ground into shadow (GitHub issue 1), matching the scene without it.
        {
            auto open = world, cliff = world;
            open.resize(2);
            for (size_t i = 2; i < cliff.size(); ++i)
                for (auto& point : cliff[i].p) {
                    point.x = point.x < 0 ? -50000.f : 50000.f;
                    point.y = point.y > 0 ? 50000.f : -100.f;
                    point.z = -12000;
                }
            std::vector<uint8_t> reference;
            check(pass.render(f, open, {}, &lighting, so) &&
                      device->readTexture(*pass.output(), 0, 0, 0, W, H, reference),
                  "open-ground sun pass runs");
            check(pass.render(f, cliff, {}, &lighting, so) && device->readTexture(*pass.output(), 0, 0, 0, W, H, out),
                  "far-cliff sun pass runs");
            check(out.size() == reference.size() && out[(34 * W + 32) * 4] == reference[(34 * W + 32) * 4] &&
                      reference[(34 * W + 32) * 4] >= 120,
                  "a sun occluder beyond the shadow reach leaves open ground lit");
        }
        // A light facing away contributes no direct illumination. Removing it must keep exactly the same AO
        // in the ambient-only scene, rather than disabling the dispatch or falling back to unity lighting.
        SceneModels ambient;
        ambient.lighting.ambient[0] = ambient.lighting.ambient[1] = ambient.lighting.ambient[2] = .3f;
        SceneLight backward = sun;
        backward.vector[1] = -1;
        backward.vector[2] = 0;
        ambient.lighting.lights.push_back(backward);
        check(pass.render(f, world, {}, &ambient, so), "ambient and backward-light pass runs");
        std::vector<uint8_t> ambientReference;
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, ambientReference), "ambient reference readback");
        ambient.lighting.lights.clear();
        check(pass.render(f, world, {}, &ambient, so), "ambient-only pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out) && out.size() == W * H * 4,
              "ambient-only readback");
        check(out == ambientReference, "ambient-only AO matches a scene with an ineffective direct light");
        bool hasOcclusion = false;
        for (size_t i = size_t(34) * W * 4; i < out.size(); i += 4)
            hasOcclusion |= out[i] < 123;
        check(hasOcclusion, "ambient-only lighting retains contact occlusion");
        auto noAmbient = ambient;
        std::fill(std::begin(noAmbient.lighting.ambient), std::end(noAmbient.lighting.ambient), 0.f);
        check(pass.render(f, world, {}, &noAmbient, so), "zero-ambient pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out), "zero-ambient readback");
        const unsigned shadeIndex = (34 * W + 32) * 4, litIndex = (34 * W + 56) * 4;
        check(out[shadeIndex] == out[litIndex] && out[shadeIndex] >= 120, "zero ambient produces no contact darkening");
        const auto zeroReference = out;
        noAmbient.lighting.ambient[0] = .3f;
        check(pass.render(f, world, {}, &noAmbient, so), "single-channel ambient pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out), "single-channel ambient readback");
        bool redOcclusion = false, zeroChannelsPreserved = true;
        for (size_t i = 0; i < out.size(); i += 4) {
            redOcclusion |= out[i] < zeroReference[i];
            zeroChannelsPreserved &= out[i + 1] == zeroReference[i + 1] && out[i + 2] == zeroReference[i + 2];
        }
        check(redOcclusion && zeroChannelsPreserved,
              "one positive ambient channel retains occlusion without darkening zero channels");
        f.shadowScale = 2;
        // The same scene as the resident (static) world: its own BVHs, rebased and translated per frame.
        pass.shareStaticWorld(std::make_shared<const std::vector<surface::Triangle>>(world), 1, 1);
        check(pass.render(f, {}, {}, &lighting, so), "the static-world pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out) && out.size() == W * H * 4, "static readback");
        if (out.size() == W * H * 4) {
            const uint8_t* shade = &out[(size_t(34) * W + 32) * 4];
            const uint8_t* lit = &out[(size_t(34) * W + 56) * 4];
            std::fprintf(stderr, "static world: shadowed ground %u, lit ground %u\n", shade[0], lit[0]);
            check(shade[0] + 30 < lit[0] && lit[0] >= 120, "the static world shadows the same way");
        }
        // A material change only (same geometry version): the wall's alpha test never passes, so it casts nothing.
        auto changed = world;
        changed[2].alphaTest = changed[3].alphaTest = 0;
        pass.shareStaticWorld(std::make_shared<const std::vector<surface::Triangle>>(changed), 1, 2);
        check(pass.render(f, {}, {}, &lighting, so), "the repacked pass runs");
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, out) && out.size() == W * H * 4, "repack readback");
        if (out.size() == W * H * 4) {
            const uint8_t* shade = &out[(size_t(34) * W + 32) * 4];
            const uint8_t* lit = &out[(size_t(34) * W + 56) * 4];
            std::fprintf(stderr, "repacked materials: formerly shadowed ground %u, lit ground %u\n", shade[0], lit[0]);
            check(shade[0] + 8 >= lit[0], "a material-only change reaches the GPU");
        }
    }
    // A native-projected model still belongs to the scene, but does not cast a second direct silhouette.
    {
        SceneModels models;
        models.lighting.ambient[0] = models.lighting.ambient[1] = models.lighting.ambient[2] = .3f;
        SceneLight sun;
        sun.vector[1] = .5f;
        sun.vector[2] = -1;
        sun.color[0] = sun.color[1] = sun.color[2] = 1;
        models.lighting.lights.push_back(sun);
        auto mesh = std::make_shared<SceneModelMesh>();
        mesh->identity = 12345;
        const float corners[4][3]{{-300, -100, -1500}, {300, -100, -1500}, {300, 400, -1500}, {-300, 400, -1500}};
        for (const auto& indices : {std::array<unsigned, 3>{0, 1, 2}, std::array<unsigned, 3>{0, 2, 3}}) {
            SceneModelTriangle tri;
            for (unsigned j = 0; j < 3; ++j)
                std::copy(corners[indices[j]], corners[indices[j]] + 3, tri.position[j]);
            mesh->triangles.push_back(tri);
        }
        models.meshes.push_back(mesh);
        SceneModelInstance instance;
        instance.transform[0] = instance.transform[5] = instance.transform[10] = 1;
        models.instances.push_back(instance);
        std::vector<surface::Triangle> ground;
        ground.push_back(triangle({-5000, -100, -10}, {5000, -100, -10}, {5000, -100, -5000}, .5f, .5f, .5f, false));
        ground.push_back(triangle({-5000, -100, -10}, {5000, -100, -5000}, {-5000, -100, -5000}, .5f, .5f, .5f, false));
        pass.shareStaticWorld(std::make_shared<const std::vector<surface::Triangle>>(ground), 2, 3);
        surface::RayOptions shadows;
        shadows.water = false;
        shadows.shadows = true;
        auto sample = [&]() {
            std::vector<uint8_t> pixels;
            check(pass.render(f, {}, {}, &models, shadows), "native model shadow pass runs");
            check(device->readTexture(*pass.output(), 0, 0, 0, W, H, pixels) && pixels.size() == W * H * 4,
                  "native model shadow readback");
            return pixels.empty() ? 0u : unsigned(pixels[(36 * W + 32) * 4]);
        };
        const auto cast = sample();
        models.instances[0].nativeShadow = true;
        const auto owned = sample();
        check(owned >= 120 && cast + 30 < owned, "native ownership removes the duplicate direct silhouette");
        models.instances[0].nativeShadow = false;
        check(sample() == cast, "unprojected model regains its ray shadow in the next submission");
        models.meshes[0] = std::make_shared<SceneModelMesh>(*mesh);
        check(sample() == cast, "identical geometry in a new snapshot preserves the shadow");
        auto textured = std::make_shared<SceneModelMesh>(*mesh);
        for (auto& tri : textured->triangles)
            tri.material = 0;
        models.meshes[0] = textured;
        auto cutout = std::make_shared<SceneModelTexture>();
        cutout->pixels.assign(128 * 128, 0x00ffffff);
        models.textures.push_back(cutout);
        const auto opaque = sample();
        cutout->flags = 8;
        check(sample() > opaque + 30, "material alpha flags invalidate cached model attributes");
        cutout->flags = 0;
        check(sample() == opaque, "restoring opaque flags restores the cached model shadow");
        auto covered = std::make_shared<SceneModelTexture>(*cutout);
        covered->flags = 8;
        covered->pixels.assign(128 * 128, 0x01ffffff);
        models.textures[0] = covered;
        check(sample() == opaque, "positive alpha coverage preserves a cutout material's opaque shadow");
        cutout->flags = 8;
        models.textures[0] = cutout;
        check(sample() > opaque + 30, "texture replacement revokes coverage with unchanged cutout flags");
    }
    // Opaque transport uses offscreen geometry even with water and direct shadows disabled. A red ceiling
    // has no raster pixels here: the supplied depth describes only the neutral floor.
    {
        pass.shareStaticWorld({}, 3, 4);
        std::vector<surface::Triangle> world;
        world.push_back(
            triangle({-20000, -100, 20000}, {20000, -100, 20000}, {20000, -100, -20000}, .5f, .5f, .5f, false));
        world.push_back(
            triangle({-20000, -100, 20000}, {20000, -100, -20000}, {-20000, -100, -20000}, .5f, .5f, .5f, false));
        auto ceiling = [&](float y, float r) {
            world.push_back(triangle({-20000, y, 20000}, {20000, y, -20000}, {20000, y, 20000}, r, 0, 0, false));
            world.push_back(triangle({-20000, y, 20000}, {-20000, y, -20000}, {20000, y, -20000}, r, 0, 0, false));
        };
        ceiling(500, 1);
        surface::RayOptions transport;
        transport.water = false;
        auto sample = [&]() {
            std::vector<uint8_t> pixels;
            const bool enabled = transport.globalIllumination || transport.materialReflections;
            const bool rendered = pass.render(f, world, {}, nullptr, transport);
            check(rendered == enabled,
                  enabled ? "opaque transport pass runs" : "disabled transport bypasses submission");
            // The caller keeps the source on a bypass; output() may still contain a previous enabled pass.
            auto* displayed = rendered ? pass.output() : color.get();
            check(device->readTexture(*displayed, 0, 0, 0, W, H, pixels) && pixels.size() == W * H * 4,
                  "opaque transport readback");
            return pixels;
        };
        auto channel = [&](const std::vector<uint8_t>& pixels, unsigned c) {
            float sum = 0;
            if (pixels.size() != W * H * 4)
                return sum;
            for (unsigned y = 40; y < 56; ++y)
                for (unsigned x = 20; x < 44; ++x)
                    sum += pixels[(y * W + x) * 4 + c];
            return sum / (16 * 24);
        };
        const auto off = sample();
        check(off == std::vector<uint8_t>(W * H * 4, 128), "disabled opaque transport preserves the complete image");
        transport.globalIllumination = true;
        const auto bounced = sample();
        check(channel(bounced, 0) > channel(off, 0) + 12 && channel(bounced, 0) > channel(bounced, 1) + 12,
              "one diffuse bounce carries red from geometry absent from raster pixels");
        // Put the same secondary ceiling into the raster, then give its raster pixels an unrelated green color.
        // A diffuse bounce must still use the red surface material/native lighting, not camera-dependent pixels.
        std::vector<float> visibleCeilingDepth(W * H, 0.0f);
        std::vector<uint32_t> visibleCeilingColor = gray;
        for (uint32_t y = 0; y < H; ++y) {
            const float ndcY = 1.0f - 2.0f * (float(y) + .5f) / float(H);
            for (uint32_t x = 0; x < W; ++x) {
                visibleCeilingDepth[y * W + x] = ndcY > 0 ? 10.0f / (500.0f / ndcY) : 10.0f / (100.0f / -ndcY);
                if (ndcY > 0)
                    visibleCeilingColor[y * W + x] = 0x8000ff00u;
            }
        }
        device->writeTexture(*color, 0, 0, 0, W, H, visibleCeilingColor.data());
        device->writeTexture(*depth, 0, 0, 0, W, H, visibleCeilingDepth.data());
        const auto visibleBounce = sample();
        bool viewIndependent = visibleBounce.size() == bounced.size() && bounced.size() == W * H * 4;
        if (viewIndependent)
            for (uint32_t y = 40; y < 56; ++y)
                for (uint32_t x = 20; x < 44; ++x)
                    for (uint32_t c = 0; c < 3; ++c) {
                        const auto i = (y * W + x) * 4 + c;
                        viewIndependent &= std::abs(int(visibleBounce[i]) - int(bounced[i])) <= 1;
                    }
        check(viewIndependent, "secondary diffuse radiance is independent of unrelated visible raster colors");
        device->writeTexture(*color, 0, 0, 0, W, H, gray.data());
        for (uint32_t y = 0; y < H / 2; ++y)
            std::fill_n(visibleCeilingDepth.begin() + y * W, W, 0.0f);
        device->writeTexture(*depth, 0, 0, 0, W, H, visibleCeilingDepth.data());
        ceiling(100, 0);
        const auto blocked = sample();
        check(channel(blocked, 0) + 12 < channel(bounced, 0), "a black intervening surface blocks the colored bounce");
        world.resize(4);
        transport.globalIllumination = false;
        transport.materialReflections = true;
        world[0].roughness = world[1].roughness = .04f;
        world[0].reflectance = world[1].reflectance = .8f;
        const auto reflected = sample();
        check(channel(reflected, 0) > channel(reflected, 1) + 80,
              "an opaque smooth material reflects offscreen red geometry");
        world[0].reflectance = world[1].reflectance = .04f;
        const auto dielectric = sample();
        check(channel(reflected, 0) > channel(dielectric, 0) + 30,
              "normal-incidence reflectance controls opaque reflection energy");
        // Identical-colored coplanar receivers with different F0 still need a sharp material boundary. Source
        // color, plane and normal guides alone cannot distinguish these two materials during reconstruction.
        const auto unsplit = world;
        world.clear();
        auto floorHalf = [&](float left, float right, float f0) {
            auto a = triangle({left, -100, 20000}, {right, -100, 20000}, {right, -100, -20000}, .5f, .5f, .5f, false);
            auto b = triangle({left, -100, 20000}, {right, -100, -20000}, {left, -100, -20000}, .5f, .5f, .5f, false);
            a.roughness = b.roughness = .04f;
            a.reflectance = b.reflectance = f0;
            world.push_back(a);
            world.push_back(b);
        };
        floorHalf(-20000, 0, .8f);
        floorHalf(0, 20000, .04f);
        world.insert(world.end(), unsplit.begin() + 2, unsplit.end());
        const auto split = sample();
        bool materialBoundary =
            split.size() == W * H * 4 && reflected.size() == split.size() && dielectric.size() == split.size();
        if (materialBoundary)
            for (uint32_t c = 0; c < 3; ++c) {
                const auto left = (48 * W + 31) * 4 + c;
                const auto right = (48 * W + 32) * 4 + c;
                materialBoundary &= std::abs(int(split[left]) - int(reflected[left])) <= 3;
                materialBoundary &= std::abs(int(split[right]) - int(dielectric[right])) <= 3;
            }
        check(materialBoundary, "transport filtering preserves equal-color material reflectance boundaries");
        world = unsplit;
        world[0].roughness = world[1].roughness = 1;
        const auto rough = sample();
        check(channel(rough, 0) + 5 < channel(dielectric, 0), "roughness broadens and masks the dielectric reflection");
        world[0].alphaTest = world[1].alphaTest = 6;
        const auto foliage = sample();
        check(foliage == off, "alpha-tested foliage does not receive an invented polished coating");
        transport.materialReflections = false;
        check(sample() == off, "turning transport off restores the original image");
        // The right half has a nearer raster silhouette with no matching ray geometry. The ground below is
        // within the old vertical shadow fallback's reach, but must never acquire transport for that silhouette.
        std::vector<float> silhouetteDepth(W * H, 0.0f);
        for (uint32_t y = H / 2; y < H; ++y) {
            const float ndcY = 1.0f - 2.0f * (float(y) + .5f) / float(H);
            for (uint32_t x = 0; x < W; ++x)
                silhouetteDepth[y * W + x] = 10.0f / (100.0f / -ndcY) * (x >= W / 2 ? 2.0f : 1.0f);
        }
        device->writeTexture(*depth, 0, 0, 0, W, H, silhouetteDepth.data());
        transport.globalIllumination = true;
        const auto edge = sample();
        bool intact = edge.size() == W * H * 4;
        if (intact)
            for (uint32_t y = 40; y < 56; ++y)
                for (uint32_t x = W / 2; x < W / 2 + 4; ++x)
                    for (uint32_t c = 0; c < 4; ++c)
                        intact &= edge[(y * W + x) * 4 + c] == 128;
        check(intact, "half-resolution transport does not bleed onto unmatched foreground silhouettes");
    }
    // In a constant-radiance enclosure, reflection replaces exactly the diffuse energy it removes. A smooth
    // view-Fresnel diffuse attenuation paired with rough GGX reflection would darken this scene at grazing angles.
    {
        pass.shareStaticWorld({}, 4, 5);
        constexpr float grayDisplay = 128.0f / 255.0f;
        std::vector<surface::Triangle> world;
        auto quad = [&](surface::Point a, surface::Point b, surface::Point c, surface::Point d) {
            world.push_back(triangle(a, b, c, grayDisplay, grayDisplay, grayDisplay, false));
            world.push_back(triangle(a, c, d, grayDisplay, grayDisplay, grayDisplay, false));
        };
        constexpr float extent = 1000000;
        quad({-extent, -100, extent}, {extent, -100, extent}, {extent, -100, -extent}, {-extent, -100, -extent});
        quad({-extent, extent, extent}, {extent, extent, extent}, {extent, extent, -extent},
             {-extent, extent, -extent});
        // Every accepted reflected direction meets the same gray radiance, including directions outside the view.
        quad({-extent, -100, -extent}, {extent, -100, -extent}, {extent, extent, -extent}, {-extent, extent, -extent});
        quad({-extent, -100, extent}, {-extent, extent, extent}, {extent, extent, extent}, {extent, -100, extent});
        quad({-extent, -100, -extent}, {-extent, extent, -extent}, {-extent, extent, extent}, {-extent, -100, extent});
        quad({extent, -100, -extent}, {extent, -100, extent}, {extent, extent, extent}, {extent, extent, -extent});
        std::vector<float> groundDepth(W * H, 0.0f);
        for (uint32_t y = H / 2; y < H; ++y) {
            const float ndcY = 1.0f - 2.0f * (float(y) + .5f) / float(H);
            for (uint32_t x = 0; x < W; ++x)
                groundDepth[y * W + x] = 10.0f / (100.0f / -ndcY);
        }
        device->writeTexture(*depth, 0, 0, 0, W, H, groundDepth.data());
        surface::RayOptions reflection;
        reflection.water = false;
        reflection.materialReflections = true;
        for (float roughness : {.04f, .3f, .75f, 1.0f}) {
            world[0].roughness = world[1].roughness = roughness;
            world[0].reflectance = world[1].reflectance = .8f;
            check(pass.render(f, world, {}, nullptr, reflection), "constant-radiance rough reflection pass runs");
            std::vector<uint8_t> pixels;
            check(device->readTexture(*pass.output(), 0, 0, 0, W, H, pixels) && pixels.size() == W * H * 4,
                  "constant-radiance reflection readback");
            unsigned error = 0;
            if (pixels.size() == W * H * 4)
                for (uint32_t y = H / 2; y < H; ++y)
                    for (uint32_t x = 0; x < W; ++x)
                        for (uint32_t c = 0; c < 3; ++c)
                            error = std::max(error, unsigned(std::abs(int(pixels[(y * W + x) * 4 + c]) - 128)));
            std::fprintf(stderr, "constant radiance roughness %.2f: max display error %u\n", roughness, error);
            check(error <= 3, "rough GGX reflection preserves constant radiance even on grazing receivers");
        }
        // A filtered reflection must attenuate each pixel's own diffuse color, not the diffuse color of the
        // half-resolution sample. Every half sample lands on a red stripe; adjacent blue stripes remain local.
        world[0].roughness = world[1].roughness = .75f;
        std::vector<uint32_t> black(W * H, 0x80000000u);
        device->writeTexture(*color, 0, 0, 0, W, H, black.data());
        check(pass.render(f, world, {}, nullptr, reflection), "constant incident radiance on black receiver runs");
        std::vector<uint8_t> incident;
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, incident), "incident reference readback");
        std::vector<uint32_t> stripes(W * H);
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x)
                stripes[y * W + x] = x % 2 ? 0x80204080u : 0x80804020u;
        device->writeTexture(*color, 0, 0, 0, W, H, stripes.data());
        check(pass.render(f, world, {}, nullptr, reflection),
              "textured receiver under constant incident radiance runs");
        std::vector<uint8_t> textured;
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, textured), "textured reflection readback");
        unsigned localColorError = 0;
        const bool texturedReadback = incident.size() == W * H * 4 && textured.size() == W * H * 4;
        if (texturedReadback)
            for (uint32_t y = 40; y < 56; ++y)
                for (uint32_t x = 20; x < 44; ++x)
                    for (uint32_t c = 0; c < 3; ++c) {
                        const size_t i = (y * W + x) * 4 + c;
                        const float incoming = std::pow(float(incident[i]) / 255.0f, 2.2f);
                        const float energy = std::clamp(incoming / std::pow(grayDisplay, 2.2f), 0.0f, 1.0f);
                        const unsigned original = (stripes[y * W + x] >> (c * 8)) & 255u;
                        const float local = std::pow(float(original) / 255.0f, 2.2f);
                        const float expected = 255 * std::pow(local * (1 - energy) + incoming, 1.0f / 2.2f);
                        localColorError = std::max(localColorError,
                                                   unsigned(std::abs(int(textured[i]) - int(std::lround(expected)))));
                    }
        std::fprintf(stderr, "textured constant incident radiance: max display error %u\n", localColorError);
        check(texturedReadback && localColorError <= 3,
              "filtered reflection attenuates local raster color without transferring neighboring color subtraction");
        device->writeTexture(*color, 0, 0, 0, W, H, gray.data());
        // Reallocate both half-resolution transport images for odd dimensions, then restore the full-size frame.
        f.width = W - 1;
        f.height = H - 3;
        check(pass.render(f, world, {}, nullptr, reflection), "odd-size transport pass runs");
        check(pass.output()->width() == f.width && pass.output()->height() == f.height,
              "odd-size transport output has the requested dimensions");
        std::vector<uint8_t> odd;
        check(device->readTexture(*pass.output(), 0, 0, 0, f.width, f.height, odd) &&
                  odd.size() == size_t(f.width) * f.height * 4,
              "odd-size transport output readback");
        bool alphaPreserved = true;
        for (size_t i = 3; i < odd.size(); i += 4)
            alphaPreserved &= odd[i] == 128;
        check(alphaPreserved, "odd-size half-resolution transport preserves raster alpha");
        f.width = W;
        f.height = H;
        check(pass.render(f, world, {}, nullptr, reflection),
              "full-size transport resumes after odd-size reallocation");
        check(pass.output()->width() == W && pass.output()->height() == H,
              "transport output returns to the full-size dimensions");
        std::vector<uint8_t> restored;
        check(device->readTexture(*pass.output(), 0, 0, 0, W, H, restored) && restored.size() == W * H * 4,
              "restored full-size transport readback");
    }
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "ray trace: ok\n");
    return 0;
}
