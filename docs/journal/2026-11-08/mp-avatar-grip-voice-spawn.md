# Multiplayer avatars: weapon grip, voices, spawn flash

User report: swapped characters sometimes show as Rynn first and a few seconds later appear at their real spawn; held
weapons are wrong on every character except Rynn; hands look wrong; more than one weapon shadow; hit, jump and
climbing sounds use Rynn's voice.

## Weapon grip (fixed, verified in game)

* Measured with the generator's own rigs (`buildRig` / `bindBasis` on the PAL MOD skeletons): the delta retarget gives a
  target joint the world orientation `sourceWorld * inverse(sourceBind) * targetBind`. On every roster rig the
  `Rweapon` bind frame is 102 degrees off Rynn's and `Lweapon` 56 to 148 degrees. Held items are Rynn's own models,
  attached to that joint, so each was turned by that constant.
* Both weapon joints are leaves on both rigs (fingers hang off the hand). `RetargetAnimOptions.worldJoints`
  pre-multiplies their local keys by `sourceBindWorld * inverse(targetBindWorld)`. That yields exactly the source
  joint's animated world orientation. A unit test checks this against an independent chain evaluation.
* `build/harness/mp-fix-0108` (Monk host / Ranwulf joiner, `coop_characters.py --motion`, real equipped dagger 33cbc):
  all 14 checks pass. `ready.png` and `b_strafe_attack_4.png` show the dagger upright in the fist and along the thrust.
* Hand / finger pose was not changed. Fingers already follow Rynn's model-axis deltas; finger bind differences are
  axis conventions. No separate defect was isolated, so this remains open if the report persists after the grip fix.

## Voices (fixed, unit tested; not yet heard live)

* Only Rynn's seven HitRxn ids were mapped, and only for the local view. Jump / hoist / falling grunts are not class
  fields: her clips trigger them through animation events ("Rynn Jump 1..3", "Hoist Rynn 1..3" in SYSTEM.SDB).
* The generator now emits `kRynnVoiceSounds` (68 ids with kind damage / death / other) from her voice fields plus the
  SDB names. `mapVoiceSound` gives damage and death cries the male damage set and silences the rest, because NPCS.SDB
  has no male human attack / jump / climb grunts.
* The hook identifies the emitter: `s0+8` on the animation-event path (`0x10d100`, return `0x10d1c0`), otherwise the
  nearest player body to the copied position. Remote stand-ins playing retargeted Rynn clips are covered too.

## Spawn flash (fixed; local hide verified, arena path not run live)

* In a versus arena the server's spawn assignment and its retried teleport come seconds after level up. Until then the
  player stands at the level start, as Rynn while the selected body loads.
* `drakan_arena::awaitingSpawn()` (bounded to 600 ticks after level up) and a bounded 300-tick wait for a loading body
  set `View::hideAll`. That hides the original model, the selected body, held items, the projected shadow and the
  scene-export copy. Protocol v10 `PlayerState.placing` (flags bit 1) makes receivers show no ghost meanwhile.
* The harness log shows `avatar hidden: selected body still loading` then `avatar shown` on each spawn and after
  save / recreate, with no Rynn frame in between.

## Duplicate weapon shadows (not reproduced)

The harness frames show one shadow per character. The selected body's held items are now part of the avatar set
(`bodyWeapons`), but no projector change was made without a reproduction. A save or screenshot of the case is needed.

## Weapon shadow / hand pose harness (`scripts/harness/mp_weapon_shadow.py`)

Two co-op instances with non-Rynn characters (default Monk / Ranwulf), dagger equipped through the native inventory.
They look down in steps (idle and mid-swing), stand still for the hands, idle 10 s, walk, then use next / previous
weapon (mouse wheel: weapon cycling, not zoom) and quick slot 1. `DRAKAN_SHADOW_TRACE=<n>` (new, opt-in) logs each
native character-projector call with the object's role (controller, selected body, original or body item, remote
ghost body or item); `shadow_summary.json` counts them per frame.

* `build/harness/wshadow1`: about 1050 frames per side. Held items are never projected on their own; they come only as
  part of their character's silhouette. One weapon shadow per character in every ground frame.
* `wshadow-noray` (`GSR_RAY_SHADOWS=0`) and `wshadow-nonative` (`DRAKAN_NATIVE_CHARACTER_SHADOWS=0`): the character
  shadow is the native projection alone. With it off, no character or weapon shadow remains, so the ray pass adds
  no second weapon shadow.
* `wshadow-sheathe` (Rynn host / Monk joiner): idle, walk and the dagger grip of the Monk match Rynn frame for frame.
  Rynn's own idle splays and flexes her off-hand fingers; the Monk reproduces exactly that, so the finger motion
  comes from her clip, not from the retarget. The fixture save has one weapon and an empty quick bar, and the
  game did not sheathe during 10 s idle, so weapon switching and putting the weapon away are not exercised yet.
