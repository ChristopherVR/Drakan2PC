# Running jump went straight up at uncapped world rates

Report: with mouse + keyboard, running forward (W) and pressing Space sometimes jumped straight up instead of
forward. Not an input bug: a gamepad hits the same thing. It is a 30 FPS assumption in the game's velocity estimate.

## How the game picks the jump

`0x4011` (Triangle / Space) sets the "up" bit at `player+0x50c`; `Controller_Update_C` (0x311220) then calls
`FUN_002f90c8` (jump start):

- `FUN_00325f30` measures the current velocity and stores the forward component (player local frame) in `player+8`.
- If a strafe bit is set in `player+0x508` (`FUN_00457798`), it is a side jump (`player+0xc4` = 0x11 / 0x12).
- Otherwise, unless back is held, `FUN_002f9520(type)` with **type 2 (running jump) if `player+8 > 1000`, else
  type 1 (standing jump, straight up)**. The type is kept in `player+0xaf0`.

`FUN_00325d30` (the velocity used by 325f30, also by 2fa328 and 321258):

```
rate = min(30, 1 / (engine+0x2c * timeScale 0x512db4))   // engine = *0x5565d4; 30 fixed while paused (142688)
v    = (pos - prevPos) * rate   (or the stored motion vector * rate, whichever is larger)
```

The PS2 world never ran faster than 30 Hz, so the cap was harmless. With `world_rate = uncapped` the tick is
much shorter (a RAM dump while running read `engine+0x2c = 0.009`, about 111 Hz; harness runs reach ~250 Hz), the
per-tick displacement shrinks with it, but the multiplier stays 30. The measured speed is real speed x 30 / rate:
a full run (~1600-2500 u/s) reads as roughly 300-700, below 1000, so most jumps were standing jumps. At lower rates
it hovered around the threshold, hence "sometimes".

## Fix

`drakan_frame_rate.h` wraps 0x325d30 when the world rate is 60 or uncapped: after the native call the result vector
is scaled by `rate / 30` (rate from the same `engine+0x2c` and time scale, capped at 1000 Hz, skipped while
`engine+0x4c4 & 2` says paused or when the rate is <= 30). Everything the game derives from it now sees the speed
it would have seen at 30 Hz, including the in-air carry-over in 2fa328.

## Verification (harness, `build/jumptest/*.txt`, `[in-pos] ... spd= mv= jt=` log fields added for this)

| case | before | after |
| --- | --- | --- |
| W for 2.5 s, Space | jt 2 | jt 2 |
| Space 300 ms after W (real speed ~1650 u/s) | **jt 1 (straight up)** | jt 2 |
| W + D diagonal, Space | jt 2 | jt 2 |
| W + mouse turn, Space | jt 2 | jt 2 |
| Rynn blocked by a wall, W held, Space | jt 1 | jt 1 (correct: not moving) |
| Shift walk in the open (real ~1290 u/s) | - | jt 2 (above the game's own 1000 threshold) |
