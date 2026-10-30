# USA settings, pause and direct mouse integration

This continues the disc-profile and settings/input work from earlier on 30 October. Testing uses the existing
`play_run` harness, scripted input, RAM dumps and the opt-in overlay screenshot inbox. No computer automation,
desktop input injection, user save changes or user INI changes were used.

## Shared camera implementation

`DirectMouseLook` is a separately validated capability. PAL and USA install the same `drakanInstallDirectMouse`
look/follow hooks and run the same `InputModule` algorithms. `CameraLayout` carries the audited object fields;
`CameraProfile` carries the translated globals, native functions, up vector and PadMgr fields. Installing a profile
pins those addresses through initial configuration and every settings reload, including old INIs with PAL addresses.
Unported bow aiming and keyboard strafing stay disabled on the basic integration.

| Canonical PAL | USA | Role |
| --- | --- | --- |
| 0x44e4a8 | 0x44dc88 | native camera look callback |
| 0x41f848 | 0x41f588 | native camera follow tick |
| 0x13ee90 | 0x13ee30 | heading rotation, float argument in FPR12 |
| 0x4e8c48 | 0x4e9ac8 | native turn up vector |
| 0x4e9594 | 0x4ea414 | PadMgr position/rate mode, base + 0x4d4 |
| 0x4e9558 | 0x4ea3d8 | PadMgr stick map, base + 0x498 |
| 0x32cda8 | 0x32cc98 | audited Rynn pitch-veto leaf |
| 0x276e40 | 0x276d78 | audited dragon pitch-veto leaf |

The turn and follow bodies retain their relevant layouts. The Rynn leaf's later object fields move by four bytes;
calling the release's native leaf preserves that difference without duplicating its implementation. The look
callback has native control-flow differences, so its original body still runs between the shared before/after hooks.
Both profiles explicitly describe the context/handle chain, camera fields, player transform/flags/vtable,
vcall slots and resource-class lookup. The basic integration identifies mounted control at the PadMgr boundary,
even if a camera callback is skipped, and retains mouse-through-right-stick flight behavior.

The shared camera code no longer treats PAL 0x4fb730 (Auto Mount Dragon) as a gameplay-enable flag. Scripted-control
state and camera state gate the migrated algorithms. A new profile regression also found that raw motion could
still be applied after the before-hook deferred to an active native right stick: the after-hook now discards those
counts too, including RSTICK bindings. The existing native-look tests now check that ownership rule.

## Harness coverage

`scripts/harness/settings_check.py` uses the existing `play_run.run_once` runner, isolated INIs/card and no-activate
window. BasicInput now loads the existing input-script grammar and processes shot, RAM dump and quit events at its
native frame boundary. The render-thread inbox invokes the same overlay hotkey allow gate/pause interceptor and
Menu Apply path used by normal input; it can close the overlay even while the EE is held. The shared runner now
requires both the script quit marker and a zero process exit status.

Commands (change `usa` to `pal` for regression runs):

```powershell
python scripts/harness/settings_check.py --region usa --mode title
python scripts/harness/settings_check.py --region usa --mode gameplay --resume 0
python scripts/harness/settings_check.py --region usa --mode gameplay --resume 1
python scripts/harness/settings_check.py --region usa --mode camera --resume 1
```

USA title checks passed: startup F1 is blocked; title F1 opens; sensitivity Apply persists to the isolated input INI;
closing returns to the same screen; native Options > PC Settings opens the overlay. PAL title/Options checks also
passed. USA and PAL gameplay checks cover waiting for native pause before opening, continued menu/input ticks,
neutral camera/player state despite scripted keyboard/mouse motion, both close policies, and retaining an existing
pause when settings did not initiate it.

Both discs' camera runs measured the same results from guest RAM:

- 64 horizontal counts at sensitivity 0.002: -0.128 radians of player heading.
- -36 vertical counts at sensitivity 0.002 with gain 1.8: +0.04 camera pitch.
- Neutral frames: no yaw drift and no pitch recentering.
- Apply horizontal sensitivity 0.004 in PC Settings, close/resume, then repeat 64 counts: -0.256 radians.

Reports, logs, RAM and screenshots stay under `build/harness/<region>-settings-*`. Both game builds use the standard
incremental builder and deploy to their existing play folders. Focus-loss and forced-hold fallback were not live
tested in this increment. Combat targets, alternate native camera modes, mounted flight and later levels still
need broader USA gameplay coverage; the unit regressions cover predicate/ownership behavior and deliberately
relocated layouts. Modern bow aim, strafing, commands/quick slots, modern menus, widescreen and the other remaining
capabilities are not implicitly enabled by direct mouse support.

## Automated validation

154 Python tests and 9 focused runtime tests passed (input, bow regression, both disc profiles, settings core/schema/
menu, and pause/settings UI). Profile tests exercise the real input module with both releases' metadata and shifted
object layouts, configuration reloads, raw pitch/yaw, controller/overlay ownership and controlled-actor lookup.
The generator rejects missing camera addresses/layout fields and conflicting camera/input-flow layouts. Ruff,
changed-file formatting, diff whitespace checks and the repository source guard were also run.
