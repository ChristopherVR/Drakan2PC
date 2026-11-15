# Final check of the 15 November late build (2026-11-16, second pass)

One verification pass over the build in `dist/DrakanPC` (`drakan.exe` 22:39, md5 `ddb77d9e...`, the same file as
`build/final-game/bin/drakan.exe`; `mods/ootf-levels.zip` 22:40, md5 `accb6cc0...`, save build `da1e598e3d3adfdd`
kept; formats at `2ef69c4`). No source was changed. Scratch, play folder, tools, runs and pictures:
`build/review/final-1116b/` (private, not tracked). Nothing from either game is quoted here.

## Set-up

* `play/` is a full copy of `dist/DrakanPC` (with `mods/`, `textures/`, the owner's `drakan.ini`, `drakan_input.ini` and
  every `mc0*` save folder); `drakan.ini` is unchanged except `elf =`, which points at a private ELF copy (`elf/`).
  `before/` holds the owner's ini files and save folders as they were before the first run.
* `dist/DrakanPC` was not written: its top-level file list and times are the same at the end
  (`dist_listing_start.txt` / `dist_listing_end.txt`), and nothing under it is newer than `marker`.
* Drivers (`tools/`): `savrun.py` (a private card copy whose `Slot33` holds one of the owner's save folders, title ->
  F8, recipes `f8load2.txt` / `f8load3.txt`: walk, F5, walk back, F8, walk; `INI:key=value` edits `play/drakan.ini` for
  one run and restores it), `livesave.py` (the same card set-up or a direct level start, then a live plan; logs the
  controlled actor with its rotation, the dragons, the scene tick and breath projectiles every 0.1 s; `land` dives in
  pulses until the dragon's landing bit), and copies of `awaken.py`, `crydrive.py` (plus `__unset` to drop an env key),
  `wdrive*.py`, `opening.py`, `uidclass.py`, `lvldump.ts`, `msum.py`, `sheet2.py`. `DRAKAN_DRAGON_TRACE=1` in the Arokh
  runs. At most two games at a time.

## Results

| # | Check | Result | Evidence (under `build/review/final-1116b/`) |
| --- | --- | --- | --- |
| 1 | Owner's saves load and play; 14 Nov save refused / loads with `save_pack_check = 0`; input ini upgrade | **Pass** | `runs/s13`, `runs/s33`, `runs/sroot`, `runs/d2cat`, `runs/s1def`, `runs/s1chk0`, `ev/drakan_input_ini_v3_upgrade.diff`, `ev/uidclass_current_pack.txt` |
| 2 | Ruined Village camp and push tree | **Pass on a new game, Fail from the owner's saves** | `runs/camp`, `runs/camp_s13`, `runs/camp_s33`, `runs/tree_fell`, `ev/camp_*.jpg`, `ev/tree_mover_in_saves.txt` |
| 3 | Arokh awakening | **Pass on a new game, statue missing from the owner's save** | `runs/aw_fresh`, `runs/awaken_s33`, `runs/awaken2`, `ev/awf_statue.jpg`, `ev/aw2_statue*.jpg`, `ev/aw_bond.jpg`, `ev/aw_s33_clips.txt`, `ev/statue_prop_in_saves.txt` |
| 4 | Arokh on mouse and keyboard | **Pass** (new game); no breath from the owner's save; the mouse still pitches him on the ground | `runs/rw_flight4`, `runs/fresh_flow2`, `runs/mount_wc`, `ev/rw_flight4_trace.txt`, `ev/fresh_flow2_dragon_trace.txt`, `ev/mount_wc_dragon_trace_summary.txt`, `ev/rw4.jpg`, `ev/ff2_a.jpg`, `ev/mwc_a.jpg` |
| 5 | Fire Crystal | **Pass** (native and modern Tab screen) | `runs/cry_native`, `runs/cry_modern4`, `ev/cry_native.jpg`, `ev/cry_modern4.jpg` |
| 6 | Ballistas, Islands trees, steep rock | **Pass** | `runs/wc_ball`, `runs/wc_ballfire`, `runs/islands`, `ev/wc_ball_sheet.jpg`, `ev/islands.jpg`, `ev/wc_peaks_crop.jpg` |
| 7 | Earlier fixes; stability | **Pass** | `runs/ootf_open` (music, hair), `runs/dist`, `ev/dist.jpg`, `ev/stability_scan.txt` |
| 8 | Runtime and formats tests | **Pass**: ctest 204 / 204 (6 skipped), vitest 765 / 765 (76 files) | `rt-test.log`, `vitest.log` |

### 1. Saves

| Save (copied into `Slot33`, F8 from the title) | Seen |
| --- | --- |
| `mc0_ootf/Slot13` (`s13`) | loads at the Book's return pad, walk, F5 writes record `da1e...`, walk back, F8 loads it again, walk |
| `mc0_ootf/Slot33` (`s33`, `awaken_s33`) | loads in Arokh's lair in front of the Soul Crystal; one step on the crystal starts the awakening |
| card root `mc0_ootf/OotF 01 - Ruined Village.rsg` (`sroot`) | loads in the village (an opening scene plays first, so the F8 helper logs `quick load: timed out; native screen restored` while the level is already loaded), walk, F5, F8, walk. `scripts/savetool` cannot parse this file ("index table does not run to EOF"); the game reads it |
| `mc0/Slot1` Catacombs, Drakan 2 (`d2cat`) | loads, walk, F5 (the new record names only the motion pack: `save_scope` works), F8 |
| `mc0_ootf/Slot1` 14 Nov (`s1def`) | refused: `different build ... quick load refused: This save was made with a different version of the mod pack` |
| the same with `save_pack_check = 0` in `drakan.ini` (`s1chk0`) | `loaded anyway (save_pack_check = 0)`, walk, F5, F8 |

The first run without god mode (`s13_nogod_fell`) walked Rynn off the pad and she died in the fall; the later runs use
`DRAKAN_PLAYTEST_GOD`. `uidclass.py` against the shipped pack: Slot13 418 + 19 saved level objects and Slot33 491 with the
same class, Slot1 3 different (uids 3376, 3380, 3383, as `save-compat.md` says).

`drakan_input.ini` after the first run differs from the owner's in two lines only: `controls_version = 2` -> `3` and
`dragon_mode = stick` -> `mouse` (with the new comments); every binding and other key is unchanged. The game also added
`save_pack_check = 1` with its comment to `drakan.ini` (`ev/drakan_ini_after_runs.diff`).

### 2. Camp and push tree

New game (direct start, `camp`): fire pit with its stone ring, spit, log, crates, barrels and three grunts round the fire
(`ev/camp_v0_crop.jpg`, `ev/camp_sheet.jpg`); no flame at the standing push tree from three sides. The felled tree was
not seen on a new game (no weapon on a direct start).

From the owner's saves the old state comes back:

* `Slot13` (`camp_s13`, `tree_fell`): the camp is complete, but a flat square of flame stands beside the standing push
  tree (`ev/camp_s13_sheet.jpg`, `ev/tree_fell_sheet.jpg`); sword hits did not fell the tree.
* `Slot33` (`camp_s33`): the tree is felled across the gap and a thin vertical flame hangs in the air over it; the camp
  shows the fire only: no pit, spit, log, crates, barrels or grunts (`ev/camp_s33_sheet.jpg`).

Cause, from the save files (`ev/tree_mover_in_saves.txt`): Slot13 and Slot33 each hold two saved objects of class
`0x13a2b` (14891, the flame class the tree Mover's "Special Effect" spawned); the 14 Nov Slot1 has none. The load restores
them from the save, so the pack's `Special Effect = 0` cannot remove them. The camp props in Slot33 are restored hidden (the
toggler's Hide fired before the save). Not checked: whether walking through detector 2716 shows them again.

### 3. Arokh awakening

* Arokh animates: 21 different clips over the scene from Slot33 (`ev/aw_s33_clips.txt`); rears with wings spread, talks
  with neck and head, bows (`ev/aw_start.jpg`).
* Stone Arokh: on a new game (`aw_fresh`, `ev/awf_statue.jpg`) the stone statue stands on the pedestal at 4.1 s, then the
  living Arokh breaks out with the stone pieces flying (5.0 s). From the owner's Slot33 (`awaken2`, dense shots
  `ev/aw2_statue.jpg`, brightened `ev/aw2_statue_bright.jpg`) the pedestal is empty at 3.7 to 4.7 s and the pieces still
  fly. Cause: the statue is level object 3353, the one object the current pack's Ruined Village has that the save-compat
  pack had not (`ev/statue_prop_in_saves.txt`: added uid 3353, class 3:18644); none of the three saves lists uid 3353 in
  its object table (0x21), so a load does not create it. The build stayed `da1e...` because added objects keep it
  (`save-compat.md`, "Adding objects keeps the build ... an old save then lacks the new objects").
* Bonding lightning: a bolt from Arokh to the crystal at 108.3 s (`ev/aw_bond.jpg`).
* Living Arokh before the scene: not visible. On a new game his actor sits at the statue's spot (485248, 29633, 198535)
  and only the statue shows; from Slot33 his actor is at (484416, 24773, 201196), above where Rynn stood, and eight look
  shots round her show no dragon (`ev/aw_pre.jpg`).
* After the scene: "the game must be saved" prompt, Save Game, Wartok Canyons loads with Rynn on Arokh, and mouse turns
  and W move him (`mount_wc`, `fresh_flow2`).

### 4. Arokh on mouse and keyboard (`dragon_mode = mouse`)

Rift World direct start (`rw_flight4`, trace `ev/rw_flight4_trace.txt`): 300 mouse counts turn him exactly 0.600 rad in
the same ticks; 100 counts up give +0.2 rad nose and he climbs; `land` (Ctrl pulses) dives him to the ground, the landing
bit (`+0x94` bit 3) is set and the nose stays within 0.07 rad; W walks him on the ground (x 364090 -> 365753 at y
83576), a mouse turn on the ground is applied (-0.6 rad), Space for 3 s takes off (y 83599 -> 108291), a second
landing works, holding Ctrl on the ground dismounts (Rynn controlled at 64 s). Breath: left mouse fires fireballs in Rift
World (`ev/rw4.jpg`, gbreath) and in Wartok Canyons on a new campaign after the awakening (`fresh_flow2`, amber breath orb,
6 breath projectiles, `ev/ff2_a.jpg` gbreath / fbreath / fbrhold). No wild nose movement was seen in any of these.

Findings:

* **From the owner's Slot33 Arokh has no breath.** After the awakening and the save, Wartok Canyons starts with a grey
  breath orb, and eight left clicks plus a 3 s hold give no breath projectile (`mount_wc`, `ev/mwc_a.jpg`). The same route
  on a new game has the amber orb and breathes. The save carries the dragon / power-up state of the pack before the breath
  stand-in (`ootf-fire-crystal.md`); not traced further.
* **The mouse still pitches him while he stands or walks on the ground.** On the ground `+0x94` is 0 (0x10 while moving),
  never bit 3, so the mouse pitch gate does not apply. Rift World (`rw_flight4`): 50 counts up set the nose to +0.11 rad and it
  stays there while he stands and walks until the mouse brings it back. Wartok Canyons after the level change
  (`fresh_flow2`, `mount_wc`): 1000 counts up / down put the nose at the +0.75 / -1.0 clamps in two ticks and the game
  levels it over 1 to 2 s; with the D strafe on the ground the nose drifted to +0.36 with no mouse input
  (`ev/mount_wc_dragon_trace_summary.txt`, frames 7500 to 7572).
* In the Wartok Canyons arrival cave Space did not lift him (y stayed 10834.7 for 3 s, twice); in the open in Rift World it
  did. Not looked into (likely the cave roof).
* Keyboard: Ctrl is Cross, so after a landing any Ctrl tap dismounts; the first three Rift World runs dismounted that way
  before the ground part of the plan.

### 5. Fire Crystal (Wartok Canyons, picked up, Rynn by Orc 135)

* Native screen (`cry_native`): `[crystals] Fire Crystal (resource 342a3) used`, `effect 40c07 spawned at Rynn ... (ok)`,
  the ring runs out from Rynn and sets the orc on fire (40 -> 25), the stack goes from 2 to 1 (`ev/cry_native.jpg`).
* Modern Tab screen (the owner's `inventory_screen = modern`, `cry_modern4`): Enter on the Fire Crystal logs the same two
  lines, the ring shows round Rynn, the stack goes from 2 to 1 (`ev/cry_modern4.jpg`). The modern screen's item order
  changed between runs (Health Elixir first in one, the crystal first in another), so a keyboard plan has to check the
  selection (`cry_modern2` used the elixir instead). The quick slot was not tried.

### 6. Ground

* Ballistas 110, 51, 50 stand on their disc bases on the ground (`ev/wc_ball_sheet.jpg`). They fire: with Rynn 4,300
  in front of ballista 110 a projectile (class `0x10008`) is born and her hit points drop 100 -> 85 (god mode refills them),
  and two more near ballista 51 (`runs/wc_ball/track.jsonl`). In `wc_ballfire` (no god mode) an orc killed her before the
  ballista fired.
* Islands: Fan Palms 1841, 1861 and the trees by 1479 / 1481 stand with their trunks in the ground (`ev/islands.jpg`,
  seen from Arokh: the dismount step did not dismount there).
* Steep rock: the Wartok faces show rock plates and snow streaks, not smooth streaks (`ev/wc_peaks_crop.jpg`).

### 7. Earlier fixes, stability

* OotF opening music (`ootf_open`, no input, `msum.py`): `track e0006 starts now (fade in 7.00 s), e0008 fades out (3.00 s
  left)` at 74.5 s, e0008 gone at 77.5 s, `scene stop: category 2, fade 9.00 s`, category empty at 189.5 s: the same as in
  `final-check.md`.
* Ponytail (`ootf_open/hair.csv`, `ponytail/tools/summ.py`): stand median 20, slow 35, walk 49 (>60: 36 %, n = 377), run 70;
  in the range of `final-check.md` (RV walk 47).
* Distance view west of the Human Bridge (`ev/dist.jpg`): bank, river, hills drawn, the house on its hill, no holes.
* 31 game runs: no crash, watchdog, VU1 report, device hang or removal (`ev/stability_scan.txt`; the only match is the
  water pipeline's watchdog setting line). `awaken2` was ended by me after its plan went wrong at the save screen.

### 8. Tests

`cmake --preset runtime-tests && cmake --build --preset runtime-tests --parallel 8 && ctest --preset runtime-tests`: 100 %
of 204 passed, 6 skipped (data-dependent), 813 s. `editor/packages/formats`: `vitest run` 765 passed (76 files).

## What a fix needs

1. **Saves made before a pack fix keep the old objects** (items 2, 3, 4): loading the owner's 15 November saves brings back
   the tree Mover's flame objects (class 14891), the hidden camp props, the breathless Arokh, and leaves out the new
   statue prop 3353. The build check passes all of these because the save layout only grew. Options: on load, drop saved
   class-14891 objects owned by a tree Mover and create level objects the save does not name; or give such fixes a new
   build so old saves are refused (with `save_pack_check = 0` as the way back).
2. **Mouse pitch on the ground** (item 4): gate the mouse pitch when he is on the ground (the state with `+0x94` = 0 / 0x10
   and a constant height), not only on bit 3, or level him there.

## Not checked

* The felled push tree on a new game; walking through detector 2716 from Slot33 to see whether the camp comes back.
* The quick slot for crystals; Ice and Lightning crystals.
* Ruined Village after mounting (the awakening goes straight to the save prompt and Wartok Canyons).
