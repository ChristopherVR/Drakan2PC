// The threaded surface builder must produce exactly the triangle lists and resident material projections of the
// synchronous path, for a stream of draws with camera changes, world epochs, several targets and vertex-free jobs.
// Also checks the memoized raster-to-world conversion against the plain one.
#include "surface_builder.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace gsr;
using namespace gsr::surface;

namespace {
uint32_t seed = 987654321u;
float random01() {
    seed = seed * 1664525u + 1013904223u;
    return float(seed >> 8) / float(1u << 24);
}

SceneGeometry grid() {
    SceneGeometry g;
    for (int gx = -8; gx < 8; ++gx)
        for (int gz = -8; gz < 8; ++gz)
            for (unsigned half = 0; half < 2; ++half)
                for (unsigned layer = 0; layer < 2; ++layer) {
                    SceneTriangle t;
                    const float x = gx * 700.f, z = gz * 700.f, y = layer ? 500.f : random01() * 200.f;
                    const float corners[2][3][2] = {{{0, 0}, {700, 0}, {0, 700}}, {{700, 0}, {700, 700}, {0, 700}}};
                    for (unsigned v = 0; v < 3; ++v) {
                        t.position[v][0] = x + corners[half][v][0];
                        t.position[v][1] = y + (layer ? 0.f : random01() * 40.f);
                        t.position[v][2] = z + corners[half][v][1];
                    }
                    t.layer = layer * 1000 + uint32_t(gx + 8) * 16 + uint32_t(gz + 8);
                    t.water = layer;
                    g.triangles.push_back(t);
                }
    return g;
}

// Affine camera: clip = world * M with clip.w = 1, raster = clip * scale + offset - xyoffset, raw z = 16 * depth.
SceneGuideCamera camera(float yaw, float shift) {
    SceneGuideCamera c;
    c.flags = SceneGuideCamera::Valid | SceneGuideCamera::World;
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    const float m[16] = {cs * .002f,  0, sn * .00001f, 0, 0,     -.002f, .00002f, 0,
                         -sn * .002f, 0, cs * .00001f, 0, shift, .1f,    .5f,     1};
    std::memcpy(c.worldToClipVU, m, sizeof m);
    c.viewportScale[0] = 320;
    c.viewportScale[1] = 224;
    c.viewportScale[2] = 100000;
    c.viewportOffset[0] = 2048;
    c.viewportOffset[1] = 2048;
    c.viewportOffset[2] = 200000;
    return c;
}

DrawVertex project(const SceneGuideCamera& c, uint64_t xyoffset, Point p) {
    float clip[4]{};
    const float in[4] = {p.x, p.y, p.z, 1};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            clip[j] += in[i] * c.worldToClipVU[i * 4 + j];
    DrawVertex v{};
    v.x = clip[0] / clip[3] * c.viewportScale[0] + c.viewportOffset[0] - float(xyoffset & 65535) / 16;
    v.y = clip[1] / clip[3] * c.viewportScale[1] + c.viewportOffset[1] - float((xyoffset >> 32) & 65535) / 16;
    v.z = 16 * (clip[2] / clip[3] * c.viewportScale[2] + c.viewportOffset[2]);
    v.r = random01() * 255;
    v.g = random01() * 255;
    v.b = random01() * 255;
    v.a = random01() * 255;
    v.q = .5f + random01();
    v.s = random01() * v.q;
    v.t = random01() * v.q;
    return v;
}

bool sameTriangles(const std::vector<Triangle>& a, const std::vector<Triangle>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (std::memcmp(x.p, y.p, sizeof x.p) || std::memcmp(x.color, y.color, sizeof x.color) || x.water != y.water ||
            std::memcmp(x.lighting, y.lighting, sizeof x.lighting) || std::memcmp(x.uv, y.uv, sizeof x.uv) ||
            x.texture != y.texture || x.vertexLit != y.vertexLit ||
            std::memcmp(x.opacity, y.opacity, sizeof x.opacity) || x.alphaTest != y.alphaTest ||
            x.textureFunction != y.textureFunction || x.alphaReference != y.alphaReference ||
            x.textureAlpha != y.textureAlpha || x.transparent != y.transparent || x.shadowGeometry != y.shadowGeometry)
            return false;
    }
    return true;
}
} // namespace

int main() {
    {
        // The world drawn behind the pause menu (screen 0x17) keeps its world camera and is reconstructed; without a
        // world camera only gameplay draws after EndWorld are.
        SceneGuideCamera paused;
        paused.flags = SceneGuideCamera::Valid | SceneGuideCamera::World;
        paused.guiScreen = 0x17;
        assert(surfaceCameraAccepted(paused, false));
        SceneGuideCamera gameplay = paused;
        gameplay.guiScreen = -1;
        assert(surfaceCameraAccepted(gameplay, false));
        SceneGuideCamera menu;
        menu.guiScreen = 0x17;
        assert(!surfaceCameraAccepted(menu, false) && !surfaceCameraAccepted(menu, true));
        menu.guiScreen = -1;
        assert(!surfaceCameraAccepted(menu, false) && surfaceCameraAccepted(menu, true));
    }
    const auto geometry = grid();
    ResidentScene syncScene, asyncScene;
    syncScene.replace(geometry);
    asyncScene.replace(geometry);
    SurfaceList syncLists[2], asyncLists[2];
    {
        SurfaceBuilder sync(syncScene, false), async(asyncScene, true);
        assert(!sync.async() && async.async());
        uint64_t epoch = 1, serial = 1, xy = 0;
        SceneGuideCamera cam = camera(.3f, .1f);
        size_t total = 0, waterTotal = 0, compared = 0;
        for (unsigned draw = 0; draw < 6000; ++draw) {
            if (random01() < .01f) { // new camera (serial) or drawing offset
                ++serial;
                cam = camera(random01() * 6.28f, random01() - .5f);
                cam.serial = serial;
            }
            if (random01() < .005f)
                xy = (uint64_t(random01() * 4096) << 32) | uint64_t(random01() * 4096);
            const unsigned target = random01() < .8f ? 0 : 1;
            std::vector<DrawVertex> vertices;
            const unsigned triangles = 1 + unsigned(random01() * 30);
            for (unsigned t = 0; t < triangles; ++t) {
                if (random01() < .7f) { // a resident triangle (maybe a strip neighbour sharing two vertices)
                    const auto& r = syncScene.triangles[size_t(random01() * syncScene.triangles.size())];
                    for (const auto& p : r.p)
                        vertices.push_back(project(cam, xy, p));
                    if (random01() < .5f && vertices.size() >= 3) {
                        const auto a = vertices[vertices.size() - 2], b = vertices[vertices.size() - 1];
                        vertices.push_back(a);
                        vertices.push_back(b);
                        vertices.push_back(project(cam, xy, r.p[0]));
                    }
                } else {
                    for (unsigned v = 0; v < 3; ++v)
                        vertices.push_back(project(
                            cam, xy, {(random01() - .5f) * 11000, random01() * 600, (random01() - .5f) * 11000}));
                }
            }
            DrawSurfaceParams d;
            d.waterState = random01() < .3f;
            d.nativeWaterPass = d.waterState && random01() < .3f;
            d.opaqueEligible = random01() < .6f;
            d.materialEligible = random01() < .5f;
            d.transparent = random01() < .2f;
            d.textureAlpha = random01() < .5f;
            d.fst = random01() < .2f;
            d.alphaTest = unsigned(random01() * 8);
            d.alphaReference = float(unsigned(random01() * 255));
            d.textureFunction = unsigned(random01() * 4);
            d.texture = random01() < .8f ? unsigned(random01() * 50) : ~0u;
            d.texWidth = float(1u << unsigned(random01() * 10));
            d.texHeight = float(1u << unsigned(random01() * 10));
            const bool withVertices = random01() < .9f;
            for (auto* builder : {&sync, &async}) {
                if (builder->needsConfigure(serial, xy))
                    builder->stageCamera(cam, xy);
                auto& lists = builder == &sync ? syncLists : asyncLists;
                auto job = builder->makeJob(&lists[target], epoch);
                job.params = d;
                builder->submit(std::move(job), vertices.data(), withVertices ? uint32_t(vertices.size()) : 0);
            }
            if (random01() < .02f) { // end of world: readers drain, then compare
                async.drain();
                for (unsigned t = 0; t < 2; ++t) {
                    assert(syncLists[t].epoch == asyncLists[t].epoch);
                    assert(sameTriangles(syncLists[t].triangles, asyncLists[t].triangles));
                    total += syncLists[t].triangles.size();
                    for (const auto& tri : syncLists[t].triangles)
                        waterTotal += tri.water;
                }
                const auto a = syncScene.projectedShared();
                const auto b = asyncScene.projectedShared();
                assert(sameTriangles(*a, *b));
                ++compared;
                ++epoch;
            }
        }
        async.drain();
        for (unsigned t = 0; t < 2; ++t)
            assert(sameTriangles(syncLists[t].triangles, asyncLists[t].triangles));
        assert(compared > 50 && total > 10000 && waterTotal > 1000);
        size_t textured = 0;
        for (const auto& tri : *syncScene.projectedShared())
            textured += tri.texture != ~0u;
        assert(textured > 100); // resident matches observed materials
        std::cout << "Threaded surface builder equals the synchronous path over " << compared << " worlds (" << total
                  << " triangles, " << waterTotal << " water, " << textured << " resident triangles textured)\n";
    }
    // The memo returns the plain conversion's results, also for failures and after reconfiguration.
    {
        CachedRasterToWorld cached;
        RasterToWorld plain;
        unsigned hits = 0;
        for (unsigned round = 0; round < 4; ++round) {
            const auto c = camera(round * 1.3f, round * .2f);
            cached.configure(c, round * 77);
            plain.configure(c, round * 77);
            float previous[3][3]{};
            for (unsigned n = 0; n < 20000; ++n) {
                float x, y, z;
                if (n > 3 && random01() < .6f) {
                    const unsigned k = unsigned(random01() * 3);
                    x = previous[k][0], y = previous[k][1], z = previous[k][2];
                    ++hits;
                } else {
                    x = random01() * 4096, y = random01() * 4096, z = random01() < .05f ? 0.f : random01() * 6.4e6f;
                }
                std::memmove(previous[1], previous[0], sizeof(float) * 6);
                previous[0][0] = x, previous[0][1] = y, previous[0][2] = z;
                Point a, b;
                const bool okA = cached.point(x, y, z, a), okB = plain.point(x, y, z, b);
                assert(okA == okB);
                assert(!okA || std::memcmp(&a, &b, sizeof a) == 0);
            }
        }
        std::cout << "Memoized raster-to-world equals the plain conversion (" << hits << " repeated vertices)\n";
    }
    std::cout << "PASS surface builder\n";
}
