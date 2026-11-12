#pragma once
// Which terrain layer the view is drawn from when the follow camera's point has none (issue #6 item 1, the Surdana
// spider cellar: "Rynn gets completely stuck and the entire rendering bugs out").
//
// The engine keeps a view layer (PAL 0x4dade4) and updates it every frame with the cell lookup from that layer at a
// point 32 units behind the camera; the lookup only searches the layers the old one lists as visible. A camera point
// behind a wall of an interior lies over none of them: the result is -1, the next frame searches all layers and
// finds the outdoor layer above the interior, which does not list the interior back. From then on the outdoor world
// is drawn from below and the player, whose layer is no longer among the visible ones, stops being moved. The PC
// camera reaches such points far more often than the PS2 pad camera: the mouse turns Rynn at once and the camera
// follow carries the camera round her on an arc, through the corner of a room.
//
// Rule (view only; nothing here moves an actor): while the camera point is near the controlled actor, a lookup that
// finds no layer, and a search over all layers that ends in a layer unrelated to the actor's, are answered from the
// actor's layer instead: the lookup is repeated from her layer, and when that finds nothing either the view uses her
// layer itself. A camera far from her (a cut scene elsewhere, a camera in flight) keeps the engine's handling, and so
// does every frame for a while after she was placed rather than moved (Settle below).
#include <cstdint>

namespace drakan_view_layer {

constexpr int32_t kNoLayer = -1;
// The follow camera sits about 1500 units behind Rynn and further behind Arokh; two cells cover both. Beyond that
// the camera is not following the controlled actor and the engine's own search stands.
constexpr float kFollowRange = 4096.0f;

inline bool nearActor(float dx, float dy, float dz) {
    return dx * dx + dy * dy + dz * dz <= kFollowRange * kFollowRange;
}

// A controlled actor that moved further than this between two view updates was not walking, flying or falling: it
// was placed (a script teleport, a play-test placement, a level arrival, a mount). For kSettleUpdates view updates
// after that, and after a change of the controlled actor, the rule stands aside, so everything such a move does is
// the engine's own handling, as before.
constexpr float kTeleportStep = 2048.0f;
constexpr int kSettleUpdates = 600;

inline bool teleported(float dx, float dy, float dz) {
    return dx * dx + dy * dy + dz * dz > kTeleportStep * kTeleportStep;
}

// Counts the view updates since the controlled actor was last placed or replaced.
struct Settle {
    int left = kSettleUpdates; // nothing is known about the first frames of a level either

    // Once per view update. Returns true while the rule must stand aside.
    bool update(bool placedOrReplaced) {
        if (placedOrReplaced)
            left = kSettleUpdates;
        else if (left > 0)
            --left;
        return left > 0;
    }
};

enum class Action {
    Native,    // the engine's result stands
    FromActor, // repeat the lookup from the controlled actor's layer; nothing there either: her layer itself
    KeepOld,   // the view keeps the layer it had
};

struct Lookup {
    int32_t oldLayer = kNoLayer;   // the view layer the lookup started from (-1: a search over all layers)
    int32_t native = kNoLayer;     // what the engine's lookup returned
    int32_t actorLayer = kNoLayer; // the controlled actor's layer (-1: none, or no controlled actor)
    bool cameraNearActor = false;  // nearActor(camera point - actor position)
    bool nativeRelated = false;    // native lists the actor's layer as visible, or is listed by it (or is it)
};

inline Action decide(const Lookup& l) {
    if (!l.cameraNearActor)
        return Action::Native;
    if (l.actorLayer == kNoLayer) {
        // She is between layers for a tick (the engine's own wall handling): do not let the view start a search
        // over all layers on that tick.
        return l.native == kNoLayer && l.oldLayer != kNoLayer ? Action::KeepOld : Action::Native;
    }
    if (l.native == kNoLayer)
        return Action::FromActor;
    if (l.oldLayer == kNoLayer && !l.nativeRelated)
        return Action::FromActor;
    return Action::Native;
}

// The view layer after `action`. fromActor is the lookup repeated from the actor's layer (only used by FromActor).
inline int32_t resolve(Action action, const Lookup& l, int32_t fromActor) {
    switch (action) {
        case Action::FromActor:
            return fromActor != kNoLayer ? fromActor : l.actorLayer;
        case Action::KeepOld:
            return l.oldLayer;
        case Action::Native:
            break;
    }
    return l.native;
}

} // namespace drakan_view_layer
