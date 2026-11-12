// Terrain detail policy and maths (terrain_detail.h): which draws are terrain layers, the world position of a pixel
// from its position and Q, the side projection's density and weights, and the detail volume.
#include "terrain_detail.h"

#include <cstdio>

static int g_fail = 0;
static void expect(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        ++g_fail;
}

using namespace gsr;
using namespace gsr::terrain;

namespace {
// A camera at `eye` looking along +z of a view rotated by yaw about y, with a perspective projection in the VU
// convention the host publishes (row vectors, clip = world * worldToClip, screen = offset + scale * clip / w).
SceneGuideCamera makeCamera(const double eye[3], double yaw) {
    SceneGuideCamera c;
    c.flags = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
    c.frameWidth = 640;
    c.frameHeight = 512;
    const double cs = std::cos(yaw), sn = std::sin(yaw);
    // view = (world - eye) * R, rows of R are the world axes expressed in view space
    const double r[3][3] = {{cs, 0, sn}, {0, -1, 0}, {-sn, 0, cs}};
    double view[16]{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            view[i * 4 + j] = r[i][j];
    for (int j = 0; j < 3; ++j)
        view[12 + j] = -(eye[0] * r[0][j] + eye[1] * r[1][j] + eye[2] * r[2][j]);
    view[15] = 1;
    const double nearPlane = 32, farPlane = 60000;
    double proj[16]{};
    proj[0] = 1.3;
    proj[5] = 1.7;
    proj[10] = farPlane / (farPlane - nearPlane);
    proj[11] = 1;
    proj[14] = -nearPlane * farPlane / (farPlane - nearPlane);
    for (int i = 0; i < 16; ++i) {
        c.worldToView[i] = float(view[i]);
        c.viewToClipVU[i] = float(proj[i]);
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double v = 0;
            for (int n = 0; n < 4; ++n)
                v += view[i * 4 + n] * proj[n * 4 + j];
            c.worldToClipVU[i * 4 + j] = float(v);
        }
    for (int i = 0; i < 3; ++i)
        c.cameraPosition[i] = float(eye[i]);
    c.viewportScale[0] = 320;
    c.viewportScale[1] = -256;
    c.viewportScale[2] = 1000;
    c.viewportOffset[0] = 2048;
    c.viewportOffset[1] = 2048;
    c.viewportOffset[2] = 0;
    return c;
}
// Project a world point as the VU does: native pixel position (XYOFFSET subtracted) and Q = 1 / w.
bool project(const SceneGuideCamera& c, uint64_t xyoffset, const double world[3], float& x, float& y, float& q) {
    double clip[4]{};
    for (int j = 0; j < 4; ++j)
        clip[j] = world[0] * c.worldToClipVU[j] + world[1] * c.worldToClipVU[4 + j] +
                  world[2] * c.worldToClipVU[8 + j] + c.worldToClipVU[12 + j];
    if (clip[3] <= 1)
        return false;
    const double xyX = double(xyoffset & 65535) / 16, xyY = double((xyoffset >> 32) & 65535) / 16;
    x = float(c.viewportOffset[0] + c.viewportScale[0] * clip[0] / clip[3] - xyX);
    y = float(c.viewportOffset[1] + c.viewportScale[1] * clip[1] / clip[3] - xyY);
    q = float(1.0 / clip[3]);
    return true;
}
} // namespace

int main() {
    std::printf("classification\n");
    DrawInfo layer;
    layer.triangles = layer.textured = layer.blend = true;
    layer.test = 0x5000d; // ATE, GREATER, AREF 0, ZTE, GEQUAL
    layer.alpha = 0x44;
    expect(classify(layer), "a splat layer as the game draws it");
    {
        DrawInfo d = layer;
        d.test = 0x5064d;
        expect(!classify(d), "an object with an alpha reference is not terrain");
        d = layer;
        d.test = 0x5000c;
        expect(!classify(d), "no alpha test: not terrain");
        d = layer;
        d.zbuf = 1ull << 32;
        expect(!classify(d), "depth writes masked (decal, water): not terrain");
        d = layer;
        d.clamp = 5;
        expect(!classify(d), "clamped texture: not terrain");
        d = layer;
        d.blend = false;
        expect(!classify(d), "opaque draw: not terrain");
        d = layer;
        d.textureIsTarget = true;
        expect(!classify(d), "sampling a render target: not terrain");
        d = layer;
        d.fst = true;
        expect(!classify(d), "screen-space texture coordinates: not terrain");
        d = layer;
        d.alpha = 0x48;
        expect(!classify(d), "additive blend: not terrain");
        d = layer;
        d.tex0 = 1ull << 35;
        expect(!classify(d), "DECAL texture function: not terrain");
        d = layer;
        d.tex0 = 1ull << 34;
        expect(classify(d), "texture alpha in the layer (TCC) stays terrain");
        d = layer;
        d.test = 0x3000d;
        expect(!classify(d), "depth test ALWAYS: not terrain");
    }

    std::printf("world position from the pixel and Q\n");
    const double eye[3] = {412345.0, 201234.0, 367890.0};
    const uint64_t xyoffset = (uint64_t(1728 * 16)) | (uint64_t(1792 * 16) << 32);
    for (const float renderScale : {1.0f, 3.0f, 2.0f}) {
        const float jx = renderScale == 2.0f ? 0.37f : 0.0f, jy = renderScale == 2.0f ? -0.21f : 0.0f;
        const SceneGuideCamera camera = makeCamera(eye, 0.7);
        Unproject u;
        expect(buildUnproject(camera, xyoffset, renderScale, renderScale * 1.5f, jx, jy, u), "camera accepted");
        double worst = 0;
        for (int i = 0; i < 200; ++i) {
            // points in front of the camera, 300 .. 40000 units away
            const double depth = 300.0 + 200.0 * i, side = (i % 7 - 3) * 0.12 * depth, up = (i % 5 - 2) * 0.1 * depth;
            const double cs = std::cos(0.7), sn = std::sin(0.7);
            const double world[3] = {eye[0] + sn * depth + cs * side, eye[1] + up, eye[2] + cs * depth - sn * side};
            float x, y, q;
            if (!project(camera, xyoffset, world, x, y, q))
                continue;
            float rel[3];
            relativeAt(u, x * renderScale + jx, y * renderScale * 1.5f + jy, q, rel);
            // The camera matrices are single precision around coordinates of 4e5: about 0.3 units of error whatever
            // the distance (a tenth of a texel of the finest detail octave), plus the part that grows with distance.
            for (int k = 0; k < 3; ++k)
                worst = std::max(worst, std::abs(rel[k] + u.anchor[k] - world[k]) / (1.0 + 2e-4 * depth));
        }
        std::printf("    render scale %.0f: worst error %.2f of (1 unit + 0.02 %% of the distance)\n", renderScale,
                    worst);
        expect(worst < 1.0, "positions come back within 1 unit + 0.02 % of their distance");
    }
    {
        SceneGuideCamera camera = makeCamera(eye, 0.2);
        Unproject u;
        camera.flags &= ~uint32_t(SceneGuideCamera::World);
        expect(!buildUnproject(camera, xyoffset, 1, 1, 0, 0, u), "no world camera: refused");
        camera = makeCamera(eye, 0.2);
        camera.viewToClipVU[0 * 4 + 3] = 0.5f; // w depends on view x: not the projection the formula assumes
        expect(!buildUnproject(camera, xyoffset, 1, 1, 0, 0, u), "sheared projection: refused");
        camera = makeCamera(eye, 0.2);
        camera.viewportScale[0] = 0;
        expect(!buildUnproject(camera, xyoffset, 1, 1, 0, 0, u), "empty viewport: refused");
    }

    std::printf("side projection\n");
    {
        // one 128 texel tile per 2048 unit cell, rotated by 90 degrees and mirrored: still one scale
        const float p[3][3] = {{0, 10, 0}, {2048, 300, 0}, {0, -50, 2048}};
        const float t[3][2] = {{0, 0}, {0, 128}, {-128, 0}};
        const float density = texelDensity(p, t);
        expect(std::abs(density - 128.0f / 2048.0f) < 1e-6f, "density of a rotated cell mapping");
        expect(std::abs(sideDensity(density, 128, 128) - 0.0625f) < 1e-7f, "one tile per cell qualifies");
        expect(std::abs(sideDensity(density * 1.01f, 128, 128) - 0.0625f) < 1e-7f, "and snaps to the exact period");
        expect(sideDensity(density * 1.3f, 128, 128) == 0, "a period that is no power of two of the cell does not");
        expect(sideDensity(256.0f / 131072.0f, 256, 256) == 0, "the far colour map (64 cells per tile) does not");
        expect(sideDensity(density, 128, 64) == 0, "a texture that is not square does not");
        const float sheared[3][2] = {{0, 0}, {128, 64}, {0, 128}};
        expect(texelDensity(p, sheared) == 0, "a sheared mapping has no single density");
        const float flat[3][3] = {{0, 0, 0}, {2048, 0, 0}, {4096, 100, 0}};
        expect(texelDensity(flat, t) == 0, "a triangle with no area from above has none");
    }
    {
        const float up[3] = {0, 1, 0}, slope[3] = {0.5f, 0.866f, 0}, wallX[3] = {1, 0, 0}, wallZ[3] = {0, 0.1f, 0.995f};
        expect(cliffWeights(up, 0).side == 0, "level ground is mapped from above only");
        expect(cliffWeights(slope, 0).side == 0, "a 30 degree slope too");
        expect(cliffWeights(slope, 0.5f).side == 0, "at any dither value");
        expect(cliffWeights(wallX, 0).side == 1 && cliffWeights(wallX, 0).planeZ == 0,
               "a wall facing x: side, plane x");
        expect(cliffWeights(wallZ, 0).side == 1 && cliffWeights(wallZ, 0).planeZ == 1,
               "a wall facing z: side, plane z");
        const float steep[3] = {0.866f, 0.5f, 0};
        const float w = cliffWeights(steep, 0).side;
        expect(w > 0.3f && w < 0.6f, "a 60 degree slope is in between");
        const float ceiling[3] = {0, -1, 0};
        expect(cliffWeights(ceiling, 0).side == 0, "a ceiling counts as level");
    }

    std::printf("smooth normals of the height field\n");
    {
        // A 6 x 6 cell height field at grid origin (10, -4): level for x < 3 cells, a 45 degree ramp beyond, and a
        // second flat layer 6000 units above it (a ceiling: wound the other way).
        struct Tri {
            float p[3][3];
        };
        std::vector<Tri> tris;
        auto height = [](int x) { return x <= 3 ? 1000.0f : 1000.0f + (x - 3) * 2048.0f; };
        auto add = [&](float x0, float z0, bool ceiling) {
            auto v = [&](Tri& t, int k, int x, int z) {
                t.p[k][0] = x * 2048.0f;
                t.p[k][2] = z * 2048.0f;
                t.p[k][1] = ceiling ? 20000.0f : height(x - 10);
            };
            const int x = int(x0), z = int(z0);
            Tri a, b;
            v(a, 0, x, z), v(a, ceiling ? 2 : 1, x, z + 1), v(a, ceiling ? 1 : 2, x + 1, z);
            v(b, 0, x + 1, z), v(b, ceiling ? 2 : 1, x, z + 1), v(b, ceiling ? 1 : 2, x + 1, z + 1);
            tris.push_back(a);
            tris.push_back(b);
        };
        for (int z = 0; z < 6; ++z)
            for (int x = 0; x < 6; ++x) {
                add(float(10 + x), float(-4 + z), false);
                add(float(10 + x), float(-4 + z), true);
            }
        const NormalGrid grid = buildNormalGrid(tris.size(), [&](size_t i, int c) { return tris[i].p[c]; });
        expect(grid.width == 7 && grid.height == 7 && grid.originX == 10 && grid.originZ == -4, "grid extent");
        expect(grid.dropped == 0, "every vertex found a slot");
        const float* flat = grid.at(0, 1, 3);
        const float* ramp = grid.at(0, 5, 3);
        expect(flat[2] == 1000.0f && std::abs(flat[3] - 1) < 1e-5f, "level ground: normal straight up, height stored");
        expect(std::abs(ramp[0] + 0.7071f) < 1e-3f && std::abs(ramp[3] - 0.7071f) < 1e-3f && std::abs(ramp[1]) < 1e-5f,
               "ramp: 45 degrees, leaning to -x");
        const float* crease = grid.at(0, 3, 3);
        expect(crease[3] > 0.75f && crease[3] < 0.99f && crease[0] < -0.1f, "the crease vertex is in between");
        const float* ceiling = grid.at(1, 5, 3);
        expect(ceiling[2] == 20000.0f && std::abs(ceiling[3] - 1) < 1e-5f, "the ceiling has its own slot, pointing up");
        expect(grid.at(2, 5, 3)[2] == kGridEmpty, "unused slots are empty");

        const float faceRamp[3] = {-0.7071f, 0.7071f, 0}, faceFlat[3] = {0, 1, 0};
        float n[3];
        const float onRamp[3] = {(10 + 4.5f) * 2048, 1000 + 1.5f * 2048, (-4 + 2.5f) * 2048};
        smoothNormal(grid, onRamp, faceRamp, n);
        expect(std::abs(n[0] + 0.7071f) < 2e-3f && std::abs(n[1] - 0.7071f) < 2e-3f,
               "inside the ramp: the ramp normal");
        // Either side of the crease line the two face normals differ by 45 degrees; the smooth one does not jump.
        const float left[3] = {(10 + 2.99f) * 2048, 1000, (-4 + 2.5f) * 2048};
        const float right[3] = {(10 + 3.01f) * 2048, 1000 + 0.01f * 2048, (-4 + 2.5f) * 2048};
        float nl[3], nr[3];
        smoothNormal(grid, left, faceFlat, nl);
        smoothNormal(grid, right, faceRamp, nr);
        expect(std::abs(nl[1] - nr[1]) < 0.02f && std::abs(nl[0] - nr[0]) < 0.02f, "continuous across a triangle edge");
        const float underCeiling[3] = {(10 + 1.5f) * 2048, 20000, (-4 + 2.5f) * 2048};
        const float down[3] = {0, -1, 0};
        smoothNormal(grid, underCeiling, down, n);
        expect(std::abs(n[1] - 1) < 1e-4f, "a ceiling pixel takes the ceiling entry (as an upward normal)");
        const float between[3] = {(10 + 1.5f) * 2048, 9000, (-4 + 2.5f) * 2048};
        const float tilted[3] = {0.6f, 0.8f, 0};
        smoothNormal(grid, between, tilted, n);
        expect(std::abs(n[0] - 0.6f) < 1e-4f, "nothing near that height: the face normal");
        const float outside[3] = {0, 1000, 0};
        smoothNormal(grid, outside, tilted, n);
        expect(std::abs(n[0] - 0.6f) < 1e-4f, "outside the grid: the face normal");
        const NormalGrid none = buildNormalGrid(0, [&](size_t i, int c) { return tris[i].p[c]; });
        expect(none.empty(), "no geometry: no grid");
        // five layers at one vertex: the fifth is dropped, not written over another
        std::vector<Tri> stack;
        for (int level = 0; level < 5; ++level) {
            Tri t{};
            t.p[1][2] = 2048;
            t.p[2][0] = 2048;
            for (auto& v : t.p)
                v[1] = level * 500.0f;
            stack.push_back(t);
        }
        const NormalGrid tall = buildNormalGrid(stack.size(), [&](size_t i, int c) { return stack[i].p[c]; });
        expect(tall.dropped == 3 && tall.at(3, 0, 0)[2] == 1500.0f,
               "more layers than slots: the extra ones are dropped");
    }

    std::printf("detail volume\n");
    {
        const uint32_t n = 32;
        const auto both = buildNoiseVolume(n);
        expect(both.size() == size_t(n) * n * n * kNoiseChannels, "size");
        std::vector<uint8_t> volume(size_t(n) * n * n); // channel 0: the noise itself
        double lit = 0, relief = 0;
        for (size_t i = 0; i < volume.size(); ++i) {
            volume[i] = both[i * kNoiseChannels];
            lit += both[i * kNoiseChannels + 1];
            relief += std::abs(int(both[i * kNoiseChannels]) - int(both[i * kNoiseChannels + 1]));
        }
        lit /= volume.size();
        relief /= volume.size();
        std::printf("    lit channel mean %.2f, mean |noise - lit| %.2f\n", lit, relief);
        expect(std::abs(lit - 127.5) < 1.5, "the lit channel has the same mean: the relief term averages to nothing");
        expect(relief > 6 && relief < 40, "and differs from the noise by a relief-sized step");
        double sum = 0, sum2 = 0;
        uint32_t clipped = 0;
        for (uint8_t b : volume) {
            sum += b;
            sum2 += double(b) * b;
            clipped += b == 0 || b == 255;
        }
        const double mean = sum / volume.size(), deviation = std::sqrt(sum2 / volume.size() - mean * mean);
        std::printf("    mean %.2f deviation %.2f clipped %.2f %%\n", mean, deviation, 100.0 * clipped / volume.size());
        expect(std::abs(mean - 127.5) < 1.0, "mean is the middle of the range: no brightness change on average");
        expect(deviation > 35 && deviation < 60, "contrast");
        expect(clipped < volume.size() / 30, "little clipping");
        // tiling: the step across the wrap is like the steps inside
        double inner = 0, wrap = 0;
        for (uint32_t z = 0; z < n; ++z)
            for (uint32_t y = 0; y < n; ++y) {
                const uint8_t* row = &volume[(size_t(z) * n + y) * n];
                for (uint32_t x = 0; x + 1 < n; ++x)
                    inner += std::abs(int(row[x + 1]) - int(row[x]));
                wrap += std::abs(int(row[0]) - int(row[n - 1]));
            }
        inner /= double(n) * n * (n - 1);
        wrap /= double(n) * n;
        std::printf("    mean step inside %.2f, across the wrap %.2f\n", inner, wrap);
        expect(wrap < inner * 1.25, "tiles without a seam");
        expect(buildNoiseVolume(n) == both, "deterministic");
        const auto half = halveVolume(both, n);
        double halfSum = 0;
        for (uint8_t b : half)
            halfSum += b;
        expect(half.size() == both.size() / 8 && std::abs(halfSum / half.size() - (mean + lit) / 2) < 0.6,
               "the next mip level keeps the mean");
    }

    std::printf("shader constants and detail factor\n");
    {
        const SceneGuideCamera camera = makeCamera(eye, 0.4);
        Unproject u;
        buildUnproject(camera, xyoffset, 2, 2, 0, 0, u);
        Settings s;
        s.detail = true;
        s.strength = 5; // clamped
        ShaderConsts k;
        buildConsts(u, s, 0.7f, k);
        expect(k.base[3] == 2.0f && k.d0[3] == 1.0f && k.dx[3] == 0.0f, "strength clamped, switches passed");
        bool fractions = true;
        for (int o = 0; o < kOctaves; ++o)
            for (int i = 0; i < 3; ++i)
                fractions = fractions && k.octave[o][i] >= 0 && k.octave[o][i] < 1.0f;
        expect(fractions, "octave offsets are fractions of a tile");
        expect(k.sampling[3] == 0.7f, "gradient scale passed");
        // Moving the camera by one period of octave 0 along x leaves its offset unchanged (the pattern is world fixed).
        double moved[3] = {eye[0] + kOctavePeriod[0], eye[1], eye[2]};
        Unproject u2;
        buildUnproject(makeCamera(moved, 0.4), xyoffset, 2, 2, 0, 0, u2);
        ShaderConsts k2;
        buildConsts(u2, s, 0.7f, k2);
        float d = std::abs(k2.octave[0][0] - k.octave[0][0]);
        d = std::min(d, 1.0f - d);
        expect(d < 2e-3f, "the detail is fixed in the world, not to the camera");

        const float mid[kOctaves] = {0.5f, 0.5f, 0.5f}, midLit[2] = {0.5f, 0.5f};
        expect(detailFactor(mid, midLit, 1) == 1.0f, "mid noise leaves the colour unchanged");
        expect(detailFactor(mid, midLit, 0) == 1.0f, "strength 0 leaves the colour unchanged");
        const float high[kOctaves] = {1, 1, 1}, low[kOctaves] = {0, 0, 0};
        const float facing[2] = {0.3f, 0.3f};
        const float lit1[2] = {1, 1}, lit0[2] = {0, 0};
        expect(detailFactor(high, lit1, 1) > 1.2f && detailFactor(low, lit0, 1) < 0.8f, "swing at strength 1");
        expect(reliefFade(0) == 1.0f && reliefFade(1e6f) == kReliefFar && reliefFade(kReliefNear + 1) < 1.0f,
               "the relief fades with distance");
        expect(detailFactor(mid, facing, 1, 1e6f) < detailFactor(mid, facing, 1, 0) &&
                   detailFactor(mid, facing, 1, 1e6f) > 1.0f,
               "and is weaker but not gone on a far mountain");
        expect(detailFactor(low, lit1, 2) >= 0, "never negative");
        expect(detailFactor(mid, facing, 1) > 1.0f, "a face that falls away towards the light is brighter");
    }
    std::printf(g_fail ? "FAILED (%d)\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
