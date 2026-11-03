# Null-controller mitigation: regression exposes a third unchecked pass

The interrupted session's patch guarded two PAL controller routines called from the scene update: step at `0x108208`
and pose at `0x11a848`. It skipped calls with a zero controller argument. That avoids a call through address zero in
the first routine and a low-memory write in the second, but does not create the missing controller.

## Reproduction (2026-11-03)

Built/deployed with the standard `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` path. Runs use
`dist/DrakanPC/drakan.exe`, isolated memory cards and native options through `scripts/harness/play_run.py`.

- Ordinary Surdana: `build/harness/recovery-nullctrl-surdana/`, clean scripted quit, screenshots at 120 and 300 input
  boundaries, 23 seconds. This was with the draft guard enabled by default, before changing it to opt-in.
- Converted axe repro: reused the private pack at `build/ootf-run/c7_axe/mods` and its recipe; campaign `ootf`,
  placement `(214120, 216453, 167663)`. `build/harness/recovery-nullctrl-axe/stdout_1.log` records both guards firing,
  then another missing-target call at `0x140eb0`. No clean scripted quit; the max-seconds watchdog stopped the run.
- The later pass reads the same scene list (`scene + 0x488`) and each object's controller at `+0x50`, then dispatches
  through that controller's vtable. It does not go through either guarded routine. Guarding those routines alone
  cannot fix the scene walk. No translated/generated game code or raw dumps are tracked here.
- Tick-area run at `(126944, 221400, 152576)` with the earlier private Tick pack likewise failed to reach a clean
  scripted quit (`build/harness/recovery-nullctrl-tick/`). Neither this area nor the weapon repro is closed out.

## Disposition

Keep the existing guards as an explicitly **opt-in diagnostic** (`DRAKAN_NULLCTRL_GUARD=1`, PAL only); unset or any
value not starting with `1` leaves ordinary gameplay unchanged. Logging is rate-limited. Do not describe this as a
weapon fix or mark the Tick/weapon lifecycle bug resolved.

Next: trace why those converted objects enter the per-frame controller list before their controller exists, and fix
construction/registration or remove invalid objects from the update lists at the correct lifecycle boundary. Verify
the later vtable pass, drawing, pickup/equip/use, repeated level changes and ordinary Drakan before defaulting a fix on.
