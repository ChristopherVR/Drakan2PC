# Snowfall clumps: host rand range

Report: snow "doesn't flow right" at `world_rate = uncapped`.

## Weather object

`Environmental Effects (rain+snow)` (class 0x10061, property groups at 0x50cc00: General 0x509fd8, Particle
Specifics 0x50a068). Message handler `0x290238`; per-tick update `0x290a98` places a cube of half-size `Radius` ahead
of the camera, `0x291400` wraps particles that leave it and fades those under covered grid cells, `0x290758` tops the
count up to `Number of particles` (deficit / 100 per tick) at random positions in the cube. Particles live in a generic
particle system (vtable 0x51bda0, add `0x1951f0`, update + draw `0x1941a0`, per-particle step `0x193fb0`, sprite draw
`0x195e38`).

The simulation is frame-time correct: position += velocity * dt (`engine+0x24`), life -= dt, wind displacement is
`dir * strength * dt`. Snow velocity is constant `(V*H, V, V*H)` (`Vertical Speed`, `Horizontal Force`), flags
0x100004 (no wind bit 0x100). The only per-tick limits (`0x290758` spawn only when dt < 0.04, `0x291400` / `0x1951f0`
thinning when dt > 0.04) only act on slow frames.

## Cause

`rand@0x4CE028` is stubbed (`configs/drakan.toml`) and the stub returned `std::rand() & 0x7FFF`. The EE newlib rand
returns 0..0x7fffffff and the game scales every result by `4.656613e-10` (1 / 2^31, constants 0x4e9df8, 0x4eae00,
0x4dfdf0, 0x4e0ae8). Every random value was therefore ~0:

- `0x290758` spawns every flake at the cube's minimum corner (RAM dump, Level 4B outside the start cave: 244-256
  particles, groups of 2-3 at identical positions, all velocity (-125, -250, -125)); the flakes stream in a few
  clumps instead of filling the volume. Size deviation was always the minimum.
- `0x195e38` draws a flake smaller than 0.4 projected units with probability 2 * size; `rand < (1 - 2s) * 2^31` was
  always true, so small distant flakes were never drawn.
- Not snow-specific: 690 call sites, e.g. wind gust direction / strength in `0x141d80`, particle jitter, AI and sound
  choices.

## Fix

`drakan_overrides.cpp` `hookRand` replaces 0x4ce028 with the game's own LCG on the guest state
(`_impure_ptr` *0x518e94, `_rand_next` +0x58, `seed * 1103515245 + 12345`, result `& 0x7fffffff`), so the game's srand
(0x4ce018, recompiled) still seeds it. The generic `ps2_stubs::rand` / `srand` now use the same newlib LCG and range
(host state) for other games.

Not verified in game yet (needs `Build-LocalGame.ps1 -Incremental`). Check: `python scripts/harness/play_run.py --name
snow --level 4b --recipe <walk out of the start cave>`; a RAM dump should show particles spread over the cube, and the
snowfall should fill the view evenly at 30 and uncapped.
