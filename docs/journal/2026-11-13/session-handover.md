# Handover: issue work, performance, renderer and campaign session (2026-11-12 to 2026-11-13)

This note is for whoever continues after the session that worked through GitHub issues #5 and #6 and the owner's
follow-up requests. It says what is released, what is on `main`, what is still running or unfinished, which
decisions are open, and what was learned about running many agents on this machine. Details are in the journal
notes named in each section (`docs/journal/2026-11-12/`).

## State at hand-over

- Released: **v0.1.12** (12 November, built from `61735d1f`).
- `main` is ahead of that release with the commits listed under "On main, not released". A push of `main` cuts the
  next release automatically once CI is green (`.github/workflows/release-after-ci.yml`).
- The owner's play folder `dist/DrakanPC` was rebuilt at `13c488b4` (12 November, 17:32) with a reconverted Order
  of the Flame pack and a regenerated HD texture pack. It does not contain anything committed after that.
- Saves: the game now saves next to `drakan.exe`. The owner's saves were copied from `iso/mc*` into
  `dist/DrakanPC/mc*`; `iso/mc*` is unchanged.
- Order of the Flame saves made on an older pack do not load on a newer pack. Every pack rebuild needs a new
  campaign game. The pack of 9 November is kept in `build/review/baseline-play/mods/ootf-levels.zip`.

## Issue #6 (player report), item by item

| Item | State | Where | Not established |
| --- | --- | --- | --- |
| 1. Surdana cellar | Cause found by real play: the PC camera follow swings the camera through a wall and past the cellar's terrain layers; the engine then runs the world from the outdoor layer. Fixed by a view-only rule (`5757266f`, in v0.1.12) and a camera carry that shortens its travel lag by the turn (`5796df9d`, `e5fbc986`, on `main`). | `issue6-cellar-glitch.md` | The camera still slips out at the corridor mouth by the kegs (rule decisions 0.0 to 1.5 per lap). The rule stands aside for 600 view updates after an arrival, load or placement. A complete fix needs the engine's wall sweep `0x21b280` on a guest thread of its own (a nested call deadlocks). Bow aiming was not tested in game with the new carry. Nobody judged the camera feel by hand. |
| 1. v0.1.12 caveat | v0.1.12 contains the first carry attempt `a670ac32`, which pokes the camera into tunnel walls more often (Crystal Mine, Catacombs). The view rule still prevents the stuck state. Fixed on `main`. | same | |
| 2. Empty health orb, pixelated HUD | Fixed and seen in the live game. | `issue6-view-dependent-textures*.md` | |
| 2. Wrong world textures | Probably fixed by the same change. | same | The reporter's blocky hillside was never reproduced, nor "worse after ray-traced shadows until restart". |
| 3. Distant NPC textures | The far model of the fat villager mesh is broken on the disc; a shape-matched override keeps the middle model. | `issue6-distant-npc-textures*.md` | Seen in game for two of Surdana's five; other levels by data only. |
| 4. Modern bow aiming | Crosshair, floating arrow and the freeze fixed with evidence. | `issue6-modern-bow-aim.md` | "Stuck after a shot" never reproduced. Corridors, crossbows and full screen not tested. |
| Saves folder | Fixed with a migration that deletes nothing. | `issue6-save-location*.md` | A real installer upgrade from v0.1.11 was not run. |

The reporter was asked to retest on v0.1.12 (two comments on the issue). No answer yet.

## Issue #5 (Order of the Flame), what changed

Fixed in the converter (formats submodule) and noted on the issue: the Volcanic crash, Scavenger health (15, a
stand-in for fleeing), blade trap ropes, weapon child parts (mace head and others), the Book scene at 4:3 with
generated subtitles, the church stair walk-down, light marker cubes, native pressure plates and a real pushable
statue in the tomb of the Soul Crystal. Notes: `ootf-volcanic-rift.md`, `flesh-eater-combat.md`,
`ootf-axe-trap-ropes.md`, `ootf-church-cinematic.md`, `ootf-lair-push-puzzle.md`, `ootf-detector-squares.md`,
`ootf-texture-quality.md`.

Open: see the "Still open" list in the issue comment of 12 November, plus: subtitles are unreviewed machine
transcription (review list in `build/review/ootf-church/subtitles/review.txt`, not tracked); the guided builder does
not run the subtitle step; the Volcanic exit scene is excluded because relaying it hung the game; the tomb's kerb
does not stop the statue; plates in other levels were only seen sinking, their order was not played.

The owner's `dist` pack predates the ropes, weapon parts, Book, subtitles, puzzle and light fixes. It needs a pack
rebuild (`python scripts/buildtool/extras.py ootf ...`, then `Build-LocalGame.ps1 -Incremental -ModDirectory
build/ootf/mods`) and a new campaign game.

## On main, not released

- Reworked cellar camera (`5796df9d`, `e5fbc986`).
- Renderer backends share their texture cache, per-draw code and render target policy (`cf17e89e`, `9e502cd7`,
  `5162001f`); WebGPU has clean-block retention and exact partial uploads, switched off (`4089aaa9`).
- GPU vertex path increments, all behind `[video] vertex_processing` (default `cpu`): `28485c31`, `8d7c3740`,
  `14519428`, `5f04d0c4`, `aee5ef87`, `4ede1ec9`, `b9beb22e`.
- Converter pin for the tomb puzzle and the light markers (`dcc6a75b`, formats `21128e2`).
- Sharp title picture in the Original game list (`14bf3527`, already in v0.1.12).

## Work still running or unfinished

1. **Renderer shared code** (agent finishing): enabling the two WebGPU capabilities to match Direct3D 11, a
   correction commit for wrong GPU memory figures in three commit bodies, a CPU-time comparison, docs. Items 4 to 6
   of its list are not started (shared backend state, present, the scene-camera phase machine). Journal:
   `renderer-shared-code.md` (to be written by that agent), plan in `vulkan-backend.md`.
2. **GPU vertex path** (orchestrating agent with subagents): strips drawn by the shader are exact on Direct3D 11
   (with DLSS motion vectors) and on WebGPU, verified off `main` and waiting to land; the batch path runs live as
   records (CPU evaluation) and exists as an exact generated shader, not yet drawing. Next: land strips, batch
   shader draw path, a leaner batch hand-over, WebGPU, performance tables. Journals:
   `gpu-vertex-feasibility.md`, `gpu-vertex-implementation.md`, `gpu-vertex-batch-path.md`. Rules: the CPU path
   stays selectable for good; the default stays `cpu` until the owner has seen numbers.
3. **Performance measurements**: a gated benchmark queue (`build/review/perf2/tools/queue.py`, started as PID
   33228) fills in the old-against-new table of the 12 November performance changes when the machine is quiet.
   Summarise with `python scripts/perf/scene_ab_bench.py --summarise build/review/perf2/runs/<job>`. Journal:
   `perf-round2.md`.

## Is the game GPU-heavy now, and is DLSS better

- **GPU vertex path: not done.** The design is proven and the default is unchanged, so nothing a player runs uses
  it yet. Measured so far at 3840x2160 with DLSS Quality on a busy machine, strips only: VU1 worker 6.35 to 3.80 ms
  and GS worker 6.01 to 4.28 ms per frame in Surdana (ranges disjoint); frame time 9.58 to 6.68 ms (ranges
  overlap, so not established). Coverage with the batch path is 53 to 90 % of VU1 time; clipped runs stay on the
  CPU; the index-list pass saves nothing without a leaner hand-over; in OotF Ruined Village the hand-over
  bookkeeping currently makes the VU1 worker 14 % slower in records mode.
- **DLSS: better, not solved.** DLSS Quality at 3840x2160 went from 101.8 to 155.0 fps in Surdana on a quiet GPU
  (`1fa0fc3e`, in v0.1.12) because it now writes the window's pixels. It is still slower than DLSS off (175.6) in
  CPU-bound scenes, because DLSS adds CPU work per frame (object motion, scene-model capture). The GPU vertex path
  does not remove that yet: with DLSS on, characters' motion vectors come from the scene-model motion pass.
  DLSS with ray-traced effects on, where an upscaler should pay, has not been measured. Journal: `dlss-speed.md`.
- **End-of-frame wait**: an opt-in frame queue (`PS2X_FRAME_QUEUE=1`, `PS2X_READBACK=late`) cuts EE CPU per frame
  from about 22 to 7 ms in Surdana; its frame-rate, pacing and latency effects are unmeasured, so it is off.

## Open decisions for the owner

1. Direct3D 11 has no device-removed handling: after a graphics driver reset the next draw writes through a null
   pointer and the game crashes (found from the code; replays died that way during 24 driver resets on the night
   of 12 November). The WebGPU path ends in order. Wanted: a notice and a clean end.
2. Texture upscale decision for a texture inside a dirty scratch target (changes pictures by at most 2 of 255 when
   automatic upscaling is on and the HD pack is off); see the renderer agent's journal.
3. Mountains and terrain: `terrain_detail`, `terrain_cliffs`, `terrain_distance` exist (detail and cliffs off by
   default). A real improvement needs a rock material in world space on steep faces or terrain drawn from the
   height field. `gfx-terrain.md`.
4. Models: curved silhouettes need a locally generated pack that subdivides model records. `gfx-models.md`.
5. `material_detail` is off by default: no motion check, lit from the outdoor sun indoors. `gfx-materials.md`.
6. Book scene on wide screens: pillar box now; a blurred side fill needs a present pass. Mock-ups in
   `build/review/ootf-church/evidence/wide/`.
7. A real flee behaviour for Scavengers instead of the 15 health stand-in.
8. The complete camera wall test for the cellar (about a day).
9. Three performance changes of 12 November lean the wrong way in one scene under load and are unproven on a quiet
   machine (`perf-cpu.md`); the resource batch change alters guest timing with no proven gain
   (`perf-loading-hitches.md`).

## Housekeeping

- `build/review/` holds every agent's scratch. Raw run frames of finished agents were deleted on 12 November to
  free about 270 GiB; evidence folders remain. `build/review/gs-share/diag/px` (18 GiB) and
  `build/review/ootf-squares/snap/` (contains directory junctions into `node_modules`; remove by hand, not with a
  recursive delete through the junction) are left for the owner.
- `iso/saves-copied-to-play-folder.txt` is a note written by the save migration; harmless.
- Git worktree `build/review/gs-share/wt` belongs to the renderer agent and should be removed with
  `git worktree remove` when it finishes.
- Speech models for subtitles (faster-whisper small.en and base.en) were already in the user's Hugging Face cache.

## Lessons from running many agents here

- One GPU with 16 GiB: more than about four GPU jobs oversubscribes memory, makes every frame-rate number noise,
  and reset the graphics driver 24 times in one night. Cap replays at four (two at internal scale 8), one game
  instance per agent, and use the shared benchmark lock in `scripts/perf/scene_ab_bench.py`.
- Never point a build's `-DistDirectory` at a folder whose textures are a proof input: the build stages the shared
  HD pack from `dist` into it.
- Give each agent's play folder an `elf =` line that points at a private copy of the ELF, or the save migration
  writes a note into `iso/`.
- Reproduce by real play. Three "reproductions" of the cellar bug that began with a harness placement were
  artefacts; the real cause was found only after walking in through the door.
- The pre-push guard refuses a push while tracked build inputs have uncommitted edits. With agents editing, the
  tree is never clean; `DRAKAN_ALLOW_DIRTY_PUSH=1` was used on the grounds that CI compiles a clean checkout
  before any release. Prefer a quiet tree when possible.
- CI machines have no hardware GPU and no Pillow: new GPU tests must skip on a software adapter and Python tests
  must import without Pillow or numpy.
- The git index is shared by every session: stage and commit in one command limited to explicit paths.
- Subagents run on Opus or Sonnet only (owner's instruction of 12 November).

## Update at the end of 13 November

Everything below is on `main` and pushed (`5df14024`); the next release is cut from it once CI is green.

### Landed since the first version of this note

- **Order of the Flame, church stair and Book scene** (`ootf-book-skip-and-reload.md`): Escape at any point of the Book
  scene now hands back to the stair foot; subtitles are correct after any load; backing out of the save screen at the
  stair, or loading a save made during the walk-down, offers the save and the Book again until it is taken; once taken,
  the exit is closed in the save. Needs a rebuilt pack (formats `5133381`) and a new campaign game.
- **Direct3D 11 device removed** (`d3d11-device-removed.md`): a graphics driver reset no longer crashes the game; a
  plain notice is shown and the session ends in order. Proven with a forced-loss hook in the game and one real reset in
  a replay tool. The owner chose not to force a real reset in the game.
- **Renderer sharing, part 2** (`renderer-shared-code-2.md`): backend forwarding, the scene-camera phase machine,
  present, the rest of the target code and the image files are shared. Identical lines between the backends:
  3,712 at the start of 12 November, 752 now. Left: the backend state headers (`*_backend_impl.h`, about a day plus a
  proof run) and the device files (about half a day).
- **GPU vertex path** (`gpu-vertex-implementation.md`, `gpu-vertex-tuning.md`): strips drawn by the exact vertex shader
  on Direct3D 11 and WebGPU; the batch path on Direct3D 11 behind `DRAKAN_VERTEX_STAGES=all`; exact strip bounds; the
  tracker rests when no batch stage is handed over; small targets drawn by the shader. Default `cpu`; `auto` resolves
  to `cpu` because nothing is marked proven.

### GPU vertex path, the numbers on a quiet machine

Owner's settings, 3840x2160, Direct3D 11, `cpu` against `gpu` (strips), three interleaved repeats, nothing else
running:

| Scene | DLSS | Frame ms, cpu to gpu | VU1 worker ms | Verdict |
| --- | --- | --- | --- | --- |
| Surdana | quality | 6.20 to 6.12 | 5.60 to 3.52 | not established |
| Surdana | off | 5.73 to 5.31 | 5.44 to 3.49 | 7.7 % faster |
| Fight | quality | 9.42 to 9.73 | 9.56 to 5.48 | 3.3 % slower |
| Fight | off | 8.95 to 8.57 | 8.92 to 4.95 | 4.0 % faster |
| OotF Ruined Village | quality | 16.67 to 16.67 | 5.34 to 5.63 | no change (held at 60 fps) |
| OotF Ruined Village | off | 11.87 to 12.05 | 6.10 to 5.04 | not established |

The VU1 worker gains 2 to 4 ms on foot in Drakan 2; the GS worker pays some of it back, so the frame gains only
without DLSS. Next steps with estimates: a leaner hand-over on the GS worker (2 to 3 days, needed before the batch
path pays), the batch path on WebGPU (1.5 to 2 days), removing the DLSS scene-model capture (3 to 4 days, riskiest),
marking strips proven (half a day, owner's decision), re-taking the WebGPU reference hashes (1 hour).

### Decisions still open for the owner

- Whether to mark strips proven so `auto` uses them (the table above says: only worth it without DLSS so far).
- The earlier list in this note, items 2 to 9, still stands except item 1 (device removed: done) and the camera wall
  test for the cellar, which is now reduced by the reworked carry (`issue6-cellar-glitch.md`, third pass).

### Lessons added

- Stopping a background task can leave its child processes running (`xargs`, chained scripts); three agents were
  caught by it. Runners must stop their whole process group and remove only a lock they created.
- Never leave unlanded code applied in the main working tree between a build and a commit: it blocks other agents'
  commits of the same files.
- The proof bar of 90 replay runs per step was more than a pure code move needs and cost most of a day on a shared
  GPU; a build, the suites and one cumulative replay matrix would have done.
