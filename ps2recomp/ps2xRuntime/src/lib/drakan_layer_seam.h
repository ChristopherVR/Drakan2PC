#pragma once
// Terrain-layer ("cell") lookups that leave an interior (issue #6, Surdana spider cellar: Rynn stuck and the picture
// breaking at "seams in the floor or stairs").
//
// The engine keeps two layer indices that matter here:
// * every actor's layer (actor +0x7c), refiled by 0x1496a0 with the cell lookup 0x14b6b8 (x, y, z, world, old layer,
//   flags 5) at the actor's bounding-sphere centre (+0xe8);
// * the view layer 0x4dade4, updated every frame by 0x112488 with the same lookup at a point just behind the camera.
//   The terrain draw 0x152330 draws that layer's `visible` list.
// The lookup only searches the old layer's `visible` list. In the cellar the walls and the stair corners stand on the
// layers' edges, so the sphere centre or the camera point regularly ends a few units over no listed layer: -1. The
// engine then looks "from no layer" (0x1499c8 for actors, the next 0x112488 call for the view), a search of all 483
// layers. Where an outdoor layer lies above the cellar that search returns it, and it does not list the cellar layers
// back, so the state sticks: Rynn is moved onto the outdoor terrain, or the view keeps drawing the outdoor world from
// below while she is in the cellar. Both reproduced on the v0.1.11 exe;
// docs/journal/2026-11-12/issue6-cellar-glitch.md.
//
// What the port changes (drakan_layer_seam_hook.h), only for the controlled actor and the view layer, and only next to
// the layer they had (within kNearLast of it or a layer it lists; teleports, camera cuts and level loads keep the
// engine's search):
// * actor: a search from -1 may not hand it to a layer that is not adjacent to its last layer (foreignLayer). It
//   stays without a layer for that tick, as the engine already leaves it at walls, and the player update rewinds it
//   and files it again. Nothing in the port moves it, except that a rewind further than kMaxRewind from where it last
//   had a layer is replaced by that position (rewindTooFar).
// * view: a lookup from a known layer that finds nothing keeps that layer while the camera point is within kEdgeSlack
//   of its listed area. Further out the engine's search from -1 runs next frame; if it ends in a layer not adjacent
//   to the one just lost (foreignLayer), the view looks from the lost layer instead, or keeps it. The view layer also
//   arrives at -1 without a miss of its own (0x1435c8 at a level load; another, unidentified store in play); such a
//   search is judged against the controlled actor's layer (viewReferenceLayer) and, when foreign, looks from it.
// Pure logic here; the hooks supply guest access.
#include <cmath>
#include <cstdint>
#include <optional>

namespace drakan_layer_seam {

constexpr int32_t kNoCell = -1;

// A layer's rectangle in cells (runtime layer record +0x14 origin x, +0x18 origin z, +0x1c width, +0x20 height).
struct LayerRect {
    int32_t originX = 0, originZ = 0, width = 0, height = 0;
};

// The containment test of the native lookup: the world position scaled to cells (x / 2048), truncated toward zero
// like cvt.w.s, minus the origin, inside [0, size).
inline bool containsPoint(const LayerRect& r, float x, float z) {
    const int32_t ix = static_cast<int32_t>(x * 0.00048828125f) - r.originX;
    const int32_t iz = static_cast<int32_t>(z * 0.00048828125f) - r.originZ;
    return ix >= 0 && ix < r.width && iz >= 0 && iz < r.height;
}

// Horizontal distance from (x, z) to the rectangle (0 inside).
inline float distanceToRect(const LayerRect& r, float x, float z) {
    const float x0 = static_cast<float>(r.originX) * 2048.0f, x1 = static_cast<float>(r.originX + r.width) * 2048.0f;
    const float z0 = static_cast<float>(r.originZ) * 2048.0f, z1 = static_cast<float>(r.originZ + r.height) * 2048.0f;
    const float dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0.0f);
    const float dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0.0f);
    return std::sqrt(dx * dx + dz * dz);
}

// How far a camera point may lean out of the view layer's listed area and still keep that layer. The baseline runs
// that broke the picture had the camera 4 to 230 units into the walls beside the stairs and the corridor.
constexpr float kEdgeSlack = 384.0f;
// Beyond this distance from the last layer's listed area the engine's own search from -1 stands (a teleport, a
// cut-scene camera).
constexpr float kNearLast = 2048.0f;

template <class VisibleCount, class VisibleAt>
bool lists(int32_t cell, int32_t other, VisibleCount visibleCount, VisibleAt visibleAt) {
    const int32_t n = visibleCount(cell);
    for (int32_t i = 0; i < n; ++i)
        if (visibleAt(cell, i) == other)
            return true;
    return false;
}

// True when (x, z) lies within `slack` of the layer or of a layer it lists.
template <class VisibleCount, class VisibleAt, class RectOf>
bool nearListedLayers(int32_t cell, float x, float z, VisibleCount visibleCount, VisibleAt visibleAt, RectOf rectOf,
                      float slack = kEdgeSlack) {
    if (cell < 0)
        return false;
    const std::optional<LayerRect> own = rectOf(cell);
    if (!own)
        return false;
    if (distanceToRect(*own, x, z) <= slack)
        return true;
    const int32_t n = visibleCount(cell);
    for (int32_t i = 0; i < n; ++i) {
        const std::optional<LayerRect> r = rectOf(visibleAt(cell, i));
        if (r && distanceToRect(*r, x, z) <= slack)
            return true;
    }
    return false;
}

// The search from -1 returned `found`; `lastCell` is the actor's layer before it lost it. True when that result must
// be refused: a different layer, not adjacent to `lastCell` (neither lists the other), while the point is still within
// kNearLast of `lastCell`'s listed area.
template <class VisibleCount, class VisibleAt, class RectOf>
bool foreignLayer(int32_t lastCell, int32_t found, float x, float z, VisibleCount visibleCount, VisibleAt visibleAt,
                  RectOf rectOf) {
    if (lastCell < 0 || found < 0 || found == lastCell || !rectOf(lastCell) || !rectOf(found))
        return false;
    if (lists(lastCell, found, visibleCount, visibleAt) || lists(found, lastCell, visibleCount, visibleAt))
        return false;
    return nearListedLayers(lastCell, x, z, visibleCount, visibleAt, rectOf, kNearLast);
}

// A move further than kNearLast (3D) since the last frame is a teleport (a harness or script placement, a level
// arrival, a cut-scene); the actor rule never refuses the search after one (a far `place` was undone otherwise).
inline bool teleported(float dx, float dy, float dz) {
    return dx * dx + dy * dy + dz * dz > kNearLast * kNearLast;
}

// The player update (0x321258) rewinds an actor without a layer to the position saved at the end of the last frame
// minus a step; at the west wall of 452 that step threw Rynn 1000 to 12500 units (to the stair head, once to the
// outdoor layer 195). A rewind further than this from where she last had a layer is replaced by that position.
constexpr float kMaxRewind = 512.0f;
inline bool rewindTooFar(float dx, float dz) {
    return dx * dx + dz * dz > kMaxRewind * kMaxRewind;
}

// The layer a view search from -1 is judged against: the view layer whose own lookup just failed, else (the engine
// reset the view, as after a camera cut) the controlled actor's layer, else the one it had before it lost it.
inline int32_t viewReferenceLayer(int32_t viewLostFrom, int32_t actorCell, int32_t actorLastCell) {
    if (viewLostFrom >= 0)
        return viewLostFrom;
    return actorCell >= 0 ? actorCell : actorLastCell;
}

} // namespace drakan_layer_seam
