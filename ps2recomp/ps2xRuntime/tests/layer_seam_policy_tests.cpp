// drakan_layer_seam: terrain-layer lookups that leave an interior (issue #6, Surdana spider cellar).
// lookupFrom below stands in for the native lookup (the first listed layer that holds the point; the real one also
// weighs the height), to show which points the native lookup misses.
#include "../src/lib/drakan_layer_seam.h"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

using namespace drakan_layer_seam;

namespace {

// Surdana cellar layers as on the PAL disc (rectangles in cells of 2048 units, `visible` lists as stored), plus the
// outdoor layers above it: 195 (north of z = 464896) and 217 (south of it, west of x = 464896).
struct World {
    std::map<int32_t, LayerRect> rects{
        {4, {223, 221, 3, 4}},     // 453, the room at the stair head
        {5, {225, 225, 1, 2}},     // 452, the corridor from the stair foot to the barrel room
        {8, {227, 228, 4, 2}},     // 449
        {9, {224, 227, 3, 4}},     // 448, the barrel room
        {10, {227, 230, 2, 2}},    // 447, the stair foot
        {11, {225, 232, 4, 1}},    // 446
        {195, {193, 201, 42, 26}}, // outdoor
        {217, {213, 227, 14, 9}},  // outdoor
    };
    std::map<int32_t, std::vector<int32_t>> visible{
        {4, {4, 1, 5, 2, 3, 9, 0, 8, 11, 10}}, {5, {5, 9, 4, 8, 1, 10, 11, 2, 0}},
        {8, {8, 7, 9, 5, 6, 3, 4, 2, 0, 1}},   {9, {9, 5, 8, 10, 11, 7, 4, 1, 2, 0}},
        {10, {10, 11, 9, 205, 5, 4, 2, 1}},    {11, {11, 10, 205, 9, 5, 259, 4, 2, 1}},
        {195, {195, 166, 212, 306, 217}},      {217, {205, 214, 261, 217, 262, 259, 260, 272}},
    };
    int32_t count(int32_t c) const {
        const auto it = visible.find(c);
        return it == visible.end() ? 0 : static_cast<int32_t>(it->second.size());
    }
    int32_t at(int32_t c, int32_t i) const { return visible.at(c)[static_cast<size_t>(i)]; }
    std::optional<LayerRect> rect(int32_t c) const {
        const auto it = rects.find(c);
        if (it == rects.end())
            return std::nullopt;
        return it->second;
    }
    // Stand-in for the native lookup: the first listed layer that contains the point (the real one also weighs the
    // height; the policy only depends on "found / not found" and on containment).
    int32_t lookupFrom(int32_t cell, float x, float z) const {
        const auto it = visible.find(cell);
        if (it == visible.end())
            return kNoCell;
        for (int32_t c : it->second)
            if (const auto r = rect(c); r && containsPoint(*r, x, z))
                return c;
        return kNoCell;
    }
    bool near(int32_t cell, float x, float z, float slack = kEdgeSlack) const {
        return nearListedLayers(
            cell, x, z, [&](int32_t c) { return count(c); }, [&](int32_t c, int32_t i) { return at(c, i); },
            [&](int32_t c) { return rect(c); }, slack);
    }
    bool foreign(int32_t last, int32_t found, float x, float z) const {
        return foreignLayer(
            last, found, x, z, [&](int32_t c) { return count(c); }, [&](int32_t c, int32_t i) { return at(c, i); },
            [&](int32_t c) { return rect(c); });
    }
};

} // namespace

int main() {
    const World w;
    // The native lookup from a cellar layer finds nothing just outside it (the stair-head wall of 453 at x 462853, the
    // camera in the wall beside 452, the seam 447 / 449 whose layers do not list each other).
    assert(w.lookupFrom(4, 462853.1f, 458492.3f) == kNoCell);
    assert(w.lookupFrom(5, 460670.0f, 464448.0f) == kNoCell);
    assert(w.lookupFrom(10, 465900.0f, 471036.0f) == kNoCell);

    // nearListedLayers: the camera points of the baseline runs that broke the picture, 130 to 230 units into the wall
    // west of 452, and the stair-head lean of 453. 600 units out (a gap behind the wall) is not near.
    assert(w.near(5, 460670.0f, 464448.0f));
    assert(w.near(5, 460570.0f, 464822.0f));
    assert(w.near(4, 462868.0f, 458500.0f));
    assert(!w.near(4, 463448.0f, 458500.0f));
    assert(!w.near(kNoCell, 460670.0f, 464448.0f));

    // foreignLayer: the search over all layers from -1 returned the outdoor layer above the cellar.
    assert(w.foreign(4, 195, 462853.1f, 458492.3f));        // the stair-head hit: refused
    assert(w.foreign(5, 195, 460670.0f, 464448.0f));        // the camera beside the corridor, north of z 464896
    assert(w.foreign(9, 217, 458757.0f, 467008.0f));        // the camera at the kegs, south of it
    assert(w.foreign(10, 8, 465900.0f, 471036.0f));         // 447 / 449 do not list each other (walled in play)
    assert(!w.foreign(217, 205, 462000.0f, 478000.0f));     // 217 lists 205: adjacent, allowed
    assert(!w.foreign(5, 9, 461000.0f, 465000.0f));         // listed directly
    assert(!w.foreign(4, 4, 460000.0f, 455000.0f));         // the same layer
    assert(!w.foreign(kNoCell, 195, 462853.1f, 458492.3f)); // no last layer: the engine's search stands
    assert(!w.foreign(4, kNoCell, 462853.1f, 458492.3f));
    assert(!w.foreign(4, 195, 300000.0f, 300000.0f)); // far away (a teleport): the engine's search stands

    // viewReferenceLayer: after its own miss the view is judged against the layer it lost; after the engine reset it
    // to -1 (the camera cut of v7-barrel-s11: camera 150 to 360 units into the wall east of 452, the search from -1
    // returned 195 every frame) against the controlled actor's layer, or the one she had before she lost it.
    assert(viewReferenceLayer(9, 5, kNoCell) == 9);
    assert(viewReferenceLayer(kNoCell, 5, 4) == 5);
    assert(viewReferenceLayer(kNoCell, kNoCell, 4) == 4);
    assert(viewReferenceLayer(kNoCell, kNoCell, kNoCell) == kNoCell);
    assert(w.lookupFrom(5, 463200.0f, 462700.0f) == kNoCell);
    assert(w.foreign(viewReferenceLayer(kNoCell, 5, kNoCell), 195, 463200.0f, 462700.0f));
    assert(!w.foreign(viewReferenceLayer(kNoCell, kNoCell, kNoCell), 195, 463200.0f, 462700.0f));

    // rewindTooFar: the logged rewinds of 1033 to 12554 units are refused, a normal one-step put-back is not.
    assert(rewindTooFar(-20.0f, -2316.0f));
    assert(rewindTooFar(782.0f, -977.0f));
    assert(!rewindTooFar(12.0f, -9.0f));
    assert(!rewindTooFar(300.0f, 300.0f));

    // teleported: a step at the west wall (a few units) is not, a `place` from the cellar to the level start, into the
    // house, or straight up onto the outdoor surface above the cellar (22600 units up) is, and the search after it is
    // never refused even when the target lies inside the listed area of her last layer.
    assert(!teleported(-12.0f, 3.0f, 9.0f));
    assert(!teleported(-1400.0f, 0.0f, 0.0f));
    assert(teleported(270761.0f - 461500.0f, 202400.0f - 158400.0f, 262998.0f - 462000.0f));
    assert(teleported(0.0f, 22600.0f, 0.0f));
    assert(teleported(457000.0f - 461500.0f, 2200.0f, 487000.0f - 462000.0f));
    assert(w.foreign(5, 195, 461500.0f, 462000.0f)); // same x / z as the corridor: only the teleport test lets it pass

    // distanceToRect and the containment test (truncation like the engine)
    {
        const LayerRect r{225, 225, 1, 2}; // x 460800..462848, z 460800..464896
        assert(distanceToRect(r, 461000.0f, 462000.0f) == 0.0f);
        assert(distanceToRect(r, 460700.0f, 462000.0f) == 100.0f);
        const float d = distanceToRect(r, 462948.0f, 465000.0f);
        assert(d > 144.0f && d < 145.0f);
        const LayerRect s{227, 230, 2, 2}; // x 464896..468992, z 471040..475136
        assert(containsPoint(s, 464896.0f, 471040.0f));
        assert(!containsPoint(s, 464895.0f, 471040.0f));
        assert(!containsPoint(s, 464896.0f, 471039.0f));
        assert(containsPoint(s, 468991.0f, 475135.0f));
        assert(!containsPoint(s, 468992.0f, 475000.0f));
    }
    std::cout << "layer_seam_policy: ok\n";
    return 0;
}
