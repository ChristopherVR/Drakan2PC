# Save-free test runs and HD palette textures

Prompted by a user screenshot of Surdana with rainbow-banded clouds and dark, opaque smoke. Reproducing it needed a
save, and the only one was `iso/mc0` (the game uses `<elf dir>/mc0`; `dist/DrakanPC/mc0` is empty, so a plain run
from dist stopped at "Loading options data").

## Harness without saves

* `DRAKAN_MEMCARD_DIR=<empty dir>`: Load Game is greyed out, so the title's Cross is New Game. Boot -> intro ->
  Level 1A took about 25 s with `DRAKAN_AUTOPRESS=3000 DRAKAN_INPUT_AUTOSTART=1`.
* New Game opens `cdrom0:/English/Levels/Level 0 - Intro/Level 0 - Intro.lvl`, then the intro's transition opens
  `.../Level 1A - Surdana/Level 1A - Surdana.lvl` (HLE file log, `DRAKAN_HLE_LOG=1000000`). `First Level Filename` /
  `Intro Level Filename` are string properties (descriptor table at `0x505768` / `0x505778`, type 0xa); the
  transition code is `0x362d38` (8 KB, prints `NEW LEVEL %s` at `0x363a34`, builds `%s/%s.rsg`). Rewriting the level
  name at file-open level (`DRAKAN_START_LEVEL`) was enough: the engine loads Kragmor / Shadowmire / Crystal Mine /
  Ravenshold under the Surdana name.
* Autopress kept opening the pause menu (screen 23) every 3 s in gameplay. In Ravenshold, at ~60 FPS, that reset the
  80-frame control count every time, and Rynn starts on Arokh, so the look hook ran only 12 times. Fixed: no taps in
  free gameplay with AUTOSTART, plus a 240-frame fallback.
* Script `shot` now also writes a renderer PNG (`gsr::requestWindowShot`): the desktop BMP of a hidden window is
  invalid (already noted in the 2026-10-17 combat journal).
* `scripts/harness/play_run.py` wraps all of it. Reference: `docs/reference/input.md` 13.
* A runtime teleport was tried and dropped (position copies in 10+ places; details in input.md 13).

## Palette textures

`docs/reference/hd-textures.md` 9. Volatile textures were drawn at native resolution (the earlier note saying "live
xBRZ" did not match the code). Indexed volatile slots whose index plane did not change now get HD index maps. In
three levels these turned out to be palette-swapped skins and terrain splats rather than cycling water.

## The screenshot itself

Not reproduced yet: the user's spot in Surdana is unknown, and none of the start positions show the banded sky. With
the harness, the next step is a run with `--recipe` walking to that spot (or a save fixture via `--save`), then A/B runs
with `--env GSR_PALETTE_HD=0`, `--env GSR_TEXREPLACE=0`, `--env DRAKAN_DLSS=off` to find the stage that adds the bands.
