# Ground turns smooth while the camera turns (Order of the Flame)

Player report: in `OotF 03 - Wartok Canyons` the ground textures sometimes turn smooth while turning, then come back.
Present before and after the refactors of 26-27 November (an older bug).

## Reproduction

Player settings (`dist/DrakanPC/drakan.ini`: D3D11, scale auto = 5 at 3840x2160, HD textures, `hd_texture_keep_s = 60`,
`hd_texture_mips = 1`, game texture cache auto). Harness start in the level, Rynn dismounted, four 2.5 s mouse turns
(30 counts every 30 ms, alternating direction) with a screenshot every 100 ms: 120 screenshots per run.

```
python scripts/harness/play_run.py --name gt_before1 --recipe <turn recipe> --attempts 1 --timeout 240 \
  --env DRAKAN_GAME=d1 --env "DRAKAN_START_LEVEL=OotF 03 - Wartok Canyons" --env "DRAKAN_START_LEVEL_FROM=Level 0 - Intro"
```

Smooth frames were found automatically: mean absolute Laplacian of the ground band (lower third of the screen, left
and right of Rynn); a frame counts as smooth when one side drops below 55 % of the median of the three frames before
and after it in the same turn. The flagged frames show the grass at the native 128 x 128 texture, blurred over the
whole visible ground, one or a few frames later back in HD.

## Cause

A temporary diagnostic build drew every native texture that was shown because its slot counted as animated in cyan
(and native textures waiting for their HD copy in magenta). Up to 49 % of the screen, the grass ground, flipped to
cyan from one screenshot to the next while turning.

The game's texture cache (8 MB in Order of the Flame levels) pages ground textures in and out while the camera
turns, often into the same texture memory slot: the log (`GSR_TEXLOG`) showed the same palettes moving between slots
(`tbp` 14355, 14445, 14715 ...) and the slots flagged `volatile (content changed 3 times in 28 frames)`. The volatile
rule of `setupUpscale` (`runtime/gs/src/gs_texture_cache.inl`) is meant for palette and pixel animation: a texture
in such a slot keeps its native look unless its exact content hash has a pack image. Many Order of the Flame ground
textures match their pack image only by the colour-only hash (`alias.txt`, the game edits palette alpha at load
time), so in a slot that streaming had made volatile they were drawn native until the slot was decoded again.
Drakan 2 (Surdana, same recipe) showed no ground in cyan: its disc levels do not page the ground this way.

## Fix

`setupUpscale` looks the pack image up (exact hash, then the colour-only hash) before the volatile check and lets an
indexed texture (or UI artwork) of a volatile slot through when either finds one. A pack image is a known still
picture of the disc, not an animation frame. Volatile full-colour uploads and textures without a pack image keep the
native path (river water, effects). The lookup is the one the function already made later; a volatile texture now
pays one colour-only hash per decode when the exact hash misses, nothing per draw.

## Measurements (same recipe, before = the build before the fix)

| Run | Smooth frames of 120 |
| --- | --- |
| before 1 / 2 / 3 | 10 / 10 / 17 |
| after 1 / 2 / 3 | 3 / 4 (of 86 shots taken) / 5 |

The frames still flagged after the fix show HD grass (the dragon's shadow or the river in the band lower the
measure). Diagnostic build, share of the screen drawn native because of a volatile slot: up to 49 % before, after the
fix only the river and Rynn's boots (at most 6 % except one frame where a translucent effect lay over the water).
Native draws while a new HD copy is being made (first sight of a texture) remain: 1 to 6 % of the screen for a frame
or two.

Frame rate in the same scene (20 s of turning, no screenshots, `DRAKAN_FRAME_AUDIT=2`): before 69.5 / 56.2 fps,
after 55.7 / 59.3 fps; the spread between two runs of one build is larger than the difference. GS worker
`resolveTexture` (inclusive, per 5 s, `GSR_PROFILE=5`): before 641 / 630 ms, after 602 / 581 ms.

Evidence (not committed): `build/harness/gt_before*`, `gt_after*`, `gt_tint2` / `gt_tint_after` (diagnostic tint),
`build/ground_turn/before_after_b3_005.png`, `tint2_quad.png`, `tint_after_quad.png`.
