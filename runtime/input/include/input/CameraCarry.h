// What turns with Rynn when the PC camera follow carries the camera round her (issue 6 item 1).
//
// While mouse / keyboard steer on foot, beforeCameraFollow turns the camera's previous position round Rynn by her
// heading change, so a turn reaches the view at once. The camera does not sit on the native target, though: it
// trails it by a travel lag (running at about 3000 units a second with the follow rate 5 a second: about 600
// units), and that lag points back along the way she came. Turning the whole offset swung the lag sideways with her
// heading: at the head of a staircase a quick turn put the camera 500 units further out than the native camera
// target ever goes, through the corner of the room and past the edge of the interior's terrain layers. The picture
// showed wall stone at point-blank range, the view layer lookup found no layer, and with the outdoor layer taken
// instead the world update all but stopped Rynn (the Surdana cellar fault).
//
// The native tick FUN_0041f848 computes its target and tests it against the world itself (FUN_0021ad48, then
// FUN_0021b280); the glue reads the target those tests returned. Only the part from the pivot to that target turns
// with her. The lag stays where it is, in the space she just came through, and the native easing takes it up as
// before. No ray is cast and no guest function is called for this; this header is the arithmetic.
#pragma once
#include <cmath>

namespace drakan_input {

// The target of the last tick is used only while it is this close to the pivot: a placement, a level arrival or a
// cut back to her leaves it far behind, and then the whole offset turns as it always did.
constexpr float kCarryTargetMaxOffset = 4096.0f;

inline bool carryFinite(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// Splits the camera position into the part that turns with Rynn (pivot -> native target of the last tick) and the
// travel lag (that target -> camera), which keeps its place. targetAge: camera ticks since the glue reported the
// target (1 = the tick before this one, the only age that is used). Returns false when the whole offset has to
// turn.
inline bool splitTravelLag(bool enabled, int targetAge, const float camera[3], const float lastTarget[3],
                           const float pivot[3], float turning[3], float lag[3]) {
    if (!enabled || targetAge != 1 || !carryFinite(camera) || !carryFinite(lastTarget) || !carryFinite(pivot))
        return false;
    float distance = 0.0f;
    for (int i = 0; i < 3; ++i)
        distance += (lastTarget[i] - pivot[i]) * (lastTarget[i] - pivot[i]);
    if (distance > kCarryTargetMaxOffset * kCarryTargetMaxOffset)
        return false;
    for (int i = 0; i < 3; ++i) {
        turning[i] = lastTarget[i] - pivot[i];
        lag[i] = camera[i] - lastTarget[i];
    }
    return true;
}

} // namespace drakan_input
