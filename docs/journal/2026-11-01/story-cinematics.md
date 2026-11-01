# Cinematics: played, edited and created in game (2026-11-01)

Harness: `scripts/harness/story_cinematic.py` (phases stock / edit / new); packs by `scripts/harness/story_cinematic/pack.mjs`
(the editor's own `ContentSession`, templates and `buildStoryPackFiles`). Screens under `build/harness/cine-*`.

## 1. A stock cinematic plays
Valley of the Fallen (`--level valley`): cut scene player 7577 (`Initial State` 1) plays sequence 497 at load: aerial establishing shot,
cut into the throne room, Zola Dane and the guards, subtitles, HUD hidden (`cine-valley-base/sheet.png`). Surdana's sequence 484 is a
conversation scene shown through the gameplay camera (HUD visible), which is why the 2026-10-31 camera edit showed nothing.

## 2. Edits show in game
* Camera key 0 y + 30000: aerial shot higher, drifting to key 1 (`cine-cam1`). All keys yawed 90 degrees: completely different views
  (`cine-cam3`). Camera track retimed x2.5, Zola animation swapped, key 0 re-aimed: harness `edit` phase (asserted, before/after in
  `cine-h-stock` / `cine-h-edit`).
* **Quaternion convention settled.** Forward = `R(q)^-1 * (0,0,-1)` (row-vector rotation, camera looks along -Z); for
  `q = qpitch(X) * qyaw(Y)`, `f = (cos p sin y, -sin p, -cos p cos y)`. Method: for all 1553 disc camera keys compare the view direction
  of 12 candidate models (6 axes x textbook / conjugate) with the direction to the nearest actor: only conjugate / -Z is non random
  (median 22.8 degrees, 56 % within 30; others 80 to 160 degrees). A yaw histogram over near pure-yaw keys peaks at `heading - yaw = -90`.
  Static Surdana cameras confirmed pitch sign (positive looks down) and that yaw steps rotate the view consistently. The old editor
  formula was wrong; `yawPitchQuat`, `lookAtAngles` fixed, `quatYawPitch`, `lookDirection` added, tests updated.
  Caveat: the Rynn start object is not where Rynn stands, so in-game landmark checks with her were misleading; the disc statistics are
  the proof, the game checks are consistent with it.
* Cinematics page: camera key rows now show yaw / pitch (editing rewrites the quaternion).
* `CutsceneSpec.camSetup` writes the disc's type 9 camera action (value 40 zoomed in strongly: a field of view).

## 3. New cut scene
Pack: new sequence 1411 (`new/LEVEL1ON.SSD`), cut scene player + trigger volume (1 cell) from the `cutscene-trigger` template in the
LVL. `DRAKAN_PLAYTEST_POS` teleports the player into the volume (inside run) or 4000 units away (outside run). Inside: the camera plays
the editor's keys (front view, then top-down), then control returns; outside: nothing. Scene keeps the HUD (no type 9 action).

## Not done
* Editing through the browser UI (the panels' code paths were driven in node, not clicked); the yaw/pitch fields have no browser shot.
* Sound step audio, message steps and the walk-in (as opposed to teleport) were not exercised.
* Frame to scene time is not linear on a loaded machine (uncapped cadence), so assertions use ordering / large image differences.
