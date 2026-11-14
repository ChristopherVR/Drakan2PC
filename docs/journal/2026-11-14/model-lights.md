# More than three point lights per model (round 6, brief A)

Brief A of `engine-limits-survey.md`. Scratch, runs and evidence: `build/review/limits-lights/` (private).

## What the engine does

* The light set builder `0x1c4148` (a0 set, a1 limit, a2 sun, a3 the level's light list, t0 mode, t1 the object's
  bounding sphere, t2 its box, t3 an optional object transform; stack +0 "full search", +8 four cached light index
  bytes at object +0x118) puts the sun in slot 0, then sorts the lights whose sphere touches the object's box by
  distance into the list at `0x56b6c0` (`{entries, capacity, count}`, entries `{distance^2, light, list index}`) and
  accepts them nearest first while the set holds fewer than the limit. Without "full search" it only tries the lights
  of its four cached index bytes. The set is 16 slots of 0x34 bytes from +0x24 (type, vector, colour, r^2, 1 / r^2,
  specular half vector, flag), count at +0x20, ambient at +0x14.
* Callers: `0x1860d8` (models, limit 4, or 0 with a light override), `0x13b244` (limit 4), `0x14f844` (terrain, 16),
  `0x1861e0` (0, the sun only).
* Accept rule per candidate: skipped when the object's sphere does not reach the light's; then either a directional
  light at the object (direction from the centre to the light, colour times 1 - d^2 / r^2 at the centre, dropped when
  the centre is out of reach or under 10 %), or, for an object at least 1024 units in radius, or an object large
  against the light (r_object^2 > r_light^2 / 4) that is not far away (d^2 <= 8 r_object^2), a real point light (type 1:
  the light's position, its own colour). Mode t0 nonzero forces directional.
* The model upload `0x1e9fa0` writes ambient into VU1 `[8]` and exactly four slots into `[13..20]` (directions as a
  4 x 4 matrix scaled by 1/254, colours as integers c * 255 packed in 8 bits as c / 2, so 2.0 is the brightest). The
  lit strip stage takes four directional terms (`gsr/vertex_program.h`). Its loop writes `count` slots, so the set
  must never hold more than four for a model.

## The counter (`DRAKAN_MODEL_LIGHTS_LOG=1`)

The wrapper of `0x1c4148` (`drakan_model_lights.cpp`) runs the builder unchanged, then replays its accept rule
(`world/ModelLights.h`, `evaluate`) over every sorted candidate, also past the limit, and checks that its own accepted
count equals the engine's (`mismatch`). Every 5 s it logs calls, full searches, candidates, lights accepted, lights it
would accept without the limit, how many of the cut lights were directional or point, and the most lit object.

Runs (`play_run.py`, the owner's `drakan.ini` copy in the private play folder, New Game with the level redirected,
god mode; a scripted walk of four 1.5 s pushes forward with a shot after each, or a placement):

| Run | Calls (full searches) | Lights per call, accepted / without the limit (max) | Calls cut / lights cut | Kind of the cut lights | mismatch |
| --- | --- | --- | --- | --- | --- |
| Catacombs 1C start corridor, walk (`runs/walk1c-m16b`) | 117,630 (2,264) | 2.34 / 2.48 (6) | 12,644 / 16,372 | 16,371 point, 1 directional | 0 |
| Catacombs 1C, same walk, earlier build (`runs/walk1c-m16`) | 278,665 (5,154) | 2.26 / 2.39 (6) | 28,282 / 36,619 | 36,617 point, 2 directional | 0 |
| Inquisitor's Lair 1E start, walk (`runs/walk1e-m4`) | 157,783 (3,543) | 1.40 / 1.40 (4) | 0 | | 0 |
| Inquisitor's Lair 1E arena spot (placed, `runs/arena1e-m16`) | 166,674 (8,711) | 1.24 / 1.24 (4) | 0 | | 0 |
| Surdana town square (placed, guard scene, `runs/surdana-m16`) | 41,145 (2,701) | 1.01 / 1.01 (9) | 23 / 48 | 48 point (one town piece of radius 18,549) | 1 |

So the limit is reached, but not where the brief expected: about one call in ten in the Catacombs corridor cuts one or
two lights, and nearly every cut light is a real point light (type 1) of a large room piece (bounding radius about
2,000 to 2,500 units: the corridor walls, the gate frame). Characters, whose lights are directional terms, were cut
twice in all the runs together: Rynn walking past the corridor torches keeps every light that reaches her. The
mismatch counter (the replayed accept rule against the engine's own count) was 0 over 7,400 full searches in the
interiors and 1 in the Surdana run.

## The lift: `[video] model_lights` (4 .. 16, default 4)

At 4 nothing changes (the wrapper calls the builder and returns; without `DRAKAN_MODEL_LIGHTS_LOG` it does not even
read the set). Above 4 the wrapper runs the builder unchanged and then adds up to (setting - 4) of the cut lights,
nearest first, to the four slots the upload takes:

* a free slot (only in the cached-light mode, when a cached light no longer reaches): as the engine would have;
* a point light: joins the point light slot nearest to it; the slot's position moves towards it by what it brings to
  the object (colour x attenuation at the nearest point of the object's sphere), that colour is added, the larger
  reach is kept;
* a directional light (characters): joins the directional slot whose direction is closest (never the sun's); the
  direction is the brightness-weighted mean, the colours add;
* nothing to join: half of it as ambient light.

Colours are capped at 2.0 (the upload's 8-bit packing wraps above it). The set never holds more than four slots, so the
upload, the VU1 memory map and the microprogram are untouched. Objects in the cached mode (no full search this frame)
get the lights their last full search cut off (a host table keyed by the object's cache bytes).

## Evidence

Pictures are from the scripted Catacombs walk (`recipes/walk1c.txt`), the owner's ini (4K, DLSS quality, HD
textures, living foliage on), the first shot (`a0`, Rynn standing at the level start, before any input). Three runs at
4 and two at 16 give the noise floor:

* Patch means at `a0` (RGB, 0 to 255) are identical between runs of the same setting. The stone piece left of the gate
  (the most lit object, 6 lights for 4 slots): 118.1 / 85.4 / 49.7 in all three runs at 4, 112.8 / 80.7 / 45.5 in both
  runs at 16. The gate frame top, its right post and the far left wall do not change (within 0.1).
* `evidence/noise_floor_vs_option_diffx10.jpg`: the difference of two runs at 4 (left) against a run at 4 and one at 16
  (right), times 10. Both show Rynn's outline and the torch flame (animation and flame differ from run to run); only
  the right one shows the stone piece and a faint change on the floor and walls.
* `evidence/catacombs_gate_m4_vs_m16.jpg` (crop) and `evidence/catacombs_full_m4_m16_diffx8.jpg`.
* Rynn is not changed: she keeps every light that reaches her in these scenes, so there is no character before / after
  picture to show. The first version of the fold (a point light turned into a directional term, larger reach) made the
  same stone piece brighter and flatter (118 to 143 in red); the current one keeps the near torch's falloff and darkens
  it slightly, because the slot's position moves towards the farther torch.
* Surdana town square (guard scene) at 4 and 16: `evidence/surdana_m4_vs_m16.jpg`, the same picture apart from the
  foliage wind and Rynn's idle pose; 48 cut point lights on one town piece in that run.

Option off: at 4 without the log the wrapper calls the builder and returns, so nothing of the guest changes. A GS
capture replay (`gsr_replay --hashes`) sits after the EE and cannot see this change either way, so none was run. The
ray traced path reads the level's world-space lights itself (`drakan_scene_models.h`, the light selection near
`capture`), not this set, and motion vectors do not depend on vertex colours; both are reasoned from the code, not
measured.

Frame time (`scene_ab_bench.py` under its turn lock, Catacombs start, owner's ini, 4 against 16, three interleaved
pairs, `bench/catacombs-lights`): not a result, the machine was busy. Pairs 1 and 2 ran beside up to five compiler
processes (other agents' builds), pair 3 beside one and two other game instances. Median frame 8.53 against 9.81 ms,
the pairs 8.53 / 9.81, 7.26 / 7.26 (the quietest pair), 12.48 / 19.97; EE CPU per world frame 8.83 (7.33 to 10.89)
against 9.36 (7.63 to 15.22). The ranges overlap; the cost of the extra work (replaying the accept rule over at most
five candidates per full search and folding one or two lights) is small next to them. A quiet-machine rerun is owed
before the default could change.

## Not verified

* Order of the Flame interiors: the direct start of Grimstone Mines (the `ootf-mines` bench scene's environment) stayed
  on a black screen with no level objects in two attempts, so no OotF numbers or pictures. The Inquisitor's Lair (start
  and arena spot) never reached the limit.
* How the VU1 program uses a type-1 (point light) slot: the model upload writes slot +4 (a position for type 1) into
  the direction block like any other slot. The fold keeps type-1 slots type 1 and only changes their position, colour
  and reach, so it adds nothing new to that path, but the exact look of a merged point light was judged from pictures,
  not from the microprogram.
* Whether the merged look is better: the stone piece gets the far torch's colour but loses some of the near torch's
  highlight. The default stays 4; no value above 4 is proven better.
* Spell lights (a cast fireball passing a character) were not played.
