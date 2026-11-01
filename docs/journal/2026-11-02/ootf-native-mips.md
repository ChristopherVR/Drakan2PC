# OotF textures: native mip filter (experimental, off by default) and the visible-entry cap (2026-11-02)

Code: `ps2recomp/ps2xRuntime/src/lib/drakan_texscale.{h,cpp}` (new, installed from `drakan_overrides.cpp`), formats `ootfterrain.ts`
(`MAX_VISIBLE_ENTRIES`), `ootfpipeline.ts` (`OOTF_TEXPIX`). Runs: `build/ootf-run/a2-bk{B,C,D}` (Book), `a2-rv` (Ruined Village),
`a2-volc*` (Volcano); campaign harness runs `build/harness/ootf-coop/a2solo`, `a2solo2`, `a2i*`, `a2m*`, `a2n*`, `a2d*`, `a2c*`.

## Why the converter quantises (cause found)

Guest function `0x1c0f28` (PAL) is the engine's surface downscale: `dst = $a0`, `src = $a1`, ratio `r = src.w / dst.w`, each output
pixel = the average of the 2x2 block at the top left of the r x r source block, per channel with the masks and shifts `0x1be858`
returns (surface fields: `+0x18` vtable, `+0x24/+0x28` size, `+0x34` pitch, `+0x38` bits per pixel, `+0x3c` flag; vtable `+0x98/+0x9c` lock
with this-adjust, `+0xa0/+0xa4` unlock). The texture upload calls it for every mip level. For 16 / 24 / 32 bpp the recompiled loop costs
about 80 instructions per channel; 8 bpp takes a cheap path (random pixel of the block), which is why the converter had to quantise big
textures to 8 bpp. Measured on Ruined Village Book with 32 bpp object textures (`DRAKAN_TEXSCALE_LOG=1`, guest path): **631,485
calls and 63.8 s inside the function by guest second 24; control not reached within 240 s.** The old "80967 texture decodes" of the
terrain note were the same loop.

## The native filter (works, but not safe enough to default on)

`drakan_texscale.cpp` replaces `0x1c0f28` for 16 / 32 bpp (24 bpp is excluded, see below; 8 bpp and odd ratios always run the original).
The lock calls can block (the surface may be swapped in through the texture cache and its semaphores): a first version that called
them nested on a context copy **hung at the 3777th call**. The final version never nests: it pushes a 0x100 byte frame on the guest
stack and tail-calls each guest step (`0x1be858`, lock dst, lock src, unlock src, unlock dst) with `$ra` in a trampoline (free
function-table slot, pattern of `drakan_dual_wield.cpp`); the filter runs between the two locks with the guest's arithmetic.

Measured (native on, 32 bpp packs, `OOTF_TEXPIX=Infinity`): Book `PASS`, level loaded at 23.8 s, 782,609 native calls, 2.6 s in the
remaining guest calls; Ruined Village (64 px terrain enlarged to 128 px, all 32 bpp) `PASS`, level at 26.8 s (Intro 20.9), `texDecodes=14032`.

**Not safe:** in the 13-level solo harness (`a2solo`, `a2solo2`) 11 to 12 of 13 levels pass; Islands Night (09) and once Book (02) stall
in the heap allocator's free-list walk (`pc=0x1e19a0`, right after gameplay starts or after a level change): heap corruption or a
race. Islands Night, native filter on (no charge): 1 pass, 5 fails over 6 runs; with a 100 us busy wait per call 3 passes, 1 fail; with the cycle charge 1 run, failed; the guest loop (`DRAKAN_TEXSCALE=0`) passed. Bisect by bit depth
(`DRAKAN_TEXSCALE_BPP`): 16 only and 32 only each passed once, 16+32 passed 1 of 3, so it is neither depth alone. Tried without effect:
leaving 24 bpp (6 calls in Islands Night) to the guest, bounds checks (no `refused` line ever printed), charging simulated EE cycles
(`DRAKAN_TEXSCALE_CHARGE`, `rt->cpTick(300 cycles per output pixel)` after the filter), a busy wait per call. The remaining suspects: a
scheduler interleaving the original relied on (the guest loop is preemptible at its back-edges; the native filter is not), or an
extent of the guest's write that I do not reproduce. A verify mode that runs the guest path on a copy of the destination and
compares is the next step. Therefore **the hook is opt-in (`DRAKAN_TEXSCALE=1`) and the converter defaults are unchanged** (8 bpp above
128 x 128, enlarged terrain 8 bpp); the lossless HD pack path of the previous note remains the way to show unquantised pictures.
`OOTF_TEXPIX=<pixels>` (converter env) overrides the pixel budget for experiments such as the above.

## Visible-entry cap lifted

`limitVisibleEntries` cut Volcano's 67,105 visible entries to 40,000 after 2026-11-01 crashes (wild `JALR` at a kill-list flush,
allocator hang). Those symptoms were heap corruption of another kind: the ODB class counts (`ModPack.patchSrsc`, 4e62a81 / b166b92)
and the template joint clamps were fixed after the cap was found. With budgets 55,000, 65,000 and 70,000 (= uncapped) Volcano loads and
runs (`a2-volc*`, about 85 fps), and level 11 passed the full solo harness uncapped (`a2solo2`). No guest allocation sized by the
visible total was found. The default budget is now unlimited (`MAX_VISIBLE_ENTRIES = Infinity`); `DRAKAN_TERRAIN_VISBUDGET=<n>`
restores a cap. Far layers keep their full visibility lists.

## One texture per cell (not changed)

PS2 stores one texture ref per terrain cell; PC has one per triangle. 1.2 % of Ruined Village cells (328 of 28514) differ. A split
needs either a finer layer grid (changes heights, collision and every UV) or engine support for two refs per cell in the chunk draw
lists (a separate larger change); the left triangle's texture still wins.
