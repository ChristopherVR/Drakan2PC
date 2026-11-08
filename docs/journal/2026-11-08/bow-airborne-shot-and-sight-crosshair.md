# Bow: airborne shots kill the forward jump; no sight crosshair

User report: a forward (running) jump turns into a straight-up jump once Rynn shoots mid-air; OotF keeps the jump.
The default OotF sight showed no aiming point. Draw and fire rate may differ from OotF (accepted).

## Cause of the lost forward travel

Static analysis of the PAL ELF (Ghidra decompiles in the scratch area, not tracked):

- `0x31da90` (attack entry) builds the refusal mask; the existing `bowAttackMask` hook removes jump bit 0x10 for the
  local bow, so the attack proceeds into `0x3016e8`.
- `0x3016e8` sets `player+0xaec = 0` at the start of the attack path. Its bow fire branch plays the archer clip with
  the player's motion setter (vtable +0x144 = `0x32cf30`) twice: first with channel `+0xaec` (return `0x301b7c`), then
  with the torso channel read from player property `0x52` (return `0x301c5c`).
- `0x32cf30` with channel 0 replaces the base motion and calls two model mode setters. A running jump (`0x2f9520`,
  type 2) plays its clip on that base channel, switches the model to velocity-driven travel and stores the jump
  velocity, so replacing the base clip ends the horizontal travel while gravity still carries the vertical arc.
- `0x31da90` judges attack completion from the channel in `+0xaec`; the sight branch of `0x3016e8` already stores the
  torso channel there.
- Original PC jump transition `0x1005a830` (2026-11-04 notes) keeps the bow on torso channel 2.

Fix: `InputModule::bowShotChannel`, hooked at `0x32cf30` for return `0x301b7c` only, moves the local player's
airborne (bit 0x10) bow shot from channel 0 to the torso channel and stores it in `+0xaec`. Grounded shots, other
weapons, other actors, scripted scenes and an unknown property reader stay native. The jump gate now also applies in
`modern` mode, whose mid-jump release tap was refused (the shot was lost).

## Crosshair

The native HUD `0x3ac540` draws a centred sight texture from bow property `0x11` only when it is loaded; the port
showed nothing. `bowCrosshairVisible()` now also covers the open native sight (camera mode 6, `cam+0x48` bit 0x20,
bow out, gameplay, no menu). Third person keeps no reticle (OotF auto-aim).

## Verification

`bow_aim_test` 202 checks, 0 failures (new: torso channel, exclusions, sight crosshair, modern jump gate);
`input_test` 3747 / 0. Incremental game build deployed to `dist/DrakanPC`.

## Live check

A fresh Surdana game only carries Rynn's Dagger (`site_bow.txt` no longer equips a bow). Surdana's own Short Wood
Bow lies at (425126, 160500, 441806) (the `ranged-test` arena map uses it): the recipe `place`s Rynn on it, picks it
up with E and equips it in the native inventory, then `place`s her back on the open start path and runs, jumps and
clicks LMB in the air. `play_run.py`, `DRAKAN_BOW_LOG=1`.

`build/harness/bow-jump-live1`, attempt 1: running jump (type 2) from (445984, 171278, 344143). The log records
`jumping attack gate: mask=104b70->104b60`, `airborne shot on torso channel 2` and the native launch entry. From the
`[in-anim]` rows: the shot starts 0.45 s into the jump (`f94` 10 -> 90, `aec` 0 -> 2, torso clip 20896) while the
base channel keeps the jump clip 208e5. Horizontal travel continues at about 3000 units/s: 1200, 1784, 2149, 2711
units at 0.45, 0.64, 0.77, 0.96 s, landing about 1.0 s after takeoff with a 657-unit rise, i.e. a full running jump
(`jump_check.py` treats ~2400 units as a moving jump). After landing the base returns to run 208ff. Opening the sight
logs `sight crosshair shown`.

Limits: the HUD crosshair cannot appear in renderer screenshots (the overlay is not part of them), so it is checked
through the state log only. The run's control jump without a shot did not take off, and later runs were spoiled by
the teleport (`bow-jump-live2/3`: Rynn held at the `place` target in the air, then a fall and level reload; one also
shows the same torso/base split at the shot). Other attempts ended with exit -1 before or after the bow sequence,
including a run without any bow (`bow-pickup-dbg`), so they are not attributed to this change. Crossbows (holder
mode 4) are not covered by any bow adapter; their native attack mask already allows jumping.

## Follow-up: A/B jump, crosshair picture, crossbows, bow combat choice

**Teleport deaths.** `place` holds the player on the target for 240 ticks; after a long teleport (the bow at
425126 to the start path at 447781) the terrain there is not resident, she ends below it and falls, so those runs
died and reloaded. Short teleports inside the bow's resident area work. The A/B runs therefore use the archery range
north of the bow (start 424900, 160800, 444500, yaw pi, facing the target at z 450304) with
`DRAKAN_PLAYTEST_HOLD=60`, D3D11 and ray effects off (a scratch copy of the player's `drakan.ini`).

`build/harness/bow-ab3` (clean scripted quit), measured from takeoff to landing in the `[in-pos]` / `[in-anim]` rows:

| Running jump (type 2) | Airborne | Horizontal | Speed | Rise |
| --- | --- | --- | --- | --- |
| no shot, from (424900, 444500) | 0.76 s | 2169 | 2843 / s | 575 |
| shot 0.36 s after takeoff, from (424932, 446355) | 0.94 s | 2702 | 2865 / s | 576 |

During the shot `f94` is 90, the torso channel (`aec` 2) plays 20896 and the base keeps the jump clip 208e5. The
horizontal speed and the rise match the jump without a shot; the airborne times differ with the slope of the ground.

**Crosshair picture.** `DRAKAN_OVERLAY_SHOT_DIR` (the settings host's harness inbox: `shot.txt` saves the presented
frame including the HUD overlay) captured the open sight in `bow-ab2` and `bow-ab3`: the gold host crosshair sits at
the centre of the game image over the archery target, ammo 199 after the one jump shot.

**Crossbows.** The decoded placed-object lists of 11 campaign levels contain only bows (Wood, Battle, Rune, Speed,
Bow of the Order, Stomp Bow); no crossbow item is placed for the player. The native Villiager Cross Bow is an NPC
weapon. The user's "crossbow" is the player's bow. Eight cut-scene / test LVLs did not decode.

**Bow combat choice.** User request: OotF behaviour by default, with a Gameplay option back to the PS2 game's own
bow ("zoomed in is the normal PS2 approach"). `[mouse] bow_aim` now takes `ootf` (default) | `native` (alias `ps2`) |
`modern`, shown as Options > Gameplay > Combat > "Bow combat" (it replaces the Controls toggle "Modern bow aiming").
`native` keeps every OotF adapter out: jump gate, torso-channel shot, sight jump / locomotion / orientation /
forward-look / shot basis, third-person auto-aim and the host crosshair. Tests: `bow_aim_test` 212, `input_test`
3747, `settings_test` 452, `settings_schema_test` 1110, all passing. The game build compiled, but its deployment to
`dist/DrakanPC` stopped because the player had the game open from there (no file was removed); the native mode still
needs a live run after the next deployment.

## Deployment with the game open, and the live bow combat check

`Build-LocalGame.ps1` cleaned `dist/DrakanPC` with `Remove-Item`, which fails on the exe and DLLs of a game running
from there. Windows allows renaming those files, so the cleanup now renames an in-use file to `<name>.in-use-<n>`
(the running game keeps its copy, the new build goes beside it) and a later build deletes the leftovers once nothing
uses them. Directories still fail loudly. Deployed with two games open from the play folder; no crash dump appeared.

Live, new build, archery-range recipe (`DRAKAN_PLAYTEST_HOLD=60`):

* `build/harness/bow-native1`, `bow_aim = native` (scratch input ini, confirmed by the `input active (config ...)`
  line): bow equipped (archer clips 208xx), no jump-gate, torso, launch or crosshair logs; during the running jump
  (208e5, `f94` 10) the attack bit never appears, the PS2 game's own refusal. Opening the sight sets `aec` 2 natively.
* `build/harness/bow-ootf-final`, the player's own `drakan_input.ini` (default ootf): jump gate, `airborne shot on
  torso channel 2`, launch entry, crosshair shown, overlay picture captured.
