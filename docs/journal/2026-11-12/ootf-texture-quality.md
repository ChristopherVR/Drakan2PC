# OotF textures next to the PS2 ones: what the gap is and what was changed (2026-11-12)

Request: "Can we update PC textures to the same quality as the PS2 ones where there is no equivalent." Read as: in the
converted Order of the Flame campaign, textures that keep the 1999 PC art (no Drakan 2 counterpart) look worse than the
PS2 textures and models shown beside them (Rynn, Arokh, the orcs and Wartoks, the substituted bark and fronds).

Everything below was measured on this machine. Probes, packs, runs and images are private under
`build/review/textures/` (not committed): `probe/*.ts` (texel density), `census.py`, `texlog.py`, `exp/` (offline
upscale comparison), `runs/` (game runs), `evidence/` (screenshots and crops). The OotF source is the owner's English
disc image unpacked by the build kit (`build/review/issue5/ootf/cab_disc`).

## 1. How PC textures enter the pack today

| Kind | Converter (formats `ootftex.ts`, `ootfterrain.ts`, `ootfassets.ts`) | Guest result |
| --- | --- | --- |
| Terrain (Proc Textures) | opaque tiles enlarged to at least 128 px (`minSize`, wrap-around Catmull-Rom), `indexed` mode | 8 bpp, refined median-cut palette |
| Objects, walls, props (RESOURES) | `truecolor`: exact 8 bpp when at most 256 colours, 8 bpp refined palette for 128 x 128 and larger when it keeps 38 dB, else 32 bpp | 90 % 8 bpp, 10 % 32 bpp |
| Skinned creatures, fast-draw weapons, display-list props | as above, plus made square (nearest-neighbour row or column doubling) and `hardAlpha` for skins | 8 bpp square |
| Far terrain (level `..TS.TXD`) | the level's own baked far textures | 8 bpp, 256 px |
| Drakan 2 substitutes (bark, fronds, waterfalls) | the disc record copied byte for byte | the disc texture and its HD image |

No texture is shrunk: the PC maximum is 256 px and every source size is kept or enlarged. The 8 MiB campaign texture
budget and the guest mip build are why large textures go 8 bpp. `--hd` writes the lossless pixels of every quantised
texture next to the pack so the HD step can upscale those instead of the 8 bpp picture.

## 2. The measured gap

**Terrain texel density** (`probe/terrain_density.ts`, `probe/pc_density.ts`; water layers excluded; one texture spans one
2048-unit cell on both platforms, so texels per world unit = edge / 2048):

| Level | Texture edge per cell (mean) | Texels per unit | Mix |
| --- | --- | --- | --- |
| Disc Surdana (LEVEL1NA) | 113 px | 0.055 | 128 px 73 %, 64 px 26 % |
| Disc Kragmor / Shadowmire / Andrellian (LEVEL2RE, 3NE) | 128 to 129 px | 0.063 | 128 px 99 % |
| Disc Ravenshold / Tundra (LEVEL4RA, 5NE) | 89 to 93 px | 0.044 | 64 px 55 to 61 % |
| Disc Valley (LEVEL6EN) | 131 px | 0.064 | 128 px 96 % |
| OotF PC source, every campaign level | 64 px (Intro 58) | 0.031 | 64 px 97 to 100 %, 8 or 16 bpp |
| Converted guest, every campaign level | 128 px (Volcanic 125) | 0.0625 | 128 px 8 bpp |

**Model texel density** (`probe/model_density.ts`, LOD 0, sqrt(texel area / surface area) in model units, so only the
ratios mean something; categories mix weapons, items and walls):

| Set | Models | Median | Ratio to the disc |
| --- | --- | --- | --- |
| Disc skinned (NPCS, SYSTEM, RESOURES) | 145 | 1095 | 1 |
| Converted skinned (PC creatures and people kept as PC models) | 47 | 850 | 0.78 |
| Disc villagers / guards | 20 | 1216 | 1 |
| Converted villagers, sailors, succubi | 6 | 993 | 0.82 |
| Disc rigid (World Common, level objects, RESOURES) | 2556 | 383 | 1 |
| Converted rigid (props, walls, items) | 1622 | 206 | 0.54 |

**Texture sizes and bit depth** (`census.py`, unique textures by hash): disc characters median 128 px with 48 % at 256;
disc props and walls median 64 px; PC-origin object textures in the pack (1337) median 64 px (64 px 37 %, 128 px 25 %,
32 px 24 %, 256 px 13 %), 90 % 8 bpp. The disc is itself 86 to 92 % 8 bpp, so palette use is no difference.

**What the converter loses in the guest:** quantisation against the lossless picture, colour PSNR: object textures (492)
minimum 34.2, 10th percentile 40.8, median 47.4 dB; terrain (807) minimum 43.8, 10th percentile 45.4, median 47.4 dB.
Small, and invisible once the HD image replaces the texture (below).

So the gap is in the source data, not made by the converter: the PC art has about half the linear texel density of the
disc on terrain (64 against mostly 128 px per cell) and on props (0.54), and about 0.8 on characters. The converter
already brings terrain up to the disc's guest density, but by a smooth enlargement that adds no detail.

## 3. The HD path for converted textures

`hd_pack.py modextract` decodes every texture of the pack zip like the renderer and hashes it; `upscale` runs
Real-ESRGAN `realesrgan-x4plus` (ncnn Vulkan, `tools/hdpack/realesrgan`) x4 with wrap padding on tiling axes; the
runtime replaces by content hash (`GSR_TEXUP_LOG=1` logs `replace` or the live `xbrz` / `smooth` upscale per texture).

**Coverage in the owner's play folder** (`build/review/baseline-play`, installed `ootf-levels.zip` of 2026-11-09): all
3645 unique pack textures have an image in `textures/` (FX 163, LEVEL1ON 131, MOUNTAON 30, PROCTEES 911, RESOURES 1840,
WORLDCON 326, level TS 244). In game, with the owner's `drakan.ini` and `GSR_TEXUP_LOG=1`, every converted texture that
was drawn was replaced from the pack: Ruined Village 281, Grotto 61, Islands 117 (none fell through to the live
upscaler; the unindexed live ones are 64 x 64 runtime targets and atlases). So converted textures are **not** "without an
HD equivalent" today: they get Real-ESRGAN x4 images made the same way as the disc's.

**But the images were made from the wrong picture.** `exact_done.txt` in that folder lists 815 hashes, of which 5 are in
the current pack: the pack's 1080 quantised textures have images made from the 8 bpp guest picture (their lossless
sources were made for an older pack whose hashes changed). And for the resized textures even the lossless source was the
resized copy:

* 656 terrain tiles: the network saw the 128 px Catmull-Rom enlargement of the 64 px PC tile;
* 50 object textures the converter made square (256 x 128 -> 256 x 256 and others): the network saw doubled rows, and
  the HD image keeps the stair steps (`exp/sheet_squared.png`, columns A and B: Succubus and dragon wing veins).

## 4. What was changed

* **formats `ootftex.ts`:** `Ps2Texture.hdSource`, what the HD sink receives: the source pixels before any resize when
  the written size differs from the source (enlarged terrain, squared textures, sizes rounded up), else the final-size
  picture before quantising. It is also emitted for a resized texture that was written exactly (31 such textures had no
  source before). `upgradeIndexedSquareTexture` gives the record's own non-square picture. The guest records do not
  change: the packs converted before and after the change are byte-identical in all 11,224 files (`pack-a`, `pack-b`), so
  the guest heap and the 8 MiB texture budget cannot move. Tests in `test/ootftex.test.ts`.
* **`analysis/scripts/hd_pack.py`:** an index entry keeps the source's own size (`sw`, `sh`, `exactv = 2`); the pack image
  is always the guest size times `--scale`. An opaque source at most half the guest size (the ground tiles) is enlarged
  to the guest size with a wrap-around Lanczos and upscaled once (`--small-source enlarge`, default; `esrgan` and `lanczos`
  are the alternatives measured below). Other size changes (squared textures) are a wrap-around Lanczos of colour and
  alpha separately, with binary alpha sharpened again. `exact_done.txt` tokens `<hash>@2` make images from an older
  source redo once. Tests `scripts/tests/test_hd_pack_sources.py` (fake upscaler, no GPU).
* Comments in `editor/scripts/ootf-convert.ts` and `scripts/buildtool/extras.py`; `docs/reference/hd-textures.md`,
  `BUILDING.md`, `docs/status/ootf-port.md`.

Two faults found on the way and fixed: a plain Lanczos resize of a tiling texture leaves a seam (seam ratio 5.2 against
1.4 for today's images; now 1.1), and Pillow's RGBA resize is premultiplied, which blackened the bled colour under
cut-out holes (dark fringes once the renderer filters across the edge).

## 5. Offline comparison (pixel exact, `build/review/textures/exp/`)

Ruined Village's six most used ground tiles and three squared wing textures, each variant at the same 512 px:
A = the play folder's image, B = x4 of the lossless 128 px enlargement, C = x4 of the 64 px PC tile plus Lanczos,
D = x4 twice over the whole first pass (x16, then down), E = x4, to 128, x4 (`--small-source esrgan`).

| Variant (6 ground tiles) | Seam ratio (1 = no seam) | Gradient energy | Mean colour shift from the PC tile |
| --- | --- | --- | --- |
| A play folder | 1.41 | 6.91 | 9.1 |
| B lossless enlargement | 1.38 | 6.96 | 9.5 |
| C one pass + Lanczos | 1.39 | 2.98 | 5.9 |
| D x16 | 1.06 | 4.70 | 10.1 |
| E two passes at guest size | 1.10 | 4.86 | 10.1 |

* A and B are the same to the eye: removing the quantisation (the cheap fix expected at the start) changes nothing visible.
* On these rock and dirt tiles D and E draw cracks and stone edges more clearly; A and B are softer with a mottled grain.
  E's outlines are harder and in places look drawn (`evidence/offline_ground_twopass.png`).
* **E was rejected after the game run:** on Ruined Village grass it turns the blades into a flat smear, flatter than even
  the guest texture (`evidence/rv_houses_twopass_rejected_crops.png`). The network reads the soft 128 px enlargement as
  fine texture and invents grass; fed the 64 px tile it denoises it away. So the shipped default enlarges the PC tile
  first (Lanczos instead of the converter's Catmull-Rom) and runs one pass: against the owner's images of the six tiles
  the mean difference is 3.4 to 6.4 levels and the gradient energy 2.9 to 6.0 against 2.8 to 6.2, i.e. the same look.
* Squared textures (`evidence/offline_squared_wings.png`): the owner's images (A, B) carry the stair steps of the doubled
  rows along every vein and bone; from the 256 x 128 original they are gone. Cut-out coverage equals the source to 0.03 %.
* The network shifts the mean colour by up to about 10 levels in today's images as well; not new.

## 6. In game

Play folder `build/review/textures/play`: a copy of `build/review/baseline-play` (owner's `drakan.ini`, DLSS quality,
`texture_upscale = auto`, anisotropic 16, `hd_textures = 1`, installed `ootf-levels.zip` and `drakan-character-motion`),
textures hard-linked; the new images in a separate `textures_after` folder (`GSR_TEXDIR=textures_after`), the owner's
images as `textures`. Same exe. Settings that differed from the owner's play: a 1280 x 960 window instead of borderless
fullscreen (`DRAKAN_FULLSCREEN=0`, internal scale auto), audio off, `DRAKAN_NO_ACTIVATE=1`, god mode, a direct level
start (`DRAKAN_START_LEVEL` from the Drakan 2 intro), `GSR_TEXUP_LOG=1`; the `elf =` line points at a private ELF copy.
Recipes `recipes/{rv,grotto,islands}.txt`: `place` with a yaw, a shot, `setting video.hd_textures 0`, a shot (the guest
texture at the same pose), on again. Runs `runs/*-before` (owner's images) and `runs/*-final` (new images).

* **Replacement:** every converted texture drawn was replaced from the pack in both runs (Ruined Village 281 / 277,
  Grotto 61 / 61, Islands 117 / 122 unique). Renderer texture cache peak 426 / 389 MB (Ruined Village), 191 / 189 MB
  (Grotto), 416 / 410 MB (Islands) of the 6144 MB budget; the new images have the same pixel sizes as the old (489 MB
  against 530 MB of PNG for the 1111 changed files).
* **Grotto cave** (`evidence/gr_a_*`, a matched frame: Rynn sits on Arokh, who holds still): cave rock before and after
  are practically the same; both are far sharper than the guest texture; Arokh (PS2) beside it is unchanged.
* **Ruined Village** (`evidence/rv_houses_*`, `rv_hall_*`, `rv_start_*`): the camera did not settle on the same pose in
  the two runs (Rynn walked to the wall in the final run, an orc moves at the start), so only regions can be compared:
  grass keeps its blades, the stone wall and the underground hall look alike. No seams or dark cut-out fringes seen.
* **Islands** (`evidence/is_*`): the Succubus (PC model, squared wing texture) moves between runs, so her wings were not
  compared in game; the stair-step fix rests on the offline images.
* Frame time and load time: not measured meaningfully. Seven to nine game instances of other agents ran on the same GPU
  (up to 99 % load) during every run; the fps overlay in the shots reads 9 to 80 fps with no pattern by variant. Nothing
  in the change touches the guest or the renderer: pack bytes identical, same replacement sizes.

## 7. Result in one paragraph

The reading "PC textures have no HD equivalent" does not hold: every converted texture already gets a Real-ESRGAN image
like the disc's. The remaining gap is the PC source (half the disc's texel density on terrain and props, 0.8 on
characters), which upscaling cannot close without inventing detail. What was fixed is small: 50 squared textures (wings,
creature skins) lose the stair steps their HD images had, quantised textures are upscaled from their lossless picture
(no visible change), and the PC original of every resized texture is kept as the HD source (engine-neutral input for
later work). Terrain looks the same as before; the sharper two-pass form was tried and rejected because it ruins grass.

## 8. Options not taken

* **x8 images for PC-origin textures** (to reach the disc's HD texel density on props): the runtime accepts up to 8x.
  Cost: four times the disk (the OotF part of the pack is about 1 GB at x4) and VRAM per texture, and detail invented from
  64 px sources; the grass result says a second network pass on such sources is risky. Not tried.
* **Per-texture choice of the two-pass form** (rock and dirt tiles gain, grass loses): needs a hand list or a classifier
  and a review of every ground tile; about a day.
* **Repainting or replacing PC art** (new terrain sets, PBR materials from `pbr_pack.py` for converted textures, another
  upscaler model): art direction work; `pbr_pack.py` would need an index of converted textures and is untested on them.
* **Keeping 32 bpp in the guest:** blocked by the guest mip build cost (2026-11-02 note); it would only matter with HD
  textures off, and the PSNR above says it would barely show.

## 9. How a player gets it

Rebuild with HD textures on (`Build-Drakan.ps1 -HdTextures on`, or the setup's HD page): the conversion reruns because the
converter code changed (189 s here, 474 s on the loaded machine), writes the `.hdsrc` sources, and the HD step redoes
the 1,111 converted images once (1,038 s here on a GPU shared with seven game instances; idle time not measured).
Disk: the same image sizes, about 40 MB less PNG, plus the `.hdsrc` folder next to the pack (1,144 files).

## 10. Not established

* No matched before/after frame of a squared texture in game (the creatures move); the offline images are the evidence.
* Ruined Village before/after frames are not the same pose; only the Grotto pair is matched.
* Borderless fullscreen at the owner's monitor resolution (higher internal scale) was not run.
* Frame time and load time on an idle GPU; the builder's time estimate for the HD step was not updated.
* The 86 unindexed live-upscaled textures in Ruined Village (84 of them 64 x 64) were not dumped this time; earlier notes
  found such ones to be runtime targets and noise.
* Why the owner's folder holds images made from quantised pictures although the builder passes `--hd`: not known.
* Levels after Islands were not run (the installed pack crashes in Volcanic, a converter fault fixed elsewhere today).
