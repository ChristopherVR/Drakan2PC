# GS / GIF capture format (`DRKGSCAP` v1)

Produced by the PS2Recomp runtime (`ps2xRuntime/src/lib/gs/gs_capture.cpp`). It records every GIF packet at the point where
the arbiter hands it to the GS, in the order the GS sees it, plus the GS privileged registers.

## Enabling

```
set PS2X_GS_CAPTURE=600            # number of vsync frames to record (unset / 0 = off)
set PS2X_GS_CAPTURE_DIR=...        # optional, default ..\..\captures relative to the working dir (= decomp/build/captures)
build\recomp_out\bin\drakan.exe ..\..\..\iso\SCES_500.06
```
File name: `gscap_YYYYMMDD_HHMMSS.bin`. Recording stops automatically after N frames (the file is flushed at every frame).
`decomp/analysis/scripts/gscap_dump.py <file> [-v]` prints a summary (PMODE/DISPFB changes, packet kinds).

## Where packets come from

`GifArbiter::drain()` (`ps2_gif_arbiter.cpp`) is the single choke point: PATH1 (VU1 XGKICK), PATH2 (VIF1 DIRECT/DIRECTHL) and PATH3
(GIF DMA channel 2, incl. IMAGE data) are queued and drained in priority order (DIRECTHL cannot preempt a PATH3 IMAGE transfer).
The capture sees the packets after this arbitration, i.e. exactly what the software GS processes. Each packet is the raw
quadword stream: one or more GIF tags with their data (PACKED / REGLIST / IMAGE), `sizeBytes` is a multiple of 16.
(Note: in the first recordings of the game only PATH3 / PACKED A+D packets occur, no PATH1/PATH2, no IMAGE transfers yet.)

## Layout (little endian)

```
file header, 32 bytes
   char     magic[8]      "DRKGSCAP"
   u32      version       1
   u32      reserved      0
   u64      startUnixMs   wall-clock time of the start
   u64      reserved      0
records, until EOF
   u32 type
   u32 payloadSize        bytes following this 8-byte record header
   u8  payload[payloadSize]
```

| type | name | payload |
|---|---|---|
| 1 | FRAME_START | `u64 frameIndex` (0-based), `u64 vsyncTick` (tick at which the frame began; 0 for frame 0) |
| 2 | GIF_PACKET | `u8 path` (1, 2 or 3), `u8 flags` (bit0 = PATH2 DIRECTHL, bit1 = PATH3 image packet), `u16 0`, `u32 sizeBytes`, then `u8 data[sizeBytes]` |
| 3 | PRIV_REGS | 20 x `u64`: PMODE, SMODE1, SMODE2, SRFSH, SYNCH1, SYNCH2, SYNCV, DISPFB1, DISPLAY1, DISPFB2, DISPLAY2, EXTBUF, EXTDATA, EXTWRITE, BGCOLOR, CSR, vsyncTick, IMR, BUSDIR, SIGLBLID |
| 4 | VSYNC | `u64 vsyncTick` - end of the frame (vertical blank) |

Ordering rules
* A frame is everything between `FRAME_START n` and the following `VSYNC`. The next `FRAME_START n+1` follows immediately.
* `PRIV_REGS` is written (a) before the first GIF packet and before any packet that follows a change of any register except
  CSR/vsyncTick, and (b) once at the end of every frame (before `VSYNC`). So the register state in force for a packet is the
  most recent `PRIV_REGS` record before it. Register writes that happen between two packets and are changed back are not seen.
* CSR (field/vblank bits) and vsyncTick change continuously; they are included for reference only.
* Interlace/field: the game flips DISPFB1/DISPFB2 between 0x1000 and 0x1080 (page units of 2048 words) every second vsync; frames
  alternate odd/even field (`CSR` bit 13).

Register bit layouts are the standard GS ones (DISPFBx: FBP[0:8], FBW[9:14], PSM[15:19], DBX/DBY; DISPLAYx: DX, DY, MAGH, MAGV, DW, DH;
PMODE: EN1/EN2, CRTMD, MMOD, AMOD, SLBG, ALP).

## GIF data inside packets

Standard GIF tag (128 bit): `NLOOP[0:14] EOP[15] PRE[46] PRIM[47:57] FLG[58:59] NREG[60:63]`, `REGS` (64-bit, 16 x 4-bit descriptors).
FLG 0 = PACKED, 1 = REGLIST, 2 = IMAGE (NLOOP qwords of raw image data), 3 = disabled. In PACKED mode with descriptor 0xE the
quadword is an A+D pair (data u64, then register address in the low byte of the second u64). `gscap_dump.py` contains a small
parser (`parse_gif`) and the GS register name table.

## What the first captures contain (Drakan, boot to idle)

* 600 frames: 2.4k packets / 6 MB, all PATH3, all PACKED; per frame ~256 sprite vertices (`PRIM` 6 = sprite, ST/RGBAQ/FOG/XYZ2
  per vertex) plus FRAME_1/ZBUF_1/TEST_1/ALPHA_1/FOGCOL setup: a full-screen black fill. PMODE=0x8023, SMODE2=1 (interlaced
  FIELD mode), DISPLAY1 DW=0x9ff (2560 px / 4 = 640), DH=0x1ff (512 lines), page flip between DISPFB 0x1000 and 0x1080.
* No texture upload (no IMAGE, no BITBLTBUF/TRXDIR) and no PATH1/PATH2 traffic yet, because the game is still before its first
  real scene (see docs/status/recomp-status.md).
