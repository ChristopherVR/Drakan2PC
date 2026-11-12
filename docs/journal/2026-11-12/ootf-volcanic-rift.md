# Issue 5: Volcanic to Rift World, the rift box after the Runeblade (bisect, cause, fix)

The open item on issue #5 (pass 3 comment, posted 2026-11-08 09:06 local) read: "the rift box does not open after the
Runeblade pickup in the latest runs. It worked on 6 and 7 November, so this is being bisected." This note checks that
claim against the files, finds what is really broken in the installed pack today, fixes it and replays the route.

Private runs under `build/review/issue5/` (`runs/`, `packs/`, `evidence/`); no original data in this note.

## 1. The claim, checked

**"It worked on 6 and 7 November": true.** `vr14` (2026-11-06, pack from formats `918de7e`) and `full2` / `rift1`
(2026-11-07, formats `f5d3c6b`) took the blade with E after Rynn was *placed* on the pillar top (the stair was walked only
later, `st1` to `st6`), then mounted in rift box 2318: message box, save screen, Rift World
([Volcanic route](../2026-11-06/ootf-volcanic-route.md), [landing and stair](../2026-11-07/ootf-volcanic-landing-and-stair.md)).
`mkrw4` used a test pack that moved the blade next to Arokh. No run forced the inventory.

**"Then it regressed": not true, and already settled before this pass.** The 2026-11-08 bisect
([rift box bisect](../2026-11-08/ootf-volcanic-rift-box-bisect.md), commit `618ac481`, an hour after the issue comment) found
that the failing runs `vr2` / `vr5` / `vr6` never took the blade: their recipe skipped the fire-arrow puzzle, so E reached
through the unmelted ice. The same pass then played the whole route on a clean pack (`pack-c`, main `a87d79aa`, formats
`9e1856f`) to Rift World and the credits in one process (`full2`, commit `47c216c9`). The issue's status line was not
updated afterwards.

**But the installed pack is broken in Volcanic today, for another reason.** On the play-folder copy
(`build/review/baseline-play`, exe sha256 `4fddfbc8...`, installed `mods/ootf-levels.zip` sha256 `80e71df5...`, built
2026-11-09 15:30), a direct start of Volcanic stops the game within a second of the level going live:

```
[in] gui screen 14 -> -1
[guest-branch:missing-target] ... op=JALR source=0x10aa6c target=0x0 ... stack-ras= +a0:1090a8 +100:140c30 +1b0:135ec4 ...
[fatal] main thread unwound to pc 0 after a missing branch target ...
```

The null call is in object teardown at the first kill-list flush after the load (`0x10a340` / `0x10a980`, called through
`0x109058` from `0x140ba8`); the address varies run to run (`0x10a3e4`, `0x10a45c`, `0x10aa6c`), the 2026-11-01 symptom of
heap corruption ([full level runs](../2026-11-01/ootf-full-level-runs.md)). So with the installed pack nothing in Volcanic
can be played, the rift box included.

## 2. Bisect

Each step: `git archive` of main's `editor/` at a commit plus the formats submodule at the commit main pins there, its own
`node_modules` junctions, a full 23-level conversion (`--campaign 1 --template LEVEL1NA`, no `OOTF_TEST_*`, the owner's
English OotF disc image unpacked by the build kit's `extras.prepare_source`), then a direct start of Volcanic on the
baseline exe and the route's first step (`ootf_volcanic_route.py --to rune`, god mode). Scripts: `build/review/issue5/bisect.sh`,
`loadtest.sh`.

| main | formats | Volcanic load |
| --- | --- | --- |
| `a87d79aa` (2026-11-08 `pack-c`) | `9e1856f` | 1 of 1 loads |
| `207450df` | `07bfdcd` | 1 of 1 loads |
| `c046c707` | `9aec4ab` | 1 of 1 loads |
| `ee8c2111` | `62477e8` | 1 of 1 loads |
| `cfb9f289` | `ffd85bc` | 1 of 1 loads |
| `7411634a` | `d052dd4` | 1 of 1 loads |
| `d391271b` | `b027834` | 3 of 3 load |
| `3e0c8e64` | **`ac234e9`** "Show the OotF scene orcs with the Drakan 2 orc model and rig" | crash in 2 of 3 |
| `4b68428a` | `d762026` | crash 1 of 1 |
| `4c818a2a` | `f4f341e` | crash 1 of 1 |
| `ee8f5cd2` | `fa98a83` | crash 1 of 1 |
| HEAD (current) | `48f6c92` | crash 4 of 4 (one plain run, three with a call trace) |
| installed pack | (2026-11-09 15:30) | crash 2 of 2 (one with DLSS and audio on) |

`ddac274c` (formats `b396bf8`, the Intro hand-over timing) between the boundary commits was converted but not run (a script
edit broke that run); its change touches only the Intro's scene timing. The crash is intermittent, so a single load is weak
evidence: the 2026-11-09 scene-creature audit's one Volcanic direct start (`scenecre-volc`) loaded, and `3e0c8e64` loaded once
in three.

Isolating it on the current converter (each 4 runs):

| Pack | `NPCS.ADB` header (clips, highest id + 1) | Volcanic load |
| --- | --- | --- |
| current, both scene-orc rows off (`--ps2-model-map`) | 3520, 8092 | 4 of 4 load |
| only Heuge Yellow on | 3520, 8152 | 4 of 4 load |
| only Heuge Purple on | 3579, 8151 | crash in 3 of 4 |
| both rows off, `NPCS.ADB` padded with 59 unused clips | 3579, 8151 | 4 of 4 load |
| both rows on, Volcanic's four Heuge Purple objects (PC 421, 423, 426, 428) dropped (`DRAKAN_DBG_DROPIDS`) | 3579, 8211 | 4 of 4 load |

So neither the clip count nor the clip id range matters; the four Heuge Purple props of Volcanic on the disc orc class do.

## 3. Cause

`ac234e9` added `Heuge Purple` / `Heuge Yellow` (class 17 cut-scene props) as scene rows: the class is shown with the disc
Purple / Yellow Orc model, and each scene that moves such a prop must play the PC clips retargeted onto the disc rig
(`SceneTock.clips`) with remapped channels (`SceneTock.channels`). Both lookups go through `tockOf` in the sequence stage of
`ootfpipeline.ts`, which did `sceneTocks.get(cls)` with the level's own class object. But `sceneTocks` is keyed by the class
plan's objects, and the plan keeps one object per class signature (`sig`: name, engine class, defaults): the first level's.
Every level parses its own, equal class objects, so the lookup hit only in the level the plan's object came from. In a full
conversion that is Ruined Village; in Volcanic it missed:

* Heuge Purple actors fell through to the generic path, which found the PC Heuge Purple clips (they are still in the pack:
  with the purple row on, `RESOURES.ADB` keeps its clip count, unlike with the yellow row; why was not traced), so Volcanic's scenes 1 and 116 drove a prop on the 55-joint disc Purple Orc model with PC
  clips and PC channel numbers. That is the crash (the purple row alone reproduces it, and removing exactly those four
  props stops it). How the engine turns that into a null call in teardown was not traced further.
* Heuge Yellow actors found no clips at all: 27 more Volcanic scene actions were dropped ("32 cinematic action(s) had no
  compatible resource mapping" against 5 before), so the yellow orcs would not animate in those scenes. No crash.

Ruined Village was unaffected (its scene orcs are the plan's objects), which is why the 2026-11-09 checks in the opening
saw them right. The scene Wartok rows (`d052dd4`) have the same lookup; in the current pack no level other than Ruined
Village has scene Wartoks, so nothing else changed (see the pack diff below).

## 4. Fix

Formats `src/ootfpipeline.ts`: `sceneTockLookup(tocks, key)` falls back to the class signature when the identity lookup
misses (the same key the class plan dedupes on), and `tockOf` uses it with `sig`. Test `test/ootfscenetock.test.ts`
(two equal class objects from two levels find the same entry; another name or other defaults do not).

The regenerated pack (`packs/fix2`) differs from the current converter's pack in exactly two entries plus the build stamp:
`new/LEVEL1ON.SSD/0311_94_87.bin` and `0311_97_87.bin` (Volcanic's sequences, now naming the retargeted disc clips and
channels), and Volcanic is back to 5 unmapped scene actions. Volcanic loads in 6 of 6 direct starts on the baseline exe.

Harness: `scripts/harness/ootf_volcanic_route.py` checked the pickup against the fixed resource `0x346a3`, the Runeblade's
number in the 2026-11-08 packs; in packs from 2026-11-09 on it is `0x346b2` (same registry slot 1104). The blade step now
reads the resource of the weapon on the pillar top before E (`resource_of`), tests in `scripts/tests/test_ootf_volcanic_route.py`.
Without this the first proof run (`proof2`) reported a failed pickup although the RAM dumps show the blade
(slot 1104, `0x346b2`) in carry cell 0 and Timer 2331 (slot 1158) gone.

A level-change arrival behaves the same: Succubus Lair direct start, its ship exit fired by the probe
(`DRAKAN_LEVELCHANGE=1@600`; converted exits have no readable destination name, so by index). The installed pack crashed
in Volcanic at about frame 2,200 after the arrival in 2 of 2 runs (`0x10a3e4`); the fixed pack ran Volcanic to frame 7,000
and 7,200 (the end of the 200 s window) in 2 of 2. The fixed pack also loads Volcanic on the new full-build exe
(`build/review/play`, sha256 `f65e9a36...`, 2 of 2).

## 5. Proof run

`build/review/issue5/proof.py` (scratch) runs the route of `ootf_volcanic_route.py` with a gate step and screenshots, in one
process: `proof4`, own play folder `build/review/issue5/play` (a copy of `baseline-play`, exe `4fddfbc8...`, fixed pack
`e6676550...` installed beside `drakan-character-motion`), a direct start of Volcanic.

Settings that differed from the play folder's `drakan.ini`: windowed 960 x 540 instead of borderless 1280 x 960 (the
harness, shared screen), `title_menu = modern` (harness; no effect after a direct start), `DRAKAN_GAME=d1` and
`DRAKAN_START_LEVEL` / `DRAKAN_FAST_START` (direct start, no New Game), `DRAKAN_PLAYTEST_GOD=1`, a fresh empty memory card,
`DRAKAN_NO_ACTIVATE`, `DRAKAN_AUTOPRESS=0`. DLSS quality, audio (WASAPI) and HD movies were switched back on through the
environment and are active in the log. Without god mode (`proof1`, `proof-rune`) Rynn died in the first step at the ruin
(placed by teleport; the route was written for god mode), so every later step ran with it.

| Step | How | Result |
| --- | --- | --- |
| rune, lock, key, cage | placed beside each trigger, then walk / E | Lava Rune carried, lava drained, cage key, scene 9.6a frees Arokh |
| mount | walked into the landed Arokh (closed loop) | controlled actor `0x1001c` |
| **gate** (no blade) | mounted, placed 5,000 units west of box 2318 facing it, W held (passed 276 units from its centre, 7 in `proof2`); then placed inside the box and held there 200 s | no message box either way (`gate_flown.png`, `gate_inside.png`) |
| breath, land | flown into box 2898, breath on 2259; flown to the cave mouth, Cross held | 2259 gone; Rynn on foot |
| switch, button | E at the Succ Switch until the platform is at identity; E at the wall button | the arrow hits 2366, Timer 2368 runs, the ice cover melts |
| **blade** | Rynn **placed on the pillar top** beside the blade, E | weapon `0x346b2` (registry slot 1104, the Runeblade) in carry cell 0, Timer 2331 (slot 1158) gone |
| remount | walked into Arokh from his right side | mounted |
| **rift** | mounted, the same placement 5,000 units west of the box | the message box opened at once on that spot (4,975 units from the box centre, before W moved him: the spot is inside the detector's volume), "In order to progress beyond this point the game must be saved" (`rift_message_box.png`), Cross, the Save Game screen "Level complete" (`rift_save_screen.png`), `[levelchange] exit taken in 'OotF 11 - Volcanic'`, Rift World on Arokh at (342418, 100119, 319535) with the blade carried (`arrived.png`) |
| fly | the tunnel, hall and shaft by closed loop | 14 of 14 waypoints |
| heads | breath rounds from placements | all four heads at -1 hit points (110 rounds, 197 s) |
| ending | none | the mirror-room scene (+30 s, `end_030.png`), the campaign end card (+60 s, `[hle] credits: read ... patched entry 8807`), the Order of the Flame title with "Continue: Rift World" (+69 s on, `end_090.png`) |

No crash, clean scripted exit. `proof2` (same settings, before the harness fix) passed the same steps up to the blade, including
the gate. Evidence: `build/review/issue5/evidence/` (`proof4/` screenshots, `result.json`, `log_lines.txt`, `ini_diff.txt`;
`proof2/` gate shots and the RAM check of the pickup; `crash/` and `arrival/` log lines; `fix2_loads.txt`).

## 6. Not established

* How the engine turns PC clips on the disc orc rig into a null call in teardown (the trace stops at `0x109058` /
  `0x10a980`); the fix restores the retargeted clips, and the crash is gone in every run since, but the engine side is inferred.
* Real-play conditions: the pickup, the remount and the rift entry each started from a placement next to the trigger, with
  god mode, from a direct start (no Volcanic save of the owner exists). The stair to the blade was not walked in this pass
  (it was on 2026-11-07, `st1` to `st6`). The rift entry fired at the placement, so a flown entry into the box after the
  pickup is not shown here (the gate's flown pass without the blade is).
* Whether the Volcanic yellow and purple scene orcs now look right in their scenes (1, 116, 94 / 97 in the pack) was not
  watched; only the log (5 unmapped actions as before `ac234e9`) and the pack diff say they get the disc clips.
* `ddac274c` (formats `b396bf8`) was converted but not loaded; the isolation runs on the current converter carry the result.
* The harness flaked once at `lock` (`proof3`: the rune carried, the lava never drained in 12 tries, most likely the
  inventory equip keys); not investigated.
* The installed pack in `dist/DrakanPC` and the builder's pack still have the bug until a pack is regenerated with this
  converter.
