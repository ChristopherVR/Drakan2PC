#include "drakan_scene_models.h"
#include "drakan_scene_assets.h"
#include "drakan_shadow_ownership.h"
#include "model_motion.h"
#include <fstream>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
int main(int argc, char** argv) {
    using namespace drakan_scene_models;
    {
        gsr::SceneLighting lighting;
        assert(!lighting.hasContribution());
        lighting.ambient[0] = std::numeric_limits<float>::quiet_NaN();
        lighting.ambient[1] = std::numeric_limits<float>::infinity();
        lighting.ambient[2] = -1;
        assert(!lighting.hasContribution());
        lighting.ambient[2] = .25f;
        assert(lighting.hasContribution()); // ambient-only worlds must run ray-traced contact occlusion
        lighting.ambient[2] = 0;
        lighting.lights.push_back({});
        assert(lighting.hasContribution()); // preserve the existing direct-light eligibility
    }
    {
        // The frame light set must not pop as the camera re-ranks torches, crosses layers or a terrain hole.
        using C = LightSelection::Candidate;
        LightSelection::Environment env;
        env.ambient[0] = env.ambient[1] = env.ambient[2] = .1f;
        env.hasSun = true;
        env.sun.vector[1] = 1;
        env.sun.color[0] = env.sun.color[1] = env.sun.color[2] = .8f;
        auto run = [](LightSelection& s, const LightSelection::Environment* e, std::vector<C> c) {
            gsr::SceneLighting out;
            s.apply(e, std::move(c), out);
            return out;
        };
        auto red = [](const gsr::SceneLighting& l, uint32_t key) { // the fade weight of that light, 0 when absent
            for (const auto& x : l.lights)
                if (x.type == gsr::SceneLight::Point && x.radiusSquared == float(key) * 1e6f)
                    return x.color[0];
            return 0.f;
        };
        auto tagged = [](uint32_t key, float score) { // the radius identifies the light, color[0] reports its weight
            C c{key, score, {}};
            c.light.type = gsr::SceneLight::Point;
            c.light.color[0] = 1;
            c.light.radiusSquared = float(key) * 1e6f;
            return c;
        };
        LightSelection s;
        std::vector<C> four{tagged(1, 10), tagged(2, 9), tagged(3, 8), tagged(4, 7)};
        auto first = run(s, &env, four); // the first capture of a level is exact: no fade-in
        assert(first.lights.size() == 4 && first.lights[0].type == gsr::SceneLight::Directional);
        assert(red(first, 1) == 1 && red(first, 2) == 1 && red(first, 3) == 1 && red(first, 4) == 0);
        assert(first.ambient[0] == .1f);
        // Light 4 now outranks light 3 by less than kHold: no change.
        four[3].score = 8 * 1.4f;
        auto held = run(s, &env, four);
        assert(red(held, 3) == 1 && red(held, 4) == 0);
        // Clearly stronger: light 3 fades out first, then 4 fades in; never more than three point lights.
        four[3].score = 8 * 2.f;
        float previous3 = 1, previous4 = 0;
        bool swapped = false;
        for (unsigned frame = 0; frame < 40; ++frame) {
            auto l = run(s, &env, four);
            assert(l.lights.size() <= 4);
            float w3 = red(l, 3), w4 = red(l, 4);
            assert(w3 <= previous3 + 1e-6f && w4 >= previous4 - 1e-6f);
            assert(previous3 - w3 <= 1.f / LightSelection::kFadeCaptures + 1e-5f);
            assert(w4 - previous4 <= 1.f / LightSelection::kFadeCaptures + 1e-5f);
            previous3 = w3;
            previous4 = w4;
            swapped |= w4 == 1;
        }
        assert(swapped && previous3 == 0);
        // A terrain hole: the last lighting is held, then the former no-lighting result returns.
        for (unsigned frame = 0; frame < LightSelection::kHoldMissing; ++frame) {
            auto l = run(s, nullptr, {});
            assert(l.lights.size() == 4 && l.ambient[0] == .1f);
        }
        auto gone = run(s, nullptr, {});
        assert(gone.lights.empty() && !gone.hasContribution());
        // Back on a layer, then on a darker one: sun and ambient move over several captures, not in one.
        run(s, &env, four);
        auto dark = env;
        dark.ambient[0] = .02f;
        dark.sun.color[0] = .2f;
        auto step = run(s, &dark, four);
        assert(step.ambient[0] < .1f && step.ambient[0] > .02f);
        assert(step.lights[0].color[0] < .8f && step.lights[0].color[0] > .2f);
        for (unsigned frame = 0; frame < 80; ++frame)
            step = run(s, &dark, four);
        assert(std::fabs(step.ambient[0] - .02f) < 1e-3f && std::fabs(step.lights[0].color[0] - .2f) < 1e-3f);
        // A layer without a valid sun fades the old one out instead of dropping it.
        dark.hasSun = false;
        auto fading = run(s, &dark, four);
        assert(fading.lights[0].type == gsr::SceneLight::Directional && fading.lights[0].color[0] > 0);
        for (unsigned frame = 0; frame < 120; ++frame)
            fading = run(s, &dark, four);
        assert(fading.lights[0].type == gsr::SceneLight::Point);
    }
    Capture cache;
    assert(cache.capture(nullptr, 0, 1)->instances.empty());
    {
        // Streaming may replace a model in place before the next level-identity poll. Preserve its address, ID,
        // vertex count and LOD while changing the source topology/UV/material data behind a warmed pose template.
        std::vector<uint8_t> ram(0x520000);
        auto put = [&](uint32_t at, auto value) { std::memcpy(ram.data() + at, &value, sizeof(value)); };
        constexpr uint32_t model = 0x110000, object = 0x120000, controller = 0x130000, vt = 0x140000;
        constexpr uint32_t header = 0x150000, pose = 0x151000, faces = 0x153000, library = 0x154000;
        constexpr uint32_t uv = 0x155000, texture = 0x156000, pixels = 0x157000;
        put(0x4dae20 + 0x20, model);
        put(model, 2u);
        put(model + 8, 3u);
        put(model + 0x18, 0x51cc98u);
        put(model + 0x1c, 4u);
        put(model + 0x20, 2u);
        put(model + 0x24, pose);
        put(model + 0x28, faces);
        put(model + 0x30, uv);
        put(model + 0x38, library);
        put(model + 0x40, 2u);
        put(library, 11u);
        put(library + 4, 12u);
        const uint16_t indices[12] = {0, 1, 2, 0, 0, 0, 0, 2, 3, 0, 0, 0};
        std::memcpy(ram.data() + faces, indices, sizeof(indices));
        const float positions[12] = {0, 0, 0, 10, 0, 0, 10, 10, 0, 0, 10, 0};
        std::memcpy(ram.data() + pose, positions, sizeof(positions));
        put(object, 0x51b8b0u);
        put(object + 4, 1u);
        put(object + 8, 2u);
        put(object + 0x20, 1.f);
        put(object + 0x30, 1.f);
        put(object + 0x40, 1.f);
        put(object + 0x50, controller);
        put(object + 0x114, 1.f);
        put(controller, vt);
        put(controller + 8, 0x100000u);
        put(controller + 0xd4, object);
        put(controller + 0x10c, 0x152000u);
        put(controller + 0x114, 2u);
        put(controller + 0x118, 4u);
        put(controller + 0x120, header);
        put(vt + 0x144, 0x106aa8u);
        put(header, pose);
        put(header + 8, 4u);
        put(0x4db280 + 0x20, texture);
        put(texture, 11u);
        put(texture + 8, 3u);
        put(texture + 0x18, 0x51cfe8u);
        put(texture + 0x24, 1u);
        put(texture + 0x28, 1u);
        put(texture + 0x34, 4u);
        put(texture + 0x38, 32u);
        put(texture + 0x50, pixels);
        put(pixels, 0x80402010u);
        Capture warm;
        const auto first = warm.capture(ram.data(), uint32_t(ram.size()), 7);
        assert(first->instances.size() == 1 && first->meshes[0]->triangles.size() == 2);
        Reader reader{ram.data(), uint32_t(ram.size())};
        auto signature = meshContentSignature(reader, model, 0);
        const auto staticSignature = meshContentSignature(reader, model, 0, true);
        put(pose, 3.f);
        assert(meshContentSignature(reader, model, 0) == signature); // animation is not template metadata
        assert(meshContentSignature(reader, model, 0, true) != staticSignature);
        auto moved = warm.capture(ram.data(), uint32_t(ram.size()), 7);
        assert(moved->meshes[0]->triangles[0].position[0][0] == 3);
        auto compareFresh = [&] {
            Capture cold;
            const auto actual = warm.capture(ram.data(), uint32_t(ram.size()), 7);
            const auto expected = cold.capture(ram.data(), uint32_t(ram.size()), 7);
            assert(actual->meshes.size() == 1 && expected->meshes.size() == 1);
            assert(actual->meshes[0]->identity == expected->meshes[0]->identity);
            assert(actual->meshes[0]->topology == expected->meshes[0]->topology);
            assert(meshContentSignature(reader, model, 0) != signature);
            signature = meshContentSignature(reader, model, 0);
        };
        put(uv, .75f);
        compareFresh();
        put(faces + 2, uint16_t(3));
        compareFresh();
        put(library, 12u);
        compareFresh();
        // Identical pixels do not justify retaining stale alpha/blend flags during a resident refresh.
        put(texture + 0x20, 0x10000u);
        std::shared_ptr<gsr::SceneModels> refreshed;
        for (unsigned i = 0; i < 40; ++i)
            refreshed = warm.capture(ram.data(), uint32_t(ram.size()), 7);
        assert(refreshed->textures.size() == 1 && refreshed->textures[0]->flags == 0x10000);
        assert(first->textures[0]->flags == 0); // queued snapshots remain immutable
        {
            // Capture::fast (the default) against the first implementation's per-capture work (fast = false): a second,
            // static model with large source arrays next to the posed one, the same memory through both captures.
            constexpr uint32_t model2 = 0x160000, object2 = 0x121000, faces2 = 0x161000, uv2 = 0x163000;
            constexpr uint32_t positions2 = 0x165000, faceCount = 200, vertexCount = 16;
            put(0x4dae20 + 0x24, model2);
            put(model2, 3u);
            put(model2 + 8, 3u);
            put(model2 + 0x18, 0x51cc98u);
            put(model2 + 0x1c, vertexCount);
            put(model2 + 0x20, faceCount);
            put(model2 + 0x24, positions2);
            put(model2 + 0x28, faces2);
            put(model2 + 0x30, uv2);
            put(model2 + 0x38, library);
            put(model2 + 0x40, 2u);
            unsigned face = 0;
            for (uint16_t a = 0; a < vertexCount && face < faceCount; ++a)
                for (uint16_t b = a + 1; b < vertexCount && face < faceCount; ++b)
                    for (uint16_t c = b + 1; c < vertexCount && face < faceCount; ++c, ++face) {
                        const uint16_t corners[6] = {a, b, c, 0, uint16_t(face & 1), 0};
                        std::memcpy(ram.data() + faces2 + face * 12, corners, sizeof(corners));
                        for (unsigned v = 0; v < 8; ++v)
                            put(uv2 + face * 32 + v * 4, float(face) + v / 8.f);
                    }
            assert(face == faceCount);
            for (unsigned v = 0; v < vertexCount; ++v)
                for (unsigned c = 0; c < 3; ++c)
                    put(positions2 + v * 12 + c * 4, float(v * 3 + c));
            put(object2, 0x51b8b0u);
            put(object2 + 4, 1u);
            put(object2 + 8, 3u);
            put(object2 + 0x20, 1.f);
            put(object2 + 0x30, 1.f);
            put(object2 + 0x40, 1.f);
            put(object2 + 0x114, 1.f);
            Capture quick, plain;
            plain.fast = false;
            assert(quick.fast); // DRAKAN_SCENE_CAPTURE_FAST is not set in the test environment
            auto equal = [](const gsr::SceneModels& x, const gsr::SceneModels& y) {
                if (x.instances.size() != y.instances.size() || x.meshes.size() != y.meshes.size() ||
                    x.untexturedTriangles != y.untexturedTriangles || x.rejectedInstances != y.rejectedInstances)
                    return false;
                for (size_t i = 0; i < x.instances.size(); ++i)
                    if (x.instances[i].object != y.instances[i].object ||
                        x.instances[i].generation != y.instances[i].generation ||
                        x.instances[i].mesh != y.instances[i].mesh ||
                        std::memcmp(x.instances[i].transform, y.instances[i].transform,
                                    sizeof(x.instances[i].transform)))
                        return false;
                for (size_t i = 0; i < x.meshes.size(); ++i)
                    if (x.meshes[i]->identity != y.meshes[i]->identity ||
                        x.meshes[i]->topology != y.meshes[i]->topology ||
                        x.meshes[i]->triangles.size() != y.meshes[i]->triangles.size())
                        return false;
                return true;
            };
            // Both captures share the live set: the second one's level change rescans it, so start both first.
            quick.capture(ram.data(), uint32_t(ram.size()), 7);
            plain.capture(ram.data(), uint32_t(ram.size()), 7);
            size_t staticMesh = ~size_t(0);
            for (unsigned frame = 0; frame < 48; ++frame) {
                put(pose, float(frame)); // a new pose every capture
                if (frame == 20)
                    put(object2 + 0x14, 250.f); // the static instance moves
                const auto a = quick.capture(ram.data(), uint32_t(ram.size()), 7);
                const auto b = plain.capture(ram.data(), uint32_t(ram.size()), 7);
                assert(a->instances.size() == 2 && a->meshes.size() == 2);
                assert(equal(*a, *b));
                for (const auto& instance : a->instances)
                    if (instance.object == object2)
                        staticMesh = instance.mesh;
            }
            assert(staticMesh != ~size_t(0));
            {
                // Motion-only capture (no ray effect reads the snapshot) against the full one: the model-motion
                // consumer must produce the same vertices from both, world after world, while the static model stands
                // (left out), starts to move after standing (it brings its previous transform), keeps moving, stops,
                // is hidden and comes back somewhere else (no previous transform may be invented for that).
                Capture full, lean;
                lean.motionOnly = true;
                gsr::motion::Models fullHistory, leanHistory;
                full.capture(ram.data(), uint32_t(ram.size()), 7);
                lean.capture(ram.data(), uint32_t(ram.size()), 7);
                unsigned leftOut = 0, carried = 0, moving = 0;
                size_t stillVertices = 0;
                for (unsigned frame = 0; frame < 40; ++frame) {
                    put(pose, float(100 + frame));
                    if (frame == 10 || frame == 11 || frame == 12 || frame == 25)
                        put(object2 + 0x14, 300.f + float(frame));
                    if (frame == 32)
                        put(object2 + 4, 0u);
                    if (frame == 34) {
                        put(object2 + 4, 1u);
                        put(object2 + 0x14, 900.f);
                    }
                    const auto a = full.capture(ram.data(), uint32_t(ram.size()), 7);
                    const auto b = lean.capture(ram.data(), uint32_t(ram.size()), 7);
                    assert(!a->motionOnly && b->motionOnly);
                    assert(a->instances.size() == b->instances.size() + b->unchangedInstances);
                    const auto fa = fullHistory.prepare(*a, false), fb = leanHistory.prepare(*b, false);
                    assert(fa.matched == fb.matched && fa.vertices.size() == fb.vertices.size());
                    for (size_t v = 0; v < fa.vertices.size(); ++v)
                        assert(!std::memcmp(fa.vertices[v].current, fb.vertices[v].current, 12) &&
                               !std::memcmp(fa.vertices[v].previous, fb.vertices[v].previous, 12) &&
                               !std::memcmp(fa.vertices[v].uv, fb.vertices[v].uv, 8));
                    const bool moved = frame == 10 || frame == 11 || frame == 12 || frame == 25;
                    if (moved) {
                        // the posed model and the moved static one both have motion; the first move after standing
                        // still and the one at 25 rely on the carried transform
                        assert(fa.vertices.size() > stillVertices);
                        ++moving;
                    } else if (frame == 5)
                        stillVertices = fa.vertices.size();
                    if (frame == 34) // back from hidden: no previous position, as in the full snapshot
                        assert(fa.vertices.size() == stillVertices);
                    leftOut += b->unchangedInstances;
                    for (const auto& instance : b->instances)
                        carried += instance.hasPreviousTransform;
                    fullHistory.commit(a);
                    leanHistory.commit(b);
                }
                assert(moving == 4 && leftOut >= 30 && carried == 4);
                put(object2 + 0x14, 250.f);
                quick.capture(ram.data(), uint32_t(ram.size()), 7);
                plain.capture(ram.data(), uint32_t(ram.size()), 7);
            }
            auto cold = [&] {
                // (its level change renumbers the live set; the other captures pick that up with their next call)
                Capture fresh;
                fresh.fast = false;
                return fresh.capture(ram.data(), uint32_t(ram.size()), 7);
            };
            // A value in the middle of a long unchanged array (outside the sampled blocks): the first implementation
            // sees it at once, the fast capture within one full-check period (16 captures). Three rounds at different
            // phases of that period, so at least one of them is really late.
            unsigned late = 0;
            for (unsigned round = 0; round < 3; ++round) {
                for (unsigned frame = 0; frame < 21; ++frame) {
                    quick.capture(ram.data(), uint32_t(ram.size()), 7);
                    plain.capture(ram.data(), uint32_t(ram.size()), 7);
                }
                put(uv2 + 288, 77.f + round); // the first S value of face 9
                const auto changed = cold();
                assert(equal(*plain.capture(ram.data(), uint32_t(ram.size()), 7), *changed));
                unsigned waited = 0;
                while (!equal(*quick.capture(ram.data(), uint32_t(ram.size()), 7), *changed))
                    assert(++waited <= 16);
                late = std::max(late, waited);
                // The source is checked in full again until it has settled, so an edit right after is seen at once.
                put(uv2 + 288, 177.f + round);
                assert(equal(*quick.capture(ram.data(), uint32_t(ram.size()), 7),
                             *plain.capture(ram.data(), uint32_t(ram.size()), 7)));
            }
            assert(late > 0);
            for (unsigned frame = 0; frame < 24; ++frame) {
                quick.capture(ram.data(), uint32_t(ram.size()), 7);
                plain.capture(ram.data(), uint32_t(ram.size()), 7);
            }
            // A sampled block (the start of the face array, as when another model is streamed in over this one) is
            // seen by the very next capture.
            put(faces2 + 4, uint16_t(5));
            assert(equal(*quick.capture(ram.data(), uint32_t(ram.size()), 7),
                         *plain.capture(ram.data(), uint32_t(ram.size()), 7)));
            // Objects that come and go through the engine hooks reach the flat copy of the live set.
            enabled = true;
            destroyed(object2);
            auto without = quick.capture(ram.data(), uint32_t(ram.size()), 7);
            assert(without->instances.size() == 1 &&
                   equal(*without, *plain.capture(ram.data(), uint32_t(ram.size()), 7)));
            constructed(object2);
            auto again = quick.capture(ram.data(), uint32_t(ram.size()), 7);
            assert(again->instances.size() == 2 && equal(*again, *plain.capture(ram.data(), uint32_t(ram.size()), 7)));
            enabled = false;
            std::cout << "Fast capture equals the per-capture checks (posed and static models, " << late
                      << " captures late for an unsampled in-place edit)\n";
        }
        live.clear();
        generations.clear();
        ++liveEpoch;
        std::cout << "Same-address pose templates track UV/topology/material replacement; equal-pixel flags refresh\n";
    }
    {
        auto source = std::make_shared<gsr::SceneModels>();
        gsr::SceneModelInstance actor;
        actor.object = 0x100000;
        actor.generation = 7;
        source->instances.push_back(actor);
        actor.object += 0x120;
        source->instances.push_back(actor);
        drakan_shadow::Frame frame;
        frame.record(source->instances[0].object, 7); // outside a world: ignored
        frame.begin(source);
        frame.record(source->instances[0].object, 6); // stale object lifetime: ignored
        assert(!frame.finish()->instances[0].nativeShadow);
        frame.begin(source);
        frame.record(source->instances[0].object, 7);
        auto result = frame.finish();
        assert(result->instances[0].nativeShadow && !result->instances[1].nativeShadow);
        assert(!source->instances[0].nativeShadow);   // queued earlier snapshot remains immutable
        assert(!frame.finish());                      // a second end-world does not republish an old result
        frame.record(source->instances[1].object, 7); // after end-world: ignored
        frame.begin(source);
        assert(!frame.finish()->instances[0].nativeShadow);
        frame.begin(source);
        frame.record(source->instances[0].object, 7);
        frame.begin(nullptr); // disabled/reset/new world invalidates pending projection
        assert(!frame.finish());
    }
    {
        std::vector<uint8_t> memory(0x101000);
        Reader reader{memory.data(), uint32_t(memory.size())};
        const auto put = [&](uint32_t at, auto value) { std::memcpy(memory.data() + at, &value, sizeof(value)); };
        constexpr uint32_t object = 0x100000, database = 0x100200, records = 0x100300;
        put(database + 8, records);
        put(database + 0x10, 1u); // native resource limit is inclusive
        put(object + 0x10, uint16_t(1));
        put(records + 0x24 + 8, 0x1001au); // sky, even when its geometry is only a pair of billboards
        assert(reader.resourceClass(object, database) == 0x1001a);
        put(records + 0x24 + 8, 0x100d3u); // ordinary scenery keeps its class
        assert(reader.resourceClass(object, database) == 0x100d3);
        put(object + 0x10, uint16_t(2));
        assert(reader.resourceClass(object, database) == 0);
        put(object + 0x10, uint16_t(0xffff));
        assert(reader.resourceClass(object, database) == 0);
        put(object + 0x10, uint16_t(1));
        put(database + 8, 0xfffffff0u);
        assert(reader.resourceClass(object, database) == 0);
        assert(reader.resourceClass(object, 0) == 0);
        assert(reader.resourceClass(uint32_t(memory.size() - 4), database) == 0);
    }
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
        // captureHashWide: deterministic, covers every byte (blocks and tail) and the length, and depends on the seed.
        std::vector<uint8_t> bytes(64 * 1024 + 13);
        uint32_t state = 12345;
        for (auto& b : bytes) {
            state = state * 1664525u + 1013904223u;
            b = uint8_t(state >> 24);
        }
        constexpr uint64_t seed = 14695981039346656037ull;
        const auto base = drakan_object_motion::captureHashWide(seed, bytes.data(), bytes.size());
        assert(base == drakan_object_motion::captureHashWide(seed, bytes.data(), bytes.size()));
        assert(base != drakan_object_motion::captureHashWide(seed + 1, bytes.data(), bytes.size()));
        assert(base != drakan_object_motion::captureHashWide(seed, bytes.data(), bytes.size() - 1));
        for (size_t at : {size_t(0), size_t(1), size_t(7), size_t(8), size_t(31), size_t(32), size_t(33), size_t(65535),
                          bytes.size() - 13, bytes.size() - 12, bytes.size() - 1}) {
            bytes[at] ^= 1;
            assert(base != drakan_object_motion::captureHashWide(seed, bytes.data(), bytes.size()));
            bytes[at] ^= 1;
        }
        assert(base == drakan_object_motion::captureHashWide(seed, bytes.data(), bytes.size()));
        assert(drakan_object_motion::captureHashWide(seed, nullptr, 0) ==
               drakan_object_motion::captureHashWide(seed, bytes.data(), 0));
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
        // The resident-texture refresh runs in slices over several captures; unchanged memory must settle on the very
        // same textures (identity and order) as the first, synchronous pass.
        for (unsigned i = 0; i < 2000; ++i)
            cache.capture(ram.data(), uint32_t(ram.size()), 9);
        auto settled = cache.capture(ram.data(), uint32_t(ram.size()), 9);
        assert(settled->textures.size() == scene->textures.size());
        for (size_t i = 0; i < settled->textures.size(); ++i)
            assert(settled->textures[i]->identity == scene->textures[i]->identity);
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
