#include "resident_scene.h"
#include "drakan_scene_geometry.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    std::vector<uint8_t> ram(32u << 20);
    auto put = [&](uint32_t a, const auto& v) { std::memcpy(ram.data() + a, &v, sizeof v); };
    put(0x4dc118, 0x700000u);
    put(0x4dc120, 2u);
    for (unsigned layer = 0; layer < 2; ++layer) {
        uint32_t a = 0x700000 + layer * 0x164;
        put(a + 8, -2048.f);
        put(a + 12, layer ? 900.f : 1000.f);
        put(a + 16, -2048.f);
        put(a + 0x1c, 1u);
        put(a + 0x20, 1u);
        put(a + 0x4c, layer ? 2u : 0u);
        put(a + 0x50, 0x400000u);
        put(a + 0x24, 0x710000u + layer * 16);
        put(a + 0x28, 0x720000u + layer * 16);
        put(a + 0x40, 0x730000u + layer * 16);
        put(0x710000 + layer * 16, uint16_t(1));
        put(0x720000 + layer * 16, uint16_t(32768));
        for (unsigned v = 0; v < 4; ++v)
            put(0x730000 + layer * 16 + v * 4, 0x80602010u);
    }
    auto scene = drakan_scene_geometry::capture(ram.data(), uint32_t(ram.size()));
    assert(scene.layers == 2 && scene.waterLayers == 1 && scene.triangles.size() == 4);
    assert(scene.triangles[0].position[0][1] == 1000 && scene.triangles[2].position[0][1] == 900);
    // Quad bit 0 set: the native draw splits the cell along (x,z)-(x+1,z+1), so both halves share that diagonal.
    for (unsigned half = 0; half < 2; ++half) {
        unsigned diagonal = 0;
        for (const auto& p : scene.triangles[half].position)
            diagonal += (p[0] == -2048 && p[2] == -2048) || (p[0] == 0 && p[2] == 0);
        assert(diagonal == 2);
    }
    gsr::surface::ResidentScene resident;
    resident.replace(scene);
    assert(resident.waterCount == 2);
    auto water = resident.triangles[2];
    assert(resident.match(water, true) >= 0 && resident.match(water, false) < 0);
    auto ground = resident.triangles[0];
    ground.texture = 3;
    for (unsigned i = 0; i < 3; ++i) {
        ground.uv[i][0] = ground.p[i].x / 2048;
        ground.uv[i][1] = ground.p[i].z / 2048;
    }
    resident.observe(ground, resident.match(ground, false));
    std::vector<gsr::surface::Triangle> exported;
    resident.append(exported);
    assert(exported.size() == 4 && exported[0].texture == 3);
    assert(exported[2].water &&
           exported[2].shadowGeometry); // exact native surface, independent of the GS coverage mask
    assert(std::abs(exported[1].uv[2][0] - exported[1].p[2].x / 2048) < 1e-6);
    // Repeated exports reuse the projection, but retain append semantics.
    resident.append(exported);
    assert(exported.size() == 8);
    for (size_t i = 0; i < 4; ++i)
        assert(std::memcmp(&exported[i], &exported[i + 4], sizeof(exported[i])) == 0);
    auto updated = ground;
    updated.texture = 7;
    for (auto& p : updated.p) {
        p.x *= 2;
        p.z *= 2;
    }
    resident.observe(updated, resident.match(ground, false));
    exported.clear();
    resident.append(exported);
    assert(exported.size() == 4 && exported[0].texture == 7);
    assert(std::abs(exported[1].uv[2][0] - exported[1].p[2].x / 4096) < 1e-6);
    // A smaller observation must not invalidate the winning projection.
    resident.observe(ground, resident.match(ground, false));
    exported.clear();
    resident.append(exported);
    assert(exported[0].texture == 7);
    put(0x700050, 0x420000u); // transient native lighting flag must not retire geometry/materials
    auto reused = drakan_scene_geometry::capture(ram.data(), uint32_t(ram.size()), scene.identity);
    assert(reused.identity == scene.identity && reused.triangles.empty());
    put(0x710000, uint16_t(0xf1)); // both triangle halves are native holes
    auto hole = drakan_scene_geometry::capture(ram.data(), uint32_t(ram.size()), scene.identity);
    assert(hole.identity != scene.identity && hole.triangles.size() == 2);
    put(0x710010, uint16_t(0x41)); // partially flagged native water strip still owns both halves
    assert(drakan_scene_geometry::capture(ram.data(), uint32_t(ram.size())).triangles.size() == 2);
    put(0x700028, 0xfffffff0u); // malformed pointers fail closed
    assert(drakan_scene_geometry::capture(ram.data(), uint32_t(ram.size())).layers == 1);
    // The cell-contiguous matcher must return what the original per-index scan returned, for terrain-like grids
    // (shared edges, stacked water layers, degenerate slivers) and arbitrary draw triangles.
    {
        gsr::SceneGeometry grid;
        uint32_t seed = 12345;
        auto random = [&] {
            seed = seed * 1664525u + 1013904223u;
            return float(seed >> 8) / float(1u << 24);
        };
        for (int gx = -12; gx < 12; ++gx)
            for (int gz = -12; gz < 12; ++gz)
                for (unsigned half = 0; half < 2; ++half)
                    for (unsigned layer = 0; layer < 2; ++layer) {
                        gsr::SceneTriangle t;
                        const float x = gx * 700.f, z = gz * 700.f, y = layer ? 500.f : random() * 200.f;
                        const float corners[2][3][2] = {{{0, 0}, {700, 0}, {0, 700}}, {{700, 0}, {700, 700}, {0, 700}}};
                        for (unsigned v = 0; v < 3; ++v) {
                            t.position[v][0] = x + corners[half][v][0];
                            t.position[v][1] = y + (layer ? 0.f : random() * 40.f);
                            t.position[v][2] = z + corners[half][v][1];
                        }
                        if ((gx + gz) % 7 == 0) // sliver: never matches
                            t.position[2][0] = t.position[0][0] + .001f, t.position[2][2] = t.position[0][2];
                        t.layer = layer * 1000 + uint32_t(gx + 12) * 24 + uint32_t(gz + 12);
                        t.water = layer;
                        grid.triangles.push_back(t);
                    }
        resident.replace(grid);
        const auto reference = [&](const gsr::surface::Triangle& draw, bool water) {
            using namespace gsr::surface;
            const auto cellOf = [](float x, float z) {
                return int64_t(uint64_t(uint32_t(int32_t(std::floor(x / 2048.f)))) << 32 |
                               uint32_t(int32_t(std::floor(z / 2048.f))));
            };
            const Point p{(draw.p[0].x + draw.p[1].x + draw.p[2].x) / 3, (draw.p[0].y + draw.p[1].y + draw.p[2].y) / 3,
                          (draw.p[0].z + draw.p[1].z + draw.p[2].z) / 3};
            const auto wanted = cellOf(p.x, p.z);
            for (size_t index = 0; index < resident.triangles.size(); ++index) {
                const auto& t = resident.triangles[index];
                if (cellOf((t.p[0].x + t.p[1].x + t.p[2].x) / 3, (t.p[0].z + t.p[1].z + t.p[2].z) / 3) != wanted ||
                    t.water != water)
                    continue;
                const float ax = t.p[1].x - t.p[0].x, az = t.p[1].z - t.p[0].z, bx = t.p[2].x - t.p[0].x,
                            bz = t.p[2].z - t.p[0].z, det = ax * bz - az * bx;
                if (std::abs(det) < 1)
                    continue;
                const float u = ((p.x - t.p[0].x) * bz - (p.z - t.p[0].z) * bx) / det;
                const float v = (ax * (p.z - t.p[0].z) - az * (p.x - t.p[0].x)) / det;
                if (u < -.01f || v < -.01f || u + v > 1.01f)
                    continue;
                const auto n = cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]));
                const float len = length(n);
                if (len < 1)
                    continue;
                bool samePlane = true;
                for (const auto& q : draw.p) {
                    const auto d = sub(q, t.p[0]);
                    if (std::abs(n.x * d.x + n.y * d.y + n.z * d.z) > len * 32.f)
                        samePlane = false;
                }
                if (samePlane)
                    return int(index);
            }
            return -1;
        };
        unsigned matched = 0;
        for (unsigned i = 0; i < 20000; ++i) {
            gsr::surface::Triangle draw;
            const float cx = (random() - .5f) * 16000, cz = (random() - .5f) * 16000,
                        cy = random() < .5f ? 500.f : random() * 260.f, size = 10 + random() * 900;
            for (auto& p : draw.p)
                p = {cx + (random() - .5f) * size, cy + (random() - .5f) * 60, cz + (random() - .5f) * size};
            for (bool waterLayer : {false, true}) {
                const int expected = reference(draw, waterLayer);
                assert(resident.match(draw, waterLayer) == expected);
                matched += expected >= 0;
            }
        }
        assert(matched > 1000);
        std::cout << "Cell matcher equals the reference scan for 40000 queries (" << matched << " matches)\n";
    }
    resident.replace({});
    assert(resident.triangles.empty() && resident.waterCount == 0);
    exported.clear();
    resident.append(exported);
    assert(exported.empty());
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        file.read(reinterpret_cast<char*>(ram.data()), ram.size());
        assert(file.gcount() == ram.size());
        auto live = drakan_scene_geometry::capture(ram.data(), uint32_t(ram.size()));
        assert(live.layers == 483 && live.waterLayers == 171 && !live.triangles.empty());
        resident.replace(live);
        unsigned differentTint = 0;
        for (const auto& t : resident.triangles)
            if (t.water && std::abs(t.lighting[0][0] - 32.f / 128) > .01f)
                ++differentTint;
        assert(differentTint > 10);
        std::cout << "Live native snapshot: " << live.layers << " layers, " << live.waterLayers << " water layers, "
                  << live.triangles.size() << " triangles, " << differentTint
                  << " water triangles beyond the old tint\n";
    }
    std::cout << "PASS pre-cull geometry, constant height, native water classification, holes, material projection, "
                 "stable lifetime, bounds and replacement\n";
}
