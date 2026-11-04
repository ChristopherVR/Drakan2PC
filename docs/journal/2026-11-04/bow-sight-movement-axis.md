# Bow sight: movement input must not drive camera pitch

The native sight camera reads forward axis 2 as pitch. With direct keyboard/mouse input, holding W therefore
drives pitch toward its limit even though mouse input is small. A single vertical mouse count without W produces
the expected stable 0.002 radians; the combined W case previously reached -0.628 radians.

The completed correction suppresses only the native axis getter called from PAL 0x44e680 for axis 2, for the local
on-foot bow in default OotF sight mode while direct keyboard/mouse owns look. Physical stick input, other callers,
other axes, other weapons, scripted scenes and modern bow mode keep their native behavior. It does not change
the movement command or global axis output.

The isolated completed source subset passes 79 focused checks. Live `build/harness/bow-axis30` uses executable
SHA256 `812F55D36E6F14335CEFE59BC6C92C600FA704AB4638DED70AC3B361BD450B11`: W-held samples keep pitch at
0.002 radians rather than clamping downward, FOV stays 40, and a shot consumes one arrow (200 to 199).
The test executable also contains separate experimental sight-jump work; that work is excluded from this commit.
Sight-mode forward locomotion, stopping on key release and jump-camera behavior remain under investigation.
