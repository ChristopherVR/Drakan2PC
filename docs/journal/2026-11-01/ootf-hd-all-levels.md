# HD textures for every converted OotF level (2026-11-01)

Tools: `analysis/scripts/ootf_hd_pack.py` (wrapper), `ootf-convert.ts --hd [--hd-out dir]`, `hd_pack.py` (modextract skips unchanged
zips via `zips.json`; upscale retries a failed Real-ESRGAN chunk tiled, skips it and exits 1 if textures stay undone; `--force`).
Run: campaign pack `build/ootf-run/hd-camp` (13 levels, 8778 files, 103 MB, converted in 82 s) and arena pack `hd-mp` (10 levels).

## Numbers

Index of both zips 18 s; 3129 unique textures (128 px 1367, 64 px 1002, 32 px 423, 256 px 284); 1382 not yet in the pack; upscale 162 s
(~8.5 textures/s) on an RTX 5080 while other games ran. `dist/DrakanPC/textures`: 3590 -> 4969 files, 1.86 GB. Second run: 1 s (all skipped).

## In-game check (2560x1440, PS2X_GS_SCALE=4, GSR_TEXUP=auto; `build/ootf-run/hdv/*.sh`, runs `hdv-<lvl>-before|after|dump`)

before = `GSR_TEXREPLACE=0` (live upscale only), after = the pack, dump = `GSR_TEXDUMP` into a scratch `GSR_TEXDIR`.
Wartok Canyons, Islands, Rift World all PASS. Laplacian variance (Islands s_start/s_walk2): 50.5/62.4 -> 61.5/77.7; Rift World
45.3/42.9 -> 59.2/60.1 (Wartok's before run came out at 1705x958 because the window was clamped, not comparable). Crops: rocks, ground,
the dragon's wing and body show clear extra detail (`build/ootf-run/hdv/shots/*_before_after.png`, uncommitted).

Hash match, dumped runtime textures vs the index of the packs (+ the disc index): Wartok 157 dumped: 105 in mod index (PROCTEES 91 =
the converter's enlarged ground textures, RESOURES 11 = model/creature, splat TXD 3), 21 disc, 31 unmatched; Islands 82 / 13 / 16; Rift
World 54 / 13 / 5. Every matched texture has an HD PNG. Enlarged / squared textures hash identically (modextract decodes the pack's
final texture exactly like gsr). The unmatched ones are 1x1 fills and 64..256 px noise (uninitialised VRAM sampled as texture,
random RGB and alpha), not real art; a few "RGB only" matches resolve through alias.txt (different palette alpha).
