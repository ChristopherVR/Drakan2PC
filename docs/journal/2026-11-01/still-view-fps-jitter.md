# Still-view fps jitter (uncapped, 4K scale 4, vsync off), 2026-11-01

Report: fps jumps around even when looking at one spot. Measured with
`play_run.py --recipe fps_steady_wall.txt --env DRAKAN_FRAME_AUDIT=2 --env PS2X_PERF=1` on Surdana (the player's installed
`drakan.ini`; another game instance was running throughout, so absolute numbers are pessimistic and vary run to run).

## Symptom

* World fps 123-150 from one second to the next, present gaps mean 7.3 ms, SD 2.4 ms, p99 17.4 ms.
* A 16-19 ms present gap recurs every **32 world frames** (the world serial of consecutive hitches differs by exactly 32; ~4 per
  second, 78-86 per 20 s). Not the FPS overlay (`DRAKAN_SHOW_FPS=0` gave 78 against 86).
* In a hitch the engine update `U` runs on time, the EE display flip `F` follows only after the next guest vblank `V`, and that
  `V` is processed 2-15 ms late: the EE thread was busy, not waiting.

## How it was found (what was ruled out)

* Scheduler: no guest thread blocks in the hitch (`blockCurrent` never fires for the main thread); the GS worker is idle
  (`WorkerIdle` 7-8 ms), so it was not waiting on the renderer lock, a readback, the VU1 worker or the commit retirement
  (scopes for all of them stayed under 2 ms). The EE thread's `GuestDispatch` scope covered the whole gap.
* A timestamped EE-thread RIP sampler (`DRAKAN_FRAME_AUDIT=3`, `ps2_perf.cpp`, resolved by `scripts/perf/ee_samples.py`) put
  ~75 % of the samples inside a hitch in `drakan_scene_models::hashBytes` and the rest in `Capture::texture`.
* `Capture::capture` rebuilds the resident texture set `if (textures.empty() || (++ticks % 32) == 0)`: for each resource a
  128x128 sample, then FNV-1a over 64 KB, byte-serial (about 4 cycles per byte). Timed: 151 textures, 11 ms, per refresh
  (`loadTexture` part 50 us). That is the 32-frame period; scene-model capture runs whenever the ray / NR effects are on.

## Fix

* `captureHashWide` (`object_motion_capture.h`): 4 independent multiply/xorshift lanes over 32-byte blocks, ~10x faster. Used
  for texture identities only (`drakan_scene_models.h`, `drakan_scene_assets.h`); identities are compared inside one run.
* `Capture::refreshTextures`: the refresh is resumable, ~250 us of work per capture (always at least one texture), and the new
  set (and the loaded-texture merge) replaces `textures` only when complete. First fill and level change stay synchronous.
* Test: `gsr_test_scene_models` checks the hash (every byte position, tail, length, seed); with level RAM supplied it also
  checks that the sliced refresh settles on the identical texture set.

## Result (same recipe, settings and machine load)

| | before | after (4 runs) |
|---|---|---|
| present gaps > 14 ms in 20 s | 78-86 | 1, 2, 5, 22 (one 2 s disturbance) |
| p99 gap | 17.4-17.6 ms | 9.7-12.9 ms |
| gap SD | 2.4 ms | 1.2-1.6 ms |
| frames/s | 123-137 | 125-157 |

## Left

Remaining spread is the content itself (gaps 4-10 ms, VU1-bound; EE thread ~95 % busy with the VU1 handoff spin). Not
investigated: whether the same scene-model capture costs time every frame (mesh hashing in `Capture::mesh`) when many
animated objects are on screen.

## Round 2: the per-frame scene-model capture (same day)

With the periodic spike gone, the sampler still put 35-40 % of the EE thread in `Capture::mesh` and its helpers every frame.
Phase timers on a RAM dump (`dump ram_<label>` in an input script) showed ~84 `mesh()` calls per capture, 2.6 of ~3 ms:
posed (skinned / animated) instances were rebuilt from scratch every frame (never cached), each with `unordered_map` / `set`
work, a 3-element `std::sort` per candidate triangle, and byte-serial FNV over every triangle.

* `Capture::posedMesh`: the first build per (resource, LOD slot) records the vertex indices of every emitted triangle; later
  poses copy the template and re-read only positions (identity re-hashed). A pose with an out-of-range position, a vertex
  span that does not fit, or a template whose first pose dropped a triangle for a non-finite position takes the full
  `mesh()` build, so the result is always what a full build would give. Templates are cleared with `meshes`.
* `hashBytes` in this file is the wide hash; `key()` sorts three values with swaps; `originals` / `emitted` reserve and use
  hash containers; `generations` and the per-frame `used` map are unordered.
* `Capture::refreshBudget` (default 250 us) is public so tests can make the texture refresh synchronous.

Verification: a scratch harness (not committed: it needs level RAM) runs the original header (`git show bf23d5f~1:...`) and
the new one over three RAM dumps of Surdana (140 captures each, poses differ between dumps) and digests everything the
renderer sees (instances, transforms, opacity, every triangle's positions / UVs / material, texture pixels, lighting).
With the synchronous refresh the digests are **identical for all 420 captures** (0 diff lines). Cost per capture on the
dumps: 2.7-3.5 ms -> 0.6-0.7 ms.

In game (Surdana still view, nothing else running; original `dist/DrakanPC` build vs this one, same ini): 155 -> 176 fps,
and against the first-round build 128-157 -> 192-215 fps with gap SD 0.7-0.9 ms, p99 6.7-7.7 ms, 93 % of frames in 4-5 ms.
The EE thread now mostly waits for the VU1 worker (`syncSlow` 16 %, VU1 worker ~80 % busy), so the remaining frame time is the
VU1 geometry load, not the host.

Not done: 80 non-posed instances per frame still retry a `mesh()` that returns null (rejected, never cached; ~0.08 ms per
frame) - caching negatives needs a retry rule for resources that load later.

## Round 3: moving, readbacks, pacing (same day)

Walk-and-turn recipe (Surdana, no RAM dumps), `GSR_PROFILE=2` for the renderer's own buckets. Other applications (and a second game
instance) loaded the GPU during these runs, so only same-run or mechanism evidence is used below.

* **Startup is not steady state.** The 30+ ms `setupUpscale` calls (six 256x256 HUD textures, inline because the GUI page is on screen)
  and the 10-25 ms GIF records belong to the first seconds after entering gameplay. In steady walking the largest renderer-side
  stalls are GPU readbacks.
* **Readbacks (item 2, not removed).** `readbackMap` = 15-20 % of the GS worker's time (avg 1.6-2.0 ms on a quiet GPU, 3-4 ms under
  contention, singles up to 13 ms), ~0.7 per frame. Causes (call stacks of 2242 readbacks): `resolveTexture` -> flushToMemory 44 %,
  `loadClut` -> flushToMemory 28 %, `startTransfer` (image upload) -> flushToMemory 28 %; 73 % of them are the 128x512 CT32 scratch
  target. Tried: flushing the command stream right after draws into the small scratch targets so the GPU can run ahead (A/B
  alternating every second inside one run): `Map` 3.7 ms with, 3.2 ms without. The wait is real GPU time (the queue behind the
  readback holds the previous frame's passes), so submission timing does not help. Removing the stall needs GPU-side CLUT loads and
  texture sampling from a render target's memory (the three call sites above), and a partial-overlap upload path
  (`docs/journal/2026-10-21/cpu-gpu-readback.md`); a mistake there corrupts rendering, so it was not attempted blind.
* **Texture creation burst (item 1).** `pumpUp` created the GPU textures of all finished upscale jobs in one present (max 4 ms
  observed). The present-time drain now has a 2 ms budget (first job always runs, rest stays queued; D3D11 and WebGPU); callers that
  need their own entry at once still drain everything. Palette-step recolouring (`setupPaletteHd`) never exceeded 2 ms in the walk.
* **Small items (item 3).** `consumeCompletedDmacCauses` polled a mutex and discarded the vector's capacity at every checkpoint: an
  atomic count makes the empty poll lock-free. Static meshes `mesh()` rejects (about 80 instances per capture) are remembered until
  the next texture refresh instead of being rebuilt every frame.
* **Pacing (item 4).** The frame limiter (`frame_limit` / `DRAKAN_FRAME_LIMIT`) delivered the right rate but with a 0.74-0.78 ms
  spread between presents, because `sleep_until` wakes up to a scheduler tick late. It now waits on a high-resolution waitable timer
  and spins the last 0.8 ms (`gs_precise_sleep.h`, test `precise_sleep`): spread 0.15-0.18 ms at 144, 120 and 90 fps, no gap over
  14 ms, and every present still shows a new world frame. Uncapped presentation is unchanged (it is paced by the frames themselves);
  a limiter just under the display's refresh (or a VRR display) is the way to get an even cadence.
