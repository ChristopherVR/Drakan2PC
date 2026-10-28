# Conservative duplicate-world-vblank handoff

Baseline: runtime 489ee20. Changes are limited to the bridge, its private policy
header and CPU tests. No renderer/backend, shared checkout, dist, GPU or game
changes. The original PS2Recomp-sub checkout and its subtitle logger change
remain untouched; this implementation lives in PS2Recomp-sub-vblank.

Every guest vsync still calls Renderer::vsync(false): pending draws flush, the
FIELD bit advances, the display latches, endFrame runs, and callbacks/counters
remain intact. The check samples draw and mutation counters AFTER that work.
Only a positively proven duplicate avoids latchSerial advancement and the
worker's host-present handoff wait. Native and SR-only modes always advance.

Duplicate proof requires all of:

- NR requested; initial weave mode with flicker filter off.
- Valid World + HaveViewport metadata, known gameplay GUI (-1), nonzero frame
  dimensions, followed by EndWorld. An unfinished/unknown scene cannot qualify.
- Both previous and current blanks belong to that completed world. New world
  metadata (including cuts) forces handoff even with no new draw.
- Identical drawCalls, H2L/L2L transfers, RT uploads/readbacks, direct bridge
  writeVram revision, liveApplied counter and all 17 privileged snapshot words.
- No live setting changes after qualification. The public renderer has no
  effective deinterlace getter; disarming for the remaining session is the
  conservative response to a possible live switch to bob. Restart rearms the
  initial-mode check. Ordinary initialization changes before arming are safe.

Known post-world native HUD preview packets (sourcePc 0x1e97a0, GUI -1) may
retain an established completed world. They cannot create one. Other non-world
packets and known GUI screens invalidate it. HUD/fade/effect draws still change
drawCalls, and movie image uploads still change transfer counters. An identical
display is not inferred from arbitrary no-draw/menu/movie blanks. All display
register changes, even those that might be visually irrelevant, force handoff.

This policy observes renderer activity through the bridge/public counters. Raw
external Renderer::vram() writes bypassing both vramTouched/counted transfers
and bridge writeVram are outside that contract and must not be introduced as
an untracked upload path. It does not hash pixels or guess image equality.

Existing opt-in PS2X_GS_STATS output now also reports cumulative latches and
duplicateWorldVblanks. No new always-on/per-blank logging is added. Existing
settings-overlay presentation, fair host handoff and 100ms wait escape remain.

## CPU evidence

84 CPU checks pass with MSVC C++20 /O2 /W4 /WX. The modified complete bridge
translation unit compiles /O2 /W3. Checks include every snapshot word and each
mutation counter, all native blanks, menus/movies, previews, incomplete worlds,
NR re-enable, bob/blend and live settings. A 60-vblank/30-world sequence keeps
60 guest vsyncs with exactly 30 latch advances and 30 required handoff waits.

Local evidence: decomp/build/vblank_handoff_sub/cpu-check.log and compile.cmd.
The test executable is policy-only: it does not validate actual thread timing,
image equivalence, live NR FPS or GPU behavior. Main owns that validation.

Integration: cherry-pick this single commit onto runtime 489ee20 (or later),
including gs_vblank_handoff.h and vblank_handoff_tests.cpp. Build the bridge;
no legacy recompiler regeneration is required by this change. Validate gameplay
30-world/60-vblank behavior plus movies, menus, fades, field modes and settings
before deployment. A duplicate that fails any proof simply retains old behavior.
