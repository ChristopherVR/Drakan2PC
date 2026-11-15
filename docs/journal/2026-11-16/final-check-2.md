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
  (`dist_listing_start.txt` / `dist_listing_end.txt`, the second taken after the last run), and nothing under it is newer than `marker`.
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
| 1 | Owner's saves load and play; 14 Nov save refused / loads with `save_pack_check = 0`; input ini upgrade | **Pass** | `runs/s13`, `runs/s33_still`, `runs/sroot`, `runs/d2cat`, `runs/s1def`, `runs/s1chk0`, `ev/drakan_input_ini_v3_upgrade.diff`, `ev/uidclass_current_pack.txt` |
| 2 | Ruined Village camp and push tree | New game: **Pass** (camp, standing tree); felled tree on a new game **not run**. Owner's saves: **Fail** (flame at the tree, camp props gone in Slot33) | `runs/camp`, `runs/camp_s13`, `runs/camp_s33`, `runs/tree_fell`, `ev/camp_*.jpg`, `ev/tree_mover_in_saves.txt` |
| 3 | Arokh awakening | New game: **Pass** (clips, statue then break, lightning, no living Arokh beside the statue, mounted control after). Owner's Slot33: **Fail**, the statue is missing | `runs/aw_fresh`, `runs/lair_look`, `runs/awaken_s33`, `runs/awaken2`, `ev/awf_statue.jpg`, `ev/lair_look_crop.jpg`, `ev/aw2_statue*.jpg`, `ev/aw_bond.jpg`, `ev/aw_s33_clips.txt`, `ev/statue_prop_in_saves.txt` |
| 4 | Arokh on mouse and keyboard | New game: **Pass** (steering, breath, landing, walking, take-off, dismount; the mouse bobs his nose on the ground for about a second). Owner's Slot33: **Fail**, no breath | `runs/rw_ground`, `runs/rw_flight`, `runs/fresh_flow2`, `runs/mount_wc`, `ev/rw_ground_trace.txt`, `ev/rw_ground.jpg`, `ev/fresh_flow2_dragon_trace.txt`, `ev/mount_wc_dragon_trace_summary.txt`, `ev/ff2_a.jpg`, `ev/mwc_a.jpg` |
| 5 | Fire Crystal | **Pass** (native and modern Tab screen) | `runs/cry_native`, `runs/cry_modern4`, `ev/cry_native.jpg`, `ev/cry_modern4.jpg` |
| 6 | Ballistas, Islands trees, steep rock | **Pass** | `runs/wc_ball`, `runs/wc_ballfire`, `runs/islands`, `ev/wc_ball_sheet.jpg`, `ev/islands.jpg`, `ev/wc_peaks_crop.jpg` |
| 7 | Earlier fixes; stability | **Pass** | `runs/ootf_open` (music, hair), `runs/dist`, `ev/dist.jpg`, `ev/stability_scan.txt` |
| 8 | Runtime and formats tests | **Pass**: ctest 204 / 204 (6 skipped), vitest 765 / 765 (76 files) | `rt-test.log`, `vitest.log` |

### 1. Saves

| Save (copied into `Slot33`, F8 from the title) | Seen |
| --- | --- |
| `mc0_ootf/Slot13` (`s13`) | loads at the Book's return pad, walk, F5 writes record `da1e...`, walk back, F8 loads it again, walk |
| `mc0_ootf/Slot33` (`s33`, `awaken_s33`, `s33_still`) | loads in Arokh's lair in front of the Soul Crystal; one step on the crystal starts the awakening, so `s33_still` only turns: F5 writes record `da1e...`, F8 loads it again |
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

Likely cause (inferred from the save files, `ev/tree_mover_in_saves.txt`; the record positions were not decoded): Slot13
and Slot33 each hold two saved objects of class `0x13a2b` (14891, the class of the "Special Effect" the tree Mover
carried, `rv-campfire.md`); the 14 Nov Slot1 has none. The load restores saved objects, so the pack's
`Special Effect = 0` does not remove them. The camp props in Slot33 are probably restored in the hidden state the old
toggler gave them (not traced). Not checked: whether walking through detector 2716 shows them again.

### 3. Arokh awakening

* Arokh animates: 21 different clips over the scene from Slot33 (`ev/aw_s33_clips.txt`); rears with wings spread, talks
  with neck and head, bows (`ev/aw_start.jpg`).
* Stone Arokh: on a new game (`aw_fresh`, `ev/awf_statue.jpg`) the stone statue stands on the pedestal at 4.1 s, then the
  living Arokh breaks out with the stone pieces flying (5.0 s). From the owner's Slot33 (`awaken2`, dense shots
  `ev/aw2_statue.jpg`, brightened `ev/aw2_statue_bright.jpg`) the pedestal is empty at 3.7 to 4.7 s and the pieces still
  fly. Likely cause: the only object the current pack's Ruined Village has that the save-compat pack (same build) had not
  is uid 3353, class 3:18644 (`ev/statue_prop_in_saves.txt`; formats `2ef69c4` gave the blow-up prop "a free id below
  the level's largest"; the class name was not looked up), and none of the three saves lists uid 3353 in its object table
  (0x21), so a load does not create it. The build stayed `da1e...` because added objects keep it (`save-compat.md`,
  "Adding objects keeps the build ... an old save then lacks the new objects").
* Bonding lightning: a bolt from Arokh to the crystal at 108.3 s (`ev/aw_bond.jpg`).
* Living Arokh before the scene: not visible. New game (`lair_look`, Rynn placed level with the pedestal from the south,
  west and east, shots at once): only the stone statue stands on the pedestal (`ev/lair_look.jpg`,
  `ev/lair_look_crop.jpg`); his actor sits at the statue's spot (485248, 29633, 198535). From Slot33 his actor is at
  (484416, 24773, 201196), above where Rynn stood; the look shots round her (`ev/aw_pre.jpg`) did not pitch up.
* After the scene: "the game must be saved" prompt, Save Game, `level 'OotF 03 - Wartok Canyons' loaded`, Rynn on Arokh;
  mouse turns and W move him (`mount_wc`, `fresh_flow2`).

### 4. Arokh on mouse and keyboard (`dragon_mode = mouse`)

Ground and air are told apart by his model clip (`track.jsonl`): Hover `20663`, landing `2063f`, Ready `20647`, Walk
`2064d`, strafe `2064b`, take-off / climb `2063b` / `2063c`, dismount `20645`.

Rift World, new game (`rw_ground`, trace `ev/rw_ground_trace.txt`, frames `ev/rw_ground.jpg`; Arokh placed over the
landing spot of `rw_flight`, Ctrl held until the Ready clip, then released):

* Landing: `2063f` then Ready at y 81,919 (the ground there), nose within 0.01 rad.
* Walking mounted: W gives the Walk clip, x 366,433 -> 367,298 on the ground; D gives the strafe clip and moves him
  sideways; a 300-count mouse turn turns him 0.600 rad in the same ticks.
* Take-off: Space for 3 s from Ready lifts him (`2063b` / `2063c`, y 81,939 -> 108,548). A second landing and a second
  take-off from the ground (y 81,971 -> 104,887) work too, then a third landing.
* Dismount: holding Ctrl on the ground plays `20645` and gives Rynn back (63.6 s).
* Nose on the ground: `+0x94` is 0 there (0x10 while moving), never bit 3, so the mouse pitch is applied. 50 counts up
  put the nose at +0.09 rad, 400 counts at +0.74 (40 degrees) within five ticks; the game levels it in about 1 to 1.5 s
  (0.07 after 1.0 s), and the same downwards (-0.74, back to -0.07). Nothing builds up or stays; with a large flick the
  nose visibly bobs for about a second. In flight (`rw_flight`) 300 counts turn exactly 0.600 rad and 100 counts up
  give +0.2 rad and a climb; during the landing bit 3 is set and the nose stays level.
* Breath: left mouse fires fireballs in Rift World and, on a new campaign after the awakening, in Wartok Canyons on the
  ground and in the air (`fresh_flow2`: amber breath orb, 6 breath projectiles, `ev/ff2_a.jpg` gbreath / fbreath /
  fbrhold). Wartok Canyons after the awakening starts in a cave with Arokh on the ground (Ready / Walk clips at y
  10,835); Space did not lift him there (twice, 3 s; likely the cave roof, not looked into).

Owner's save: **from Slot33 Arokh has no breath.** After the awakening and the save, Wartok Canyons starts with a grey
breath orb and eight left clicks plus a 3 s hold give no breath projectile (`mount_wc`, `ev/mwc_a.jpg`); the same route on
a new game has the amber orb and breathes. Likely the saved dragon / power-up state from the pack before the breath
stand-in (`ootf-fire-crystal.md`); not traced.

Earlier runs `rw_flight2`, `rw_flight3`, `rw_flight4` and `rw_ground_hover` ended in hover (the landing bit appears
during the descent, and a Ctrl tap after a landing dismounts); they count only for the flight parts.

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
* 35 game runs: no crash, watchdog, VU1 report, device hang or removal (`ev/stability_scan.txt`, taken after the last run;
  the water pipeline's watchdog setting line is left out). Every run ends in `[run] shutdown` except `awaken2`, which I
  ended after its plan went wrong at the save screen.

### 8. Tests

`cmake --preset runtime-tests && cmake --build --preset runtime-tests --parallel 8 && ctest --preset runtime-tests`: 100 %
of 204 passed, 6 skipped (data-dependent), 813 s. `editor/packages/formats`: `vitest run` 765 passed (76 files).

## What a fix needs

1. **Saves made before a pack fix keep the old objects** (items 2, 3, 4): loading the owner's 15 November saves brings back
   the tree Mover's flame objects (class 14891), the hidden camp props, the breathless Arokh, and leaves out the new
   statue prop 3353. The build check passes all of these because the save layout only grew. Options: on load, drop saved
   class-14891 objects owned by a tree Mover and create level objects the save does not name; or give such fixes a new
   build so old saves are refused (with `save_pack_check = 0` as the way back).
2. **Mouse pitch on the ground** (item 4, minor): the mouse pitch is not gated while he stands or walks (`+0x94` 0 / 0x10;
   Ready / Walk clips), so a mouse flick bobs his nose by up to the 0.75 / 1.0 rad clamps for about a second before the
   game levels it. Gate it on the ground state if that is not wanted.

## Not checked

* The felled push tree on a new game (no weapon on a direct start); walking through detector 2716 from Slot33 to see whether the camp comes back.
* The quick slot for crystals; Ice and Lightning crystals.
* Ruined Village after mounting (the awakening goes straight to the save prompt and Wartok Canyons).
