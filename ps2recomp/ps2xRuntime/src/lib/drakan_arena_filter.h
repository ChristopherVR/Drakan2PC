// Campaign cleanup is based on ownership before class / HP. A player avatar may itself be an NPC-class creature.
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace drakan_arena::detail {
inline bool protectedPlayerActor(uint32_t actor, uint32_t player, uint32_t camera, bool nativePlayer,
                                 std::span<const uint32_t> avatars) {
    return actor == player || actor == camera || nativePlayer ||
           std::find(avatars.begin(), avatars.end(), actor) != avatars.end();
}
} // namespace drakan_arena::detail
