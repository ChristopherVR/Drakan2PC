// Point lights per model ([video] model_lights): pure rules of the light set builder, unit tested in
// world_model_lights_test.
//
// The engine lights a model with a set of directional lights that its light set builder (PAL 0x1c4148) makes once per
// object: slot 0 is the sun, then the point lights whose sphere reaches the object's box, nearest first, while the set
// holds fewer than the caller's limit. Models ask for 4 (the sun and the 3 nearest point lights), because the model
// upload writes exactly four slots into the VU1 light block. A point light is turned into a directional light at the
// object: the direction from the object's centre to the light and the colour scaled by 1 - d^2 / r^2 at that centre
// (lights that reach less than 10 % are dropped). Big objects next to a light keep a real point light (kind Point).
//
// The option keeps the engine's own set exactly as it is and adds the lights the limit cut off: into a free slot when
// the set has one (as the engine would have), otherwise folded into the slot whose direction is closest (directions
// weighted by brightness, colours added). So the VU1 light block keeps its four slots and nothing else changes.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace world::model_lights {

inline constexpr int kOriginal = 4;       // lights per model on the disc: the sun and three point lights
inline constexpr int kMax = 16;           // slots of the engine's light set
inline constexpr int kSlots = 4;          // slots the model upload writes into VU1 [13..20]
inline constexpr float kMaxColour = 2.0f; // the upload packs a colour as (c * 255 / 2) in 8 bits: 2.0 is the most

// The option value: 4 (or anything at or below it) = the original, otherwise up to 16 lights per model.
inline int clampSetting(int value) {
    return value <= kOriginal ? kOriginal : std::min(value, kMax);
}

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// A light of the level's light list: colour at +0, radius at +0xc, position at +0x10.
struct Light {
    Vec3 colour;
    float radius = 0;
    Vec3 position;
};

// The object transform the builder may get (its t3): origin at +0, 3 x 3 rotation at +0xc, scale at +0x30.
struct Transform {
    bool present = false;
    Vec3 origin;
    float rotation[9]{};
    Vec3 scale{1, 1, 1};
};

// The object: its bounding sphere (t1) and the builder's mode word (t0, nonzero = always directional).
struct Object {
    Vec3 centre;
    float radius = 0;
    bool directionalOnly = false;
};

enum class Kind { Skip, Reject, Directional, Point };

struct Evaluated {
    Kind kind = Kind::Skip;
    Vec3 vector;           // Directional: unit direction from the object to the light; Point: the light's position
    Vec3 direction;        // unit direction from the object to the light (both kinds)
    Vec3 colour;           // Directional: attenuated at the centre; Point: the light's own colour
    float attenuation = 0; // 1 - d^2 / r^2 at the centre (clamped at 0)
    float radiusSquared = 0, inverseRadiusSquared = 0;
};

inline bool accepted(Kind kind) {
    return kind == Kind::Directional || kind == Kind::Point;
}

// What the builder's accept loop does with one candidate (0x1c4b18 .. 0x1c4efc), in the same float steps.
inline Evaluated evaluate(const Light& light, const Transform& transform, const Object& object) {
    Evaluated out;
    float radius = light.radius;
    Vec3 position = light.position;
    if (transform.present) {
        radius *= transform.scale.x;
        const Vec3 v = position - transform.origin; // 0x13f498: transposed rotation, then the scale squared
        const float* m = transform.rotation;
        position.x = (v.x * m[0] + v.y * m[3] + v.z * m[6]) * (transform.scale.x * transform.scale.x);
        position.y = (v.x * m[1] + v.y * m[4] + v.z * m[7]) * (transform.scale.y * transform.scale.y);
        position.z = (v.x * m[2] + v.y * m[5] + v.z * m[8]) * (transform.scale.z * transform.scale.z);
    }
    const Vec3 d = position - object.centre;
    const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const float inverse = 1.0f / distance;
    out.direction = d * inverse;
    if (!(object.radius + radius > distance)) // outside, or not a number
        return out;
    const float distanceSquared = distance * distance;
    out.radiusSquared = radius * radius;
    out.inverseRadiusSquared = 1.0f / out.radiusSquared;
    out.attenuation = std::max(0.0f, 1.0f - distanceSquared * out.inverseRadiusSquared);
    const float objectSquared = object.radius * object.radius;
    bool point = false;
    if (!object.directionalOnly) {
        const float limit = (transform.present ? transform.scale.x : 1.0f) * 1024.0f;
        if (!(objectSquared < limit * limit))
            point = true;
        else if (!(objectSquared * 8.0f < distanceSquared))
            point = double(objectSquared) > double(out.radiusSquared) * 0.25;
    }
    if (point) {
        out.kind = Kind::Point;
        out.vector = position;
        out.colour = light.colour;
        return out;
    }
    if (out.radiusSquared < distanceSquared) {
        out.kind = Kind::Reject;
        return out;
    }
    const float attenuation = 1.0f - distanceSquared * out.inverseRadiusSquared;
    if (attenuation <= 0.1f) {
        out.kind = Kind::Reject;
        return out;
    }
    out.kind = Kind::Directional;
    out.vector = out.direction;
    out.colour = light.colour * attenuation;
    return out;
}

// One slot of the light set (0x34 bytes from +0x24).
struct Slot {
    uint32_t type = 0;                                 // 0 directional, 1 point
    Vec3 vector;                                       // +4: direction (type 0) or position (type 1)
    Vec3 colour;                                       // +0x10
    float radiusSquared = 0, inverseRadiusSquared = 0; // +0x1c, +0x20
    Vec3 half;                                         // +0x24: normalised eye + direction (specular)
    uint32_t specular = 0;                             // +0x30
};

// The light set: +0 specular on, +4 eye direction, +0x10 has a point light, +0x14 ambient, +0x20 count.
struct Set {
    uint32_t specular = 0;
    Vec3 eye;
    uint32_t hasPoint = 0;
    Vec3 ambient;
    int count = 0;
    Slot slots[kSlots];
};

inline float brightness(Vec3 c) {
    return std::max(0.0f, c.x) + std::max(0.0f, c.y) + std::max(0.0f, c.z);
}

inline Vec3 clampColour(Vec3 c) {
    return {std::min(c.x, kMaxColour), std::min(c.y, kMaxColour), std::min(c.z, kMaxColour)};
}

// The builder's tail for an accepted slot (0x1c4e10 .. 0x1c4ef8): the specular half vector.
inline void finishSlot(const Set& set, Slot& slot) {
    if (!set.specular)
        return;
    if (slot.type != 0) {
        slot.specular = 1;
        return;
    }
    if (!(dot(slot.colour, slot.colour) >= 0.0004f))
        return;
    const Vec3 h = set.eye + slot.vector;
    const float length = std::sqrt(dot(h, h));
    if (length > 0)
        slot.half = h * (1.0f / length);
    slot.specular = 1;
}

// Adds one light the limit cut off. Returns 1 when it took a free slot, 2 when it was folded into a slot, 0 when it
// had nothing to add.
inline int add(Set& set, const Evaluated& light) {
    if (!accepted(light.kind))
        return 0;
    if (set.count >= 1 && set.count < kSlots) { // a free slot: what the engine would have done
        Slot& slot = set.slots[set.count++];
        slot = Slot{};
        slot.type = light.kind == Kind::Point ? 1u : 0u;
        slot.vector = light.vector;
        slot.colour = light.colour;
        slot.radiusSquared = light.radiusSquared;
        slot.inverseRadiusSquared = light.inverseRadiusSquared;
        if (slot.type)
            set.hasPoint = 1;
        finishSlot(set, slot);
        return 1;
    }
    // folded: as a directional light at the centre (a point light gets the same attenuation a directional one has)
    const Vec3 colour = light.kind == Kind::Point ? light.colour * light.attenuation : light.colour;
    const float weight = brightness(colour);
    if (!(weight > 0))
        return 0;
    int best = -1;
    float bestDot = -2.0f;
    for (int i = 1; i < std::min(set.count, kSlots); ++i) { // the point light slots, never the sun
        const Slot& s = set.slots[i];
        if (s.type != 0)
            continue;
        const float d = dot(s.vector, light.direction);
        if (d > bestDot)
            bestDot = d, best = i;
    }
    if (best < 0) { // no directional point light slot: half of it as ambient light
        set.ambient = clampColour(set.ambient + colour * 0.5f);
        return 2;
    }
    Slot& slot = set.slots[best];
    const float own = brightness(slot.colour);
    const Vec3 sum = slot.vector * own + light.direction * weight;
    const float length = std::sqrt(dot(sum, sum));
    if (length > 1e-6f)
        slot.vector = sum * (1.0f / length);
    slot.colour = clampColour(slot.colour + colour);
    finishSlot(set, slot);
    return 2;
}

} // namespace world::model_lights
