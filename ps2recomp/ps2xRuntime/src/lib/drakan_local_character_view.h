// Render-only local avatar substitution. EE-thread state; never changes the gameplay actor or its flags.
#pragma once
#include <cstdint>

namespace drakan_local_character {
struct View {
    uint32_t controller = 0, body = 0;
    uint32_t weapons[2] = {};
    uint32_t poseSource = 0; // gameplay actor; may differ from the native visual model in instance data +0x80

    // Keep each body's native render preparation (skeleton / material state). Never substitute just the pointer
    // at the final draw: that is too late and reuses the controller's skinning / texture state.
    uint32_t nativeObject(uint32_t object) const {
        if (!controller || !body)
            return object;
        if (object == controller)
            return 0;
        if ((weapons[0] && object == weapons[0]) || (weapons[1] && object == weapons[1]))
            return 0;
        return object;
    }
    // Native scene capture must include the new body, not the invisible gameplay model / original held items.
    bool hiddenModel(uint32_t object) const {
        return controller && body &&
               (object == controller || (poseSource && object == poseSource) || (weapons[0] && object == weapons[0]) ||
                (weapons[1] && object == weapons[1]));
    }
    // The native projected-shadow pass is separate from the visible mesh pass. Redirect at its full model entry,
    // before skeleton / silhouette preparation, and do not also submit the cosmetic body's own shadow.
    uint32_t shadowObject(uint32_t object) const {
        if (!controller || !body)
            return object;
        if (object == controller)
            return body;
        if (object == body || (weapons[0] && object == weapons[0]) || (weapons[1] && object == weapons[1]))
            return 0;
        return object;
    }
};
inline View g_view;
} // namespace drakan_local_character
