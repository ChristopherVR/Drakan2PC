# Issue 1: local avatar lifetime at Quit to Title

## Source-proven hazard

The final host-disconnect acceptance attempt (`build/harness/issue1-host-disconnect-final/b/stdout_1.log`) reaches
native title screen 8 and binds the modern title at frame 5329, then stops advancing input boundaries. Just before
the title bind it reports the selected **local** cosmetic body `bdcd60` (ghost id 1) gone. Remote body id 2 had already
been queued for destruction at disconnect. No level-load callback occurs during this Quit to Title: the last logged
level load belongs to initial match startup.

Review found a reachable stale-view hazard independent of the reported remote LOD appearance:

1. The ghost liveness loop erased a dead local body without clearing its local view and identity.
2. `localCharacterTick` attempted to read the native player pose before testing whether that local ghost still
   existed. During title teardown that read can fail, leaving the old `g_view` in place indefinitely.
3. RenderWorld copied the source transform into `g_view.body + 0x14` after validating only the source. The later
   `localRenderCell` destination-vtable check happened after the copy, so it could not protect that destination write.

The corresponding failed run has no RAM dump or native thread stack. The watchdog's guest PC `0x139010` identifies
the render entry boundary but does not distinguish a host wrapper from a native function or establish an overwrite.
This evidence establishes the lifetime gap, **not the cause of the stall**. A same-binary retry also stopped at the
title boundary; its host stopped after manual Quit to Title without the rejected-session resume path. No claim is
made that this correction resolves those stalls.

## Narrow correction

- All removal paths for the owned local ghost now reset its view and identity before erasing the ghost, under the
  existing ownership lock, before a failed pose read can return.
- Binding records the body's skeleton/controller pointer and model resource. Before RenderWorld can copy a
  transform, it validates the destination pointer, vtable, non-dying flag, controller and resource identity.
- A failed render-entry validation clears only EE-owned view/render-scope state. It introduces no ownership lock
  or ghost-list mutation inside the render wrapper. Existing PadMgr removal handles ghost ownership separately.
- Ordinary valid bodies retain the native cell insertion, transform copy, skinning and rendering path. Remote
  ghosts and network pose interpolation are unchanged.

The existing `character_roster_test` exercises the identity guard for an intact body, dying body, wrong vtable and
same-address reuse with a different controller or model. Existing view tests continue to cover visibility/shadow
substitution and restoring the native body when the view is cleared. The parent integration task owns compilation,
test execution and new live acceptance after the final rebuild. Source formatting and `git diff --check` passed.

## Native stand-in reference correction

The first destination guard incorrectly required a nonzero registry reference. Live build `C2ED...` returned to the
title and allowed rehosting, but repeatedly rebound selected Sevoth (`177d`) and rendered native Rynn instead of the
selected body (`build/harness/issue1-host-disconnect-lifetime/b/`). This was a regression in the guard, not evidence
that the selected body was dead.

Archived native evidence establishes the unsupported invariant: in all three
`build/harness/coop-position-final-npcs/a/ram_{before,inventory,after}.ram` snapshots, live local selected body `dad1f0`
has **zero** at actor `+0x12`, while its controller remains `e29e60`, resource `51323` and actor vtable valid. Spawn logs
can show a nonzero earlier reference; that value is not a permanent liveness condition. The existing stand-in
liveness path already intentionally skips the registry requirement. The revised render guard therefore uses the
bound controller and resource, with actor vtable and non-dying flag, and does not inspect the registry reference.
Erasure still clears the owned local view before pose reading. The parent task owns a fresh selected-Sevoth visual
acceptance and Quit to Title test on the rebuilt guard; broad all-character certification is not claimed.

## Integrated live result

Canonical build `AFF3EE9E...` passed the selected-Sevoth visual check and host/client title transition, displayed the
connection error and allowed a new host lobby (`build/harness/issue1-host-disconnect-avatar`). Both scripts shut down
normally. Root inspected the playing/error/lobby screenshots. This validates the corrected guard for this selected
NPC and scenario; it does not prove the causal mechanism of the earlier stalls or certify every character.
