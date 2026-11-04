# Arena avatar deletion and repeated Preparing screen

## User evidence and cause

The user reported a flashing black `Preparing Monk...` screen. Their `dist/DrakanPC/drakan.log` shows a versus arena,
not the co-op scenario used by the preceding avatar tests:

* Monk (`16bf`) successfully spawns and draws as local body id 1, controller `a9d540` unchanged.
* The arena's periodic campaign filter reports removing one creature / NPC.
* The ghost layer reports that the local body is gone, then selects / spawns id 2; the same sequence repeats through
  dozens of ids. This is not ordinary camera culling or asset decode latency.
* The cosmetic preparation gate muted input and covered the world on each respawn. Arena auto-equipment logged
  `not in free gameplay` repeatedly and eventually gave up. Successful co-op spawns did not exercise this failure path.

`removeCampaign` protected native-player types and ghost-held items, but did not protect NPC-class player bodies.

## Fix

* Snapshot all ghost-owned body handles once per cleanup pass. Apply ownership protection before creature / story /
  loose-item classification. Protect both local and remote avatars without exempting ordinary campaign NPCs.
* Factor the guard into `drakan_arena_filter.h`; the character policy test covers local / remote NPC avatars, native
  players and camera, and confirms an unrelated campaign creature remains removable.
* Delete the cosmetic black-cover / input-loading gate and its input-module API. Avatar preparation may never block
  gameplay; native level / save progress remains unchanged. Prepare avatars while a session's level loads, not only
  after Playing. This supersedes the loading-cover experiment in the previous day's journal.
* Preserve the live original-mesh menu preview, the 15 equippable combat-NPC roster, and the separate informational
  `Dragon Appearance: Arokh (original)` row. Arokh is not a selectable humanoid player.

## Validation

The incremental PAL build was deployed through the normal `Build-LocalGame.ps1` path. `character_roster`,
`character_preview_test` and `gameui_multiplayer_test` pass. The arena regression run uses `surdana-meadow` with Monk
selected on both instances, including repeated campaign cleanup and arena loadout / combat.

**[game]** `build/harness/arena-monk-stability/`: both instances spawned their local avatar exactly once, reached its
native draw exactly once, had **zero** unexpected local-avatar deletions, and exited cleanly. This contrasts with the
user log's dozens of body ids. Campaign cleanup still removed 352 actors; the remaining HP-bearing NPCs in the world
dump are the protected player bodies, not campaign creatures.

The older all-purpose arena harness was not a clean pass: its zero-creature assertion incorrectly counts NPC avatars,
and the run used an unlimited frag limit while that harness expects a finished match. Its B auto-equip and pickup
checks also failed, so these results must not be represented as a complete PvP / equipment validation. Original-model
visibility flag mutation was removed as an additional precaution: keep native animation / equipment preparation intact
and suppress only draw submission. Avatar stability is verified separately from those broader combat checks.

**[game] Final focused run after removing visibility-flag mutation:** `build/harness/arena-monk-final/` runs 30 seconds
of arena gameplay per instance, across repeated cleanup passes. A: one local avatar spawn, zero deletions, armed,
clean exit. B: one local avatar spawn, zero deletions, armed, clean exit. Both native starting weapons were equipped.
The source and focused recipe are `build/final_arena_avatar_check.py` (ignored); the older all-purpose pickup / match
checks are intentionally not claimed by this focused recipe. The black-cover function, loading gate API and associated
input changes are absent from the final source and deployed executable.
