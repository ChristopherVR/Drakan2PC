# Site redesign: title-screen look, HD slider, input clips (2026-10-29)

## What changed

- Home page rebuilt after the game's title screen: gameplay backdrop (HD Surdana with the quest text cropped off) under a dusk gradient, gilt `.ornate` frame, cream menu entries with an ember-orange selected row. Shorter copy throughout; the old "Make it your own" / welcome panels are gone.
- The HD texture comparison is now a before/after slider with four scenes (Surdana, palace gate, Catacombs, City of the Damned). The old `hd-textures.jpg` was a pre-stitched pair of *different* frames.
- New clips: `mouse-look.mp4` (turn, look up, turn back, look down) and `bow-aim.mp4` (draw, aim with the crosshair while panning, release, twice; the log shows two `[bow] aimed shot` lines). Both have an input panel drawn on top from the recipe's own mouse deltas and LMB state.
- Guides trimmed (enhancements rewritten; controls, extras, settings, browser, troubleshooting shortened). Fixed mojibake in `controls.md` (`â€“`, `â†’`).

## Capture findings

- `DRAKAN_HD_TEXTURES` does not exist; HD on/off is `GSR_TEXREPLACE` (+ `GSR_TEXUP=1` to also stop live upscaling). The first attempt with the wrong variable gave near-identical frames.
- Same recipe, two runs: camera-only scenes match exactly. Walking (`key down W` for 180 samples) diverged in position, and flight levels (Kragmor, Ravenshold, Andrellian Isles start on Arokh) differ in wing phase, so they were left out.
- `shot` every 2 samples in a 4K borderless window dropped about a quarter of them. A 1280×720 window via `DRAKAN_INI` kept every shot, even one per sample.
- The script runner does not sort events; out-of-order lines stopped it early (only the first 21 shots ran).
- The old bow recipe (`build/harness/bow6_modern`) works only with `inventory_screen = native`. With the current default (modern) its Tab/Enter steps never equipped the bow (no `[bow] aimed shot` in the log). Its card (`bow6_modern/card/mc0`) starts beside the bow table.
- At `world_rate = 30`, one input sample is 1/30 s (mouse-look frames were ~0.067 s apart at every second sample).

Recipes: `scripts/harness/recipes/site_*.txt`. The conversion script stays under ignored `build/site-media/prepare2.py`, like the first batch. Hosting and usage: `docs/reference/github-pages.md`.

## Third batch: frame rate, ray tracing, DLAA, inventory tours

- Live window recording (`scripts/harness/record_window.py`, FFmpeg gdigrab, 60 FPS) instead of per-frame `shot`s, for clips where timing matters. The game keeps running unfocused with `DRAKAN_PAUSE_UNFOCUSED=0`; `DRAKAN_WINDOW_POS` keeps the window on screen.
- 30 FPS vs uncapped: same `clock milliseconds` recipe, two runs. The counter read 29.9–31 vs 85–130 FPS (while recording, 1280×720).
- Ray tracing: pond and frozen-river fixtures, first frame only (see the reference for why later frames diverge).
- DLAA vs off is near-invisible at internal scale 3 (already supersampled); at scale 1 the antialiasing is obvious.
- Tab inventory tour from the `aui-after` card: cursor glides over the bow, club, potion and dagger (details panel follows), Enter equips the dagger, E to Character and Spells; then the I strip (RIGHT ×3) and the M map (DOWN ×3 highlights objectives and places).
