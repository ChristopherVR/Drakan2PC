# OotF animation / skeleton study (PS2 0x0208 + ADB)

Code: `editor/packages/formats/src/ootfanim.ts`, tests `editor/packages/formats/test/ootfanim.test.ts` (synthetic only).
Probes were Python scratch scripts (not committed). Data: PAL ISO `NPCS/SYSTEM .MOD/.ADB`, OotF `Common/NPC/NPCs.{mod,adb}`.

## PS2 0x0208 (solved, 116 of 116 skeletons parse to the last byte)

Order: 64-byte bounds, `u16 lod_count`, `pstr[lod_count-1]` LOD names, `u16 node_count`, nodes (32-byte name + i32 joint),
**`u32 total_vertices`** (= sum of LOD vertex counts), `u16 joint_count`, joints, LOD table (`u16 1` + 28 bytes each, as
section 11), `u16 n` + `u32` animation ids, channels, spheres.

* Joint: **12 floats translation first, then the 3x3 rows** (PC: rows then translation; the numbers are the same, e.g.
  spider joint 4 t = 0.0721 in both), `i32 mesh, i32 first_child, i32 next_sibling`, then per LOD `u16 n` + `n x {u32 vertex, f32 weight}`.
  Matrix = inverse of the model-relative bind transform (row-vector convention).
* Channel: `i32 joint, u16 cap_count, caps` (cap = 5 x u32, `u16 n`, `n x {u32, f32}`). **No xformA/xformB** (PC has both, 100 bytes + caps).
  PS2 keeps fewer channels (spider: first 4 of the PC 16, equal to the 0x0501 channel count of its animations).
* Spheres: `u16 n`, then **one flat list** (not per LOD) of 24-byte `{u16 first_child, u16 next_sibling, f32 x y z, f32 radius, u16 channel, u16 0}`.
  PC: `u16 lod_count`, then per LOD a list of 16-byte spheres that name a vertex.
* The old "extra u32 and 6 bytes" note: the u32 is the vertex total; the rest was the channel / sphere differences.
* Skeleton tree, node names and joint order are identical between games for shared creatures (spider(h)(B), id 3228:
  61 nodes / 37 joints both, same parents). Some creatures were re-authored (crimsondragon 41 vs 44 joints, skeldragon 54 vs 50,
  primitive 39 vs 44): those cannot share animations.

## PS2 ADB (solved container, 2476 of 2476 records close exactly)

* 0x0501: `pstr name` (no NUL), `f32 duration, u32 flags (1 = no loop), u32 track_count, u32 channel_count, f32 0.2 (rot threshold),
  u32 1, f32 scale thr, u32 0, f32 translation thr` (0.1..3.0). `track_count` = joint count of the model; `channel_count` =
  channel count of the model.
* 0x0502: `u32 n`, `n x 24 bytes {u32 key_offset, u32 key_count, u32 flags, f32 base xyz}`, `u32 key_bytes`, keys.
  Flag 0 = 20-byte keys `{f32 t, f32 qx qy qz qw}`; flag 0x80000000 = 32-byte keys with `f32 x y z` appended. Offsets are
  contiguous. `base` = local bind translation of that joint (matches the skeleton exactly), so translation is only keyed on moving tracks
  (root: translation is the delta from the first key).
* 0x0505 (6 bytes, `01 00 00 00 00 00` on most) and 0x0500/0x0504 singletons: not needed, copied as-is would be safe.
* Time is in seconds (`t <= duration`); quaternion w last, matrix = transpose of the standard quaternion matrix.

## PC side (OpenDrakan layout confirmed)

0x0208 parses to the end on all 49 NPCs.mod skeletons. 0x0501 `pstr, f32 dur, u32 orig frames, u32 frames, u32 flags (1 no loop),
u32 nodes, u32 channels, u32, u32 refs, 3 f32`; 0x0502 `u16 n, n x {f32 t, 3x3 rows, xyz}`; 0x0503 `u16 n, n x {u32 first, u32 count}` per joint.

## Comparison of shared creatures (spider 3228, anims 590 normaldeath, 591 crawl, 598 ceiling_drop)

* **Ids and names are shared**: PS2 and PC both have `590 normaldeath`, `591 crawl`, `598 ceiling_drop` with identical durations. Same for the
  model's animation list prefix (590..598, 2004). So for creatures present on both discs an **id map by name is the identity**.
* PS2 keys are far fewer (normaldeath joint 0: 18 vs 28; crawl: 12 vs 23 on average a third) - the PS2 build key-reduces with its thresholds.
* PC keys are rotation *deltas against the bind pose* (key 0 of joint 1..N is identity; root key 0 translation = bind translation), PS2 keys
  are *absolute parent-relative* rotations. The rotation angle is preserved exactly (joint 9: 0.336 rad both), but the axes are
  permuted per joint in a way **not yet resolved**: a brute-force fit of `Q = L * (P K P^T)` (L = PS2 local bind rotation, K = PC key,
  P = signed permutation) gives error < 0.01 for chain joints 1-5, 18-20 but 0.04-0.25 for others, with no single P. The PS2 animation
  may also have been re-authored from the Softimage source rather than exported from the PC one.

## Rotation basis (SOLVED, 2026-10-31 evening)

Test bed: spider 3228 (identical PC and PS2 skeleton: 37 joints, same parents and names, bind locals equal to 1e-3), its nine shared
animations (590..598, 2004). PS2 keys were sampled by slerp at the PC key times (about 2.3 k key samples per joint set).

    Q_j = B_j * Wp * K * Wp^T        (3x3, row-vector convention as stored)

* `K` = PC key rotation (first 9 floats of the 0x0502 key), `B_j` = joint's local bind rotation, `Wp` = bind rotation of the **parent in
  model space** (product of the bind locals root..parent; identity for a root), `Q_j` = PS2 absolute parent-relative rotation.
* Reading: a PC key is a rotation in **model axes**, applied after the joint's bind-world orientation (posed world rotation =
  bind world * K when the ancestors are at rest). That is why the per-joint "axis permutation" of the first attempt looked unsolvable:
  it was the parent's bind world rotation (the body chain joints have a cyclic axis permutation, the leg/arm segments below them
  accumulate the segment rotations). A single signed permutation per joint never fits below depth 1.
* Found by: (1) `Bi*Q` (equal angle to K on every sample), (2) fitting a free rotation C to the axis*angle pairs, error 0.02 rad on
  all 36 joints, (3) matching C against bind products: C = Wp^T on every joint (Frobenius 0.00-0.03 where the key has an angle >3 deg).
* **Translation**: PS2 keys position on the root only (`hasPos`); position = PC key translation minus the root's first PC key
  translation (exact: 0.0026 - (-0.0038) = 0.0064 matches the PS2 pos). Non-root PC translations (up to 0.2 units on the spider, and
  constant offsets on leaf joints) are bone squash noise and are dropped; the PS2 track keeps the bind translation as `base`.
* Root rotation: identity basis (B0 = identity, Wp = I), keys equal exactly.
* **Validation** (`editor/packages/formats/src/ootfcreature.ts`, `compareAnims`): PC animation converted with the basis above, key
  reduced at 0.35 degrees, against the disc's own PS2 animation of the same id, 40 samples over the duration, all joints:

  | anim | mean rot err | max rot err | mean joint position err (units) | keys PS2 / ours |
  | --- | --- | --- | --- | --- |
  | 590 normaldeath | 0.64 deg | 5.7 | 0.0016 | 494 / 680 |
  | 591 crawl | 0.71 | 10.5 | 0.0011 | 713 / 1167 |
  | 593 flipdeath | 0.93 | 63.9 | 0.0069 | 638 / 715 |
  | 595 idle | 0.57 | 32.5 | 0.0013 | 67 / 116 |
  | 597 knockback | 1.52 | 41.2 | 0.0021 | 412 / 575 |
  | all ten (590..598, 2004) | **0.79 deg mean** | | 0.001..0.007 (spider is 0.3 units long) | |

  The residual is the PS2 tool's own key reduction and the nlerp/slerp difference; the large single-sample maxima are at fast
  joint flips (flipdeath), not at a systematic offset.
* The nine PS2 clips are NOT bit-identical to the PC ones (PS2 re-keyed), so equal poses is the strongest check available.

## Which creatures can share animations (census, 49 PC skeletons vs 154 PS2 skinned models on the disc)

Only **six model ids exist in both games**: 3228 spider (identical rig, usable), 3933 death null (3 joints, trivial), and four
re-authored ones that do NOT match (3671 crimsondragon 44 vs 41 joints, 3684 primitive 39 vs 44, 3685 skeldragon 54 vs 50, 3252 bladedragon
36 vs 39). Name matching adds nothing (goblin(h) PC 3680 vs PS2 wargoblin8 4127 are different models and rigs). The PC `Common/System`
Rynn/armour/dragon models (Arokh 3471, Rynn armours) are different from the PS2 ones as well. So for OotF almost every creature
takes the **convert** path; "reuse the PS2 model" only helps the spider. A PS2 creature of the same role (class mapper's
`PS2_SUBSTITUTES`) is the fallback when the converted mesh cannot be drawn (see below); then PC animation ids are irrelevant
because the class brings its own PS2 animations.

## Retarget strategy (implemented: `convertCreatureAssets`)

1. `sameJointTree(pcSkel, ps2Skel)` (joint count, parents, node names, bind pose within 0.02) true -> `reuse-ps2`: keep the disc model,
   skeleton and every animation id it already has; convert only PC animations the PS2 id set lacks and append their ids.
2. otherwise `convert`: `writePs2Skeleton` (0x0208, channels trimmed to 4 like the PS2 spider) + every PC animation converted with the
   basis above; the caller writes the skinned geometry records (0x0200/0x0203/0x0204 via `ootfmodel`, vertex weights are inside the
   0x0208 record).
3. `fail`: skeleton without joints, or no PC animation has one track per joint.
What always fails: the PS2 display list copy 0x0211 of a new skinned mesh is not generated (open since `ootf-models.md`); an animation whose
track count differs from the skeleton's joint count; the 0x0505 side table (sound/event ids) is not produced (665 of 1861 PS2 clips
have none, so the engine tolerates absence); PC translation keys of non-root joints; scale keys.

### How W1 calls it

```ts
import { parseSkeleton, parsePcAnim, parsePs2Anim } from './ootfanim.ts';
import { convertCreatureAssets } from './ootfcreature.ts';
const r = convertCreatureAssets({
    pcSkeleton: parseSkeleton(pc0208, 'pc'),
    pcAnims: new Map(ids.map((id) => [id, parsePcAnim(pc0501(id), pc0502(id), pc0503(id))])),
    ps2Skeleton: ps2Has ? parseSkeleton(ps2_0208, 'ps2') : null,       // same model id on the PAL disc, or null
    ps2Anims: new Map(...parsePs2Anim(...)),                           // that model's disc animations by id
    pcPositions: pcModel.positions,                                    // convert mode only: sphere centres
});
// r.mode: 'reuse-ps2' | 'convert' | 'fail'; r.reason
// r.skeletonRecord: 0x0208 payload to put in the level MOD (convert mode), null otherwise
// r.anims[]: { id, name, info (0x0501), data (0x0502) } -> ADB entries (the pack's `new/<STEM>.ADB/0501_<id>_0.bin` and 0502)
// r.animIds: the full id list for the model's 0x0208; r.animIdMap: pc id -> ps2 id; r.skipped: animations that did not fit
```

Lower level: `convertPcAnimToPs2(pcAnim, bindBasis(buildRig(skeleton, 0)), { toleranceDeg, channels })`, `compareAnims(rig, a, b)`,
`pcKeyToPs2Rotation`. Tests: `editor/packages/formats/test/ootfcreature.test.ts` (synthetic). Dev probes (not committed): `build/ootf-anim/*.ts`.

## In-game proof (spider, Surdana)

Pack `build/ootf-anim/pack_conv` (private, not committed; generator `build/ootf-anim/pack.ts`): `overrides/LEVEL1NA.LVL/0020_0_0.bin` adds five
Spider objects (class 1981, NPC db index 5, templated from the placed ones) around Rynn's start (450762,171700,340186), and
`overrides/NPCS.ADB/0501_<id>_0.bin` + `0502_<id>_0.bin` replace the ten spider clips (590..598, 2004) with the **PC clips converted by
`convertCreatureAssets`**. Run: `scripts/harness/play_run.py --recipe <5 shots> --env DRAKAN_MODS=<mods dir with animtest/ and mods.ini>`
(recipe path must be absolute: the game's cwd is `dist/DrakanPC`). Results (`build/ootf-anim/out/anim_{conv,bad}/a*.png`):

* conv (verified basis): spiders spawn, are skinned correctly and crawl / rear up / attack Rynn with natural poses.
* bad (same pack but `Q = K`, no bind conversion): legs are mangled and folded over the body. So the ADB overrides do apply, the engine
  accepts the writer's output (key counts, `hasPos` root track, 4 channels) and the basis matters visibly.
Not tested: a skinned model that is absent on the disc (needs 0x0211 and a new class), non-root translation keys, 0x0505.

## Earlier status (superseded by the sections above)

### What was deliverable

* Done and tested: `parseSkeleton` (both platforms), `writePs2Skeleton` (PC to PS2 0x0208: translation-first matrices, channel
  trimming, sphere centres from vertices, vertex total), `parsePcAnim`, `parsePs2Anim`/`writePs2Anim` (round-trip), `buildAnimMap`
  (pc anim id to ps2 anim id by name), `jointParents`.
* (done now, see above) PC animation to PS2 ADB.
  Also unvalidated visually: the viewer decodes no skinned pose, so no converted character was rendered; skeleton writing is checked by
  round trip only, not by the engine.
* Skeleton writing alone is not useful without matching animations: the PS2 engine pairs a model's animation ids with ADB entries of
  the same joint count.

