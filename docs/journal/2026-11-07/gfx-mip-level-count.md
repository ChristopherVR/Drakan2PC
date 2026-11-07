# Renderer stop "mip level count N is invalid" (issue 5 OotF runs): cause and fix

Symptom: OotF runs (issue 5, [giant, Islands, rift](ootf-giant-islands-rift.md) `long-2`,
[closed-loop driver](ootf-closed-loop-driver.md) `g2`, `s1`, `s3`, `s4`, `st2`, `issue5-route/stairF13`) stopped
with a wgpu validation error in `wgpuDeviceCreateTexture` (`gs texture`, once `hd texture`), "mip level count 8 is
invalid, maximum allowed is 1" (also 3/1, 4/1, 4/3, 5/3), then a panic in `wgpuQueueSubmit`, exit 3. Seen at the
Alwarren start and mid-level.

## Cause

* Code path: `resolveTexture` (`runtime/gs/src/wgpu_textures.cpp`, same in `d3d11_textures.cpp`) took the mip count
  straight from `TEX1.MXL` (3 bits, 0..7). It decoded `MXL + 1` levels of `max(1, W >> l) x max(1, H >> l)` texels,
  created the texture with `MXL + 1` levels and uploaded each one; the HD copy (`setupUpscale`, `setupPaletteHd`)
  built `MXL + 1` levels too. A GPU texture holds at most `floor(log2(max(W, H))) + 1` levels, so any MXL greater
  than `max(tw, th)` fails creation (WebGPU: invalid texture, then the submit panics; D3D11: `CreateTexture2D`
  fails and the draw is lost). The texture views and uploads all used the same too-large count, which is why
  clamping only at creation moved the error to the later write of mip level 1.
* Which texture: no real texture. With the logging added below, `alw-2` (one of four Alwarren starts, issue 5 pack
  `ootf-i5/pack`, private runs under `build/harness/gfx-mip/`) printed
  `TEX1.MXL 6 past the 1x16 texture's last level 4 (tbp=9 tbw=40 psm=00 tex0=00230021000a0009
  tex1=000000fb0000013b miptbp1=000c000000050024 miptbp2=756f5320706f7453)`. The TEX0, MIPTBP1 and MIPTBP2 values
  are consecutive bytes of the converted level `OOTF07EN.LVL` (offset 2636856): a class property table, where
  MIPTBP2 is the ASCII of "Stop Sou(nd)". So the GS got a register-list packet read from memory that now holds
  level data (a stale or wrong DMA source). The `VU1 reserved lower` runs (`s1`, `s3`, `s4`, `st2`, `stairF13`:
  VU1 running data as microcode) send garbage GS state the same way. Neither is a converter texture: `ootftex.ts`
  writes power-of-two sizes of at least 8 px, with no mip data of its own (the guest builds the chain).
* Stock levels: no harness log ever had the error. Surdana, Shadowmire and Andrellian Isles starts with the logging
  build (`stock-*` runs) printed no clamp. Only converted-pack runs hit it.
* With the fix: 7 Alwarren starts (`alw-1` to `alw-7`), all clean scripted exits, no GPU error; `alw-2` cut the bad
  MXL four times and drew on.

## Fix (runtime/gs)

* `textureMipLevelMax(tw, th, mxl)` (`gs_texdecode.h`) = `min(MXL, 6, max(tw, th))`: the last level the size has (the
  GS limit is 6). `resolveTexture` uses it for the cache key, the decode loop, creation, uploads, the texture event
  and the draw's LOD clamp (`TexBind::mxl`), on both backends. Each cut is counted (`Stats::texMipClamped`) and the
  first 16 are logged (`[gsr-tex] TEX1.MXL ... past the ...`, all with `GSR_TEXLOG`).
* Found by the new test: an upscaled copy of a non-square texture had black deep levels. Level l of the copy is
  `max(1, W * N >> l)`, which is N times the native level only while `l <= log2(min(W, H))`; past that the native
  short side stays 1 but the copy's keeps halving, and the shader's per-texel scale N read past the level.
  `upscaledMipLevelMax(W, H, mxl)` cuts the copy's chain there (`UpEntry::mxl`) and the draw clamps its LOD to it
  while the copy is bound. Only far LODs of non-square mip-mapped textures in HD mode change (they stop one level
  earlier instead of going black).
* Test: `mipChainLimitTest` in `runtime/gs/tests/test_synthetic.cpp` (`synthetic_webgpu` in ctest, and the D3D11
  `gsr_test_synthetic`; `--mip-chain-only` runs it alone): sizes 1x1 to 64x64, square and not, every MXL 0..7, CT32 and
  T8, native and 4x upscaled; checks the decoded level count and the drawn colour. Before the fix it ends in the same
  `wgpuQueueSubmit` panic as the game.

## Open

* The stale GS packet itself: the guest sends a register list built from memory that holds level data. Harmless for
  the renderer now, but it means something DMAs from a freed or reused buffer in OotF levels (the VU1 garbage runs are
  the other source, under investigation in the VU1 work). Finding the guest buffer needs the GIF source address of
  that packet (a `GSR_TEXLOG` run with a DMA trace).
