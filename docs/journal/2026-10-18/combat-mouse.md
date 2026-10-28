# Direct mouse combat follow-up, 18 October 2026

Superseded combat policy: [free-combat-fov.md](free-combat-fov.md)
records the subsequent user-requested removal of mouse target lock and combat
animation look restrictions, plus the new FOV setting. The preserved targeting
behavior described below belongs to the earlier delivery only.

User report: mouse/keyboard combat intermittently locks or feels like an analog
stick; the intended feel is Order of the Flame on PC.

Two concrete defects were corrected in `runtime/input/src/DrakanInput.cpp`:

- Native camera look retains smoothed rates at camera +9c/+a0/+a4. Disabling
  stick position mode alone does not remove that momentum. Direct mouse look
  now clears these rates **before** native integration, including neutral mouse
  frames. Physical stick input, explicit RSTICK bindings, menus, suspension,
  stick look mode, camera freeze and the default dragon stick path retain their
  existing behavior. An active physical left stick also retains native rates.
  This clearing is independent of the persistent-pitch setting.
- `FUN_00423790` selects an alternate camera channel when camera flag 0x800 is
  set without 0x1000. `Camera_UpdateLook` adds pitch/yaw to +94/+98 in that mode;
  Rynn's 0x32cd88 body-turn predicate returns zero there. Previously the host
  rejected pitch and attempted only a forbidden body turn, losing both mouse
  axes. Raw displacement now reaches +94/+98 directly. Native vertical-axis
  suppression is honored. No stick synthesis, dt multiplier, body turn or
  duplicate application is introduced. A saved target-pitch snapshot cannot
  cross into this channel during a native callback pair.

The normal camera retains its native pitch limits, animation/control vetoes,
target selection and target-facing behavior. F/MMB still explicitly request
native targeting; this change does not remove its enemy-facing assistance.
Dragon steering remains the existing stick implementation. This is not a claim
of complete Order-of-the-Flame combat parity.

## Validation

- New regression against the previous DrakanInput.cpp: 36 failures in 3,153
  checks. The same frozen test/source set with the correction passes 3,153.
  Adding the physical-left-stick preservation case yields 3,162 passing checks.
- Shared input suite, including the concurrently added dialog regression:
  **3,174 checks, zero failures**. Those dialog edits are separate work.
- Release `ps2EntryRunner` build succeeds. Existing ShowCursor duplicate-symbol
  and /FORCE linker warnings remain. No runtime/glue source edits belong to
  this correction; other in-progress shared changes were not reverted.
- Private scripted combat run completes with exit 0 and script quit. Forty
  counts right / twenty counts up produce -.08 rad heading / +.04 rad pitch;
  opposite counts return to the original view. Real target 83 is acquired and
  pitch holds across neutral samples. LMB/RMB both release; final pad and held
  command sets are neutral. HP falls from 100 to 90 during the encounter.
- A separate final-binary delayed incoming-attack recipe completes with exit 0:
  pitch is exactly -.148104459 at hold00/10/20. HP is 100 at those three samples
  and 90 after target release, so this run does **not** prove an incoming hit
  occurred inside the sampled hold. No successful-parry or combo claim.
- Neither live recipe entered alternate camera flag 0x800. That correction is
  established by native-code inspection and CPU regression, not live rendering
  of that mode. Scripted input also does not establish real Raw Input arrival
  or subjective mouse feel across every animation.

Private evidence and executable: `build/native-combat-mouse/`. The frozen source
and CPU runner are under `source/` and `frozen-cpu/`; live manifests, RAM reports,
input audits and completion receipts are under `t/`. Private cards are copied
from the existing hostile675 fixture, with unchanged card content checked
against that fixture. Original saves are not used for writes.
