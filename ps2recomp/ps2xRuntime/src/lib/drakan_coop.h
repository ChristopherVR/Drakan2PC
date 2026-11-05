// Drakan: co-op (docs/journal/2026-10-31/coop-design.md, phase 2). Two players fight through a level together.
// The host streams its creatures (position, yaw, HP, 10 Hz) through the multiplayer session (drakan::net protocol v6:
// CoopCreatures / CoopEvent relayed by the MatchServer, see drakan_netplay's coop* API); on the client the matching
// creatures stop thinking (behaviour event 4 is swallowed by a host handler, the ghost technique) and follow: kinematic
// proxies. On top of that, reliable events on the same socket (keyed by creature / object keys, see the .cpp wire
// table):
//   * kills: the host reports a death, the client's proxy gets its own handler back and a lethal damage message
//     (it plays its own death animation and drops its own loot);
//   * client hits: the engine's damage message (0x10db, through the message send 0x171150) from the local weapon on a
//   proxy
//     becomes a claim; the host validates it and posts the same message to the real creature (HP and death replicate);
//   * enemies target the partner: a host creature's tick (and a door's) runs with the local player's body moved onto
//   the
//     partner's ghost when it is clearly nearer; damage that lands on the ghost is sent to the client, which applies it
//     through Rynn's own damage method (never lethal yet);
//   * world events: doors open for the partner by the same proximity trick, pickups taken on one side are removed on
//     the other (objects keyed by class, resource id and registry order).
// Uses the ghosts of drakan_netplay as the partner's body (drakan_pvp stands down while a Coop match plays).
//
// Active while the session is a Coop match in the Playing phase (host / join it from the Multiplayer menu, or with
// DRAKAN_NET_HOST=<port> DRAKAN_NET_MODE=coop and DRAKAN_NET_CONNECT=<host:port>); the player that owns the creature
// world is the match's co-op authority (the first to join). DRAKAN_COOP=0 leaves the hooks out. Environment:
//   DRAKAN_COOP_MODE=follow|mark     client behaviour (default follow). mark = measure only: nothing is written to the
//                                    guest, the log reports how far the local creatures are from the host's
//   DRAKAN_COOP_SWALLOW=<hex>        behaviour events (value mask) a proxy no longer handles (default 4 = per-tick)
//   DRAKAN_COOP_RETARGET=0           host creatures and doors keep looking only at the local player
//   DRAKAN_COOP_DESTROY=1            the client removes the corpse of a replicated death after 5 s
//   DRAKAN_COOP_PROBE=1              log message types, door field changes, damage messages
//   DRAKAN_COOP_DAMAGE_SCALE=<f>     test aid: multiplies the damage of the local player's / the client's hits (host)
//   DRAKAN_COOP_TEST_*               harness aids (docs/journal/2026-10-31/coop-design.md section 7)
// Compiled only with the GS renderer (PS2X_HAVE_GSR), like drakan_world. PAL disc profile.
#pragma once
#include <cstdint>

class PS2Runtime;
namespace drakan_input {
class IGuest;
}

namespace drakan_coop {

void install(PS2Runtime& runtime);
bool enabled();
// A nested PadMgr call must not publish the temporary AI target as the local player.
bool borrowingPlayerTransform();
// EE thread, at the PadMgr_Update boundary.
void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram);

} // namespace drakan_coop
