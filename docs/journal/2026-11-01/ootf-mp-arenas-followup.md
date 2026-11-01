# OotF PvP arenas: weapons, air arenas, dark Relic (2026-11-01)

Follows [ootf-coop-pvp-in-game.md](ootf-coop-pvp-in-game.md) ("Open: nobody can fight", air arenas, dark Relic). Runs are private
(`build/ootf-run/mp3..mp13`, harness output `build/harness/mp*-relic|aod`), packs built with `editor/scripts/ootf-convert.ts`.

## 1. Weapons: fixed

The arena already had a `loadout =` key (resources the factory creates and the stow routine files into the inventory). The converted
levels (template LEVEL1NA) spawn the club `0x3372b`, great sword `0x31f30`, iron club `0x3372a` and axe `0x33734` as loadout
(single instance, `DRAKAN_ARENA_LOADOUT`: factory actors live, class `0x1000e`), so no engine work was needed. `ootfArenaFromPc` now writes
`loadout = 0x3372b 0x33734` and a `pickup =` (30 s respawn, terrain + 120) for every PC **Weapon Pad** (Relic 8, resources rotating over
great sword / iron club / club; the axe is *not* spawnable as a pickup in these levels: `never registered, disabled`, so it is loadout only).
Two instances on Relic (`mp3-relic`): both `armed: right hand holds actor ... (class 1000e)` (the melee skill is raised to the weapon's
need, as on Drakan 2 maps), 3 kills, match over (`player 1 wins`), pickups of all 8 pads came back after the respawn time. The
pickup-claim checks of the harness (A walks to pickup 0, `claim refused: too far`) failed there because the test aid's pickup is far from
A's spawn on this map; in the `mp5` / `mp6` runs the claim was accepted and B's copy removed. Editor test `ootf-arena.test.ts` (loadout, pickups on Weapon Pads).
PC weapon objects (`Basic : Katana / Axe / Maul / Cutlass`, `MP : Fire Axe ...`) are not mapped: they have no PS2 class and the pad
positions carry the same information; the potions / life crystals (`MP : Greater Life Crystal`) cannot be spawned (potions never register).

## 2. Air arenas: Rynn starts on Arokh

Dragon pads, Soul Crystal etc. stay as `marker` lines (the game does not read them). The ground fall of the first attempt was not a missing
floor: the level is a canyon, the spawns are fine; it is that Rynn on foot is not meant to be there. A level starts mounted when it holds a
**Dragon object (rfl 28) with `Initial Mode` 0** (Ravenshold: Dragon `Initial Mode 0`; Surdana's parked Arokh has 1) - the unexplained
"Ravenshold is the only level that starts mounted" of coop-dragons-health-mount.md. `ensureSingletons` (formats, `Multiplayer/Air/*`) clones the
template Dragon with `Initial Mode` 0 at Rynn's start (test aid `DRAKAN_DBG_DRAGON=0|1`). In game (`mp4-aod`, single instance, tour of the five
spawns): Rynn is mounted (`extra_8000.png`: Arokh seen from behind over the canyon floor), the arena teleports the dragon between spawns.
Two instances (`mp6-aod`): both place at different spawns, ghost and **dragon ghost** of the other spawned (`dragon of player 1 spawned`,
link check intact). First run: **both crashed** (`missing-target` JALR 0 / 0x56d2c0 at `ra` 0x33d3c0 / 0x33eec8): the arena's loadout stow
ran on the controlled actor, which is Arokh, whose data object has no inventory. `drakan_arena.cpp`: `controlledIsDragon` (actor class
`0x1001c`) skips the loadout and the auto-equip. Re-run (`mp6b-aod`) after the rebuild: no `missing-target`, world dumps written.
Not done: a way to hurt anyone from a dragon (PvP damage is Rynn's melee only; mounted ghosts are shown but nothing claims hits), pad-based
`dragon =` spawn lines (the existing spawns come from the same pads, so no new map key was needed; backward compatible).

## 3. Dark Relic: not the level, not the lighting

Observation: Relic alone renders lit (dusk sky, ground, torches; `mp3-relic`, `mp6`, `mp7`, `mp8`, `mp10`, `mp11`), but black with a strip of floor in
`mpco-arena-relic` (the arena pack beside the campaign pack), `mp5` (Intro + RV + Wartok + Relic in one pack with `--campaign 1`), `mp9`
(same, Relic first) and `mp12` (same, `--tex-max 64`). The Relic LVL is **byte-identical** in the lit `mp3` and the dark `mpco` pack, so
the PC fog / ambient -> PS2 Level Properties mapping is not the cause. The log differs: dark runs report `level up: 110 actors, player at
32768 200498 32768` (the engine's default start), lit runs `156..171 actors, player at 22533 202497 26629`: the level's object list is not
(fully) used, Rynn stands on the fallback start, camera in the void. Correlates with the pack's shared containers: dark `RESOURES.MOD` 9.96 MB /
`RESOURES.TXD` 26.9 MB, lit at most 8.4 / 15.7 MB; `PROCTEES.TXD` size and the terrain texture size are not the cause (mp12: 5.4 MB, dark).
The separate-pack case `mpco` is dark with the arena's own (small) container files winning, so the extra `new/*` and `overrides/*` entries of the
campaign pack are enough. Not found: which limit (object / class table, resource memory) the combination breaks. Workaround: play arenas with
an arena-only pack (modding.md "Known problem"). Open for the converter: shrink the shared containers of big packs, or find the limit.

## 4. Menu start (OotF co-op) and the Ruined Village -> Wartok hop

See the bottom of this file (filled in after the run).
