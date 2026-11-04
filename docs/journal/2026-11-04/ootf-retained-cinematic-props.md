# Retained OotF cinematic prop display lists

This investigation concerns the imported Intro Owl Face model, which the production pipeline still drops with other rfl17/NPCS generic skinned props. The private retained-prop candidates exercise that path without removing its production exclusion. They are diagnostic assets, not a promoted campaign pack.

## Geometry and native evidence

The original PC mesh renders an intact round owl in a private CPU reference (`build/scratch/ootf-props-anim/owl-source-1.png`). Source model 9 has 68 total vertices; LOD0 uses 66 vertices and 128 triangles, with two joints and two animation clips. Converted Resources model 7351 retains all 128 triangles, the two joints, the source transforms and both clips, with two vertex padding entries. Its 0x0211 display list has one group of 66 vertices and one batch; its longest strip contains 82 elements.

Expanding that converted display list gives exactly the source geometric triangle multiset: zero missing or extra triangles. All 128 triangles consistently reverse the source polygon winding, as intended by the encoder. Corner UVs remain intact, and a CPU render of the decoded list reproduces the original complete owl (`owl-decoded-1.png`). These checks exclude a lost source triangle, a mixed-winding strip expansion, or an unrelated rig/texture substitution as the cause of the observed radial gaps.

A native GIF capture reconstructs the same missing wedges before D3D rendering. Running the same retained pack with `DRAKAN_VU_AOT=0`, `DRAKAN_VU_BATCH=0`, and `PS2X_VU1_THREAD=0` reproduces the gaps and exits cleanly (55 seconds). The issue is therefore not isolated to the AOT specialization. No runtime renderer, VU, model transform, skeleton, animation or global winding change was made.

## Native strip constraints

Auditing 1,593 decoded original models from SYSTEM, RESOURES, NPCS, WORLDCON and LEVEL1ON yields 139,353 strips. Of these, 22,249 are in models with character records and 117,104 are static. Both groups have an empirical maximum of 30 elements. Neither group contains any strip whose vertices and zero terminator cross a 64-element stream boundary. Starts are not required to align to four elements: all four residues occur, and many strips cross 32-element boundaries. Variable runs of zero entries pad the stream before a strip would cross the next 64-element boundary.

This second rule explains why a length-only correction was insufficient. Splitting the Owl's 82-element strip into lengths 30, 30 and 26 placed them at stream offsets 0, 31 and 62. The third strip still crossed the native 64-element boundary. Length-only rendering removed the large wedges but left a thin incorrect diagonal across the beak/cheek.

## Controlled live comparisons

Every comparison starts through the title and a fresh New Game using staged executable `6D747DB43DFD7C50C39781F5DDD973C6834654C7BF409D8C87CA0A1E0C116D46`, D3D11, ray effects off and world rate 60. The private pack changes only converted Owl model 7351's 0x0211 record in RESOURES.MOD; vertex layout, polygons, UV values, winding, textures, skeleton, animations and scene actions remain fixed. Archive rewriting appends the replacement record; all other SRSC payloads remain unchanged.

| Encoding | Native result | Private evidence |
| --- | --- | --- |
| Original 82-element strip | Large radial gaps | `ootf-props-anim/intro-retained/intro_2.png` |
| One triangle per strip | Intact owl, clean exit 0 / 54 seconds | `ootf-owl-short-strips/short-strips/intro.png` |
| Maximum 30, no internal block padding | Thin incorrect cheek/beak triangles remain | `ootf-owl-cap30/cap30/intro.png` |
| Maximum 30 plus zero/UV padding before a block crossing | Intact owl including the previously incorrect cheek, clean exit 0 / 52 seconds | `ootf-owl-block64/block64/intro.png` |

All evidence paths are under ignored `build/scratch/`. The final candidate is generated with the actual corrected encoder, and its expanded oriented triangle/corner-UV multiset and 66-vertex identity layout are verified equal to the prior record. Single-triangle strips are a diagnostic control, not the production approach.

## Converter correction and checks

The encoder now splits long strips at 30 elements, carrying the final two vertices into the continuation. The advance is 28 triangles, which is even, so resetting strip parity preserves winding. Before serialization it inserts matching zero stream entries and zero UV pairs whenever the next strip and its terminator would cross a 64-element boundary. Batch planning includes this padding when enforcing the existing 448-element capacity; batches after a flush retain the established vertex window rather than reloading unrelated groups.

Synthetic tests cover lengths 30, 31, 32, 58, 59, 86 and 222 with both winding modes. They compare complete oriented triangle/UV multisets, inspect raw block boundaries and zero UV padding, and enforce batch capacity. A separate 85-quad case fits under the old unpadded batch count but requires a padding-aware flush; it verifies 168 plus 2 triangles across two batches and no extra second-batch vertex loads. Full formats validation passes: 329 tests in 30 files, TypeScript compilation, Prettier and diff checks.

The change affects only display lists synthesized by the converter. Original native records are neither rewritten nor re-encoded. The retained-prop exclusion remains unchanged: additional solo/co-op lifecycle and actor-rig acceptance is still needed before restoring those objects in the production campaign. This bounded rendering repair does not establish full cinematic or campaign fidelity.
