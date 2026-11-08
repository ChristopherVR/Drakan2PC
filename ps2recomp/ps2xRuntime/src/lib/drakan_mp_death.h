// Drakan: environmental deaths in a multiplayer session (PvP arena, co-op).
//
// The engine's own death (HP reaching 0) hides the body and raises the single-player game over, whose answer reloads
// the last save / returns to the title and its game choice. PvP and co-op already keep HP above 0 for the deaths they
// know about (server hits, host enemies), but a fall into a pit, lava, drowning or any other environmental damage
// lowers the HP through the engine's own HP setter (0x3d5710, the leaf of the damage method 0x314ab0) and kills Rynn.
// This module hooks that setter: in a session, a value of 0 or less is turned into 1 and the death is queued for the
// session code (drakan_pvp / drakan_coop), which freezes the player and respawns her. Single-player is untouched.
#pragma once

#include <cstdint>

class PS2Runtime;

namespace drakan_mp_death {

// Pure policy (unit tested): is this HP write a death that a session has to turn into a respawn?
//   sessionActive  a PvP arena match or a co-op match is running
//   isPlayerStats  the write targets the local player's stats block
//   newHp          the value the engine is about to store
//   currentHp      the HP stored right now
inline bool interceptsDeath(bool sessionActive, bool isPlayerStats, int32_t newHp, int32_t currentHp) {
    return sessionActive && isPlayerStats && newHp <= 0 && currentHp > 0;
}

// The HP the engine is given instead of the lethal value.
constexpr int32_t kSurviveHp = 1;

// EE thread. Installs the hook (once; harmless in single-player, where it only forwards).
void install(PS2Runtime& rt);

// EE thread. True once per intercepted death (an environmental death that the session has to handle).
bool takeDeath();

// A death that is being shown for `ageSec` seconds and has no server / host respawn coming: the local player has
// to get up on her own after this long.
constexpr float kSelfRespawnDelaySec = 3.0f;
inline bool selfRespawnDue(float ageSec) {
    return ageSec >= kSelfRespawnDelaySec;
}

} // namespace drakan_mp_death
