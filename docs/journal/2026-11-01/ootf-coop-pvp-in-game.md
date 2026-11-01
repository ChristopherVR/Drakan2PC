# Co-op and PvP on Order of the Flame packs, run in game (2026-11-01)

Follows [mp-shared-coop-on-campaign.md](mp-shared-coop-on-campaign.md) and [game-select-ootf-boot.md](game-select-ootf-boot.md): the
co-op start level and the shared map list were only checked by reading code. This is the two-instance run.

## Setup

* Packs (private, `build/ootf-run/mpco/mods`, `DRAKAN_MODS`): `ootf-levels` = `ootf-convert.ts --campaign 1` of Intro, Ruined Village,
  Wartok Canyons; `ootf-arena` = a `Multiplayer/*` level on its own (`--id ootf-arena`).
* `editor/scripts/ootf-convert.ts` did not write the `maps/<level>.ini` that the editor's import dialog does (`ootfArenaFromPc` was only
  called from the browser). It does now for every `Multiplayer/*` level (it was swept into another session's commit a4eaec4).
* Two instances: A `DRAKAN_NET_HOST=<port> DRAKAN_NET_MODE=coop`, B `DRAKAN_NET_CONNECT=127.0.0.1:<port>`, `DRAKAN_GAME=d1`, no
  `DRAKAN_START_LEVEL`, own working dirs and memory cards (helpers of `netplay_two_instances.py`; driver kept in `build/ootf-run/mpco/run.py`,
  not in the repo). `DRAKAN_LEVELCHANGE=0@1500` on A fires the Intro's exit.

## Co-op on the campaign: works

* Both instances load `OotF 00 - Intro` (New Game opens the campaign start through the `Level 0 - Intro` redirect; `[game] playing d1`).
* A takes the exit: `level change: telling the others (exit 200448 207035 164352, destination hash 00000000)`, B logs
  `player 1 changed level through the exit ... following`, `level follow: arrived in the leader's level`, then `follow exit ...: found` and
  both end in `OotF 01 - Ruined Village` (hash 6306caae) with ghosts of each other (`ghost of player 2 'Bravo' spawned`), co-op keying 79 creatures
  and 42 watched objects on the client. Screenshots of both: `build/harness/mpco-coopT2/{a,b}/*.png`, contact sheets
  `build/harness/mpco-coopT2_{a,b}_sheet.png` (each sees the other Rynn and the other's name / health in the party HUD).
* The destination hash is 0 for OotF exits (`drakan_levelchange::exits()` has no destination names for the converted levels), so the
  follower cannot verify the level it landed in, only that it followed. Open (cosmetic for the 2-level chain; matters for the mismatch recovery).
* The Ruined Village arrival is the known "flat sky, rock silhouette, no terrain" picture (ootf-campaign-e2e.md), unchanged.

## Found and fixed

1. **The Intro crashed after load in about half of the co-op runs** (both instances, `missing-target` JALR 0 at 0x10822c, `ra` 0x108234 =
   the null per-object controller of ootf-full-level-runs.md, a frame or two after `afterLevelLoad`; solo runs of the same pack did not
   show it in 2 tries, co-op: 4 of 6, 2 of 4, 4 of 5 failures in different packs). Bisect on the converter side (`DRAKAN_DBG_DROPRFL` /
   `DROPNAME`, 4 coop runs each): dropping the ambient creatures (rfl 65, Birds) did nothing (2 of 4 still failed); dropping the four skinned
   generic props **Owl Face, Owl, Aroch and the cinematic Rynn** (rfl 17 clones of the NPCS skinned templates Lil' Goblin / Temp Golem /
   Golem Palace / Stomp Gothral) passed 4 of 4. Fix (formats `ootfpipeline.ts`): in a level with a Stomp Player sequencer (engine
   class 51) objects of synthesized NPCS-template rfl 17 classes are dropped (logged `DROPPED skinned prop`). With it: 4 of 4 clean co-op
   loads of the Intro on the rebuilt runtime. The Intro loses its owl / Aroch / Rynn props; the Book level shares the rule (no sequencer
   props there are rfl 17 NPCS clones that matter).
   A cleaner fix would give those clones a working controller (the Alwarren / Rift Crystal family); this keeps co-op starts reliable.
2. **The Co-op Host page listed the arena base levels** (`OotF - Arena of Death (air)` next to the campaign levels). `mpState`
   (`drakan_settings_host.cpp`) now skips pack levels that are the base level of an arena map when the chosen game is Order of the Flame.
   Verified in the Co-op scope screenshot (`build/harness/mpco-pages-coop/a/a_host_form.png`): OotF 00 / 01 / 02 only, Game Type Co-op.
3. `ootfArenaFromPc` listed a pad that the PC level repeats at the same position once per record (Relic: 18 records for 9 places, so 18
   spawns; now 9). Test added.

## MP map list tags

`build/harness/mpco-pages-vs/a/a_host_form.png` (Multiplayer > Host Game, Versus scope, `DRAKAN_GAME=d1`): Surdana Meadow, City of the
Damned: Skeleton..., Inquisitor's Lair: The Depths tagged DRAKAN 2, `Arena of Death (air)` tagged DRAKAN 1 and CUSTOM; the preview
panel shows GAME. No Co-op card enabled in the Versus scope (card reads "Coming").

## Launch pads and spawn heights

Class names confirmed from the PC data: `Rynn Launch Pad` (ground maps: Relic, Grotto of Death, Hill of Fire ...), `DragonLaunch Pad`
(air maps), `RedDragon Launch pad`; the regexes match them. Heights: pads sit 509 units above the terrain (Rynn's own start 509, Relic
202557 pad vs 202048 ground), so `terrain + 400` is within 110 units of the PC start, same as the converted Rynn start.
**Air maps** (Arena of Death): the five `DragonLaunch Pad` spawns sit on terrain (ground 20000..35250) but the game hosts it, both place at
distinct spawns and the arena logic runs, then the on-foot players fall into the void (`placement of spawn 0 failed: the player is
153256 units from it after 40 tries (no floor there?)`, black screen, `build/harness/mpco-arena/a/a_arena.png`): air arenas are dragon
fights over a floating structure with no floor for Rynn. Hosting works, play does not until air arenas get dragon spawns (Dragon's Duel mode,
"Coming" in the menu). Ground maps are the PvP verification (see below).

## PvP arena from an OotF pack (ground map)

`arena_two_instances.py --map relic-ground --maps-file <the pack's maps/OOTFREND.ini> ... DRAKAN_GAME=d1 DRAKAN_MODS=<pack>`
(`build/harness/mpco-arena-relic/`, sheet `build/harness/mpco-arena-relic_sheet.png`): A hosts, B joins, both load `OotF - Relic (ground)`
through the Level 0 redirect, the campaign actors are removed (`removed 3 campaign actors`), the server hands out different spawns (A spawn
0 at 44032 202448 35840, B spawn 3 at 9216 202444 25600) and `placement of spawn 0 took after 1 try (161 units from the point)`: the
`terrain + 400` heights land on the floor. Checks passed: arena world mode, campaign removed, placed at a spawn, different spawns, save
vetoed, quick save / load ignored, no creatures left. Checks failed: everything needing a weapon.
**Open: nobody can fight in an OotF arena.** `[arena] armed: no weapon in the inventory`: a Drakan 1 New Game starts with an empty
inventory (the Drakan 2 one starts with the dagger, which the arena only equips) and `ootfArenaFromPc` emits no pickups (the PC maps'
weapon / power-up items are not turned into `pickup =` entries). The arena needs either a granted starting weapon or pickups; granting
needs the engine's add-to-inventory path (not found in the runtime yet). The Relic level also renders very dark (a slice of floor only in
the screenshots), unrelated to the arena layer.

## Not done

* Co-op started from the menu (Title > New Game > Co-op > Begin Game) on OotF: only the host / join pages were exercised (`mp_pages_check.py
  --coop`, Co-op scope, OotF levels); the match start itself was done with the `DRAKAN_NET_*` shortcuts.
* The Ruined Village -> Wartok Canyons second hop (`DRAKAN_LEVELCHANGE=0,1`) was started once (the Intro hit the crash above before the
  fix); not re-run after it.
* Run-to-run flake count of the Intro fix is 4 of 4 clean loads; the earlier failure rate was 50-80 %, so the sample is small but separated.
