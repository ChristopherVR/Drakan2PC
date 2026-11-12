# Issue 6 item 2: textures and HUD that change with the camera angle

Report (v0.1.10, retested on v0.1.11): some world textures switch to a wrong look depending on the camera angle; the
health orb suddenly appears empty when looking down; HUD parts turn slightly pixelated at some angles; enabling
ray-traced shadows makes it much worse and it stays worse after disabling them, until the game is restarted.

Checked on 2026-11-12, see [the check](../2026-11-12/issue6-view-dependent-textures-check.md): the empty orb and its
cause are confirmed in the live game and fixed there; one gap in the fix (uploads patched into a dirty target) was
closed; the ray-traced shadows observation below ("10, 550, 550") was not reproduced; the evidence folders cited here
(`build/issue6-2/`) no longer exist.

## Result

One cause for the empty orb, the pixelated HUD and the wrong world textures: an uploaded texture was sampled from an
old render target that still covered its memory. Reproduced from a recorded GS stream of the unmodified
`dist/DrakanPC` game, fixed in `runtime/gs` (Direct3D 11 and WebGPU), covered by two tests. Not run in the game itself
(no game build in this task). The ray-shadow part is only partly explained, see "Not established".

## Cause

The game renders character shadows, distant characters and the orb liquid into small targets (128 wide CT32, 64 wide
CT24) that it places anywhere in its texture memory (pages 384 to 510 in Surdana), and later reuses that memory for
ordinary uploaded textures. Where a texture lands depends on what is visible, which is the camera dependence.

In the renderer a target object:

- is allocated with at least 512 rows (`getRT`), so a 128 wide target covers 32 pages of memory;
- lives on for 600 frames after its last use while clean, and for ever while dirty;
- has its last use refreshed every time it is sampled as a texture.

`findRTForTexture` accepted any target of the same buffer width whose memory contains the texture's first page, with
no regard to what the target holds there. So an uploaded true colour texture that starts on a page inside an old
target was drawn from that target instead of being decoded:

1. The target only mirrors the upload (`syncRT` pushes changed page rows up at native resolution, nearest): no HD
   pack image, no upscale, no mip levels. This is the "slightly pixelated" HUD and part of the blocky world look.
2. Only the first row was bounds checked (`py >= r->h`). A texture that starts near the end of the target runs past
   its last row and samples nothing there. Measured: the HUD atlas (CT32 128 x 128, tbp 14880) was taken from the old
   target at page 435 at row 480 of 512, so 96 of its 128 rows were missing. The orb liquid sits in that part: the
   health orb was drawn empty.
3. A world texture drawn through a target loses its wrapping and mip chain: a cliff in Surdana came out as vertical
   streaks.
4. Because sampling refreshes the last use, the old target never retires while a texture sits on it: the state stays
   until the game moves the texture, or for the rest of the session.

## Evidence

Capture: `scripts/harness/play_run.py` (New Game, Surdana start, scripted pitch and yaw sweeps,
`build/issue6-2/recipe_a.txt`, the player's `drakan.ini` in a window) with `PS2X_GS_CAPTURE=6000`, replayed with
`gsr_replay --scale 3`. 2141 vsync frames.

- Frames 1317 to 1320 (camera pitching down) show the empty, pixelated orb. `GSR_DRAWLOG` / `GSR_TEXLOG` /
  `GSR_RTLIST` for frame 1317: the orb sprites (tex0 tbp 14880) log
  `sample render target fbp=435 fbw=2 cls=0 as tbp=14880 psm=00 128x128`, `src=(0.0,480.0,...)`, and the target list
  has `rt fbp=435 ... 128x512 dirty=0 dy=0..128 lastUse=1011`: clean, last used 306 frames earlier, 512 rows.
- Every fifth frame from 600 on, before against after the fix: 226 of 309 frames identical. The 83 that differ are
  the HUD (orb present in frame 1320, sharper orb art in the others) and three frames (1775, 1810, 1815) where a
  cliff changes from vertical streaks to its rock texture. Nothing else changed (shadows, distant characters).
  Texture resolves served by a target: 4026 before, 1383 after.
- Live game with `GSR_TEXLOG=1` and the same sweep three times (ray shadows off, on, off again): resolves from a
  target that the texture overruns: 10, 550, 550. In the second and third pass a 128 x 128 texture at tbp 14176 came
  from the old target at page 417 at row 416 (its last 32 rows missing) for the rest of the run. (Not reproduced on
  2026-11-12: 25, 0 and 10 overruns in the three phases, tied to camera direction.)

Images (not tracked): `build/issue6-2/evidence/orb_frame1318_before_after.png`,
`build/issue6-2/evidence/cmp_1815_cliff.png` (left before, right after),
`build/issue6-2/evidence/cmp_2000_hud.png` (before, after, difference x6).

## Fix

`runtime/gs/src/rt_texture_source.h` (`rt_source::targetOwnsTexture`, pure policy) and its use in
`findRTForTexture` of both backends (`d3d11_targets.cpp`, `wgpu_targets.cpp`):

- a texture that does not start at the target's base and runs past the target's last row never comes from it;
- a target is the source only when at most half of the texture's page rows merely mirror memory. A page row mirrors
  memory when it is outside the target's dirty range and was filled from memory (`RT::rowFromMemory`: set at
  creation, by `syncRT`, when `trimDirtyRows` takes rows off the dirty range and (since 2026-11-12) when an
  upload is patched into a row outside the dirty range; cleared by `readbackRT`, whose rows
  keep draws), or when memory is newer than the row. Rows in the dirty range count as drawn.

Everything else falls through to the ordinary decode path, which already handles a target inside a texture
(`aliasedTarget`, `flushForRead`). A drawn target, a drawn target that was read back, and a frame buffer sampled with
a larger power of two size are sampled from the GPU copy as before.

One behaviour changes on purpose: an uploaded atlas that starts on a page row of a scratch target (the inventory /
equipment case of `rtShiftedRowTest`, modes 1 and 2) is now decoded like the shifted mode 0 instead of being read
from the target. The pixel checks of that test are unchanged; its path check now expects no target sample.

## Tests

- `runtime/gs/tests/test_rt_texture_source.cpp` (ctest `rt_texture_source`): the policy with the measured cases.
- `runtime/gs/tests/test_synthetic.cpp` `staleTargetTextureTest`: a texture at row 480 of an old 512 row target
  (rows past the end must show), a texture uploaded at an old target's base (decoded, not sampled from the target),
  and a target drawn again (still sampled from the GPU copy). With the rule disabled the first two fail
  (`rows past its end ... got (0,255,0) want (255,0,0)`).
- `build/issue6-2/gs`: 36 gs tests, 35 pass, `live` skips (needs a capture). WebGPU: `gsr_test_synthetic_webgpu`
  built from the root `runtime-tests` preset in `build/issue6-2/rt`, 327 checks pass, including the new test.

## Not established

- Not run in the game: the fix was checked by replaying the recorded GS stream and by the tests. The replay has no
  engine side channels (world open / close, GUI screen), HD pack or DLSS. (Run in the game on 2026-11-12, see the
  check.)
- The player's screenshot (blocky hillside in Surdana) was not reproduced as such. The replay shows a different wrong
  look of the same class (streaks on a cliff). A blocky hillside is what point 1 above gives for a world texture, but
  that exact frame was not captured.
- Ray-traced shadows: the live log shows the bad state appearing after shadows were enabled and staying after they
  were disabled, and point 4 explains why it stays. Why enabling them starts it was not determined (one run; the
  setting change flushes the texture caches and drops the depth buffers, it does not drop targets).
- The claim that the base bug exists without HD textures was not tested separately. The empty orb does not depend on
  them (the replay ran without a pack); the pixelated look needs an internal scale above 1 or an HD image to differ.
- Inventory, shop and menu screens that stream atlases through scratch targets take the decode path more often now.
  Only the synthetic tests cover them; they were not viewed in the game.
- Issue 6 item 3 (wrong textures on distant low-detail characters): those characters are drawn through the same
  small targets, and a texture or target of one landing inside an older target is the same lookup. Likely related,
  not checked here.
