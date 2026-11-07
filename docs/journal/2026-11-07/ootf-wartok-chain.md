# Issue 5: the mount after the awakening scene, and the chain from Wartok Canyons

Follows [the Soul Crystal note](ootf-ruined-village-crystal.md). Runs are private under `build/ootf9/` (own copy of the
16:27 executable and of the `build/ootf8/pack` pack, direct start of "OotF 01 - Ruined Village", god mode, one game at a time
through the harness slot lock). The recipe starts Rynn on the lair ledge (482600 25200 201196), holds W for about 17 s until
the awakening scene plays, then waits on the engine's own screens instead of tapping Cross on a timer. Nothing from the
game is quoted.

## (a) Rynn is mounted on Arokh after the scene: yes

Run `a4`: scene, message box (screen 2), save screen (screen 5), one Cross tap to confirm, then `sync play`.

| Sample | Controlled actor | Rynn | Arokh |
| --- | --- | --- | --- |
| 2 s after the load | class `0x1001c` (Arokh) | 76833 11091 14003 | 76810 10835 13961 |
| 6 s after the load | Arokh | 76759 11118 13974 | 76810 10835 13961 |
| after W for 4 s | Arokh | 76333 11086 18231 | 76279 10835 18144 |

The controlled actor is Arokh, Rynn sits about 260 units above him, and W moves both (4,200 units in 4 s). The lifecycle
log shows `mountChanged 1->2` at the scene and no change back. The player is mounted on arrival, as at a direct Wartok start
(Rynn 87671 14483 39256 on Arokh 87660 14215 39210, controlled actor Arokh).

A trap that cost three runs: **a Cross tap after the Wartok load dismounts Rynn** (runs `a1` to `a3`, which kept the old
every-4-seconds Cross taps running through the load). Rynn then stands on Arokh's back, the controlled actor is Rynn
(`0x10009`), `mountChanged 2->1` shows 3.5 s after the load, she cannot walk off (W moved her 15 units in 4 s), and the camera
sits point blank on her in the dark. That is the "dark close-ups" of the previous note; it is a test artefact, not a converter
or scene fault. Recipes must stop tapping Cross once the save screen is confirmed.

Caveat: `a4` ran while another run of mine (`chain9`) was still holding the game slot, so two games shared the GPU for its
duration; the positions above do not depend on timing.

## (b) The chain from Wartok Canyons: every exit fires, Volcanic reached

Direct level starts, god mode, `DRAKAN_PLAYTEST_HOLD=30`, the walk-in recipe of `ootf8` (place Rynn near the exit, dismount
with C where she arrives mounted, walk into the exit box), the engine's own message box and save screen, then the load.
Segments ran in separate processes (a fresh card each) because a recipe cannot recover from a stalled menu (see the
traps). Within a segment the levels chained in one process.

| Level | Exit position fired | Leads to | Process time of the next load |
| --- | --- | --- | --- |
| Ruined Village (the crystal scene) | 148480 24496 211968 | Wartok Canyons (mounted arrival) | 277 s, the scene included |
| 03 Wartok Canyons | 326656 20288 340992 | 04 Grimstone Mines | 124 s (place at 106 s) |
| 04 Grimstone Mines | 177152 57144 164864 | 05 Grotto | 39 s |
| 05 Grotto | 244736 39050 68608 | 06 Islands | 48 s |
| 06 Islands | 656085 0 469973 | **08 Rift Crystal** (not 07 Alwarren) | 41 s |
| 08 Rift Crystal | 230592 208278 121472 | 09 Islands Night | 60 s |
| 09 Islands Night | 254847 59090 457707 | 10 Succubus Lair (ship ride) | 31 s |
| 10 Succubus Lair | 221184 219000 247808 | 11 Volcanic | 76 s |

Volcanic to Rift World (cage, Runeblade, mount, rift box) and the Rift World fight and credits were proven in the earlier
`chain2` run of [progression 3](ootf-progression-3.md); not repeated. Alwarren (07) is not on this walk-in path: the Islands
exit leads to Rift Crystal, so Alwarren is a side branch (the giant, the hammer) that these runs did not enter.

`DRAKAN_LEVELCHANGE_KEEP_DRAGON` was not needed: the dragon-bring rule only touches the disc game's own levels
(`drakan_game::isCampaignPackLevel` returns early for a pack level), so a campaign arrival keeps the author's placement.
No watchdog stall, crash or guest spin happened in any of the exits above.

### Traps that stalled recipes (all in the test method, none in the game)

* **Placement hold.** With the default hold the walk after `place` moves nothing for several seconds (Grimstone, `chain9`:
  Rynn stayed at the placed 181887 61400 141500 for the whole 3 s walk). Use `DRAKAN_PLAYTEST_HOLD=30`.
* **Quick save F5 plus F8, then S.** At Islands the S key walked Arokh into the exit box during the recipe's own setup, so
  the message box (screen 2) opened and the `sync play` that follows never came. Do not mix the quick-save probe with a
  walk-in recipe.
* **Overwrite dialog.** After an F5 quick save the level-change save screen lists that save in slot 1 and Cross opens
  "Spielstand ueberschreiben?" with the cursor on Abbrechen; further Cross taps cancel it again and the recipe waits on the
  slot screen for good (Grotto, `chain9b`, watchdog line at 756 s: the guest is waiting in the menu, not hung). Page to an empty slot with
  R1 (page n for the n-th save of a card) and tap Cross there, after the slot list has loaded (4 s; a tap during "Memory Card
  wird gelesen" is ignored).
* **Cross after the load dismounts** (section (a)).
* **No timeout in the input script grammar:** a stalled `sync` holds the game until `RUN_TIMEOUT`. Keep `PS2X_MAX_SECONDS`
  and `RUN_TIMEOUT` small per segment.

Process note: three of my runs overlapped another of mine (`a4` with `chain9`, `g1` with `chain9b`) because `run.py` runs
anyway when the slot lock is held. The data above does not depend on it, but it was a breach of one game at a time and I could
not stop the stuck game myself (the kill was refused), so `chain9` held the slot for its full 40 minutes.

## Not done in this pass

* (c) Delon on foot, (d) human flyability of the RibDoor tunnel and (e) credits after a title-chosen game were not
  attempted: the time went on (a) and (b) and the recipe traps above. Their next steps are unchanged in
  `outstanding-work.md`. For (e) a run needs the whole campaign from the title or a way to start a title-chosen game on a late
  level, which the harness does not have; the code path is covered by `override_test`.
