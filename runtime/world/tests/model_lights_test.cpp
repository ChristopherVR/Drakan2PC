// Point lights per model (world/ModelLights.h): pure functions, synthetic numbers only.
#include <cmath>
#include <cstdio>

#include "world/ModelLights.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

namespace ml = world::model_lights;

ml::Set fourSlots() {
    ml::Set set;
    set.count = 4;
    set.slots[0].vector = {0, 1, 0}; // sun from above
    set.slots[0].colour = {0.5f, 0.5f, 0.5f};
    set.slots[1].vector = {1, 0, 0};
    set.slots[1].colour = {0.4f, 0.2f, 0.1f};
    set.slots[2].vector = {-1, 0, 0};
    set.slots[2].colour = {0.4f, 0.2f, 0.1f};
    set.slots[3].vector = {0, 0, 1};
    set.slots[3].colour = {0.4f, 0.2f, 0.1f};
    return set;
}

ml::Evaluated directional(ml::Vec3 direction, ml::Vec3 colour) {
    ml::Evaluated e;
    e.kind = ml::Kind::Directional;
    e.vector = e.direction = direction;
    e.colour = colour;
    e.attenuation = 0.5f;
    return e;
}

} // namespace

int main() {
    // the setting: 4 and below = original, capped at the 16 slots of the set
    CHECK(ml::clampSetting(4) == 4 && ml::clampSetting(0) == 4 && ml::clampSetting(-3) == 4);
    CHECK(ml::clampSetting(8) == 8 && ml::clampSetting(16) == 16 && ml::clampSetting(99) == 16);

    // a torch 300 away with a reach of 1000: directional at the centre, attenuated by 1 - 0.09
    ml::Light torch{{1.0f, 0.5f, 0.25f}, 1000.0f, {300.0f, 0, 0}};
    ml::Object model{{0, 0, 0}, 50.0f, true};
    ml::Evaluated e = ml::evaluate(torch, ml::Transform{}, model);
    CHECK(e.kind == ml::Kind::Directional);
    CHECK(near(e.vector.x, 1) && near(e.vector.y, 0) && near(e.vector.z, 0));
    CHECK(near(e.colour.x, 0.91f) && near(e.colour.y, 0.455f) && near(e.colour.z, 0.2275f));
    CHECK(near(e.radiusSquared, 1.0e6f) && near(e.inverseRadiusSquared, 1.0e-6f, 1e-9f));

    // out of reach of the sphere: skipped; reaching the sphere but not the centre: rejected; under 10 %: rejected
    CHECK(ml::evaluate({{1, 1, 1}, 100.0f, {200.0f, 0, 0}}, {}, model).kind == ml::Kind::Skip);
    CHECK(ml::evaluate({{1, 1, 1}, 100.0f, {120.0f, 0, 0}}, {}, model).kind == ml::Kind::Reject);
    CHECK(ml::evaluate({{1, 1, 1}, 100.0f, {96.0f, 0, 0}}, {}, model).kind == ml::Kind::Reject);
    CHECK(ml::evaluate({{1, 1, 1}, 100.0f, {90.0f, 0, 0}}, {}, model).kind == ml::Kind::Directional);

    // a big object next to the light keeps a real point light unless the caller asks for directional lights only
    ml::Object hall{{0, 0, 0}, 2000.0f, false};
    ml::Evaluated p = ml::evaluate({{1, 1, 1}, 1500.0f, {500.0f, 0, 0}}, {}, hall);
    CHECK(p.kind == ml::Kind::Point && near(p.vector.x, 500.0f) && near(p.colour.x, 1.0f));
    ml::Object crate{{0, 0, 0}, 300.0f, false};
    CHECK(ml::evaluate({{1, 1, 1}, 400.0f, {350.0f, 0, 0}}, {}, crate).kind == ml::Kind::Point); // r^2 > R^2 / 4
    CHECK(ml::evaluate({{1, 1, 1}, 1000.0f, {350.0f, 0, 0}}, {}, crate).kind == ml::Kind::Directional);
    hall.directionalOnly = true;
    CHECK(ml::evaluate({{1, 1, 1}, 1500.0f, {500.0f, 0, 0}}, {}, hall).kind == ml::Kind::Directional);

    // a transform moves the light into object space and scales the reach
    ml::Transform moved;
    moved.present = true;
    moved.origin = {1000.0f, 0, 0};
    moved.rotation[0] = moved.rotation[4] = moved.rotation[8] = 1.0f;
    ml::Evaluated t = ml::evaluate({{1, 1, 1}, 1000.0f, {1300.0f, 0, 0}}, moved, model);
    CHECK(t.kind == ml::Kind::Directional && near(t.colour.x, 0.91f));
    moved.scale = {2, 2, 2}; // position x 4 (scale squared), reach x 2
    t = ml::evaluate({{1, 1, 1}, 1000.0f, {1300.0f, 0, 0}}, moved, model);
    CHECK(t.kind == ml::Kind::Directional && near(t.colour.x, 1.0f - (1200.0f * 1200.0f) / (2000.0f * 2000.0f)));

    // a free slot takes the light as the engine would have
    ml::Set three = fourSlots();
    three.count = 3;
    CHECK(ml::add(three, directional({0, 0, -1}, {0.3f, 0.3f, 0.3f})) == 1);
    CHECK(three.count == 4 && three.slots[3].type == 0 && near(three.slots[3].vector.z, -1) &&
          near(three.slots[3].colour.x, 0.3f));
    ml::Set pointSet = fourSlots();
    pointSet.count = 2;
    CHECK(ml::add(pointSet, p) == 1 && pointSet.count == 3 && pointSet.slots[2].type == 1 && pointSet.hasPoint == 1);

    // a full set: folded into the closest point-light slot, never the sun's, four slots stay four
    ml::Set full = fourSlots();
    CHECK(ml::add(full, directional({0.8f, 0.6f, 0}, {0.4f, 0.2f, 0.1f})) == 2);
    CHECK(full.count == 4);
    CHECK(near(full.slots[1].colour.x, 0.8f) && near(full.slots[1].colour.y, 0.4f));
    CHECK(near(full.slots[1].vector.x, 0.9f / std::sqrt(0.81f + 0.09f)) && full.slots[1].vector.y > 0);
    CHECK(near(full.slots[0].vector.y, 1) && near(full.slots[0].colour.x, 0.5f)); // the sun is untouched
    CHECK(near(full.slots[2].colour.x, 0.4f) && near(full.slots[3].colour.x, 0.4f));
    // straight up is closest to the sun, but the sun is never a target: the nearest point light slot takes it
    ml::Set up = fourSlots();
    CHECK(ml::add(up, directional({0, 1, 0}, {0.1f, 0.1f, 0.1f})) == 2 && near(up.slots[0].colour.x, 0.5f));
    // the packed colour cannot pass 2.0
    ml::Set bright = fourSlots();
    for (int i = 0; i < 8; ++i)
        ml::add(bright, directional({1, 0, 0}, {1, 1, 1}));
    CHECK(bright.slots[1].colour.x == ml::kMaxColour && bright.slots[1].colour.z == ml::kMaxColour);
    // a point light folded in brings its attenuation at the centre
    ml::Set withPoint = fourSlots();
    ml::Evaluated pt = p;
    pt.direction = {1, 0, 0};
    pt.attenuation = 0.25f;
    CHECK(ml::add(withPoint, pt) == 2 && near(withPoint.slots[1].colour.x, 0.65f));
    // nothing to add: black, or not accepted
    ml::Set same = fourSlots();
    CHECK(ml::add(same, directional({1, 0, 0}, {0, 0, 0})) == 0 && near(same.slots[1].colour.x, 0.4f));
    ml::Evaluated rejected;
    rejected.kind = ml::Kind::Reject;
    CHECK(ml::add(same, rejected) == 0);
    // no directional point-light slot to fold into: half of it goes to the ambient light
    ml::Set onlyPoints = fourSlots();
    for (int i = 1; i < 4; ++i)
        onlyPoints.slots[i].type = 1;
    CHECK(ml::add(onlyPoints, directional({1, 0, 0}, {0.4f, 0.4f, 0.4f})) == 2 && near(onlyPoints.ambient.x, 0.2f));

    // specular: the half vector follows the folded direction
    ml::Set shiny = fourSlots();
    shiny.specular = 1;
    shiny.eye = {0, 0, 1};
    ml::add(shiny, directional({1, 0, 0}, {0.4f, 0.2f, 0.1f}));
    CHECK(shiny.slots[1].specular == 1 && near(shiny.slots[1].half.x, std::sqrt(0.5f)) &&
          near(shiny.slots[1].half.z, std::sqrt(0.5f)));

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("world_model_lights_test: all checks passed\n");
    return g_failed ? 1 : 0;
}
