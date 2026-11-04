# OOTF coherent class-reference candidate: normal opening and controls

Candidate: `build/scratch/ootf-classref/mods/ootf-levels.zip`, SHA256
`B9C39467CA0EECA3680F66857FEFB3B7F18877D23B9523DD27E55621E9856E88`.
Runtime: implementation-next executable SHA256
`812F55D36E6F14335CEFE59BC6C92C600FA704AB4638DED70AC3B361BD450B11`.
Run: `build/scratch/ootf-classref/normal-control`, private memory card, world rate 60,
input log 120. Normal title selection, OOTF New Game, natural Intro exit and all
three original RV scenes were retained. No teleport, forced exit, HP adjustment
or cinematic skip was used.

## Comparison qualification

The source baseline was isolated at formats `ad867c7`, but that source already
contained cinematic speech conversion absent from the older 137,794,044-byte
baseline pack. Packed RV 1.1 has 334 actions versus 224 in the old pack: exactly
110 added type 3 sound actions; transform, AI, message and other action counts
match. No animation actions were added. This is a coherent forward candidate,
**not** a one-variable pack comparison of class references. It must not be used
to attribute all heap differences solely to the class-reference repair.

## Measured control and ordinary death

`DRAKAN_INPUT_LOG=120` enables periodic profile reports and profile transitions,
not just GUI-change lines. At 293.17s the host entered gameplay with the current
player controller and increasing native-look callback count. Earlier telemetry
used a stale Intro player cache during the cinematic. In the actual RAM snapshots,
`ctx+0x4b0` consistently points to valid handle `0x9f4230`; garbage at the old
Intro pointer is not demonstrated corruption of the current player.

Held W/D/A snapshots show live movement. The D interval moves from approximately
(218709,19248,170022) to (219869,19045,164105); A returns near
(218858,19248,170170). Subsequent mouse input changes body yaw from about 1.5084
to 1.2050. This confirms normal gameplay input; the earlier suspicion that W was
necessarily mapped to menu DPAD_UP is withdrawn. Camera data yaw at+0x90 remains 0
in this camera mode and is not the correct standalone measure of body rotation.

Player HP is 100/100 through the W/D/A snapshots, 90/100 after mouse movement,
and 0/100 in the late snapshot, using the existing HP-key decoder. An adjacent
Wartok is visible in `after_mouse.png`. At 353.27s the game leaves gameplay and
plays animation 20577; the world unloads and returns to the title at 368.44s.
This supports ordinary enemy death while idling, **not** a demonstrated native
heap failure in this run. No watchdog stall capture was emitted.

F5 was scheduled after the idle period, so it ran at the title rather than saving
the live campaign. `quicksave.png` is not a successful quicksave. The next run
must save soon after confirmed control, then use normal movement/combat to avoid
idle death. No god mode or save editing is needed.

## Remaining acceptance

Native dust-devil actors still appear in the current candidate, and heap usage
still grows. Passing the old failure time before ordinary death is not a proof of
long-term heap stability. A bounded factory probe will identify the native dust
producer. Source reference omissions and the revised referenced-kind candidate
remain separately audited. No claim of complete campaign progression, effect
fidelity, or full-game acceptance follows from this opening/control checkpoint.
