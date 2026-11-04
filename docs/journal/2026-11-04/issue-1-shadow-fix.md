# Issue #1: sky classification and direct-shadow ownership

## Reporter evidence and local configuration

Re-read issue https://github.com/ChristopherVR/Drakan2PC/issues/1 and downloaded its attached `drakan.log` and both
PNGs into ignored `build/issue-review/downloads/`. The issue still reports duplicate entity shadows and a circular
camera-following shadow on both DX11 and WebGPU. The user identified the location as the first Surdana cliff.

The deployed INI now requests D3D11, scale 4, 16:9, borderless, initial window 1280x960, vsync off, game filtering,
weave, flicker filter off, uncapped world rate, foliage 0.90 and German, with modern water, reflections and ray
shadows enabled. The original INI is backed up in the download directory. Unknown reporter DLSS/HD texture choices
were not inferred. Harness runs isolate those features and use empty mod/override folders.

## Confirmed nonphysical occluder

The earlier shell filter missed a four-triangle, two-material model near the camera. Its resource is `b04a6`;
the disc's WorldCon class names it **Aachen B Planets**, with RFL class 26 (native `0x1001a`, sky) and Follow Mode /
Offset / parallax fields. The native class table maps its observed handler to the same class. This is semantic
ownership evidence, not a shape/proximity guess.

The exporter now reads the actor's resource record class with bounds/overflow/index checks and excludes explicit
sky-class models. Existing fallback shell checks remain for generated geometry without a resource class.
On the same private RAM snapshot, the old exporter yields 1219 instances and the new exporter 1215. The removed
instances are the planet billboards, two sky barrels and a tint box; every retained metadata row is identical.
Receipt: `build/issue-review/sky-exclusion-receipt.json`. The barrels/box were already filtered later by DXR;
the open planet billboards were admitted blockers. Their exact contribution to the reporter's marked circle
has not been established with a matched capture of that camera pose.

## Duplicate direct shadows

Native projection and direct ray casting previously had independent ownership of the same character. The fix
keeps native projection, records its resolved selected-body object and lifetime generation during this world,
and publishes ownership before the renderer's EndWorld event. Both backends reserve instance mask 8 for direct
casters. A native-owned model keeps land/reflection mask 1, so it remains a receiver, reflector and AO contributor,
but cannot create a second direct-ray silhouette. World geometry and unprojected models keep direct casting.

This differs from the withdrawn automatic native-suppression experiment: it never removes native projection or
depends on last-frame renderer feedback. A failed/bypassed modern pass keeps the original native character shadow.
The tradeoff is explicit: natively projected characters use their original projected silhouette, including its
original receiver/direction limitations. They do not gain a separate ray silhouette on other surfaces such as water.
The native projector hook is part of the normally enabled multiplayer-hook installation; disabling those hooks
also removes ownership recording. The explicit native-off diagnostic records no owner and restores ray casting.

## Validation

Focused `scene_models`, `ray_trace` and `ray_water` CTests passed together. CPU regressions cover sky resource-class
lookup bounds, ownership lifetime mismatch, per-world reset, immutable queued snapshots and repeated EndWorld.
GPU regressions on both backends prove that native ownership removes direct casting and clearing ownership restores
it. DXR additionally checks retained reflection and contact AO. Existing world/static-world shadow tests pass with
the new mask, so the mask change does not simply disable the whole effect.

The pre-fix reporter-profile matrix (`build/harness/issue1-shadow-fix-baseline/`) completed all five cases with
successful execution/configuration receipts. Its gate-ground native-off view removes the clear native silhouette;
the surrounding world shade does not reveal a distinct replacement silhouette there. The first movement probe had
an incorrectly ordered scratch recipe and is not acceptance evidence. A separately relocated private save reached
the starting house, rather than the reporter's cliff; those images also do not reproduce the exact report.

The sky-only build completed successful reporter-profile DX11/WebGPU captures under
`build/harness/issue1-sky-ownership-fixed/`. The combined fix was then rebuilt/deployed through the canonical
incremental builder.

### Combined live validation

`build/harness/issue1-shadow-ownership-final/` completed all four runs (DX11/WebGPU, native-on/ray-on and
native-off/ray-on) with clean scripted exits, complete backend-supported receipts and unchanged executable identity
within and across runs. SHA-256: `db63554e3ce143c8860f259805fb70a197254e11ea609a305d70f292a8369eae`.
All runs retained the reporter's known scale/aspect/presentation/foliage settings and modern effects.

The DX11 native-on epoch-1000 CSV has 1215 instances, zero planet billboards and exactly one native-owned caster:
Rynn's resource 3, object `a9d070`, generation 9606. The native-off CSV has no owners. The world-end ownership
receipt also appears on WebGPU's native-on run. Inspected ground captures on both backends retain one clear
original silhouette; the native-off captures isolate modern shading. These gate views do not reproduce the exact
cliff/camera arrangement in the report. WebGPU still lacks the DX11 scene/metadata/depth dump, and its harness
reflection-enable receipt remains explicitly unverified.

Five focused CTests passed: `scene_models`, `ray_trace`, `ray_water`, `model_motion`, `character_roster`.
Clang-format dry-run and whitespace checks passed for the changed C++ files. Repository source audit passed with
zero prohibited tracked files. Concurrent unrelated workspace changes were preserved; no source commit or issue
comment was made by this investigation.

After the final guest-address normalization and concurrent workspace rebuild, the deployed binary is
`812f55d36e6f14335cefe59bc6c92c600fa704ab4638ded70ac3b361bd450b11`. A further DX11 reporter-profile native-on/ray-on
run (`build/harness/issue1-shadow-final-deployment/`) exited cleanly with complete supported receipts, unchanged
binary identity, one current native-owned Rynn instance and no planet billboard instances. The play-folder INI
still has modern water, ray reflections and ray shadows enabled.
