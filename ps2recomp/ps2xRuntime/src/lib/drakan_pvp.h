// Drakan: PvP combat glue (docs/reference/netcode.md "PvP", docs/journal/2026-10-31/pvp.md).
//
// Attacker side: the engine's blade hit application (0x498020) is wrapped. A hit on a ghost (drakan_ghost, filed in the
// collision cells so the sweep finds it) runs the engine's normal path (effects at the contact; the ghost's handler
// swallows the damage message) and, when the blade's own hit list grew, becomes a claim for the server through
// drakan_netplay::sendHitClaim.
// Victim side: the server's accepted Hit / Death / Respawn / PhaseChange events (drakan_netplay forwards them through
// onServer*) are applied to the local player at the PadMgr boundary: Rynn's own damage method for the flinch (posted,
// never lethal), the stats HP block set to the server's value, and a death that never reaches HP 0 in the engine (that
// raises the single-player game over): the player is frozen with the death clip until the Respawn, then moved to a
// spawn point. On by default with the multiplayer module (DRAKAN_PVP=0 turns it off). Compiled only with the GS
// renderer.
#pragma once
#include <cstdint>

class PS2Runtime;
namespace drakan_input {
class IGuest;
}

namespace drakan_pvp {

void install(PS2Runtime& runtime);
bool enabled();
// EE thread, at the PadMgr_Update boundary (after drakan_netplay::beforePadMgr).
void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram);

// Thread safe. Server decisions as received by the network thread.
void onServerHit(uint8_t attacker, uint8_t victim, uint8_t weapon, uint16_t damage, uint16_t healthAfter);
void onServerDeath(uint8_t victim, uint8_t killer, uint8_t weapon);
void onServerRespawn(uint8_t player, uint8_t spawnIndex, uint16_t health);
void onServerPhase(uint8_t phase); // net::MatchPhase: 0 lobby, 1 loading, 2 playing, 3 results
void setLocalPlayer(uint8_t id);   // our own player id once connected (0 = not in a match)

} // namespace drakan_pvp
