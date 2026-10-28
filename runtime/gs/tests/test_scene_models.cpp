#include "drakan_scene_models.h"
#include "drakan_scene_assets.h"
#include <fstream>
#include <cassert>
#include <iostream>
int main(int argc, char** argv) {
    using namespace drakan_scene_models;
    Capture cache;
    assert(cache.capture(nullptr, 0, 1)->instances.empty());
    {
        gsr::SceneModelMesh sphere;
        sphere.nativeId = 3;
        const float axes[6][3] = {{10240, 0, 0},  {-10240, 0, 0}, {0, 10240, 0},
                                  {0, -10240, 0}, {0, 0, 10240},  {0, 0, -10240}};
        for (unsigned x = 0; x < 2; ++x)
            for (unsigned y = 2; y < 4; ++y)
                for (unsigned z = 4; z < 6; ++z) {
                    gsr::SceneModelTriangle t;
                    std::memcpy(t.position[0], axes[x], 12);
                    std::memcpy(t.position[1], axes[y], 12);
                    std::memcpy(t.position[2], axes[z], 12);
                    sphere.triangles.push_back(t);
                }
        float transform[]{1, 0, 0, 400000, 0, 1, 0, 160000, 0, 0, 1, 440000}, camera[]{400000, 160000, 440000};
        assert(cameraBackdrop(sphere, transform, camera));
        transform[3] += 1;
        assert(!cameraBackdrop(sphere, transform, camera));
        transform[3] -= 1;
        sphere.nativeId = 0x10003;
        assert(!cameraBackdrop(sphere, transform, camera));
        sphere.nativeId = 3;
        transform[0] = .5f;
        assert(!cameraBackdrop(sphere, transform, camera));
        transform[0] = 1;
        auto partial = sphere;
        partial.triangles.resize(1);
        assert(!cameraBackdrop(partial, transform, camera));
        auto small = sphere;
        for (auto& t : small.triangles)
            for (auto& p : t.position)
                for (auto& c : p)
                    c *= .01f;
        assert(!cameraBackdrop(small, transform, camera));
        BackdropTracker tracker;
        gsr::SceneModelInstance instance;
        instance.object = 123;
        instance.generation = 1;
        std::copy(transform, transform + 12, instance.transform);
        sphere.identity = 1234;
        assert(tracker.exclude(sphere, instance, camera, true));
        camera[0] += 500;
        assert(tracker.exclude(sphere, instance, camera, true)); // camera leads sky
        instance.generation = 2;
        assert(!tracker.exclude(sphere, instance, camera, true)); // reused address
        camera[0] -= 500;
        assert(tracker.exclude(sphere, instance, camera, true));
        camera[0] += 500;
        sphere.identity++;
        assert(!tracker.exclude(sphere, instance, camera, true));
        camera[0] -= 500;
        assert(tracker.exclude(sphere, instance, camera, true));
        camera[0] += 500;
        instance.transform[0] = .5f;
        assert(!tracker.exclude(sphere, instance, camera, true));
        camera[0] -= 500;
        instance.transform[0] = 1;
        assert(tracker.exclude(sphere, instance, camera, true));
        camera[0] += 500;
        assert(!tracker.exclude(sphere, instance, camera, false));
        camera[0] -= 500;
        assert(tracker.exclude(sphere, instance, camera, true));
        tracker.clear();
        camera[0] += 500;
        assert(!tracker.exclude(sphere, instance, camera, true));
        camera[0] -= 500;
        sphere.triangles[0].position[0][0] = NAN;
        assert(!cameraBackdrop(sphere, transform, camera));
    }
    {
        std::vector<uint8_t> memory(0x101000);
        Reader r{memory.data(), uint32_t(memory.size())};
        auto put = [&](uint32_t at, auto v) { std::memcpy(memory.data() + at, &v, sizeof(v)); };
        constexpr uint32_t layer = 0x100000, quads = 0x100200, heights = 0x100300;
        put(layer + 0x1c, 1u);
        put(layer + 0x20, 1u);
        put(layer + 0x24, quads);
        put(layer + 0x28, heights);
        put(heights, uint16_t(32768));
        put(heights + 2, uint16_t(33768));
        put(heights + 4, uint16_t(32768));
        put(heights + 6, uint16_t(34768));
        float ground;
        // Bit 0 cleared: the native draw splits along (x+1,z)-(x,z+1); set: along (x,z)-(x+1,z+1).
        assert(layerGround(r, layer, .25f, .25f, ground) && ground == 500);
        assert(layerGround(r, layer, .75f, .75f, ground) && ground == 2500);
        put(quads, uint16_t(1));
        assert(layerGround(r, layer, .25f, .75f, ground) && ground == 1000);
        assert(layerGround(r, layer, .75f, .25f, ground) && ground == 2000);
        put(quads, uint16_t(0x51));
        assert(!layerGround(r, layer, .25f, .75f, ground));
        assert(layerGround(r, layer, .75f, .25f, ground));
        assert(!layerGround(r, layer, 1, 0, ground) && !layerGround(r, layer, NAN, 0, ground));
        put(quads, uint16_t(0));
        put(layer + 0x50, 0x400000u);
        assert(layerGround(r, layer, .5f, .5f, ground) && ground == 0);
        put(layer + 0x28, 0xfffffff0u);
        assert(!layerGround(r, layer, .5f, .5f, ground));
    }
    hle::IsoFs fs;
    Assets assets;
    if (argc > 2) {
        assert(fs.openIso(argv[2]));
        fs.setLanguageRoot("ENGLISH");
        assets.source(&fs);
        cache.loadTexture = [&](uint32_t id, const std::string& path) { return assets.load(id, path); };
    }
    assert(key(4, {3, 1, 2}) == key(4, {2, 3, 1}));
    assert(key(3, {1, 2, 3}) != key(4, {1, 2, 3}));
    enabled = true;
    constructed(0x100080);
    assert(live.contains(0x100080));
    destroyed(0x100080);
    assert(live.empty());
    if (argc > 1) {
        std::vector<uint8_t> ram(32u << 20);
        std::ifstream in(argv[1], std::ios::binary);
        in.read(reinterpret_cast<char*>(ram.data()), ram.size());
        assert(in.gcount() == ram.size());
        if (argc > 2) {
            auto disk = assets.load(0x31924, "cdrom0:/English/Common/Resources/Resources.TXD");
            assert(disk);
            Reader r{ram.data(), uint32_t(ram.size())};
            auto resources = r.resources(0x4db280, 0x51cfe8);
            auto found = resources.find(0x31924);
            assert(found != resources.end());
            auto p = found->second;
            assert(r.u(p + 0x24) == 128 && r.u(p + 0x28) == 128 && r.u(p + 0x38) == 8);
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 0; x < 128; ++x) {
                    unsigned index = ram[r.u(p + 0x50) + y * r.u(p + 0x34) + x];
                    index = (index & ~24u) | ((index & 8) << 1) | ((index & 16) >> 1);
                    assert(disk->pixels[y * 128 + x] == r.u(r.u(r.u(p + 0x3c) + 8) + index * 4));
                }
        }
        auto scene = cache.capture(ram.data(), uint32_t(ram.size()), 9);
        size_t triangles = 0, textured = 0;
        for (const auto& m : scene->meshes)
            for (const auto& t : m->triangles) {
                ++triangles;
                textured += t.material < scene->textures.size();
            }
        // The captured river's generated sky sphere must not occlude cliffs,
        // cast a sun shadow, or write neural geometry/motion guides.
        assert(std::none_of(scene->instances.begin(), scene->instances.end(),
                            [](const auto& i) { return i.object == 0xa5eca0; }));
        assert(!scene->lighting.lights.empty());
        const auto& sun = scene->lighting.lights.front();
        assert(sun.type == gsr::SceneLight::Directional);
        std::cout << "Native world-space sun=" << sun.vector[0] << "," << sun.vector[1] << "," << sun.vector[2]
                  << " lights=" << scene->lighting.lights.size() << "\n";
        std::cout << scene->instances.size() << " instances, " << scene->meshes.size() << " unique meshes, "
                  << triangles << " unique triangles, " << textured << " textured, " << scene->textures.size()
                  << " resident textures, " << scene->rejectedInstances << " rejected instances" << std::endl;
        assert(scene->instances.size() > 1000 && scene->meshes.size() > 50 && triangles > 30000 &&
               textured > triangles * .8 && scene->textures.size() > 100);
        if (argc > 2)
            assert(textured > triangles * .98);
        auto posed = std::find_if(scene->instances.begin(), scene->instances.end(),
                                  [&](const auto& i) { return scene->meshes[i.mesh]->nativeId == 2; });
        assert(posed != scene->instances.end());
        const auto& animated = scene->meshes[posed->mesh];
        assert(animated->triangles.size() > 2500 && animated->triangles.size() < 3100);
        Reader reader{ram.data(), uint32_t(ram.size())};
        auto source = drakan_object_motion::provenPoseSource(ram.data(), uint32_t(ram.size()), posed->object);
        assert(source.count == 1628);
        bool movedPose = false;
        auto posedRam = ram;
        for (unsigned i = 0; i < source.count; ++i) {
            float x = reader.f(source.address + i * 12) + 20;
            std::memcpy(posedRam.data() + source.address + i * 12, &x, 4);
        }
        auto animatedFrame = cache.capture(posedRam.data(), uint32_t(posedRam.size()), 9);
        for (const auto& i : animatedFrame->instances)
            if (i.object == posed->object) {
                const auto& next = animatedFrame->meshes[i.mesh];
                assert(next->identity != animated->identity);
                assert(next->triangles.size() == animated->triangles.size());
                movedPose = true;
            }
        assert(movedPose);
        const auto previous = scene->meshes.front();
        auto again = cache.capture(ram.data(), uint32_t(ram.size()), 9);
        assert(again->meshes.front() == previous);
        for (const auto& instance : scene->instances) {
            Reader r{ram.data(), uint32_t(ram.size())};
            assert(r.u(instance.object + 4) & 1);
        }
        const auto object = scene->instances.front().object;
        const auto index = scene->instances.front().mesh;
        const float before = scene->instances.front().transform[3];
        float moved = before + 1000;
        std::memcpy(ram.data() + object + 0x14, &moved, 4);
        auto updated = cache.capture(ram.data(), uint32_t(ram.size()), 9);
        assert(updated->instances.front().transform[3] == moved && scene->instances.front().transform[3] == before);
        assert(updated->meshes[index] == scene->meshes[index]);
        destroyed(object);
        auto retired = cache.capture(ram.data(), uint32_t(ram.size()), 9);
        assert(retired->instances.size() + 1 == updated->instances.size());
        cache.reset();
        auto reenabled = cache.capture(ram.data(), uint32_t(ram.size()), 9);
        assert(reenabled->instances.size() == updated->instances.size());
        std::fill(ram.begin(), ram.end(), 0);
        auto cleared = cache.capture(ram.data(), uint32_t(ram.size()), 10);
        assert(cleared->instances.empty());
    }
    std::cout << "PASS source topology, native UV/material ownership, immutable snapshots, live transforms, "
                 "destruction and level replacement\n";
}
