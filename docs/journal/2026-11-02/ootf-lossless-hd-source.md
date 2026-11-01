# OotF textures: lossless HD source for quantised guest textures (2026-11-02)

Code: `editor/packages/formats/src/ootftex.ts` (`Ps2Texture.exact`, `setExactSink`), `editor/scripts/ootf-convert.ts` (`--hd` writes the
sidecar), `analysis/scripts/hd_pack.py` (`modextract`, `upscale`), test in `test/ootftex.test.ts`. Runs: `build/ootf-run/px/` (before =
the old pack, after = `hdv-{wart,isl,rw}-after`, 2560x1440, `PS2X_GS_SCALE=4`, `GSR_TEXUP=auto`).

## Audit: where the converter still lost quality

| Step | Loss | Why it exists |
| --- | --- | --- |
| Terrain: PC textures above 256 px | none (PC maximum is 256) | - |
| Terrain: 64 px sources enlarged to 128 px (Catmull-Rom) then median-cut to 8 bpp | colour (256 palette) | 128 px at 32 bpp is 64 KB per texture and thrashes the GS texture memory (Ruined Village: 80967 texture decodes, 30 s to gameplay) |
| Object / creature textures above 128x128 px with more than 256 colours | median cut to 8 bpp | the guest builds mip maps of every texture it uploads; ~33 256x256 32 bpp textures made Book load in 60 s instead of 6 s |
| 16 bpp route (`mode auto`) | 565 -> 555, 1 bit alpha | opt-in only |
| Chunk texture list | none (chunks split at 10 refs) | guest chunk record holds 10 refs |
| Visible-entry total capped at 40000 (`limitVisibleEntries`) | Volcano only (67105 entries): far-layer lists cut, no texture loss | the guest sizes something by the total; not located in the guest code, not lifted |
| One texture per cell | the right triangle's texture is dropped in 1.2 % of cells | PS2 format |

## Change: keep the guest cheap, give the engine the lossless picture

The engine already replaces textures by content hash from `textures/` (the HD pack, 16x anisotropic, mips from the replacement). So
the quantised guest texture never has to be what is shown. The converter now reports the lossless pixels (final size, before
quantising or 5 bit reduction) of every lossy encode to an optional sink. With `ootf-convert.ts --hd` they go to
`<out>.hdsrc/<sha1 of the 0x0040 record>.rgba`; `hd_pack.py modextract` hashes each record, and when a sidecar exists it indexes that
picture (and flags `exact`) as the Real-ESRGAN input, while the hash and alias still come from the decoded guest texture, so the
runtime match is unchanged. `upscale` redoes flagged hashes once (`exact_done.txt` in the textures folder) even when a PNG from an
older run exists.

Numbers (13-level campaign, 4454 textures, 3234 unique): **815 lossless sources** replaced quantised ones; upscale 102 s. Harness runs
Wartok Canyons, Islands (Rift World below) load and reach control; Laplacian variance 65.6 -> 67.8 and 77.7 -> 78.8 at similar
framing (the camera spots are not identical; the gain is limited to the quantised textures: banded gradients on cliffs and creature
skins now come from the unquantised source).

## Not done

* The guest still stores those textures 8 bpp: with HD textures off (`hd_textures = 0`) or no pack the banding stays.
  Lifting that needs the guest's per-upload mip build to be cheap (a native fast path for it in `runtime/gs` / `ps2xRuntime`).
* The visible-entry cap (Volcano) needs the guest allocation found; the converter keeps the nearest layers.
* The source resolution is 16..256 px; a pack cannot add detail beyond what Real-ESRGAN invents.
