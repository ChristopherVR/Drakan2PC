# Drakan PS2 (SCES_500.06) - graphics pipeline analysis and PC replacement design

Scope: how the game drives the GS through VIF1 / VU1 / DMA, what the VU1 microcode does, which EE
functions build and kick the packets, and how to replace all of it on PC.
Confidence tags: **[confirmed]** = read directly from the binary, **[inferred]** = deduced from code
shape, **[unknown]** = needs a runtime capture.

All addresses are PS2 virtual addresses in the ELF (`iso/SCES_500.06`, .text 0x100000-0x4d73f4).

## 0. TL;DR

* There is exactly **one VU1 microprogram**: 1523 instructions (0x2f98 bytes), loaded once at start-up
  from a prebuilt DMA/VIF packet that sits in `.vutext` (0x4d7400). The six `.DVP.overlay` entries are
  just six consecutive 256-instruction `MPG` chunks of that one program (overlay k is loaded at micro-memory
  instruction 256*k); the six `.DVP.overlay` ELF sections themselves are zero-filled placeholders. **[confirmed]**
* The program is a deterministic "uber vertex shader + triangle clipper + GIF packet builder":
  transform, optional lighting / UV transform / distance fade / bounding-box frustum cull / full
  Sutherland-Hodgman clipping against 5 planes (+z,+x,-x,+y,-y; the CLIP -z bit 0x20 is never tested), then `XGKICK` of packed-mode GIF packets
  (`REGS = ST, RGBAQ, FOG, XYZ2`, `PRIM = TRISTRIP` or `TRIFAN`). No `JR/JALR/BAL`, no M/D/T bits, no
  EFU use except one `ERLENG`/`MFP` pair. **[confirmed]** It is trivially interpretable (and statically
  recompilable); cycle accuracy is not needed (one branch-hazard site to test, see 3.6).
* On the EE side the game has a thin "Surreal PS2 Display" driver (GS register/primitive/packet-buffer
  helpers, `sceDma`-style send/sync) and a "VU renderer" object (global at 0x4e9ac0) that builds
  per-object VIF packets (`UNPACK`, `MSCAL`, `MSCNT`, `DIRECT`) in uncached DMA buffers and kicks them with
  DMA channel 1 (VIF1, chain mode with TTE). Texture uploads / 2D / screen effects use the "core"
  EE-side GIF path (also through the same chains and DMA ch.2 / PATH3). **[confirmed/inferred]**
* Recommendation: **option A (low-level: VU1 interpreter + GIF/GS parser + GS renderer)**, first with a simple
  *GS software rasterizer on emulated GS local memory* (correct-by-construction, exposes every texture format
  and framebuffer feedback trick), then swap the back end for a D3D11/Vulkan hardware renderer while keeping
  the VU1/GIF front end unchanged. Option B (hook the render functions) is attractive only as a later
  enhancement layer (widescreen, higher-res, better filtering) because the game's draw API is not a mesh API
  (see section 7).

## 1. Files produced

| path | what |
|---|---|
| `analysis/scripts/vudis.py` | VU0/VU1 disassembler (upper+lower, all FMAC/EFU/branch/flag ops, I/E/M/D/T bits, LOI). `python -I vudis.py --elf iso/SCES_500.06 --outdir analysis/vu` regenerates the dumps; `python -I vudis.py <file> --offset N --size N --base A` disassembles any raw microcode blob. Tables cross-checked against PCSX2 `VUops.cpp`. |
| `analysis/vu/vu1_all.txt` | whole microprogram (instr 0..1522), labels `L_xxxx`, flags `{IEMDT}` |
| `analysis/vu/vu1_ovl{0..5}_*.txt` | the six MPG chunks / "overlays" (micro-mem byte offsets 0x0000,0x0800,0x1000,0x1800,0x2000,0x2800) |
| `analysis/vu/vu1_micromem.bin` | the reassembled 0x2f98-byte microprogram image (what VU1 micro-mem contains) |
| `analysis/vu/vu1_summary.txt` | opcode histogram, E-bit sites, XGKICK sites, constants, absolute VU-memory addresses touched |
| `analysis/vu/vu1_branch_hazards.txt` | integer-register write->branch read pairs (see 3.6) |
| `analysis/scripts/vu_analyze.py`, `vu_hazards.py` | the two analyzers above |
| `analysis/scripts/gfx_scan.py` | scan .text for MMIO bases (DMA/VIF/GIF/GS privileged/IPU/SPR) |
| `analysis/scripts/gfx_vifscan.py` | scan .text for VIF-code / UNPACK immediates |
| `analysis/scripts/gfx_dis.py` | capstone-based R5900 disassembler wrapper (adds lq/sq/COP2 decoding) |
| `analysis/scripts/gfx_xref.py`, `gfx_callers.py`, `gfx_funcsum.py`, `gfx_adregs.py` | address xrefs, callers of `jal` targets, per-function summaries, GS A+D register histogram |

Note: `.DVP.ovlytab` (0x45786c, 6 entries x 12 bytes) = `{name string ptr, image address in .vutext, VU micro-mem byte offset}`
= `{0x5b85fd,0x4d7410,0x0000} {0x5b861d,0x4d7c18,0x0800} {0x5b8641,0x4d8420,0x1000} {0x5b8666,0x4d8c28,0x1800}
{0x5b868b,0x4d9430,0x2000} {0x5b86b0,0x4d9c38,0x2800}`; names in `.DVP.ovlystrtab` are
`.DVP.overlay..0x0...`, `..unknvma...` x5. **[confirmed]** The image address points at the first instruction after
the `NOP, MPG` VIF-code pair of each chunk.

## 2. How the microcode is stored and uploaded

`.vutext` (0x4d7400, 0x2fe0 bytes) is a ready-to-DMA chain **[confirmed]**:

```
0x4d7400  DMA tag  {QWC=0x2fc, ID=1 (CNT)} , vifcode0 = NOP , vifcode1 = MPG(num=0 (=256), addr=0x000)
0x4d7410  256 instr (0x800 B)
0x4d7c10  NOP, MPG(num=256, addr=0x100) | 256 instr (0x4d7c18..)
...       addr 0x200, 0x300, 0x400, and finally MPG(num=0xF3, addr=0x500) (243 instr, 0x798 B)
end       DMA tag 0x70000000 (ID=7, END)
```

(MPG addr/num are in 8-byte instructions; a single MPG packet is limited to 256 instr, hence six of them.)

Upload: the renderer constructor `0x1e88f8` ends with `jal 0x1f13b8(a0=0x4e9b70, a1=0x4d7400)` which sends this
chain on the VIF1 channel (`0x1f1750`, CHCR = 0x145 chain+TTE). It also stores the two **entry points** into the
renderer object: `renderer+0x14 = 0x0000`, `renderer+0x18 = 0x26e0` (see 3.2). The program is uploaded once and
never replaced. Static hardware setup that goes with it (`0x4af9b0` area): VIF1_FBRST reset (`0x10003c10`), `VIF1_ERR=6`
(`0x10003c20`), and a 2-qword init packet written straight into VIF1_FIFO (0x10005000) from 0x5153c0:
`STCYCL CL=4 WL=4, STMASK 0, STMOD 0, MSKPATH3 0, BASE 0, OFFSET 0, ITOP 0` - i.e. **no VU1 double buffering
(BASE/OFFSET=0, TOPS never used), all UNPACKs use absolute VU addresses with CL=WL (contiguous)**.

## 3. The VU1 microprogram

### 3.1 Size/shape (from `vu1_summary.txt`)

1523 instructions; 113 branch targets; 45 IBGTZ + 33 IBLEZ + 19 IBEQ + 13 IBNE + 5 IBLTZ + 26 B; 15 DIV, 5 WAITQ,
8 CLIP (+8 FCGET, 2 FMAND), 7 XGKICK, 10 LOI constants (255.0, 200.0, 128.0, 65536.0, 2.0, -1.0, 1/128), 1 ERLENG + 1 MFP,
no SQRT/RSQRT/E-unit transcendental, no JR/JALR/BAL (so no computed jumps, no subroutines), no M/D/T bits.
E-bit (end) sites: `0x01e0, 0x0e60, 0x1008, 0x1570, 0x2a30, 0x2a40, 0x2a50, 0x2c38, 0x2f88`.
XGKICK sites: `0x1060 0x13c8 0x1420 0x2578 0x26b0 0x2bf8 0x2f48`.

### 3.2 Entry points and the MSCAL/MSCNT "E-ladder" **[confirmed + inferred]**

* Entry table in the renderer object: `[+0x14] = 0x0000`, `[+0x18] = 0x26e0`; `MSCAL` operand = `entry >> 3`
  (instruction index). `0x1eb3c8` (`MSCALF`) picks `table[renderer->mode]`; `0x1edf90`, `0x1ee840`, `0x1ef930`
  hard-code entry `[+0x18] = 0x26e0`. No other MSCAL/MSCALF/MSCNT address constants exist in .text.
* After an E-bit instruction (plus its delay slot) the program stops; the next `MSCNT` resumes at the next
  instruction. The code is laid out as a chain of such stops, so the EE just emits a *fixed number* of `MSCNT`s
  per packet, and different exit paths fall into the ladder at different rungs (e.g. `0x2a30`,`0x2a40`,`0x2a50` are
  three consecutive E-bit stops, used so that culled/clipped/trivially-accepted objects consume the same number of
  MSCNTs). **The interpreter must implement MSCAL, MSCNT (resume PC), MSCALF, E-bit stop semantics and VIF
  stalling until VU1 stops.** Typical packet seen on the EE (`0x1ee840`, `0x1ef930`, `0x1edf90`):

```
DMA tag CNT QWC=5 | vif0 = FLUSH(0x11000000)  vif1 = UNPACK V4-32 (0x6c04_0017: 4 qw -> VU addr 23)
4 qwords : 4x4 object (model) matrix (3x4 transposed + w=1.0)   ->  VU data [23..26]
qword    : MSCAL (0x14000000 | 0x26e0>>3) , MSCNT (0x17000000)
```
* **Program P0 (entry 0x0000)** - indexed vertex/primitive batch pipeline. `0x0000-0x01e0`: loads
  `vf01..vf04 = [0..3]` (already concatenated model-view-projection), `vf05..07 = [4..6]` (fog/clamp/viewport
  scale+offset), optionally computes a fade value for the object; E at `0x1e0`. Then every `MSCNT` resumes at
  `0x1f0`, which reads the batch header `[9]` and flag word `[7].x`, and dispatches:
  * `0x1f0-0xe60` **vertex pass** (E at 0xe60, loops back to 0x1f0 through the `B` at 0xe70): over `[9].y` vertices at
    VU address `[9].x`:
    per-vertex optional steps selected by bits of `[7].x`: morph/alpha fade from depth (0x278-0x3a0, bit 0x80),
    2x3 texture-coordinate transform (0x468-0x520, bit 0x200 - scrolling/rotating UVs), a "grid/warp" generator
    (0x520-0x640, bits 0x1000/0x2000, uses LOI 65536.0 - **[inferred]**, probably the "Screen Warp" effect),
    point-light accumulation with ERLENG distance attenuation (0x648-0x880, bits 0xC000), 3 directional lights + ambient
    (0xaa8-0xc48, bit 0x10, clamp to 200.0 via `MINIi`, output via `FTOI0`), then the transform+perspective divide,
    fog factor (`MINIz/MAXy/MSUBAw`), `FTOI4` to 12.4 fixed point, or - when clipping is required (bit 0x1) - the
    `CLIP`/`FCGET` outcode path (0xd80-0xe58) which stores the 6-bit clip flags in `.w` of the result array.
  * `0x0e80-0x1570` **primitive pass** (E at 0x1008 and 0x1570): reads index lists (4 packed indices per qword, header
    `[9].x/.y/.z/.w`), looks at the per-vertex clip outcodes (`LQ/ILW 515(vi).w`), trivially rejects
    (all verts outside the same plane), assembles 4-qword vertices (`ST, RGBAQ, FOG, XYZ`) into the GIF output
    area and `XGKICK`s it (0x1060, 0x13c8/0x1420 variants).
  * `0x15f8-0x26b8` **clipper**: Sutherland-Hodgman against five planes, one block per CLIP-flag bit tested in the order
    0x10 (+z), 0x01 (+x), 0x02 (-x), 0x04 (+y), 0x08 (-y) (blocks at 0x1740, 0x1a18, 0x1cf8, 0x1fe0, 0x22c0; each ~0x2e0 bytes:
    `CLIP` + `DIV` for the intersection parameter, ping-pong polygon buffers at VU addr 773 and 802, polygon re-emitted as a
    TRIFAN via the second GIF tag, `XGKICK` at 0x2578/0x26b0). The initial "needs clipping" mask is 31 (0x1f).
* **Program P1 (entry 0x26e0)** - *object-level setup + direct strip drawing*. `0x26e0-0x2790` builds
  `MVP = VP[0..3] * model[23..26]` (vf01..vf04). Flag bit 0x800: projects the object centre `[22]` and computes a
  fade/alpha factor into `[8].w`; if it is <= 0 the object is skipped (jumps to the E-ladder). Flag bit 0x400: frustum
  tests the 8 bounding-box corners stored at `[27..34]` (viewport scale `[35]`) with `CLIP`, ANDs/ORs the outcodes
  (all-outside => cull, all-inside => no clipping needed). Then (MSCNT stages) `0x2a60-0x2c38` draws
  pre-built strips (flag bit 8): positions from VU addr 54.. (count table at 294.. - e.g. `ILW 240(vi09)`),
  ST from `478(vi09)`, constant colour `[8]`, output double-buffered between VU addr 774 and 899, `XGKICK` at 0x2bf8;
  `0x2c48-0x2f88` is the same with per-vertex lighting from `[13..20]`, `XGKICK` at 0x2f48.

The three `fetch -> compute -> kick` shapes are all that the EE ever asks of VU1, so **a faithful interpreter
is also the *simplest* implementation**; re-implementing it in C++/HLSL by hand would mean reproducing the clipper
and the packet layout exactly for no gain.

### 3.3 VU1 data memory map (inferred from absolute `vi00` accesses and UNPACK addresses)

| addr (qw) | content |
|---|---|
| 0-3 | P0: object MVP matrix rows (pre-multiplied); P1: view-projection rows (`UNPACK 6c04_0000` in `0x1e8c78`) |
| 4-6 | `[4]` fog/clip limits (y,z used by MAXy/MINIz), `[5]` screen offset (+z/ADC bias), `[6]` screen scale (`0x1e93b8`: `6c03_0004`) |
| 7 | `.x` = feature-flag word (`vi01`): 0x1 clip needed, 0x8 direct strips, 0x10 dir lights, 0x80 depth-alpha, 0x100 lights B, 0x200 UV xform, 0x400 bbox cull, 0x800 fade/cull by distance, 0x1000/0x2000 grid, 0xC000 point lights |
| 8 | fixed vertex colour / ambient (UNPACK `6e01_4008` = V4-8 unsigned, ITOF0'd in VU); `.w` = per-object fade alpha |
| 9 | batch header (V3-32 `6801_0009`): `.x` vertex base, `.y` count, `.z` index qwords, `.w` aux counter |
| 11, 12 | GIF tag templates (UNPACK `6c02_000b` from `0x1eb3c8`): [11] = TRISTRIP, [12] = TRIFAN; both `PRE=1`, `NREG=4`, `REGS=0x5a12` (nibbles from LSB = ST, RGBAQ, FOG, XYZ2); NLOOP patched by `ISWR.x` before each XGKICK **[confirmed from 0x1eb7d4-0x1eb8c0]** |
| 13-20 | lights (point-light pos/colour, 3 directional lights as ITOF'd 4x4), `[21]`,`[22]` fade distance params (`0x1e9bb0` `6c02_0015`) |
| 23-26 | model matrix (4 rows) uploaded per object/instance |
| 27-35 | bounding-box corners [27..34] and viewport scale [35] (`0x1eea90`: `6c09_001b`) |
| 43-47 | scratch/aux: `[43]` = 3 packed ints, `[44],[45]` extra UV/consts, `[47]` null vertex scratch |
| 54.. | P1 direct strips: positions read with `LQI` from vi09 (starts at 55), strip lengths via `ILW.x 240(vi09)`, ST via `LQ 478(vi09)` |
| 65 | scratch ints (stage hand-off; `ISW/ILW .x/.y/.z 65(vi00)`) and, indexed by a vertex slot, the transformed-position array `65(vi)` |
| vertex slots | P0 inputs at `[9].x` (set A), `+225` (set B), `+450` (set C); processed arrays at `65(vi)` (position), `290(vi)` (colour/UV), `515(vi)` (`.w` = 6-bit clip outcode) - the 225-entry stride gives at most ~225 vertices per batch **[inferred]** |
| 773-980 | GIF output buffers (773/774 A, 802 clip ping-pong, 899 B, 980 fan output) |

### 3.4 What the microcode asks of the GS

Output is always a packed-mode GIF packet: `[tag]` + N x `{ST(s*q,t*q,q), RGBAQ(r,g,b,a,q), FOG(.w=F<<4), XYZ2 (12.4 fixed, .w = ADC)}`,
`PRIM = TRISTRIP(4)` for normal output and `TRIFAN(5)` for clipped polygons; PRIM attribute bits (TME, FGE, ABE,
IIP, AA1, CTXT...) come from a shadow copy of the GS `PRIM`/context state (`ld $v1, -0x6ff0(...)` in `0x1eb7e8`).
So the GS needs *textured, Gouraud-shaded, perspective-corrected, fogged triangle strips/fans with alpha*.

### 3.5 Interpretability verdict

* branch-heavy but entirely static control flow (no indirect jumps), all data-dependent behaviour is integer
  compare/branch, FMAC math with ITOF/FTOI, 15 `DIV`s with `Q`, one `P` (ERLENG) read;
* flag usage: the CLIP flag (`CLIP` -> `FCGET` -> mask, 8 sites) and, at two sites (0x0e10, 0x29c0), `FMAND` with mask 0x10 = the
  **MAC sign flag of the W result** after `SUBz.w` (w-z < 0, i.e. beyond the +z plane). Status flags (`FS*`) and the
  overflow/underflow/zero MAC bits are never consumed, so only the CLIP flag and the MAC sign bits need to be exact;
* FP semantics: PS2 VU float (no denorms/NaN/Inf, round-toward-zero-ish truncation, clamped overflow) matters mostly for
  determinism of the clipper; use a PCSX2-style `vuFloat` helper (or just IEEE with flush-to-zero) and verify by
  trace comparison with PCSX2 on a few frames;
* instruction timing: not required (in-order sequential semantics give the same result because FMAC/Q/P consumers either
  are separated by the programmer's NOPs/`WAITQ` or are ordered so program order == hardware order), *except*:

### 3.6 One branch-hazard site to emulate

`vu1_branch_hazards.txt` lists 57 write->branch-read pairs; 56 have one instruction of separation (fine). **One site
has the writer immediately before the branch**: `0x1108: IADDIU vi12,vi12,1` / `0x1110: IBEQ vi12,vi15,L_1188`. On real
VU1 (and in PCSX2 via `VIBackupCycles`) the branch sees the **old** `vi12`. Implement PCSX2's integer-backup rule in the
interpreter (or special-case this site).

## 4. EE-side graphics architecture

```
game code (C++ objects, vtables)                         -> 0x158148/0x161a78 (model/landscape draw), 0x1ab530/0x1c7410 (3D object draw),
  |                                                           HUD/2D in 0x1c1a34.., many vtable (jalr) call sites
  v
VU renderer (global obj 0x4e9ac0; ctor 0x1e88f8)         -> builds VIF packets in 3 uncached DMA buffers (0x50000/0x20000/0x50000 bytes)
  UNPACK/MSCAL/MSCNT/DIRECT/FLUSH/MSKPATH3 + A+D          chain tags: CNT(1) / NEXT(2) / END(7); tag upper half carries VIF codes (TTE)
  v
Display driver (global obj 0x4e8ef0, "Surreal PS2 Display", ctor 0x1d8930; buffer mgr 0x1d93c8/0x1dacb8)
  v
sceDma-like layer 0x1f1420 / 0x1f1750 / 0x1f17e0 / 0x1f1870 (send/sync)  -> DMA ch1 (VIF1, CHCR=0x145) and ch2 (GIF, CHCR=0x105)
  v
VIF1: UNPACK -> VU1 data mem, MPG -> micro mem, MSCAL/MSCNT -> run VU1, DIRECT -> PATH2 GIF packets
  v
VU1 microprogram -> XGKICK -> PATH1 -> GIF -> GS   (plus PATH2 DIRECT and PATH3 image uploads, with MSKPATH3 arbitration)
```

### 4.1 Key EE functions

| addr | role | conf. |
|---|---|---|
| `0x1d8930` | display ctor/init: `sceGsResetPath`(0x4afa18), `sceGsResetGraph(0, interlace=1, PAL(3), FIELD)` (0x4afb48 -> `SetGsCrt` syscall 0x02 via wrapper 0x4bbd60, `GsPutIMR(0xff00)`), allocates the packet buffer object, programs FRAME_1/ZBUF_1/XYOFFSET_1/SCISSOR_1/PRMODECONT/CLAMP_1,2/TEXA/COLCLAMP; draws a clear sprite twice | conf. |
| `0x1d8df8` | initial GS register shadow values (PMODE, SMODE2, DISPFB1/2, DISPLAY1/2 at display+0x130..0x168) | conf. |
| `0x1dfff8` | **per-frame display flip**: writes `PMODE 0x12000000`, `SMODE2 0x12000020`, `DISPFB1 0x70`, `DISPLAY1 0x80`, `DISPFB2 0x90`, `DISPLAY2 0xa0`, and queues FRAME_1/FRAME_2 (0x4c/0x4d) | conf. |
| `0x1e0180`, `0x1d93c8`, `0x1d97a8` | append an A+D register write (`reg`, 64-bit value) to the pending list / the DMA chain; `0x1d9de8` = `DrawPrim(prim, verts, n)` (e.g. prim 6 = SPRITE x2 for 2D) | conf./inf. |
| `0x1e0220 0x1e0268 0x1e04f4 0x1e0518 0x1e0290 0x1e03e8 0x1e0390 0x1e0478 0x1e01a0 0x1d9b88` | GS state setters: FOGCOL, FOG, DTHE, DIMX, TEST_x alpha-test, TEST_x ztest, FBA, ZBUF zmask, CLAMP, ALPHA_x | conf. |
| `0x1dacb8` | flush/kick: ends the current DMA chain and sends it (DMA ch1 via 0x1f1420) | inf. |
| `0x1dbfa8` | GS/DMA reset-sync: GS CSR 0x12001000 / BUSDIR 0x12001040 handling, waits on both channels | inf. |
| `0x1da260 0x1da7f0 0x1db730 0x1dc738 0x1dcb20 0x1dd2d0` | scratchpad-staged image transfers / EE-side primitive builders (8 vertex-format variants; `0x1da260` is called from the IPU/movie code at 0x177654) | inf. |
| `0x1dfcb8` | scratchpad (0x70000000) double-buffer swap helper used by all of the above | inf. |
| `0x1f1420` | `sceDmaSend`-like for chained transfers: D_STAT/D_PCR update, `QWC=0`, `TADR=tag`, `CHCR = 0x105 (ch2 GIF) / 0x145 (other, VIF1: TTE)`; accepts SPR addresses (`(a & 0x70000000)==0x70000000` kept, other addresses masked `& 0xcfffffff` to strip the 0x2.../0x3... cache aliases) | conf. |
| `0x1f1750`, `0x1f17e0` | ditto with explicit TADR flag / normal-mode send (CHCR=0x100) | conf. |
| `0x1f1870`, `0x1f1930`, `0x1d78f0` | DMA sync/wait (`"libdma: sync timeout"`) | conf. |
| `0x1e88f8` | **VU renderer init** (see section 2) | conf. |
| `0x1e8c78` | begin-scene: `UNPACK 6c04_0000` VP matrix, `MSKPATH3` (0x06000000/0x06008000) | conf. |
| `0x1e93b8 0x1e97a0` | upload viewport/fog consts to VU addr 4 (`6c03_0004`) | conf. |
| `0x1e9bb0`, `0x1e9fa0`, `0x1ea918` | upload fade params (21), lights (13..20, `6c08_000d`), ambient `[8]` and misc | conf. |
| `0x1eada8`, `0x1ec160`, `0x1ec8f0`, `0x1ecd48`, `0x1ecea0`, `0x1ed0b8`, `0x1ed350`, `0x1ef1c8` | per-batch emitters (vertex arrays via V3-32/V4-8/V4-16/S-* UNPACKs, `[9]` header, `MSCNT`) | inf. |
| `0x1edf90`, `0x1ee840`, `0x1ef9c8` | upload model matrix(es) and `MSCAL 0x26e0` (`1edf90`: one object; `1ee840`: array of 0x30-byte instance matrices, one packet each; `1ef9c8`: single matrix packet builder) | conf. |
| `0x1ef930` | loops over an object's parts calling `0x1ec160` (when a2!=0) and `0x1ec8f0` | conf. |
| `0x1eb3c8` | end-of-batch: GIF tag templates to VU addr 11/12, `MSCALF`, then flushes `DIRECT` A+D state | conf. |
| `0x1ebd60`, `0x1ed8f0`, `0x1ed508` | `DIRECT`/`DIRECTHL` GS state, `FLUSH`/`FLUSHA`, scene end + `MSKPATH3` | conf. |
| `0x1eea90` | bounding-box + viewport upload (`6c09_001b` -> VU 27..35) | conf. |
| `0x158148`, `0x161a78` | big model/landscape render routines (use DIRECT/DIRECTHL, VU renderer and the core path) | inf. |
| `0x1485c8` / `0x1489f0` | "render object with lights" sequence: `1e8c78, 1eba38, 1ef8b0, 1dacb8, 1eea90, 1e93b8, 1e9fa0, 1e9bb0, 1edf90, 1ed8f0, 1eb3c8, 1ed508` - good template for what B would have to hook | conf. |
| `0x4bb718..0x4bb8d0`, `0x1767a0`, `0x177654`, `0x177828` | libipu / movie playback (IPU regs 0x10002000.., D3/D4 channels 0x1000b000/0x1000b400); string "Initializing IPU library..." at 0x51b598 | conf. |
| `0x4bbd44 + 0x10*n` | syscall wrappers (`li v1,n; syscall`); `n=2` SetGsCrt, `0x70/0x71` GsGet/PutIMR | conf. |

Config/debug strings give names to the layers: "Disable VU Renderer", "Disable Core Renderer", "Sync DMA", "Render
Method: Direct DMA,Object Surface", "Display Rendering VU Time/Core Time/VB Time", "Texture Cache Size", "Model Cache
Size", "Splat Cache" (0x51d8a0-0x51e170, 0x5441b8). So the game itself distinguishes the **VU renderer** (VU1 microprogram
path) and the **core renderer** (EE-side transform, direct GIF prims through VU0 macro-mode, used for HUD/particles/etc).

### 4.2 Hardware/MMIO touched by the EE code (what the runtime must model)

| range | use |
|---|---|
| DMA ch1 VIF1 `0x10009000` (CHCR/MADR 10/QWC 20/TADR 30), ch2 GIF `0x1000a000`, ch3/4 IPU `0x1000b000/0x1000b400`, ch5 SIF0 `0x1000c000`; D_CTRL `0x1000e000`, D_STAT `e010`, D_PCR `e020`, D_SQWC `e030`, D_RBSR `e040`, D_RBOR `e050`, D_STADR `e060`; D_ENABLER/W `0x1000f520/f590` | channel descriptors are table-driven (`0x4e9b70..0x4e9bd8`; fields +4 QWC*, +8 TADR*, +0xc MADR*, +0x10 CHCR*, +0x14 chan#) |
| VIF1_STAT `0x10003c00`, FBRST `3c10`, ERR `3c20`, VIF1_FIFO `0x10005000`; GIF_CTRL `0x10003000`, GIF_MODE `3010`, GIF_STAT `3020` | init/sync only |
| GS priv: PMODE `0x12000000`, SMODE2 `0x20`, DISPFB1 `0x70`, DISPLAY1 `0x80`, DISPFB2 `0x90`, DISPLAY2 `0xa0`, BGCOLOR `0xe0`, CSR `0x12001000`, IMR `0x12001010`, BUSDIR `0x12001040` | display, screenshot (GS->EE via BUSDIR; `"ScreenShot%02d.tga"`) |
| IPU `0x10002000..0x10002030` + D3/D4 | FMV |
| scratchpad `0x70000000` (16 KB) | heavily used as double buffer for packet construction and as DMA chain source |
| VU0 macro mode (COP2) | only 69 instrs in 6 functions (0x1165b0, 0x116990, 0x197e40, 0x1d9de8, 0x1dd2d0, 0x1e05a8): `ADD/SUB/MUL(x,z,q)/MADD*/MULA*/MADDA*/MAX/MINI/ITOF/FTOI/DIV/WAITQ`, `lqc2/sqc2` x52. **No VCALLMS - VU0 micro-programs are not used.** |
| timers `0x10000000..` (profiling), INTC `0x1000f000` (DI/EI helpers), SBUS `0x1000f130/f180` | not graphics |

DMA/memory facts the runtime needs: packet buffers are accessed through the **uncached alias** (`| 0x20000000`); the DMA
engine must mask to 512 MB physical (or 32 MB), accept SPR addresses (`0x70000000` and `0x80000000` flag), and
honour TTE (`CHCR bit6`) so that the 2nd 64 bits of each chain tag are interpreted as two VIF codes.

### 4.3 VIF codes seen as immediates (`gfx_vifscan.py`, EE functions 0x1e8c78-0x1ef930)

`UNPACK` formats used: V4-32 (0x6c) for matrices/lights/consts, V3-32 (0x68), V4-8 (0x6e, incl. USN for
colours), V4-16/S-* in the vertex-array emitters; `STMASK 0x20000000`+mask word, `STROW 0x30000000` (row fill for masked
unpack), `STCOL 0x31...` (in non-render lib code), `MSKPATH3 0x06000000/06008000`, `FLUSH 0x11000000`, `FLUSHA 0x13000000`
(0x1ebec8), `MSCAL 0x14000000` (3 sites), `MSCALF 0x15000000` (1), `MSCNT 0x17000000` (8), `DIRECT 0x50000000` /
`DIRECTHL 0x51000000` (158148, 161a78, 1eb3c8, 1ebd60, 1ed8f0), `MPG` only in the one-time `.vutext` chain.
No double-buffer (TOPS/flag-bit) unpacks, no `ITOP`-relative addressing.

## 5. Display set-up

* `sceGsResetGraph(0, 1, 3, 0)` -> `SetGsCrt(interlace=1, mode=PAL(3), FIELD)` - the SCES (PAL) game runs 50 Hz interlaced
  field mode. Exact FRAME/DISPFB/DISPLAY geometry is computed bit-by-bit in `0x1d8df8` from fields in the display object
  and is **[unknown]** statically (needs one runtime capture of `PMODE/SMODE2/DISPFB*/DISPLAY*`/`FRAME_1`/`ZBUF_1`).
  Config keys `ScreenXOffset/ScreenYOffset` exist.
* `PMODE` uses both read circuits (code writes DISPFB1/2 and DISPLAY1/2): circuit blending is used for the "Screen Warp
  Effect" / "Warp Framebuffer, Warp Texture" feature and presumably fades - the PC presenter must support
  two-circuit alpha blending of two framebuffers, or the warp must be emulated at a higher level.

## 6. GS features the game uses

Confirmed from code (A+D register traffic through `0x1d93c8` and DIRECT packets):

* two rendering contexts (`FRAME_1/2`, `TEST_1/2`, `ZBUF_1/2`, `CLAMP_1/2` set in pairs; context index in display+0x8c);
* **alpha test** (`TEST.ATE/ATST/AREF/AFAIL/DATE/DATM`), **depth test** toggled between `GEQUAL` and `ALWAYS`, **z-write mask**;
* **alpha blending** (`ALPHA_x` via `0x1d9b88`), `FBA`, `COLCLAMP`, `DTHE`/`DIMX` (dithering matrix), `TEXA`, `PRMODECONT`, scissor, XYOFFSET;
* **fog**: per-vertex FOG (`FGE` in PRIM), `FOGCOL`; fog distances configured in the game's "Min/Max Fog Depth", "Fog Color at Zenith/Horizon/Bottom";
* textured primitives: `TEX0/TEX1/TEXFLUSH/CLAMP`, bilinear/trilinear modes (strings `Bilinear`, `Trilinear`,
  "Only upload used mips", "Landscape Texture Resolution" -> mipmaps via `MIPTBP`, `TEX1.MXL/L/K`);
* PRIM types: SPRITE (2D, `DrawPrim(6,..)`), TRISTRIP and TRIFAN (VU output); points/lines not seen;
* PATH2 `DIRECT` for state, PATH3 for image/texture transfers (with `MSKPATH3` around VU1 batches);
* GS->EE readback (BUSDIR) for screenshots; framebuffer used as a texture ("Warp Framebuffer", "Screen Brightness Texture",
  "Heatmap Texture (8bit)", "Procedural Texture with Hues" suggest **8-bit indexed (PSMT8) + CLUT** and render-to-texture).

**[unknown] statically:** the exact texture pixel formats (PSMCT32/24/16, PSMT8, PSMT4 and which use CLUT), because texture
`TEX0/BITBLTBUF` words come from texture-cache data and prebuilt GIF packets rather than literals. The 8-bit palette
strings strongly hint at PSMT8/PSMT4 + CSM1 CLUTs. A one-shot GS-packet logger (see 8) will settle this in minutes.

## 7. Strategy: (A) low-level GS/VU1 emulation vs (B) high-level replacement

### Option A - VU1 interpreter/recompiler + GIF parser + GS renderer (mini-PCSX2)

Pros
* **The game talks to the hardware only through a handful of well-defined paths** (VIF1 chains, GIF chains, GS priv regs, SPR,
  IPU). Everything above is ordinary recompiled EE code that needs no changes.
* The microprogram is small, static and branch-simple: interpretation is ~60 opcodes, <1000 lines; or run `vu1_micromem.bin`
  through a static VU->C++ recompiler (no indirect jumps) for speed.
* Bit-exact feature coverage for free: clipper, fog, lighting, UV warp grid, depth-alpha, instancing - **nothing to reverse
  engineer or re-write**, and all game-side quirks (state in the PRIM shadow, MSKPATH3 ordering) just work.
* Handles the unknowns (texture formats, framebuffer-as-texture, warp, screenshots, FMV blit through GS) without having to
  discover them first.

Cons
* Needs a decent GS implementation: local memory (4 MB, PSMCT32/24/16/16S, PSMT8/4/8H/4HL/4HH swizzling, CLUT0/1 CSM1/CSM2,
  TRXDIR transfers incl. local->local and host->local), two contexts, alpha/test/fog/dither/scissor, sprite + strip + fan.
* A hardware-accelerated GS back end that must be correct with aliased local memory (framebuffer reused as texture) is the
  hard part (that is most of PCSX2's complexity).
* CPU cost of the VU1 interpreter (modest for this game) and of a software rasterizer (640x448, two circuits: fine on a modern
  PC, but no upscaling).

### Option B - high-level replacement (hook the render functions, draw meshes with D3D11/Vulkan)

Pros: native resolution/widescreen, texture replacement, trivially fast, no GS memory emulation.

Cons (specific to this game, from the analysis above)
* The render API is **not a mesh API**: data reaches VU1 as UNPACKed *batches* of packed vertex/index arrays, flag words and
  GIF tag templates assembled across ~15 emitter functions (1ebd60..1ef1c8) with state carried in A+D streams and a
  PRIM shadow; you would have to re-implement the vertex/index decoding, vertex-cache semantics, lighting, UV-warp grid, fade/cull
  and clipping rules - i.e. re-write the microprogram plus its EE packer in C++.
* A second, independent renderer exists (the "core" path: EE-side VU0-macro transforms writing GIF prims, ~8 variants of
  vertex formats in 0x1da260-0x1dd2d0 plus all 2D/HUD, particles, landscape tiles in 158148/161a78/1626a8) which must also be
  hooked, plus screen warp / framebuffer feedback / palette tricks / `DISPFB` tricks.
* Texture management is GS-memory based (`Texture Cache`, mip upload, CLUT upload, "Force Texture uploads"): you must hook the
  upload and reconstruct textures from EE memory + TEX0 state anyway.
* Much larger reverse-engineering effort and many game-specific bugs; no automatic correctness oracle.

### Recommendation

**Do A, in two stages:**
1. **A0 (bring-up):** MMIO shim -> VIF1/GIF packet processors -> VU1 interpreter (implement MSCAL/MSCNT/MSCALF, E-bit, XGKICK
   *synchronously* copying the packet out of VU memory at kick time, integer-backup branch rule from 3.6) -> GIF parser -> GS
   **software rasterizer on full emulated GS local memory**, presented through a D3D11/Vulkan texture + two-circuit PMODE blend.
   Correct first; get the intro/menus/game running; log every GS packet.
2. **A1 (speed/quality):** keep VU1 + GIF front end; replace the rasterizer by a hardware renderer that batches the (fairly
   regular) TRISTRIP/TRIFAN/SPRITE output with a GS-memory-backed texture cache (key = TBP0/TBW/PSM/CLUT, CPU/GPU "dirty" tracking
   as in PCSX2), resolution scale, and render-target aliasing only for the few feedback effects. Optionally add a thin
   option-B layer later (hook `0x1485c8`-style entry points) for widescreen/camera tweaks.

Interim pragmatic tip for A0: because the microprogram only emits `{ST,RGBAQ,FOG,XYZ2}` vertices with `PRIM` from the shadow, the
GIF parser can feed a *vertex-level* hardware renderer very early (before a full GS memory model exists) as long as all
textures are first uploaded to emulated local memory by the PATH3/IMAGE transfers.

## 8. Next steps / things to verify at runtime

1. GS packet/GIF logger in the runtime: dump every `TEX0/TEX1/CLAMP/ALPHA/TEST/FRAME/ZBUF/FOGCOL/MIPTBP` write and every
   `BITBLTBUF.DPSM` to settle texture formats, CLUT usage and display geometry (section 5/6 unknowns).
2. Log the VIF1 chain: confirm that every `MSCAL` uses 0x0000 or 0x26e0 and count `MSCNT`s per packet to validate the E-ladder
   assumption in 3.2.
3. Compare the VU1 interpreter against PCSX2's on identical packets (golden XGKICK dumps).
4. Confirm the channel-descriptor layout around 0x4e9b70-0x4e9bd8 by dumping it after `sceDmaReset` (4af9b0 area).
5. Identify the FMV path (IPU `sceIpu*` in 0x1767a0-0x177828, PSS demux) - needs an MPEG-2 software decoder on PC.
6. The SPR/DMA aliasing rules (0x70000000 vs 0x80000000, 0x2.../0x3... uncached aliases) must be in the memory model before
   any DMA is run.
