# OotF Intro owl: brown and dark triangles over the chest

Report: the perched owl of the Intro looks broken (clipping, wrong parts). Fixed in the converter (formats `ootftex.ts`,
`ootfassets.ts`); nothing in the runtime changed.

## What was checked first

* The model (`3d_owl`, PC model 66: 249 vertices, 410 triangles, 37 joints, one texture) converts completely: the decoded
  0x0211 list has exactly the 410 source triangles at the same positions, every vertex has weights (sum 1), and the LOD / stump
  cap rule of the previous commit keeps all 410 polygons. A CPU render of the PC mesh in bind pose is an intact folded-wing owl.
* The converted texture matches the PC one (256 x 256, 8 bpp, 4 bit graded alpha kept in the palette, square).
* Culling of the Intro (back faces) does not touch the owl: it is a skinned model, not a culled class.

## Cause

The owl texture has graded alpha (feather edges), so it is written with alpha selector 8 (blend). A blended draw does not
write depth, and the owl mesh is built of overlapping feather cards (inner wing and chest layers lie within 1 % of the outer
surface in 5 % of the pixels). In strip order the cards that should be hidden behind the chest painted over it: sharp brown
and dark triangles on the chest and belly in the Intro 1 close-up, and ragged wing sides.

## Fix

Textures of skinned models (already forced 8 bpp square) now cut alpha at 50 % (`hardAlpha`): every texel is opaque or clear,
which selects the depth-writing alpha-test selector 1. `OOTF_SKIN_SOFT_ALPHA=1` restores the blend for comparison.

## Result

Private two-run comparison (same executable copy, same Intro pack except the texture flag; screenshots under
`build/introgfx/owl1` (blend) and `owl2` (cut-out)): at 16 s and 17 s of the Intro the blend run shows the triangles, the
cut-out run shows a clean chest and wings. Feather edges are slightly harder.

## Open

Other skinned classes with graded alpha also get the cut-out; only the Intro was re-shot. The owl turns its head sideways in
the clip (animation, as on the PC). Run notes: the harness needs `DRAKAN_AUTOPRESS_UNTIL=11000` and no pad taps now, or
the auto-press skips and pauses the Intro scenes.

## Follow-up: the owl in motion, and the Intro's closing wait

Report after watching the new intro movie: "when the owl moves his body quite a few textures clip or are missing", and "once Arokh
flies away the owl stays frozen and it takes a few seconds for the next scene to load".

### Owl in motion: compared with the PC model, no converter fault found

Private runs (copy of the play folder with the user's textures, 1920x1080, scale 3, shots every 100 ms from 26 to 44 s) and an
offline render of the PC model: a CPU z-buffer rasterizer over the PC mesh, skinned with the PC rig and the converted clips
(`parsePcModel`, `buildRig`, `convertPcAnimToPs2`), 50 % alpha cut, same texture. Result:

* Every distinctive shape in the game frames is in the offline render of the same clip too: the two long horn cards over the head
  (they cross the face when the head tilts), the white ruff cards that flare out beside the head, the thin dark seam down the
  chest where two chest cards meet, the hard straight edge of the chest card under the beak. They are the model's own geometry
  (the bind pose, no animation, already has the horns).
* All 410 triangles and both wings are drawn in every frame of the runs; the wings are never lost when the body turns.
* Skin weights are 1 or 2 influences per vertex (101 and 111 of 212), no vertex is dropped.
* Lowering the alpha cut (50, 30, 15 %) changes the offline chest and wing by a few texels only; the seam is geometry, not alpha.
* No flicker: frame to frame difference of the owl area in the movie is 0.01 to 0.03 grey levels while it idles.

So the converted owl matches what the PC data gives. What the user sees is real data that reads as clipping at the movie's size
(card edges, cards through the head), not missing faces. Not excluded: a difference in how the PC engine blends the graded alpha
of the cards (soft edges against the cut-out here), which needs a PC capture to judge. Evidence under `build/review/owl/sheets`
(`c3`, `c4`, `c5` = game crops, `pc_z` = offline PC render, `owlmovie` = the movie frames).

### The closing wait

The hand-over is a timer (`INTRO_SECONDS`) and was 44 s. Measured on the movie (60 fps, engine time): Intro 3 starts at 24.85 s,
Arokh crosses the camera at 34.7 to 35.8 s (his PC keys put him behind the camera from 11.1 s of the scene), Intro 3 ends at
41.20 s (24.85 + 16.39, the Gui Trigger time), then the scene objects go, the picture dips to black and fades the wide owl shot back
in (41.4 to 41.85 s), and nothing happens until the timer at 44.0 s. 5.3 s of the closing wait are authored: after Arokh the scene
holds the owl and the lightning for 5.3 s (the sequence data; PC too). The other 2.8 s were the timer's margin.

Fix: `INTRO_SECONDS` 44 -> 41.4 (formats `ootfcampaign.ts`; the hand-over now follows the last scene by 0.2 s). Live runs, vsync on:
the exit is taken at 41.55 s of wall time into the level (it was 43.70 s), and Ruined Village is up 1.5 s later. The Intro level
is a resource-bound scene change: the preloader stalls of 0.4 to 0.6 s at each scene change (intro-movie note) are not part of the
wait. A shorter hand-over (at Arokh's exit, about 38 s) would cut the authored owl tail; that is a content decision left open.
Timeline: `build/review/owl/freeze_timeline.txt`. The movie must be re-rendered for the new length (it ends at 44.02 s now).

### Follow-up 2: converted clips against raw PC keys and the PS2 engine's own clips

Question: does `convertPcAnimToPs2` rotate end joints (horns, ruff, hair) wrongly? Checks:

* Raw PC owl keys (37 joints, clips 37 / 38 / 39): no scale (row norms 1.000), every key and bind determinant 1 (no mirrored joint), no
  keys beyond the head, the spine and the two ear joints that matter. `l_ear` / `r_ear` have one key (static) in clips 37 and 39 and
  move (up to 40 degrees) only in clip 38, so a horn swinging across the face is the head's own roll (68 degree keys) carrying the
  horns, as in the bind-pose geometry.
* The rotation basis `Q = B Wp K Wp^T` was re-validated against the PS2 engine's own clips instead of the one spider: 12 same-named
  NPCS animations with the same joint count, 425 joint samples: median error 1.6 degrees, p95 10 degrees; the only large errors are
  finger middle joints (29 to 34 degrees on jnt5_2, jnt7_2, jnt9_2, jnt13_2), not head, ear, spine or limb joints. No wrong
  quaternion order or sign, no missing parent-space conversion shows up.
* Non-root translation keys (the dropped "squash" translations; up to 0.2 on the ears in clip 38): posed in the offline rasterizer
  as absolute keys, as bind plus delta, and as bind plus delta turned into the parent frame, they deform the owl (collapsed body,
  torn ruff cards). The conversion without them is the sane one, so dropping them stays.
* PS2 clips do carry non-root `hasPos` tracks (1684 in SYSTEM.ADB, 2820 in NPCS.ADB), so the engine can take them; the PC keys
  are not usable as they are.

No converter change. Open: the finger middle-joint differences (not owl, not hair) are PS2 versus PC authoring or a real fault in
chains with a twisted bind; not traced. Evidence under `build/review/owl`: `anim_variants_A_B_C_D.png`, `anim_converted_poses.png`,
`anim_validation.txt`.
