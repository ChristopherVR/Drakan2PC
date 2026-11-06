// Arokh's flight look-ahead and objects he is already inside.
//
// Every flight tick the dragon (0x271e18) calls 0x274250: a ray 6,144 units ahead along his velocity through the world
// (0x372d38) scales the velocity by clamp((distance - 3072) / 3072, 0, 1) when it hits something. An object hit always
// counts, and an object whose box holds the start of the ray is hit right there (distance 0) or at the far face of its
// box (double-sided polygons): the velocity is zero in every direction and he never gets out. On the disc no flight
// path starts inside an object. A converted Order of the Flame level can: the Grotto to Islands arrival keeps Rynn's
// offset from the exit and puts Arokh inside the PC "Bounding Box" slab on the floor of the arrival tunnel, so W did
// nothing (docs/journal/2026-11-06/ootf-arokh-flight.md). This policy drops a look-ahead hit on an object whose box
// holds the start of the ray: he flies out of it, and anything beyond it still counts once he is out. Pure logic:
// drakan_dragon_flight.cpp reads the hit object's world box (object + 0xb8: minimum corner, then three axes scaled by
// the box size) and applies the result.
#pragma once

namespace drakan_dragon_flight {

// True when `p` lies inside the box with minimum corner `corner` and edge vectors `axes` (3 x 3, row per edge), with
// a small tolerance. A box with a zero edge holds nothing.
inline bool insideBox(const float corner[3], const float axes[9], const float p[3], float tolerance = 1e-3f) {
    const float d[3] = {p[0] - corner[0], p[1] - corner[1], p[2] - corner[2]};
    for (int k = 0; k < 3; ++k) {
        const float* a = axes + 3 * k;
        const float len2 = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
        if (!(len2 > 0.0f))
            return false;
        const float t = (d[0] * a[0] + d[1] * a[1] + d[2] * a[2]) / len2;
        if (t < -tolerance || t > 1.0f + tolerance)
            return false;
    }
    return true;
}

// The look-ahead hit is dropped when it is an object hit (not terrain) and the ray starts inside that object's box.
inline bool dropLookAheadHit(bool hit, bool objectHit, const float corner[3], const float axes[9],
                             const float from[3]) {
    return hit && objectHit && insideBox(corner, axes, from);
}

} // namespace drakan_dragon_flight
