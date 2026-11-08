# Focus loss swung a lowered camera pitch back up

Report: look down with the mouse, press Alt (or Alt+Tab / click another window), and the camera tilts up to level.

## Cause

`Camera_UpdateLook` re-centres `cam+0x84` towards the stick position (0 when centred) through `Camera_SoftLimitAxis`
whenever the stick-mode word `*(PadMgr + 0x4d4)` (`addr_stick_mode_flag = 0x4e9594`) is non-zero. The input module
clears that word around the call while the mouse looks (`InputModule::beforeCameraUpdateLook`, restored in
`afterCameraUpdateLook`). The early return at the top of `beforeCameraUpdateLook` also fired for `!frame_.captured`, so
as soon as focus was lost (`frame_.focused == false` sets `userReleased_` and drops the capture in `beginFrame`) the flag
stayed set, the pad was neutral, and the native code eased the pitch back to level in about 20 frames while the window
sat in the background. The Esc cursor release had the same effect.

Alt itself is harmless here: `drakan_window.cpp` swallows `SC_KEYMENU`, and Alt is not bound. Losing foreground (Alt+Tab,
or Alt opening another app's handling) is what drops the capture.

## Fix

`runtime/input/src/DrakanInput.cpp`: the uncaptured branch still clears the stick-mode word (same `holdPitch` helper as
the captured path) when the mouse look would otherwise apply (direct mode, game layer, no host ownership, no right
stick), the mouse / keyboard last owned the view, `pitch_persist` is on and it is not Arokh with `dragon_stick`. No mouse
delta is applied while uncaptured. A controller user keeps the native re-centring.

## Verified

- `input_test`: new checks in `testControlsV2` (unfocused frame keeps the flag cleared for the call and restored after,
  pitch unchanged; a pad-owned unfocused frame leaves the flag alone). `ctest --preset runtime-tests -R input` passes.
- Not yet checked live in `dist/DrakanPC`.
