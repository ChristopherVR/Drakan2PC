// The WebGPU ray pass (wgpu_ray.cpp, ray_trace.wgsl) on a small scene: a native water plane in front of a red wall,
// seen by a camera just above the water. The shader must compile, leave the sky above the horizon untouched and
// reflect the wall in the water. Exit 77 = no adapter (or no compute support).
#include "guide_camera.h"
#include "ray_bvh.h"
#include "wgpu_ray.h"

#include <gpu/Gpu.h>

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
    bvhChecks();
    drakan::gpu::ContextOptions options;
    options.allowSoftwareAdapter = true;
    std::string error;
    auto context = drakan::gpu::Context::create(options, &error);
    if (!context) {
        std::fprintf(stderr, "SKIP: %s\n", error.c_str());
        return g_failures ? 1 : 77;
    }
    auto device = rhi::Device::create(*context);
    ray::WgpuRay pass(*device);
    if (!pass.ready()) {
        std::fprintf(stderr, "SKIP: %s\n", pass.status().c_str());
        return g_failures ? 1 : 77;
    }
    constexpr uint32_t W = 64, H = 64;
    auto color = device->createTexture({W, H, 1, rhi::Format::RGBA8, rhi::Sampled | rhi::CopyDst, "color"});
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
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "ray trace: ok\n");
    return 0;
}
