#include "resident_scene.h"
#include "ray_normals.h"
#include "ray_world_cache.h"
#include "drakan_scene_geometry.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cfenv>
#include <random>

// Original packing path, retained as a differential oracle for the bounded rounding optimization.
static float referenceNormal(gsr::surface::Point n) {
    const float l1 = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    if (!(l1 > 0) || !std::isfinite(l1))
        n = {0, 0, 1};
    else
        n = {n.x / l1, n.y / l1, n.z / l1};
    float u = n.x, v = n.y;
    if (n.z < 0) {
        u = (1 - std::abs(n.y)) * (n.x >= 0 ? 1.f : -1.f);
        v = (1 - std::abs(n.x)) * (n.y >= 0 ? 1.f : -1.f);
    }
    const auto quantize = [](float x) { return uint32_t(std::lround((std::clamp(x, -1.f, 1.f) * .5f + .5f) * 4095)); };
    return float(quantize(u) | quantize(v) << 12);
}

int main(int argc, char** argv) {
    {
        std::array<gsr::surface::Triangle, 2> batch{};
        for (auto& tri : batch)
            tri.shadowGeometry = false;
        assert(!gsr::surface::hasValidatedOpaque(batch));
        batch[0].water = batch[0].shadowGeometry = true;
        assert(!gsr::surface::hasValidatedOpaque(batch));
        batch[1].shadowGeometry = true;
        assert(gsr::surface::hasValidatedOpaque(batch)); // mixed batches retain per-hit validation
        batch[1].water = true;
        assert(!gsr::surface::hasValidatedOpaque(batch));
    }
    {
        using namespace gsr::surface;
        ModelAlphaCoverage cache;
        gsr::SceneModels models;
        auto texture = std::make_shared<gsr::SceneModelTexture>();
        texture->identity = 10;
        texture->flags = 8;
        texture->pixels.assign(Texture::size * Texture::size, 0x01000000);
        models.textures.push_back(texture);
        auto proof = cache.update(&models);
        assert(proof.size() == 1 && proof[0] == kRayAlphaPositive);
        gsr::SceneModelMesh mesh;
        mesh.triangles.resize(1);
        mesh.triangles[0].material = 0;
        const std::vector<Point> normals(3, {0, 1, 0});
        std::vector<Attribute> attributes, reference;
        packModelAttributes(mesh, models, 0, normals, attributes, {}, proof);
        packModelAttributes(mesh, models, 0, normals, reference);
        assert(uint32_t(attributes[5][2]) == (uint32_t(reference[5][2]) | kRayAlphaPositive));
        // A replacement with the same identity must not inherit coverage; even
        // one transparent texel matters at a bilinear silhouette boundary.
        auto hole = std::make_shared<gsr::SceneModelTexture>(*texture);
        hole->pixels.back() = 0;
        models.textures[0] = hole;
        assert(cache.update(&models)[0] == 0);
        models.textures[0] = texture;
        proof = cache.update(&models);
        texture->flags = 0x80000; // coverage never substitutes a translucent material's variable alpha
        attributes.clear();
        packModelAttributes(mesh, models, 0, normals, attributes, {}, proof);
        assert(!(uint32_t(attributes[5][2]) & kRayAlphaPositive));
        texture->identity = 0; // ad-hoc inputs can mutate in place
        assert(cache.update(&models)[0] == kRayAlphaPositive);
        texture->pixels[0] = 0;
        assert(cache.update(&models)[0] == 0);
        texture->pixels.resize(10);
        assert(cache.update(&models)[0] == 0);
        assert(cache.update(nullptr).empty());
    }
    {
        using namespace gsr::surface;
        // Material parameters survive all three packing paths and a warm cache invalidates on material edits.
        Triangle tri;
        tri.p[0] = {0, 0, 0};
        tri.p[1] = {1, 0, 0};
        tri.p[2] = {0, 0, 1};
        tri.roughness = .3f;
        tri.reflectance = .6f;
        std::vector<Attribute> direct, cached;
        packAttributes(tri, 0, direct);
        const auto packed = uint32_t(direct[6][3]);
        assert(std::abs(float(packed & 255) / 255 - .3f) <= 1.f / 255);
        assert(std::abs(float(packed >> 8) / 255 - .6f) <= 1.f / 255);
        assert(packRayMaterial(NAN, INFINITY) == packRayMaterial(.75f, .04f));
        assert(packRayMaterial(-1, -1) == packRayMaterial(.04f, 0));
        assert(packRayMaterial(2, 2) == 65535);
        RayWorldCache cache;
        std::vector<Point> positions;
        uint32_t counts[3]{};
        cache.append(std::span(&tri, 1), {}, 0, positions, cached, counts);
        assert(cached == direct);
        positions.clear();
        cached.clear();
        tri.roughness = .9f;
        cache.append(std::span(&tri, 1), {}, 0, positions, cached, counts);
        assert(cache.rebuiltBlocks() == 1 && cached[6][3] != direct[6][3]);
        gsr::SceneModels models;
        auto texture = std::make_shared<gsr::SceneModelTexture>();
        texture->roughness = tri.roughness;
        texture->reflectance = tri.reflectance;
        models.textures.push_back(texture);
        gsr::SceneModelMesh mesh;
        mesh.triangles.resize(1);
        mesh.triangles[0].material = 0;
        mesh.triangles[0].position[1][0] = 1;
        mesh.triangles[0].position[2][2] = 1;
        std::vector<Point> normals(3, Point{0, -1, 0});
        std::vector<Attribute> native;
        packModelAttributes(mesh, models, 4, normals, native);
        assert(native[6][3] == cached[6][3]);
        rebaseModelAttributes(mesh, models, 12, native);
        assert(native[5][1] == 12 && native[6][3] == cached[6][3]);
    }
    {
        std::mt19937 random(532);
        std::uniform_real_distribution<float> component(-1, 1);
        std::vector<gsr::surface::Point> inputs(200000);
        for (auto& p : inputs)
            p = {component(random), component(random), component(random)};
        inputs[0] = {};
        inputs[1] = {INFINITY, 0, 0};
        inputs[2] = {NAN, 0, 0};
        const int previous = std::fegetround();
        for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
            std::fesetround(mode);
            for (const auto p : inputs)
                assert(gsr::surface::packNormal(p) == referenceNormal(p));
        }
        std::fesetround(previous);
        std::cout << "800000 normal packing comparisons across rounding modes: zero mismatches\n";
        gsr::surface::CornerWelds welds;
        for (uint32_t i = 0; i < inputs.size(); ++i) {
            const uint32_t corner = (i * 7) % uint32_t(inputs.size());
            welds.corners.push_back(corner);
            if (i % 8 && i % 8 != 4)
                inputs[corner] = inputs[welds.corners[i - 1]];
            if (i % 8 == 7)
                welds.ends.push_back(i + 1);
        }
        std::vector<float> packed;
        for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
            std::fesetround(mode);
            gsr::surface::packCornerNormals(welds, inputs, packed);
            for (size_t i = 0; i < inputs.size(); ++i)
                assert(packed[i] == referenceNormal(inputs[i]));
        }
        welds.corners.clear(); // stale topology takes the independent-corner path
        gsr::surface::packCornerNormals(welds, inputs, packed);
        for (size_t i = 0; i < inputs.size(); ++i)
            assert(packed[i] == referenceNormal(inputs[i]));
        std::fesetround(previous);
    }
    {
        // A pose with every vertex on one point leaves its corners unwelded and says so; a real pose welds again.
        using gsr::surface::Point;
        constexpr size_t kTriangles = 200;
        bool collapsedPose = true;
        const auto corners = [&](size_t i, Point(&p)[3]) {
            for (unsigned j = 0; j < 3; ++j)
                p[j] = collapsedPose ? Point{} : Point{float(j), float(i % 8), float(j == 2)}; // 25 per point
        };
        gsr::surface::CornerWelds welds;
        gsr::surface::weldCorners(kTriangles, corners, welds);
        assert(welds.collapsed && welds.corners.size() == kTriangles * 3 && welds.ends.size() == kTriangles * 3);
        collapsedPose = false;
        gsr::surface::weldCorners(kTriangles, corners, welds);
        assert(!welds.collapsed && welds.ends.size() < kTriangles * 3);
        std::vector<Point> normals;
        gsr::surface::smoothNormals(kTriangles, corners, welds, normals);
        assert(normals.size() == kTriangles * 3);
    }
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
    const auto layout = drakan_scene_geometry::layoutIdentity(ram.data(), uint32_t(ram.size()));
    assert(layout && !drakan_scene_geometry::layoutIdentity(nullptr, uint32_t(ram.size())));
    // Reused layer-array address/count must still expose replaced height/material storage immediately, while
    // animated lighting does not force a full terrain recapture every world.
    put(0x700028, 0x740000u);
    assert(drakan_scene_geometry::layoutIdentity(ram.data(), uint32_t(ram.size())) != layout);
    put(0x700028, 0x720000u);
    put(0x700130, .5f);
    assert(drakan_scene_geometry::layoutIdentity(ram.data(), uint32_t(ram.size())) == layout);
    put(0x4dc120, 0u);
    assert(!drakan_scene_geometry::layoutIdentity(ram.data(), uint32_t(ram.size())));
    put(0x4dc120, 2u);
    assert(drakan_scene_geometry::layoutIdentity(ram.data(), uint32_t(ram.size())) == layout);
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
    {
        // Projections held elsewhere (ray passes) stay unchanged; later ones reuse released buffers and update only
        // the changed layers, matching a fresh full projection.
        const auto first = resident.projectedShared();
        const auto firstCopy = *first;
        auto held = first;
        for (uint32_t step = 0; step < 6; ++step) {
            auto bigger = updated;
            bigger.texture = 10 + step;
            for (auto& p : bigger.p) {
                p.x *= 1.5f + step;
                p.z *= 1.5f + step;
            }
            resident.observe(bigger, resident.match(ground, false));
            const auto current = resident.projectedShared();
            gsr::surface::ResidentScene fresh;
            fresh.replace(scene);
            fresh.observe(bigger, fresh.match(ground, false));
            const auto expected = fresh.projectedShared();
            assert(current->size() == expected->size());
            for (size_t i = 0; i < current->size(); ++i) {
                const auto &a = (*current)[i], &b = (*expected)[i];
                assert(a.texture == b.texture && std::memcmp(a.uv, b.uv, sizeof(a.uv)) == 0 &&
                       std::memcmp(a.p, b.p, sizeof(a.p)) == 0 && a.water == b.water);
            }
            if (step == 2)
                held = current; // keep one more buffer alive across further updates
        }
        assert(first->size() == firstCopy.size() && first->at(0).texture == 7);
        for (size_t i = 0; i < first->size(); ++i)
            assert(std::memcmp(&(*first)[i], &firstCopy[i], sizeof(firstCopy[i])) == 0);
        assert(held->at(0).texture == 12);
    }
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
