# Skinned playback in the editor - 2026-10-31

Decode: the 0x0208 / ADB containers were already parsed by `formats/src/ootfanim.ts` (OotF session). This pass added the
playback semantics and proved them on the PAL disc: `formats/src/skeleton.ts` (rig, bind pose, weights, LBS), `formats/src/anim.ts`
(clip sampling, root motion), `editor/scripts/anim-probe.ts` (disc proof), tests `formats/test/skeleton.test.ts` (synthetic).
Layout and evidence: `docs/reference/level-format.md` sections 11 (0x0208) and 12b (ADB).

## Disc probe (`node --experimental-transform-types --no-warnings editor/scripts/anim-probe.ts <iso>`, about 1 minute)

19 MOD files, 154 skinned models, 2512 ADB clips, 7747 clip ids listed by models.

| Check | Result |
| --- | --- |
| Skeleton decodes to the last byte | 154 of 154 (the RESOURES model 4871 fails earlier, in 0x0204) |
| Rest pose through the rig reproduces the mesh | 154 of 154 (error < 1e-3) |
| Clips decode | 2512 of 2512 |
| Listed clip exists / fits the model's joint count | 7746 of 7747 / 7746 of 7746 |
| Posed vertices finite and bounded (30984 samples) | all but 7 samples of two clips whose root travels several model units |
| Vertices with no weight (ride the root) | 4200 of the disc's vertices, none with more than 4 influences |
| Clips whose root travels more than 5 percent of the model size | 1435 (locomotion, rolls) |

Unkeyed tracks do not always carry the local bind translation (27822 of 429549 tracks differ: weapon, sheath, foot and
sphere joints); playback uses the track's own base as authored.

## Editor

* `scene/skinned.ts` `SkinnedModel`: LOD 0 skinned on the CPU every frame (Rynn 1.6k vertices); `ClipClock` for play, pause, speed.
* `editor/animpreview.ts`: `createAnimPreview` (orbit canvas on one shared WebGL renderer, play / pause / scrub / speed / in place),
  `objectAnimationBody` (Details "Animation" section: class animation fields first, then the model's clips, idle by default, find box).
* Animation picker preview (`refpicker.ts`): the highlighted clip plays on the owning class's model; a clip with a different joint
  count is reported, a class without a model borrows a matching skeleton from the clip's container.
* Worker: `animdata.ts` (`loadRefRig`, `loadRefClip`, `classModelRef`, `rigForTracks`), `RefService.rig/clip/classModel/modelForTracks`.
* Animation picker rows now say `0.97 s, 65 joints` instead of the old "frames" (that header word is the track count).

Checked in the browser (Level 1A, Playwright against a production build of the app): Rynn idle / run / walk / crouch attack,
Purple Orc Center idle / walk / Attack_Mid, Blade Dragon (Crow) idle / flight / wingstretch, and the picker preview on an orc's Walk field.

## Open

* No in-viewport playback on the placed instance (the preview is in Details and the picker): `ModelLayer` batches static instances and
  rewrites their matrices on every sync, so swapping one for an animated mesh needs a hook there.
* Interpolation is nlerp; blending, layering of channels (lower / upper body) and the weapon attachment joints are not modelled.
* Animations of PC (OotF) models are not played (different key encoding, see `ootf-animation.md`).
