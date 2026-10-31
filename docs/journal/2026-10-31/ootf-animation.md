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

## What is deliverable

* Done and tested: `parseSkeleton` (both platforms), `writePs2Skeleton` (PC to PS2 0x0208: translation-first matrices, channel
  trimming, sphere centres from vertices, vertex total), `parsePcAnim`, `parsePs2Anim`/`writePs2Anim` (round-trip), `buildAnimMap`
  (pc anim id to ps2 anim id by name), `jointParents`.
* **Not done: PC animation to PS2 ADB** (needs the per-joint rotation basis above, then key reduction and quaternion fitting).
  Also unvalidated visually: the viewer decodes no skinned pose, so no converted character was rendered; skeleton writing is checked by
  round trip only, not by the engine.
* Skeleton writing alone is not useful without matching animations: the PS2 engine pairs a model's animation ids with ADB entries of
  the same joint count.

## Retarget plan (recommended)

1. For an OotF character whose model name exists on PS2 (the class-name mapper already finds these): keep the PS2 model, skeleton and its
   whole animation list; map every OotF animation id through `buildAnimMap(pcAnims, ps2Anims)` (name match; unmatched ids fall back to the
   PS2 idle/same-role animation chosen by the sequence converter).
2. Characters absent on PS2 (Drakan-only NPCs, OotF bosses): substitute the nearest PS2 skeleton with the same joint topology
   (`jointParents` equality + node names), otherwise drop as now.
3. To import PC skeleton + animations for real: finish the basis fit (take two or three more creatures with equal joint trees, solve P per
   joint from the first non-identity key with a least-squares quaternion fit), then add `convertPcAnimToPs2` using `writePs2Anim`.
   `writePs2Skeleton` already handles the model half; the 0x0211 display-list copy and vertex counts are the other unknowns for new meshes.
