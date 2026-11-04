#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace drakan_pvp::detail {

// Only an actual opponent's NPC avatar bypasses the original player-versus-NPC melee exclusion.
inline bool usePlayerMeleeMask(bool coop, uint32_t owner, uint32_t controlled, int remotePlayer, uint32_t actorClass) {
    return !coop && owner && owner == controlled && remotePlayer > 0 && actorClass == 0x1009du;
}

struct ProjectileTarget {
    int ghost = 0;
    int player = 0; // zero: local cosmetic / unassigned body, never a damage target
    float pos[3] = {};
};

inline bool segmentHitsTarget(const float a[3], const float b[3], const float target[3]) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int steps = std::clamp(static_cast<int>(length / 50.0f) + 1, 1, 64);
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = a[0] + dx * t - target[0], z = a[2] + dz * t - target[2];
        const float y = a[1] + dy * t - target[1];
        if (x * x + z * z <= 150.0f * 150.0f && y >= -60.0f && y <= 460.0f)
            return true;
    }
    return false;
}

inline const ProjectileTarget* firstProjectileTarget(const std::vector<ProjectileTarget>& targets, const float a[3],
                                                     const float b[3]) {
    for (const ProjectileTarget& target : targets)
        if (target.player > 0 && segmentHitsTarget(a, b, target.pos))
            return &target;
    return nullptr;
}

} // namespace drakan_pvp::detail
