# PS2 model record 0x0211 decoded and generated (2026-11-01)

Follow-up to `2026-10-31/ootf-displaylist.md` (skinned models and the Birch tree class vanish without 0x0211). Code:
`editor/packages/formats/src/ootfdisplaylist.ts` (decoder `decodeDisplayList` / `expandTriangles`, encoder `buildDisplayList`,
`withDisplayList`, `remapSkeletonVertices`), tests `test/ootfdisplaylist.test.ts` (synthetic fixtures only). Layout is in
`docs/reference/level-format.md` (section 11, "0x0211"). Probes and packs are private scratch under `build/dl/*.py|ts` and
`build/ootf-run/m-dl1|m-rynn*`.

## What it is

Not GS packets: a strip list the engine's vertex program consumes. Per texture batch a stream of 1-based vertex-window slots
(0 ends a strip) plus a parallel (u, v) per element, preceded by "vertex groups" (contiguous runs of 0x0203 vertices) that are
copied into a vertex window before the batch. The record header carries the group counts and the batch count. That is why the
small models in the 2026-10-31 notes looked like "index triples + UV rows": a one-group, one-texture model is just one strip
batch.

## How it was decoded

1. 804 (cube, 8 vertices, 12 triangles): the u8 stream `07 05 08 06 00 03 04 01 02 00 ...` are 1-based vertex numbers in groups of
   four with a 0 terminator (two triangles per strip); the floats after it are one (u, v) pair per stream element including a (0,0)
   pair on the terminator. Stream and UV block both have a capacity `cap` (64 here) stored in front of each (`u32 cap`).
2. Header words by comparing models of different sizes: `1, V, 0`, then `count/cum` per group (cum padded to a multiple of 4;
   the 0x0203 vertex count is the padded total: 97 -> 100, 147 -> 148, 18 -> 20), then batch count and the first batch's texture.
   Each batch ends with the next batch's texture id; before each batch comes `u32 W` and W x `{group, slot}`.
3. Checked with a decoder against the 0x0204 polygons: models with one group match the triangle set per texture exactly (62 of
   64 in LEVEL1ON, 422 of 434 in RESOURES; the rest have a tail section, below); with the window model (slot s-1 holds the
   vertex `start[g] + (s-1-slot)` of the last group loaded there) **every multi-group model matches exactly**: LEVEL1ON 196,
   RESOURCES 192, SYSTEM 7, WORLDCON 332, none failing (`build/dl/ver2.py`). That includes Birch Tree01_H (893).
4. UVs equal the 0x0204 UVs up to an integer shift (83 % shift 0; the shift looks arbitrary, not normalised per strip).
5. Strip orientation relative to the polygon winding: constant inside a strip, but 62 % of strips are opposite and 38 % equal
   (after parity correction). Both occur on every kind of model, so the engine does not rely on it; the encoder defaults to the
   majority and has `sameWinding` for the other. An in-game test of the other orientation looked the same as the default.
6. `cap` is the element count (strips + one terminator each) rounded up to a multiple of 64 on 2813 of 3649 batches, the rest
   are looser; `triangles` is exactly the sum of (strip length - 2) on every batch.
7. Window sizes: largest group on the disc 225 (static) / 221 (skinned, RESOURES 6237), caps up to 1408 elements per batch.
   Joint count per group of skinned models: at most 31 (4331), usually under 15; Rynn's groups hold 1..14 joints. The
   exporter clusters vertices by their joint sets.

## Disc scheme for skinned models (Rynn, SYSTEM 3877)

Parsed with the same grammar (the leading word 2 changes nothing): 24 groups, 10 batches, 465 strips of average length 9.8,
exactly the 3068 LOD 0 triangles. **Every vertex exists once**; groups are joint clusters (1..187 vertices, 1..14 joints) and a
batch loads *several* groups, packed from slot 0 (`W` up to 7, e.g. `{g7,0}{g1,124}{g6,149}{g8,191}{g9,199}{g3,204}{g4,206}`),
small groups shared by many batches. Batches are texture-major (0,0,0,0,1,1,1,1,2,2). The 7014 bytes after the last batch are
the second LOD (387 polygons), in the u32 section form.

## Not understood

* The tail section (second LOD on Rynn; on the 24 other large models 540..17486 bytes of u32 words, many counting down, e.g.
  `01 00 00 00 01 00 00 00 b4 00 00 00 ca 00 00 00 40 01 00 00 89 02 ...`; there the header covers part of the triangles and the
  tail the rest, probably 32-bit vertex numbers for models above 255 vertices). The encoder splits instead and is single-LOD.
* The leading header word 2 on those models. `0x0214` (2180 bytes, identical on all Rynn sets) is kept as is.
* What decides the slot values on the disc (the encoder packs loads from slot 0).
* What exactly the engine limits per batch/group: the first Rynn attempts (below) lost triangles, the disc-style layout did not.

## Encoder choices

* Fits one window (nv at most 225, joints at most `maxJoints`): one group in the original vertex order, one batch list per
  texture, W = 1 on the first batch. Output vertex count = nv padded to a multiple of 4 (pad vertices zero, no weights).
* Models without joint info that do not fit (trees): chunks of at most `maxWindow` vertices, each chunk a group with **copied**
  vertices (polygons become triangles). Validated in game on the Birch tree.
* Skinned (`vertexJoints` given, not fitting): the disc scheme above: vertices grouped by primary joint (48 vertices / 16 joints
  per group), batches load all groups they touch. `source` is a permutation plus pad vertices, so the 0x0204 polygons only
  need re-indexing and the 0x0208 weights follow through `remapSkeletonVertices` (ascending vertex order per list, LOD vertex
  count = padded total, poly count, total vertices).
* Greedy strips that only extend over a triangle whose shared corners carry equal UVs (seams break a strip); average length
  about 5 against 10 on the disc (twice the strips; harmless in game).
* Batches are cut at 448 elements.

## In-game results (New Game on Level 1A, PAL, `scripts/harness/play_run.py` via `build/ootf-run/run.sh`, game lock held)

| Pack | Change | Result |
| --- | --- | --- |
| `m-dl1` | WORLDCON model 893 (Birch Tree01_H, 304 vertices, not skinned): 0x0211 regenerated by the encoder (3 copied groups, 3 batches), 0x0203/0x0204 relaid | **all Birch trees draw again** (the 2026-10-31 `wc1` pack made them vanish); trunks and leaf cards textured. `shots/dl_birch_regenerated_0211.png` |
| `m-rynn` | SYSTEM Rynn models (all eight armour sets), first LOD only, 0x0211 regenerated with copied-vertex groups of 225, 0x0203/0x0204/0x0208 relaid with copied weights | Rynn draws textured and skinned (walks, turns) **with holes** on arms, torso, head |
| `m-rynnw` | window 128 | fewer holes |
| `m-rynnf` | strips wound like the polygons | same holes: not the winding |
| `m-rynnj`, `m-rynnk`, `m-rynnm` | groups also limited to 16 / 8 joints, triangles clustered by joint, weight lists sorted | holes shrink to a few patches (elbow, thigh, neck) but stay |
| `m-rynns` | **disc scheme**: vertices unique, joint-cluster groups of 48, several loads per batch | **no visible holes**: Rynn complete, skinned and animated. `shots/dl_rynn_regenerated_shared_groups.png` |

Reading: copied vertices break skinned groups somewhere (the weights of a copy are right, but the engine seems to bind weights to
a group's vertex range), whereas the disc's unique-vertex layout works. Static/tree models are fine with copies.
`m-rynn*` replace all eight Rynn sets with a single-LOD version (LOD 1 dropped), so the far LOD is missing in those packs.

## What is left

* Wire it into the creature path: `ootfmodel.ts` still reports skinned models as skipped. The pieces to call are
  `withDisplayList(model, { vertexJoints })` (vertex layout + 0x0211), `remapSkeletonVertices` on the skeleton, then
  `writePs2Skeleton` + `writePs2ModelRecords` (without 0x0207) and `convertCreatureAssets` for the animations. LODs: single only.
* A PC creature has not been run through the whole chain in game yet (this test used a disc skeleton and disc weights); the PC
  skeleton's weights must first be mapped to PS2 vertex order (0x0208 `weights` per joint in converted vertex numbering).
* `ootfmodel.ts` writes 0x0207 for every model; skinned disc models have none.
* `writePs2Skeleton` does not pad odd-length LOD names (`pstr`), which breaks byte-exact rewrite of the two-LOD SYSTEM
  skeletons (37 of 44 rewrite byte for byte).
