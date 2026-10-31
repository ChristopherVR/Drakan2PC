// Drakan: ghost players (docs/journal/2026-10-31/actor-spawn.md, ghost-spike.md). Spawns a second, class 0x10009 (Rynn)
// actor next to the controlled one with the engine's own factory (0x181ad8) and swaps its behaviour handler for a host
// handler, so the clone is inert (no AI, no damage reaching the global player HP) and can be driven kinematically.
// Opt in with DRAKAN_GHOST=1: F9 (or the F1 World panel button) spawns one at the next PadMgr_Update boundary, F10
// destroys the newest. DRAKAN_GHOST_FWD=<hex mask> picks which behaviour events the host handler still forwards to
// Rynn's real handler (bit n = event 1<<n; default all but event 4), DRAKAN_GHOST_LOG=1 logs every handler event.
// Compiled only with the GS renderer (PS2X_HAVE_GSR), like drakan_world.
#pragma once
#include <cstdint>
#include <functional>

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
void destroy(int id); // queue 0x1413c0; the engine frees it at its next flush
// Applied on the EE thread at the next PadMgr boundary (position and rotation only; no sector update yet).
void setTransform(int id, const float pos[3], const float rot[9]);
// Stub: S2 (pose model) owns the clip state; the call is recorded and not applied yet.
void setAnim(int id, int channel, int clip, float time);
void forEach(const std::function<void(const Info&)>& fn);

// EE thread, PadMgr_Update boundary: F9 / F10, queued requests, liveness.
void beforePadMgr(drakan_input::IGuest& guest, drakan_input::InputModule& input);
// UI thread, in the F1 overlay: spawn / destroy buttons and the ghost list.
void drawControls();

} // namespace drakan_ghost
