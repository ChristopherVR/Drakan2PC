# Free mouse combat and field of view

Update, 21 October 2026: explicit F/MMB targeting is restored. Free combat
yields to native target lock and the alternate bow sight camera. The original
no-targeting policy below is historical. See runtime/input/README.md.

The user requested Order-of-the-Flame style mouse combat, including removal of
controller target lock. Free mouse combat is now enabled by default.

## Player behavior

- Mouse/keyboard input disables native target-lock requests, including F and
  middle-click and custom bindings that reach the same native helper. A target
  inherited from controller play is released with the game's own cleanup before
  mouse control resumes. No target reference is orphaned by direct flag edits.
- Direct mouse input turns Rynn and changes camera pitch during attack and
  magic/parry animation states that previously vetoed it. Camera orbit is cleared
  so the camera follows Rynn instead of circling an enemy. Mouse displacement is
  still consumed once, with no stick curve, acceleration or frame-time multiplier.
- Actual gamepad activity retains ownership across button release and keeps
  native targeting. Mouse or keyboard activity explicitly takes ownership back.
  An idle connected pad does not take control. The original dragon path remains.
- Menus, lost focus, suspension, scripted sequences, death/control locks and
  non-Rynn camera overrides retain their ownership. Native vertical camera limits
  remain; this does not implement a new camera collision system or combat rules.
- F1 > Controls > Mouse and gamepad > **Free mouse combat** restores legacy
  mouse targeting when disabled. The persisted key is `[mouse] free_combat` in
  drakan_input.ini; old files default to enabled without replacing bindings.

The native helper hook is 0x45ef50, on entry only. The mouse override is gated to
Rynn's known vtable, active gameplay, camera mode 6 and keyboard/mouse ownership.
Existing animation/combat flags are not cleared to force mouse turn acceptance.

## Field of view

F1 > Display > **Field of view** applies live, saves through Apply/OK, and ranges
from 60 to 120 degrees. 80 is the original view. These are horizontal degrees at
4:3; wider aspect ratios add horizontal coverage. The setting is `[video] fov`
in drakan.ini (environment override DRAKAN_FOV). Invalid/nonfinite inputs fall
back to 80; finite out-of-range values are clamped.

Both projection axes and CPU culling are adjusted together inside the existing
world-render scope. The scale is tan(40 degrees)/tan(fov/2), latched with aspect
for an entire world render even across guest yields. EndWorld restores native
projection before HUD/inventory/UI rendering. Scripted cinematics retain their
authored FOV; normal gameplay and its world backdrop use the chosen value.

## Verification

- Input: **3,200 checks, zero failures**, including raw turn/pitch through native
  animation vetoes, one-time sample consumption, controller ownership, mouse
  takeover, suspension/menu/death/cinematic exclusions, and configuration reload.
  Legacy target-pitch tests explicitly select free_combat=0.
- Settings: **327 core checks and 124 menu checks**, zero failures. FOV and combat
  persistence, bounds, invalid values, dirty tracking and live FOV apply covered.
- Projection: **2,513 checks passed**, including 900 existing side-plane witnesses
  and new configurable-FOV/culling, scope isolation and native UI restoration.
- Styled settings rendering: **63 checks passed** at 720p, 1080p and 4K. Display
  and Mouse/gamepad screenshots were visually inspected at 720p.
- Release game build succeeds with the existing ShowCursor and /FORCE warnings.
- Live hostile encounter: F and MMB leave target slot zero. A physical-pad script
  acquires target 83; mouse input releases it. During native animation flag94=80,
  ten (2,-1) mouse samples produce approximately -.040 rad body yaw and +.020 rad
  pitch. Opposite samples during magic/parry return both axes, and neutral mouse
  samples hold the view. HP decreases from 100 to 90 during the encounter. Both
  combat buttons release; the final pad/command hold sets are empty.
- Paired 80/100-degree gameplay runs finish normally with private cards. At 100,
  logged scale=.7040882, sx=-.6293247, sy=-1.1987138 in camera mode6/scripted0.
  Native/scripted scale remains 1. Inventory tutorial is dismissed with Enter
  and the scene resumes. The separate hostile run ended inside that tutorial;
  its completion is not evidence of inventory close. Renderer screenshots, not
  desktop/BitBlt captures, are used for visual FOV inspection.

Private binary, source hashes, scripts, RAM and projection reports, screenshots
and completion receipts are under `build/free-combat-fov/`. Cards are private
copies of existing fixtures. Successful parry, combo parity, all weapons/areas,
portal visibility at every FOV and dragon combat are not claimed by these runs.
