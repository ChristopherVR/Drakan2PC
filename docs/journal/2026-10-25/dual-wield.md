# Dual wielding option — 2026-10-25

Request: a Gameplay option to dual wield weapons, with combat support. Reference summary:
[combat-controls.md, Dual wielding](../../reference/combat-controls.md#dual-wielding).

## Research (decompile `build/recomp_ui/alldecomp`, RAM dumps)

* The game has no off-hand weapon. Its equipment table at `player+0xa48` has five slots; the item -> slot rule is the
  controller's `0x344cb8`. The shield slot (2) is the only left-arm slot a melee weapon can borrow without touching
  native structures: all six readers of `player+0xa68` (`Game_OnInputCommand`, `0x3016e8` x6, `0x314578`, `0x31ca20`,
  `0x32c510`, `0x342478`) check class `0x10090` first.
* The right-hand weapon is a world actor attached to bone 37 through the right holder (`player+0xeb4`). Its blade is
  its own actor update (`0x4979a8`): while `+0x1c` bit 3 is set it sweeps from the previous to the current transform,
  skips its owner (`+0x60`) and applies `prop(0x1a) * +0x194` via `0x498020`. So a second weapon attached to bone 19
  hits by itself once its bit 3 follows the right one.
* RAM (camera-melee fixture): dagger in the right holder, mode 0, bone 37, owner ref 4 (Rynn's model), multiplier
  1.0. magic5: club (damage 8) and dagger (4) both class `0x1000e`, stance (`0x1e`) 0.
* The skeleton attach (`0x103af0`) can allocate under the allocator semaphore, so it is tail-called, not run as a
  nested call (see `drakan_menus.cpp`, title Quit box, for the same reason).
* `PS2_JAL` unwinds when the callee leaves `pc` elsewhere, the dispatcher continues there; the generated function
  table only holds entries and resume points, `0x100000` / `0x100004` are empty (entry `0x100008`).

## Live runs (`scripts/harness/play_run.py --save build/harness/magic5/card/mc0`)

Env `DRAKAN_DUAL_WIELD=1 DRAKAN_DUAL_WIELD_LOG=1 DRAKAN_INVENTORY_SCREEN=native DRAKAN_PAUSE_MENU=native`; equips
through the native Tab screen with the script cursor on the cells (cell 1 at 77,126, cell 16 at 42,235 of the 640x512
script space; the mouse steers the native selection, so arrow keys alone do not).

| Run | Result |
| --- | --- |
| dualwield01 | Arrow keys selected the elixir (drunk); the dagger then went to the right hand natively (right hand not a one-handed weapon): the refusal path. |
| dualwield02 | Club -> slot 0 / bone 37, dagger -> slot 2 / bone 19, cells 1 and 16 flagged equipped, dagger owner 4. Two attacks: right and off-hand blade both `0xc` in the same dumps, both `0x4` otherwise. Screenshots show the dagger held in the left hand. No enemy: no hits. |
| dualwield03 | Using the dagger again: slot 2 empty, cell 16 unflagged, bone 19 detached through the trampoline, play continues. |

Open: a hit with the off-hand on an enemy, save / load with the off-hand equipped, bow draw, drop, two-handed
weapons (stance values other than 0 are refused until one is seen).

## Follow-up: clearer equipping, left-handed strikes

Request: make equipping the off-hand clear in the modern and native inventories, and give the off-hand real attack
animations.

* The hand rule moved to `gameui/DualWield.h`; both inventory screens name the hand (reference: "Inventory"). Note:
  another session's commit `808dfb7` captured an intermediate state of this work from the shared tree.
* The pose pipeline was traced by a sub-investigation (scratch scripts outside the repo) and re-checked here:
  65 orthonormal records at `sk+0x10c`; attachment and export matrices equal `local * node` / the node to 0.0000 in
  `dualwield02/ram_09_swing_c`; bind table `*(res+0xc8)` from `0x1165b0`.
* No left-handed clips exist, so the strike is the native clip mirrored; the synthetic test
  (`ps2recomp/ps2xRuntime/tests/pose_mirror_tests.cpp`) checks the palette identity, det +1, mirror twice = identity,
  blends and the strike alternation.

| Run (`build/harness/`) | Result |
| --- | --- |
| dualwield04 / 05 | Native panel: "Equips to right hand" (bow drawn by a mis-aimed click: correct refusal of the left hand), "Equips to left hand", "In left hand", "In right hand" (4:3 window: cell 1 at 104,128, cell 16 at 55,235 of the script space). |
| dualwield07 / 08 / 10 | Six clicks: combo 1 native, 2 mirrored (ramp 7 frames, weight 1 to the end), 1, 2. Grips at weight 1 match the reflected native grips within 6-11 units. Screenshots: dagger arm sweeping on her left, club arm resting forward-right; no skinning tears. Heavy RAM dumping stalls the world (clip time frozen), use screenshots. |
| dualwield12 / 13 | Modern screen: Equip Left Hand, Unequip Left Hand, R / L marks, "Equipped, Left hand", hands in the Equipped strip and on the Character card; the stale native line fixed in 13. |
| dualwield14 | Native: "In right hand" right after equipping (refresh on attach). |

## Harness

`scripts/harness/dual_wield_check.py` (reference: "Harness"). Runs on the magic5 card:

| Run | Result |
| --- | --- |
| dualwield-check (frame-counted recipe, earlier exe) | 6 / 7 PASS; blade FAIL as expected (that exe had no blade trace yet). |
| dualwield-check (rebuilt exe, frame-counted recipe) | Everything FAIL: the club equip never happened. The new input build delivers ~10x more samples per second (frame 8598 at 21 s), so frame-counted waits and one-sample taps were too short for the game. Recipes switched to `clock milliseconds` with 120 ms holds. |
| dualwield-check (ms recipe) | 7 / 7 PASS: strikes R1 L2 R1, mirror 63 frames worst 11 units, blade 6 both-live, no lag. |
| dualwield-check2 | mirror FAIL (198 units): a checker artifact. Grip lines are traced through the blend-out after a strike, when no pose line is, and were scored with the last pose line's full weight. The grip line now carries its own weight (`grips w=0.40 ...`); regression test added. |

## Enemy fixture and the limitations

Request: reach an enemy with the harness and fix the limitations (left-hand weapon hitting outside its strikes, the
unmirrored lunge, DLSS on the switch, the two-handed assumption).

* Fixture `build/harness/dw-hostile` = the magic5 card with Rynn moved to (675929, 159648, 523349) by
  `combat_save_fixture.py` (the hostile675 spot); three axe-wielding hostiles, Rynn dies after ~5.6 s of fighting.
* `dw-enemy1` (10 clicks): right and left blades each hit an enemy (`hit hand=right strike=right`, `hit hand=left
  strike=left`); Rynn died before the recipe's unequip step, so `--engage` now ends after the strikes.
* `dw-enemy2`: every strike came out left. Locked on, the game starts the attack, drops `player+0x94 & 0x80` for a
  frame and restarts the clip at t = 0; the tracker took that as a new strike. Fixed: a restart of the same clip and
  combo step within 0.1 s of clip time continues the strike (unit test). Its root check also scored blending frames,
  where the sideways part is scaled by 1 - 2w by design: now full weight only.
* `dw-enemy3` / `dw-enemy4`: strikes R1 L1 R1 and R1 L1 R2; blades by hand with no wrong-hand observation; root at full
  weight within 0.06 / 0.09 units (one-decimal trace). Enemy 3: the one left strike missed; enemy 4: left 1 / right 3
  hits. `--engage` now fights up to three times.
* Two-handed: the disc's weapon schema "Weapon Type = One Handed, Two Handed, Hammer/Axe" matches the native per-type
  action table at `0x4fc108`; the stance property is the weapon type, so the type-0-only rule stands on data now.
* DLSS: no fix needed; object motion follows vertices and the switch is a 0.12 s blend.

## Resume validation

The feature was already integrated when this continuation resumed. Preserved the pending split-pose helpers and
body-part tracing; fixed a shadowed test variable that prevented the MSVC warnings-as-errors test build. The split
helpers are tested but are not wired into the live pose hook. Corrected stale descriptions of simultaneous blades
to describe the implemented alternating strikes.

Built through `Build-LocalGame.ps1 -Incremental` in `build/final-game`, deployed by that script to `dist/DrakanPC`.
Executable SHA-256: `31EA994B8E3583E61A4EEB7518F15AC6A75EAF7C93E1FB4EA0959641E0A089BC`.

* `dw-resume-validation`: all eight checks pass, six alternating strikes, zero wrong-hand blade observations,
  full-weight grip error at most 12 units; unequip clears the slot and attachment.
* `dw-resume-enemy`: all eight checks pass on the first fight, two hits per hand, zero wrong-hand hits;
  full-weight grip error at most 11 units and root error at most 0.09 units.
* Eleven focused CTest tests (settings, gameui, pose mirror) and eleven Python harness tests pass.
* Full runtime CTest: 55 pass, 3 skip; the graphics `synthetic` test was manually stopped after it did not finish.
  This is not a full-suite pass. Repository audit and `git diff --check` pass.

Save/load with the off-hand equipped, bow transitions and dropping the weapon remain unverified in this run.

## Save/load, bow and drop transitions

Follow-up request: exercise those remaining transitions and fix failures. All runs use copies of the magic5 card;
the player's save folders were not modified. Runtime rebuilt/deployed through `Build-LocalGame.ps1 -Incremental`,
SHA-256 `8A9C9E37E3E5D4D9AC9D6482CF38C415DE7CCBAC87F0514447191B0EE557CFBE`.

Two runtime defects were reproduced and fixed:

* Bow return: native drawing preserves a melee item in slot 2 (only a class-0x10090 shield is unequipped). Returning
  to the club left slot 2 and its inventory flag set but bone 19 empty. Reconcile attachments on mask 2 / 4 as well
  as 8. The bow owns the bone while drawn; the off-hand returns automatically when the bow releases it.
* Primary drop: the club left inventory, but the dagger remained equipped in the left hand and could not attack
  without a primary weapon. When the primary and bow holder are both empty, the hook now tail-calls blade-off,
  native off-hand unequip and bone detach, preserving the original return value through the trampoline. The dagger
  stays in inventory and can be equipped normally. This also keeps native inventory flags consistent.

Evidence (`build/harness/`, each has `transition_report.json`):

| Run | Result |
| --- | --- |
| dw-trans-save | Saved over slot 1 through the native overwrite confirmation; `.rsg` content changed with both hands equipped. |
| dw-trans-load | Fresh executable loads that card: both slots, equipped flags, left attachment and owner reference restored; alternating strikes and blade windows pass. |
| dw-fixed2-reload | Unequip dagger, verify empty left slot/bone, then reload slot 1 through Pause > Load: dual wielding restored and subsequent alternating attacks pass. |
| dw-fixed-fire-bow | Bow owns bone 19, off-hand stays reserved and inactive. Three shots consume ammo 181 -> 178. Returning to the club restores the dagger; subsequent alternating attacks and blade windows pass. |
| dw-fixed-drop-right | Club removed; remaining dagger stowed, slot 2 and bone cleared; dagger can be equipped in the right hand and attack again. Dropped club's blade stays inactive. |

The initial left-drop test used Rynn's Dagger, which has native drop property 0xd = 0 and correctly refuses to drop.
The actual left-drop case instead equips the dagger right and club left: dropping the club clears its slot and bone,
preserves the dagger in the right hand and leaves the dropped club's blade inactive. `dw-trans-drop-left` established
this before the runtime fixes; `dw-fixed-drop-left` repeats it on the new executable.

Added `scripts/harness/dual_wield_transitions.py` with the six reproducible cases and isolated cards. Bow combat
scoring starts at the return-to-melee RAM marker: the initial checker incorrectly counted unmirrored bow shots as
failed dual-wield attacks. Re-evaluating `dw-fixed-fire-bow` with that correction passes every check; ammo, equipment
and blade checks were already passing. Thirteen Python dual-wield tests and five focused CTest tests pass; formatting
and diff checks pass. An unrelated shared-tree commit (`3a28708`) captured the runtime fixes during this work.

## Strafing attacks and disabling the setting

User reported broken left/right movement during attacks, and requested that switching the setting off unequip the
left weapon. Reproduced with the saved dual-wield card, holding A then D while attacking (`dw-strafe-before`).
The native pose assigns root/legs to locomotion clips 0x20672 / 0x20674 while spine/arms play attack clips 0x20686 /
0x20687. The runtime mirrored the whole body and root delta, and incorrectly tracked strikes from the root clip.

Changes:

* Track strikes from the channel assigned to the spine (node 2). Detect separate locomotion from the current
  root/spine clips. Use the existing split-pose math to mirror the upper body around the native pelvis, keeping
  root/pelvis/legs unchanged throughout the blend. Leave root motion unmirrored immediately when locomotion is
  separate, including on the frame strafing begins. Stationary attacks retain the full mirror.
* When disabled, stop requesting left-hand damage and reconcile the off-hand after Rynn's skin update on the EE
  thread. Reuse the tail-call blade-off / native unequip / detach sequence; only detach once native unequip succeeds.
  A drawn bow is preserved. The UI thread only changes the atomic setting. No inventory memory writes from UI.
* Added input-script command `<time> dual_wield on|off`, applying the same `setEnabled` function as Settings without
  saving the INI. The user requested harness-based setting tests rather than UI automation. Added parser timing and
  invalid-value tests, and checks that legs remain native at all upper-body blend weights.

Validation:

* `dw-strafe-fixed`: 737 moving mirrored poses, maximum lower-body matrix error 0; 737 root moves unchanged,
  alternating strikes and blade windows pass. Inspected attack screenshots for both directions.
* `dw-toggle-fixed`: all seven checks pass. Disable during attacks clears the left slot/bone/equipped flag and
  stops its blade; re-enable and equip succeeds. Disable with the bow drawn preserves the bow, clears the reserved
  off-hand, and returning to melee does not silently re-equip it.
* `dw-toggle-startoff`: loading a dual-wield save with the option disabled also stows the dagger, preserves the club,
  and clears the left attachment and inventory equipped flag.
* `dw-strafe-combat-fight2`: all eight stationary enemy checks pass, one hit from each hand, zero wrong-hand hits,
  full-mirror grip error <=11 units and root error <=0.09. First fight's only left strike missed; other checks passed.
* Seven focused CTest tests (input, settings, gameui dual wield, pose mirror) and thirteen Python dual-wield tests pass.

Reusable harnesses: `scripts/harness/dual_wield_movement.py` and `dual_wield_toggle.py`, both take a card already
saved while dual wielding. Runtime built/deployed with the standard incremental script. Final executable SHA-256:
`CE5836682DBDF7B32D1C77A88236831F26F6AF5C7BC6EFE09E9DF6E9FC0964D7`.


## Floating off-hand weapon when drawing the bow

The previous bow harness verified bone ownership and blade state but missed a visible stale weapon.
The saved `dw-fixed-fire-bow/bow_shots.png` reproduces it. Bow attach replaces the skeleton entry,
leaving the displaced melee model attached flag and visibility state behind. Clearing only the attached
flag, or even using native item hide to clear scene memberships, still leaves the equipped model visible.
The completed Rynn skin hook now clears the stale attached flag, calls native model visibility
`0x17e418(model,0)`, and uses native item hide `0x34d068`. On melee return, native skeleton attach
restores model visibility and the hook calls item show `0x34d1f8` to restore scene memberships.
The slot-2 assignment remains intact while the bow is drawn. Guest calls use the existing EE trampoline.

Validation: standard incremental build deployed to `dist/DrakanPC`; deployed executable SHA256
`9C13B4B9B96BE38AB310BDDBBDCFFA58B4B22D7FC9C6887061078857503337F3` matches the built executable.
The deployment script completed refresh but its final hash print hit a transient file lock; a subsequent
independent hash check succeeded for both files. `dw-bow-visible-fixed-bow` passes all 16 transition
checks. Visually inspected bow firing and melee-return screenshots: no floating weapon with the bow,
both weapons visible again in melee. Harness assertions now cover attached flag, model visibility,
scene modes, bow ammo, off-hand reservation, return attachment, and alternating blade windows.
The earlier flag-only passing runs are not evidence of a visual fix. Focused runtime tests: 7/7 pass;
Python dual-wield tests: 13/13 pass.

`dw-bow-toggle-regression` also passes all 9 toggle checks, including hidden off-hand before and after disabling with the bow drawn.
