// Render-only local avatar substitution. EE-thread state; never changes the gameplay actor or its flags.
#pragma once
#include <cstdint>

namespace drakan_local_character {
struct View {
    uint32_t controller = 0, body = 0;
    uint32_t weapons[2] = {};
    uint32_t poseSource = 0; // gameplay actor; may differ from the native visual model in instance data +0x80
    uint32_t bodySkeleton = 0;
    uint32_t bodyResource = 0;
    uint32_t bodyWeapons[2] = {}; // the selected body's own held items
    // The whole local avatar is hidden (original model, selected body, every held item and their shadows): the player
    // still stands at the level start before her arena spawn placement, or the selected body is still loading. Valid
    // with or without `body`.
    bool hideAll = false;

    bool isAvatar(uint32_t object) const {
        return object && (object == controller || object == poseSource || object == body || object == weapons[0] ||
                          object == weapons[1] || object == bodyWeapons[0] || object == bodyWeapons[1]);
    }

    // A pooled address can be reused during title teardown without a level-load notification. Validate the bound
    // body's identity before writing its transform, not just its class/vtable after the write.
    bool matchesBody(uint32_t vtable, uint32_t flags, uint32_t skeleton, uint32_t resource,
                     uint32_t handleVtable) const {
        return body && bodySkeleton && vtable == handleVtable && !(flags & 0x80000000u) && skeleton == bodySkeleton &&
               resource == bodyResource;
    }

    // Keep each body's native render preparation (skeleton / material state). Never substitute just the pointer
    // at the final draw: that is too late and reuses the controller's skinning / texture state.
    uint32_t nativeObject(uint32_t object) const {
        if (hideAll && isAvatar(object))
            return 0;
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
        if (hideAll && isAvatar(object))
            return true;
        return controller && body &&
               (object == controller || (poseSource && object == poseSource) || (weapons[0] && object == weapons[0]) ||
                (weapons[1] && object == weapons[1]));
    }
    // The native projected-shadow pass is separate from the visible mesh pass. Redirect at its full model entry,
    // before skeleton / silhouette preparation, and do not also submit the cosmetic body's own shadow.
    uint32_t shadowObject(uint32_t object) const {
        if (hideAll && isAvatar(object))
            return 0;
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
