// Drakan: ghost players (docs/journal/2026-10-31/actor-spawn.md, ghost-spike.md). Spawns a second, class 0x10009 (Rynn)
// actor next to the controlled one with the engine's own factory (0x181ad8) and swaps its behaviour handler for a host
// handler, so the clone is inert (no AI, no damage reaching the global player HP) and can be driven kinematically.
// Opt in with DRAKAN_GHOST=1: F9 (or the F1 World panel button) spawns one at the next PadMgr_Update boundary, F10
// destroys the newest. DRAKAN_GHOST_FWD=<hex mask> picks which behaviour events the host handler still forwards to
// Rynn's real handler (bit n = event 1<<n; default all but event 4), DRAKAN_GHOST_LOG=1 logs every handler event.
// DRAKAN_GHOST_MIRROR=1 (implies DRAKAN_GHOST): no network; the local player's pose is recorded each tick and the ghost
// (auto spawned a few seconds after Rynn is under control, respawned after a level load or save) replays it 1.0 s
// later, +300 units in x. API contract (stable, thread safe, queue-and-apply at the next PadMgr boundary): spawn -> id,
// destroy(id), setTransform, setAnim, setPose, forEach. Calls with an unknown id are ignored. Ghosts are destroyed
// automatically before a level load and a save write (drakan_lifecycle) and forgotten; the owner must re-spawn
// afterwards. Compiled only with the GS renderer (PS2X_HAVE_GSR), like drakan_world.
#pragma once
#include <cstdint>
#include <functional>

namespace world {
struct PlayerPose;
}

class PS2Runtime;
namespace drakan_input {
class IGuest;
class InputModule;
} // namespace drakan_input

namespace drakan_ghost {

struct Transform {
    float pos[3] = {};
    float rot[9] = {}; // 3x3 rows; the 0x30 bytes at actor+0x14 are {pos, rot}
};

struct Info {
    int id = 0;
    uint32_t handle = 0; // guest actor pointer, 0 while the spawn is in flight
    uint32_t ref = 0;    // actor+0x12 registry ref
    uint32_t resIndex = 0;
};

void install(PS2Runtime& runtime); // reads DRAKAN_GHOST; registers the host trampolines
bool enabled();

// Thread safe. Queue a spawn (at `t`, or +300 units beside the controlled actor when null); returns the ghost id
// (> 0), or 0 when disabled. The actor exists a few frames later (forEach shows handle != 0).
int spawn(const Transform* t = nullptr);
// spawn() returns 0 and logs "[ghost] spawn refused: <reason>" (also counted in spawnFailures()) while ready() is
// false: cut scene levels ("Level 0*": the intro hangs the guest), a load / save in progress, or the first 90 PadMgr
// boundaries after a load or restore. A spawn that passed this but is refused at the boundary (the controlled actor is
// not Rynn, the world became not ready) also bumps spawnFailures() and the ghost disappears from forEach; callers back
// off.
bool ready();
unsigned spawnFailures();
void destroy(int id); // queue 0x1413c0; the engine frees it at its next flush
// Applied on the EE thread at the next PadMgr boundary (position and rotation only; no sector update yet).
void setTransform(int id, const float pos[3], const float rot[9]);
// Clip state: write clip resource id `clip` and time `time` (seconds) into skeleton channel `channel` (< 32; -1 = every
// channel) of the ghost. Applied by the host every tick at the PadMgr boundary (it is held until changed), before the
// engine evaluates the skeleton.
void setAnim(int id, int channel, int clip, float time);
// Position, rotation and animation in one call (what the net layer calls per remote sample): pose.position / rotation
// as setTransform, pose.lower on channels set in pose.lowerMask, pose.upper on the others (setAnim per channel).
void setPose(int id, const world::PlayerPose& pose);
// Weapons the ghost holds, by resource record id (PlayerPose.weaponRight / weaponLeft, e.g. 0x33cbc; 0 = none; the left
// hand is the bow's). Applied at the PadMgr boundary: an item actor of that record is created with the engine factory,
// owned by the ghost and attached to its hand bone (properties 0x45 / 0x46); a changed id drops the old item first.
// The items are destroyed with the ghost and before level loads / save writes. The local player's inventory, equipment
// and HP are not touched. setPose applies PlayerPose.weaponRight / weaponLeft the same way when they are above 0xff.
void setWeapons(int id, uint32_t rightRecordId, uint32_t leftRecordId);
void forEach(const std::function<void(const Info&)>& fn);

// EE thread, PadMgr_Update boundary: F9 / F10, queued requests, liveness.
void beforePadMgr(drakan_input::IGuest& guest, drakan_input::InputModule& input);
// UI thread, in the F1 overlay: spawn / destroy buttons and the ghost list.
void drawControls();

} // namespace drakan_ghost
