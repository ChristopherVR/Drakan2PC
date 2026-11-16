# Issue #11: A / D strafe Arokh in every mode (2026-11-16)

Report: with `[keyboard] strafe = 1` and `[mouse] dragon_mode = stick`, A / D turned Arokh in flight instead of
sliding him sideways. The reporter asked for the `lx -> axis 3` remap of the `PadMgr+0x498` record on foot, in flight
and on the ground while mounted, falling back to the game's record only with the option off or in a menu.

State before the fix: `acd47820` had already added `[keyboard] dragon_strafe` (default 1), but
`InputModule::beforePadMgrUpdate` applied it only with `dragon_mode = mouse` and `look_mode = direct`. The reporter's
`stick` mode (kept on purpose, or written after the controls v3 upgrade) still got the PS2 mapping.

Change (`runtime/input/src/DrakanInput.cpp`): on the controlled dragon the remap now follows `dragon_strafe` alone.
`dragon` is the controlled-actor class (`controlledDragon`), so flight and mounted ground are the same case. The other
exclusions are unchanged: menus, `strafe = 0`, `dragon_strafe = 0`, spell gestures (on foot), a deflected physical left
stick, host ownership, unfocused window. The F1 row "A / D strafe on Arokh" is now enabled whenever "A / D strafe" is
on, not only with mouse steering.

Tests: `input_test` checks the remap on the mount with `dragon_mode = stick`, with `look_mode = stick`, across
dismount / remount, the menu fallback on the mount and `dragon_strafe = 0` restoring the game's record.

Not verified in real play: no run flew or walked Arokh with A / D held after this change. The flight model takes axis
3 as its lateral input (measured in `arokh-mkb-controls.md` with `dragon_mode = mouse`: D 30 ticks = +12,474 right,
heading 0); with `stick` the remap writes the same record, so the same response is expected, but mounted-ground
strafing was never exercised in either mode.
