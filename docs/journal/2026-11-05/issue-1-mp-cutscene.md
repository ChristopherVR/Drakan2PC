# Issue 1 follow-up: Surdana arena starts campaign cutscenes

Source: [issue 1 follow-up](https://github.com/ChristopherVR/Drakan2PC/issues/1#issuecomment-6637936574).

## Cause and existing fix

The original arena cleanup ran after load, waiting for a controllable player and ghost readiness, then another
12 pad boundaries before its first pass. Campaign actor initialization could already start a cutscene and take
the player/camera. Surdana's campaign placements included 18 Stomp Players, detectors, timers and conversations.
Deleting those actors after initialization cannot reliably undo their side effects.

Commit `7b88809` (`Fix runtime regressions and localize modern game menus`) already introduced the relevant fix:
`runtime/hle/src/ArenaLevel.cpp` filters placed actors before guest initialization, and the FileService open
filter in `drakan_overrides.cpp` applies this private LVL view to active versus arena source levels. The policy
allows environment/player/camera classes, rejects campaign classes and NPC-bank placements, and prunes links
to removed placements. Campaign and co-op reads keep the original source bytes. `DRAKAN_ARENA_NOFILTER` explicitly
disables this protection for investigations.

The older post-load sweep remains a safety net. No further production change is justified by the reported
symptom alone: the tracked implementation already addresses the observed ordering error. The current reference
contract is in `docs/reference/netcode.md`, under “Campaign-free arena loading”. The earlier live title-menu
Surdana validation is recorded in `docs/journal/2026-11-04/arena-preinit-and-avatar-shadows.md`.

## Verification on 2026-11-05

Ran the existing `build/runtime-tests/bin/Release/arena_level_test.exe` (timestamp 2026-11-05 06:07 local) with the
player-supplied PAL ISO. Exit status 0. Its synthetic regression covers removed Stomp/detector/timer/NPC placements,
pruned links, unchanged terrain and source filesystem bytes, private filtered handles, subsequent original campaign
opens, and malformed input rejection.

The optional ISO smoke test also passed:

| Source level | Campaign placements removed before init | Retained placements |
| --- | ---: | ---: |
| Surdana | 1157 | 3518 |
| City of the Damned | 300 | 290 |
| Inquisitor's Lair | 135 | 547 |

This follow-up did not rebuild the executable or launch a live game. The smoke validates the existing HLE filter
against actual level data, not current deployment/menu wiring. A refreshed combined deployment should repeat
title menu → Multiplayer → Host Surdana Meadow → Start, confirming free movement and no campaign cinematic.
No game assets or extracted level bytes were added to the repository.
