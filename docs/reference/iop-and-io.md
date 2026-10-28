# Drakan: The Ancients' Gates (PAL, SCES-500.06) - IOP modules, SIF RPC and file I/O

Scope: what the IOP side does, how the EE talks to it, how game data is read, and a concrete
high-level-emulation (HLE) plan for the PC runtime. Everything here was derived from
`iso/SCES_500.06` (EE ELF), `iso/IOP/**`, the ISO directory listing and the scripts in
`analysis/scripts/iop_*.py`. Confidence is marked **[V]** verified (cross-checked from both sides or by a
script), **[I]** inferred from disassembly, **[?]** open question.

Conventions: EE addresses are virtual (ELF base 0x100000). IOP addresses are module-relative `.text`
offsets (relocatable IRX, base 0). `RSRCIOP(disc)` = `iso/IOP/MODULES/RSRCIOP.IRX` (36652 bytes, debug build
with error strings, offsets used by `analysis/iop/RSRCIOP.asm`); `RSRCIOP(embedded)` = the build the game
really loads (35136 bytes, `analysis/iop/RSRCIOP_embedded.asm`). See 1.2.

---------------------------------------------------------------------------------------------------

## 0. Key findings (TL;DR)

1. The EE does **not** load the IRX files from `\IOP\MODULES\`. It reboots the IOP with
   `cdrom0:\IOP\IOPRP23.IMG` (stock Sony SDK 2.3 kernel replacement image, 16 ROMDIR modules) and then loads
   **seven modules from memory** (`sceSifLoadModuleBuffer`) out of a bundle stored in
   `\<LANG>\LEVELS.RRC` (XOR-type cipher + zlib; decryptor reimplemented in `iop_rrc.py` [V]). The six Sony
   modules in that bundle are byte-identical to the files on disc; **`RSRCIOP` is a different, smaller
   (release) build** than the disc file [V].
2. Only one custom IOP module exists: `RSRCIOP` ("RsrcLoader" v1.1). It registers **three SIF RPC servers**:
   `0x1201` File Command, `0x1202` Resource Request, `0x1203` Sound Command, and itself calls back into
   **EE-registered servers** `0x1301`, `0x1302`, `0x1303` (+ optional debug server `0x1401`) [V]. It implements its own DVD file layer
   (`sceCdSearchFile`/`sceCdRead`), the SRSC resource-container index lookup, DMA to EE memory **and direct
   DVD -> SPU2 RAM uploads** (libsd), plus a batching layer for `sceSdProcBatch`.
3. Everything else is stock Sony: `sdrdrv` (server `0x80000701`, callback client `0x80000704`), `libsd`,
   `padman` (`0x80000100/101`), `mcserv`/`mcman` (`0x80000400`), `sio2man`. The EE uses the SDK libraries
   `libpad`, `libmc`, `libsdr`, `libcdvd`, fio, loadfile.
4. All game data lives in **SRSC containers** (`SRSC` magic; format fully decoded, 14-byte index at the
   end, `iop_srsc.py`) addressed by (type,id,sub). Paths are given to the IOP with long names ("Level 5 -
   Shiverbane"); the IOP converts each component to ISO-9660 8.3 with a deterministic rule
   (`iop_mangle.py`, 111/111 dependency paths + 14/14 levels validated [V]).
5. HLE recommendation: hook at the **EE SDK boundary** (`sceSifBindRpc/CallRpc/RegisterRpc/RpcLoop`, plus
   `scePad*`, `sceMc*`), implement three native services (File, Resource, Sound) + libsd/SPU2 + pad + memory
   card. No IOP CPU needed. A fallback (run the original IRXs in `tools/PS2Recomp/ps2xIOP`) is described in 7.8.

---------------------------------------------------------------------------------------------------

## 1. IOP modules

### 1.1 Per-module table

`.iopmod` name/version and import tables parsed by `analysis/scripts/iop_irxinfo.py`
(raw output: `analysis/iop/irx_info.txt`; full disassembly with annotated import calls: `analysis/iop/*.asm`).

| File | .iopmod name | Ver | Bytes (disc / embedded) | Loaded by game? | RPC servers it registers | Notes |
|---|---|---|---|---|---|---|
| IOPRP23.IMG | (ROMDIR, 18 entries) | - | 230329 | yes, `sceSifRebootIop` | CDVDFSV, LOADFILE, FILEIO, SIFCMD ... | SDK 2.3 IOP kernel replacement: LOADCORE 2.1, SIFCMD 2.7, SIFMAN 2.1, THREADMAN 2.2, IOMAN 2.1, MODLOAD 2.3, FILEIO 2.10, CDVDMAN 2.21, CDVDFSV 2.21, LOADFILE 2.1, TIMEMANI, ROMDRV, EESYNC, SYSCLIB, STDIO (extracted to `analysis/iop/ioprp_modules/`) |
| SIO2MAN.IRX | sio2man | 2.4 | 6161 / 6208 | yes (bundle #0) | - | stock; imports loadcore, intrman, dmacman, thbase, thevent |
| PADMAN.IRX | padman | 4.2 | 43861 / 43904 | yes (#1) | `0x80000100`, `0x80000101` | stock libpad server; imports sio2man, vblank, sifcmd |
| LIBSD.IRX | Sound_Device_Library | 1.4 | 26301 / 26304 | yes (#2) | (exports `libsd`, 28 slots) | stock SPU2 driver |
| SDRDRV.IRX | sdr_driver | 4.1 ("SDR driver version 4.0.1") | 6345 / 6400 | yes (#3) | `0x80000701`; calls EE `0x80000704` | stock libsd remote |
| MCMAN.IRX | mcman | 2.26 | 90533 / 90560 | yes (#4) | - (exports `mcman`) | stock |
| MCSERV.IRX | mcserv | 2.14 | 7353 / 7360 | yes (#5) | `0x80000400` | stock |
| **RSRCIOP.IRX** | **RsrcLoader** | 1.1 | **36652 / 35136** | yes (#6, last, with args `RDT`, `\<Language>`) | **`0x1201`, `0x1202`, `0x1203`** | custom Surreal module, see section 3 |
| PANICSYS.IRX | System_Panic_Reporter | 1.1 | 3465 / - | **no** (not in bundle, not referenced) | - | SDK leftover |

Bundle order = load order = #0..#6 above (`analysis/iop/embedded/mod0..6.irx`, decrypted bundle
`iso/sample/LEVELS.RRC.dec`). The disc copies of the six Sony modules are `cmp`-identical to the bundle ones
(up to 16-byte padding).

### 1.2 RSRCIOP: disc build vs embedded build

Same architecture, same server table (disc: `.data` @0x6374; embedded: @0x5f18), same wire formats; the
embedded one has no `printf` strings and different offsets. Key entry points:

| | disc build | embedded build (**authoritative**) |
|---|---|---|
| entry (`_start`) | 0x17d0 | 0x16d0-area (`.iopmod` entry 5840 = 0x16d0) |
| server table (name, SID, handler, buffer) | 0x6374: ("Resource Request",0x1202,0x2d80), ("File Command",0x1201,0xc38), ("Sound Command",0x1203,0x4a68) | 0x5f18: (0x1202,0x2c20), (0x1201,0xc38), (0x1203,0x4848) |
| RPC thread body | 0x1a58 (thread arg 0/1/2 selects row of table) | same logic |
| imports | ioman 4,5,6,8; sifcmd 14-21; cdvdman 4,6,8,10,11,13,37,75; libsd 15,16,17,19,26; sysmem, intrman, thbase, thsemap, sysclib | identical set |

Useful when reading disc build error strings: "ERROR: Unable to initialize the file I/O system!",
"I/O ERROR %d while reading cluster (file ID %d)!", "ERROR: Resource %#04x/T:0x%x (%#08x) has size ZERO!",
"!! Queue full, waiting for availability !!", "Unable to bind to EE SIF-RPC Handler".

### 1.3 IOP-side library imports that matter for HLE

* `cdvdman` 4 `sceCdInit`, 6 `sceCdRead`, 8 `sceCdGetError`, 10 `sceCdSearchFile`, 11 `sceCdSync`, 13
  `sceCdDiskReady`, 37 `?` (media change/ spindle control, called with 0 before a search), 75 `sceCdMmode`
  (arg 2 when RSRCIOP is started with `RDT` = **DVD mode**; this is a DVD-9 image, 3.4 GB).
* `ioman` open/close/read/lseek: used for paths beginning with `cdrom`/non-raw files.
* `libsd` 15 `sceSdProcBatch`, 16 `ProcBatchEx`, 17 `sceSdVoiceTrans`, 19 `VoiceTransStatus`, 26
  `SetTransIntrHandler`.
* `sifcmd`: RegisterRpc/SetRpcQueue/GetNextRequest/ExecRequest (server), Bind/Call/CheckStat (client),
  `sifman` SetDma/DmaStat (IOP -> EE DMA of file data).

---------------------------------------------------------------------------------------------------

## 2. EE boot sequence and module loading [V]

Call chain (all EE addresses): `crt0` calls `main` @0x101400 (via `jal 0x101400` at 0x1000c4):

1. `0x101db8` **ReplaceIopImage(path)** with `"cdrom0:\IOP\IOPRP23.IMG"` (string @0x519550): `sceCdInit(0)` (0x4b8af8),
   `sceCdMmode(2)` (0x4b8fd0, DVD), `sceSifInitRpc`, `sceSifRebootIop` (0x4c3f00), spin on `sceSifSyncIop`
   (0x4c3ec8), re-`sceSifInitRpc`, `sceCdInit`, `sceCdMmode(2)`, reset loadfile/fio client state
   (0x4c3190 / 0x4bff60).
2. `0x1015d8` **SelectLanguage**: `sceScfGetLanguage` (0x4bae48) -> table @0x519570 -> index into
   {English, German, French, Italian, Spanish} (names @0x51d55c, stride 0x24). Result stored at 0x4deAD0.
   (scf 1=EN, 2=FR, 3=ES, 4=DE, 5=IT; anything else -> English.) The PC build should just pick a language.
3. `0x101710` **LoadIopModules**:
   * path = `sprintf("%s\%s", "cdrom0:", langname)` upper-cased, then the obfuscated suffix constant @0x5195a8
     (`"-7?NN*OHARAHX"`: reverse + 4 per byte) = `"\LEVELS.RRC;1"`; so
     `cdrom0:\ENGLISH\LEVELS.RRC;1`.
   * `sceOpen` (0x4bff98) / `sceLseek` (0x4c03a0) / `sceRead` (0x4c05d8) / `sceClose` (0x4c0220) read the whole
     file into IOP heap (`sceSifInitIopHeap` 0x4c2d00, `sceSifAllocIopHeap` 0x4c2d88) via a bounce buffer.
   * `0x1e5668(buf,len,key,seed)` decrypts in place; key = 37 bytes @0x4e9a78, seed = file size; the key
     itself is "mutated" once by `0x1e5618` before use (`k[i] += n + (n>>8), n = 0x17*(i+1)`); this is an
     anti-tamper style guard, keep both. Then zlib (`inflateInit_` 0x4a75b0 with "1.1.3", `inflate`
     0x4a6d90, `inflateEnd` 0x4a7410). Result = `u32 count(7); u32 off[7]` (+pad to 0x20) followed by 7 IRX ELFs
     (`iop_rrc.py` produces `LEVELS.RRC.dec`, `iop_bundle.py` splits it) [V: decrypt+inflate succeeds,
     modules are valid ELF with correct `.iopmod`].
   * for modules 0..5: `sceSifLoadModuleBuffer(iopaddr + off, 0, NULL)` (0x4c3798). For the last (RSRCIOP): args
     built by `0x101e40` = `"RDT\0" + "\" + <Language>` (e.g. `RDT\0\English\0`, length returned = strlen+5) ->
     RSRCIOP sees argv[1]=`RDT` (sets DVD-mode flag, gp-0x7fa8) and argv[2]=`\English` (language root dir,
     copied to a 0x6334 buffer in the disc build). [V from RSRCIOP init @0x17d0.]
   * `sceSifFreeIopHeap` (0x4c2ef0).
4. `sceMcInit` (0x4b95b0, called at 0x101534), pad init later (`0x1e37d8` game pad manager), `sceSdRemoteInit`
   (0x4b9090) and the game sound/resource subsystems:
   * Resource client init **0x1efdc0**: creates 3 EE threads (stack 0x2000, priority table), each runs
     `0x1f11e8` = `GetThreadId; sceSifSetRpcQueue; sceSifRegisterRpc(sid,func,buf); sceSifRpcLoop`, for
     SIDs **0x1301** (handler 0x1f0110), **0x1302** (0x1f0550), **0x1303** (0x1eff90); then
     `BindRpc(0x1201)` and `BindRpc(0x1202)` through the game wrapper `0x4af310`.
   * Debug server **0x1e35b0** (SID 0x1401, handler 0x1e3528) is created earlier in `main`.
   * Sound client init **0x1e5768**: `sceSdRemote(Init=0x8000)` through `0x4af4f0`, `BindRpc(0x1203)`, creates
     the sound thread.

---------------------------------------------------------------------------------------------------

## 3. RSRCIOP in detail

IOP threads (priority 0x32-0x36): main/loader (0x2fbc disc), sound (0x48b0), completion notifier (0x598, arg
= struct @0x6100), stream/callback pump (0x39d8), and three RPC server threads (0x1a58, arg 0/1/2).

All payload offsets below are relative to the RPC buffer (`data`). RPC handlers have the standard
signature `void* f(u32 fno, void* data, int size)` and return a pointer to the reply buffer.

### 3.1 SID 0x1201 "File Command" (handler 0xc38) [V both sides]

Client: game class `File` (ctor/bind `0x4ae4e8`, `Open` `0x4ae448`, other methods in `0x4adc00-0x4ae440`
via vtable). Calls through the locking wrapper `0x4af5d8(client,fno,mode,send,ssz,recv,rsz)`.

| fno | Name | Send | Reply | Notes |
|---|---|---|---|---|
| 0 | open | `{u32 isWrite (mode==2), u32 0, char path[0x100]}` (size 0x10c; path strncpy 0xff) | `{s32 handle_or_err, u32 fileSize}` (8 bytes) | path is a *long* name, see section 5.3. Write mode creates a file via ioman (0x202). Err -7 = no free handle. IOP reads the SRSC header+index on open (0x20b8) so the file is immediately usable for Resource Requests. |
| 1 | close | `{s32 handle}` | `s32` (0 ok, -4 bad handle) | |
| 2 | read | `{s32 handle, u32 eeDestAddr, u32 size}` (0xc) | `s32 bytesRead` | data is DMA'd to `eeDestAddr` with `sceSifSetDma`; reads are clamped to file size; sector (2048) cache inside. EE aligns requests (`&~0x7ff` seeks). |
| 3 | (no-op) | - | 0 | |
| 4 | lseek | `{s32 handle, s32 off, s32 whence}` | `s32 newPos` | **quirk:** whence==2 returns `size - off` (offset subtracted), whence 1 = cur+off |
| 5 | mem info | - | `u32` | `QueryFreeMemSize - (sysmem #8)`; diagnostic only |

HLE: trivial `pread` on the resolved file (5.4).

### 3.2 SID 0x1202 "Resource Request" (handler disc 0x2d80 / embedded 0x2c20) [V structure, I semantics]

One asynchronous call per **batch** (`sceSifCallRpc` mode 1 = no-wait, EE `0x1f0e34`, buffer @0x56c2c0,
`count*0x1c` bytes). **`fno` = the IOP file handle** (the value returned by File Command open) [V: IOP request
+0xc is fed to the file-table lookup]. No reply is used; completion comes later through server 0x1302.

Wire entry (28 bytes), EE writer at `0x1f0d84-0x1f0dd8`, IOP reader `0x2dd8-0x2f14`, validation `0x844`:

| off | size | meaning |
|---|---|---|
| 0x00 | u32 | cookie: EE pointer to the request object; echoed back in completion (`0x1302`) |
| 0x04 | u32 (low 16 used) | flags. bits 0-3 = **destination type**: 0 = EE memory (DMA), 1 = **SPU2 RAM** (uploaded with `sceSdVoiceTrans`), 2 = copy into an IOP-side buffer (rare). bit 0x80 = "EE allocates destination": IOP asks the EE via SID 0x1303 for the buffer. Post-processing bits for SPU ADPCM data: 0x20 = set loop-start flags (`|= 0x600`) on the first 16-byte block; 0x10 = set loop-end/repeat (`|=0x300`) on the last block; 0x40 = clear those bits on the last block and zero the 0x7777 sentinel block |
| 0x08 | u16 | resource **type** (key 1 of the SRSC index) |
| 0x0a | u16 | resource **id** (key 2) |
| 0x0c | s16 | resource **sub**/instance (key 3) |
| 0x0e | u16 | extra/aux (stored, not interpreted in traced paths) |
| 0x10 | u32 | byte offset inside the resource (added to the index entry's offset) |
| 0x14 | u32 | requested length (clamped to the entry size; > 0x40800 => error -9) |
| 0x18 | u32 | destination address (EE physical address for type 0 unless 0x80 set; SPU2 RAM byte address for type 1) |

IOP behaviour (disc `0x2d80`/`0x844`, thread `0x2fbc`): look up `(type,id,sub)` in the file's in-memory
index (entries sorted by (type,id), 12 bytes each: `u16 type,u16 id,u32 off,u32 size`, converted from the 14-byte
on-disk index), compute LSN = fileLSN + (off>>11), sector-aligned read through the cluster cache (reads
with `sceCdRead`), then deliver per destination type, then enqueue `{cookie, status}`
(`0x4dc`) for the completion thread. Error status = negative IOP code (e.g. -1 resource not found, -9 too big).

HLE equivalent: `for each entry: find(type,id,sub) -> (off,size); bytes = file[off+reqOff : +min(len,size)];
write to guest RAM / SPU2 RAM; post {cookie, 0}`. Complete requests *in order* from a worker thread.

### 3.3 SID 0x1203 "Sound Command" (handler disc 0x4a68 / embedded 0x4848) [V structure, I fno 1/2]

Client: game sound player (init `0x1e5768`; command ring flushed by `0x1e6484`; fno 2 call at `0x1e5d9c`).

| fno | Send | Action |
|---|---|---|
| 0 | array of `sdBatch` records (8 bytes: `u16 func, u16 entry, u32 value`), `size/8` records | `sceSdProcBatch`. Records are fed to libsd in groups; whenever a KON/KOFF switch write (entry high byte 0x15/0x16 on a core) is repeated for a core, the batch is flushed and 50 us is waited ("voice on/off ordering throttle"). `func` values are the stock libsd ones (1 SETPARAM, 2 SETSWITCH, 3 SETADDR, 4 SETCOREATTR, 5 WRITEIOP, 6 WRITEEE, 7 EERETURN, 0x11 GETPARAM, 0x12 GETSWITCH, 0x13 GETADDR, 0x14 GETCOREATTR; table @LIBSD .data 0x4050) |
| 1 | `{u32 spuAddr, u32 eeAddr, u32 size}` | **SPU2 RAM -> EE readback**: IOP buffer = AllocSysMemory(size); `sceSdVoiceTrans(ch1, mode READ, buf, spuAddr, size)`; wait; DMA IOP->EE at `eeAddr`. [I: confirm direction by logging; could be used for waveform/IRQ-position tricks] |
| 2 | `{u32 srcSpu, u32 dstSpu, u32 size}` | SPU2 RAM -> SPU2 RAM copy (read to IOP buffer, then write back) |

The EE stores pending sdBatch records in an object ring at `obj+0x200` (max 64 entries) and flushes with fno 0
(`0x1e64bc`, `size = count*8`).

### 3.4 EE-side servers RSRCIOP calls (IOP is the client) [V]

| SID | EE handler | IOP caller (client struct) | Call | Meaning |
|---|---|---|---|---|
| 0x1302 | 0x1f0550 | completion thread (disc 0x598/0x5e8, client in struct@0x6100+0x200) | `CallRpc(client, fno=count, mode 0 (blocking), data=count x {u32 cookie, u32 status}, size=count*8)` | resource request completions (max 64 queued; IOP waits 100 us if full). EE walks entries, calls the request's completion callback (virtual call through the request object), sets flag `0x80000000`, appends to its done list |
| 0x1301 | 0x1f0110 | stream pump (client @0x6fc0) | `CallRpc(client, fno=<EE request ptr>, mode 1 (nowait), data=0x10 bytes, size 0x10)` | streaming notification for a request (progress/chunk). `fno` is an **EE pointer** to the request object (the handler dereferences it). [I] |
| 0x1303 | 0x1eff90 | resource loader (client @0x6fe8) | `CallRpc(client, fno=<EE request ptr>, mode 0, send=recv=0x10 bytes)` when flags bit 0x80 is set | **EE buffer allocation callback**: IOP sends `{size rounded up to 4,...}`; reply word[1] = EE destination address (0 = fail). [I] |
| 0x1310 | 0x1e6208 (server created by the sound thread 0x1e6198; getter 0x1e62e0) | RSRCIOP sound thread (embedded 0x4690, client @0x5c70) | `CallRpc(client, fno 0, mode 0 (blocking), send 0xc0 bytes)` every 500 ms | **voice position snapshot**: before each call the thread runs `libsd ProcBatchEx(GETADDR entry 0x227e/0x227f = NAX of core 0/1, voice mask 0xffffff)` (2 records at 0x5830) and sends 2 x 24 u32 = current playback byte address of every voice (core 0 voices 0..23, then core 1). The EE handler copies them under a semaphore to 0x574760; the voice manager reads them (index = voice, +0x60 for core 1). Not an event; no reply used. HLE: poll `Spu2::getAddr(SD_VADDR_NAX|SD_VOICE(core,v))` every 500 ms and post it (runtime `soundPump`). |
| 0x1401 | 0x1e3528 (server created by `0x1e35b0`, called from main at 0x1014f0) | debug/error channel (client @0x60d0, bound with a 500 us timeout at 0x1700 / embedded 0x160c) | `CallRpc(client, fno=1 or 2)` before a fatal `break` / text report | optional: if the bind fails RSRCIOP just clears its "debug" flag (gp-0x7fb8). HLE: register the server or let the bind fail; never needed for normal operation |

All three servers are created *before* RSRCIOP binds: RSRCIOP retries `BindRpc` for ~2 s ("Unable to bind to
EE SIF-RPC Handler"). In HLE the EE servers exist from the first `sceSifRegisterRpc`, so nothing special is needed.

### 3.5 Stock servers

* **SDRDRV `0x80000701`**: dispatch on `(fno & 0xfff0)` @SDRDRV 0x3f4, args at `data+4,+8,...`, reply in a
  4-byte buffer @0xe20:

  | fno | libsd call | args (data offset: type) |
  |---|---|---|
  | 0x8000 | #4 sceSdInit | +4 u32 flag |
  | 0x8010 | #5 SetParam | +4 u16 entry, +8 u16 value |
  | 0x8020 | #6 GetParam | +4 u16 entry -> u16 |
  | 0x8030 | #7 SetSwitch | +4 u16, +8 u32 |
  | 0x8040 | #8 GetSwitch | +4 u16 -> u32 |
  | 0x8050 | #9 SetAddr | +4 u16, +8 u32 |
  | 0x8060 | #10 GetAddr | +4 u16 -> u32 |
  | 0x8070 | #11 SetCoreAttr | +4 u16, +8 u16 |
  | 0x8080 | #12 GetCoreAttr | +4 u16 -> u16 |
  | 0x8090 | #13 Note2Pitch | +4 u16,+8 u16,+0xc u16,+0x10 s16 |
  | 0x80a0 | #14 Pitch2Note | +4 u16,+8 u16,+0xc u16 |
  | 0x80b0 | #15 ProcBatch | +4 batch ptr (IOP), +8 rets ptr, +0xc num |
  | 0x80c0 | #16 ProcBatchEx | + 4 ptrs/num, +0x10 voice |
  | 0x80d0 | #17 VoiceTrans | +4 s16 ch, +8 u16 mode, +0xc iop addr, +0x10 spu addr, +0x14 size |
  | 0x80e0 | #18 BlockTrans | same 5 args |
  | 0x80f0 / 0x8100 | #19 VoiceTransStatus / #20 BlockTransStatus | +4 s16 ch, +8 s16 flag |
  | 0x8130 | #23 SetEffectAttr | core = fno & 0xf, attr struct in the RPC buffer |
  | 0x8140 | #24 GetEffectAttr | core = fno & 0xf, result in reply buffer |
  | 0x8150 | #25 ClearEffectWorkArea | +4 core,+8 channel,+0xc mode |
  | 0x8160 | #26 SetTransIntrHandler | +4 ch: installs a handler that raises callback code 0xb/0xc |
  | 0x8170 | #27 SetSpu2IntrHandler | installs handler that raises callback code 0xd (+arg) |
  | 0xe620 | creates+starts the callback thread (0x9b4): binds EE SID `0x80000704` |
  | 0x8f10 | internal helper (0x27c) |

  EE side: `sceSdRemoteInit` 0x4b9090, `sceSdRemote` 0x4b91d8 (does the sifDMA of batch buffers),
  callback thread start 0x4b938c (cmd 0xe620), callback server thread 0x4b9468 and dispatcher 0x4b94d0
  (codes 1..13 -> user callback slots in .bss @0x516f84-0x516f9c). The game's own thin wrapper is `0x4af4f0
  (cmd, a, b)` used by the sound player at 0x1e5784 (0x8000), 0x1e67a0 (0x8020), 0x1e67d4 (0x8040), 0x1e6838
  (0x8060).
* **PADMAN `0x80000100` / `0x80000101`**: stock libpad: PortOpen hands padman an EE buffer (64-byte aligned);
  padman DMAs pad state into it every vblank. Not needed in HLE (hook libpad at API level, 7.4).
* **MCSERV `0x80000400`**: stock; dispatcher @MCSERV 0x320 switches on fno 1..0x15 (+0xfe/0xff version
  queries). Game libmc functions (EE) and their fno: Init 0x4b95b0 (0xfe), GetInfo 0x4ba0e8 (1), Open 0x4b990c
  (2), Close 0x4b9a78 (3), Seek 0x4b9b30 (4), Read 0x4b9ca0 (5), Write 0x4b9db8 (6), Flush 0x4ba78c (0xa),
  ChDir 0x4ba448 (0xc), GetDir 0x4ba268 (0xd), SetFileInfo/Mkdir 0x4ba840,0x4baa1c (0xe), Delete 0x4ba664
  (0xf), Format 0x4ba59c (0x10), Unformat 0x4bab84 (0x11), GetEntSpace 0x4bac4c (0x12), plus 0x4b9794 (0x14) and
  0x4b9848 (0x15). Names follow ps2sdk's libmc numbering [I].

---------------------------------------------------------------------------------------------------

## 4. EE client-side function map

All are SDK or game functions in the recompiled ELF; use them for `stubs`/hooks. SIF core (libkernl):

| Address | Function | Evidence |
|---|---|---|
| 0x4be828 | sceSifInitRpc | called before every bind |
| 0x4bee98 | sceSifBindRpc(client,sid,mode) | 11 call sites with SIDs below |
| 0x4bf068 | sceSifCallRpc(client,fno,mode,send,ssz,recv,rsz,endfunc,efarg) | 107 call sites |
| 0x4bf298 / 0x4bf330 / 0x4bf748 | sceSifSetRpcQueue / sceSifRegisterRpc / sceSifRpcLoop | used by server thread 0x1f11e8 |
| 0x4bf524 / 0x4bf57c | sceSifGetNextRequest / sceSifExecRequest (inside RpcLoop 0x4bf740) | [I] |
| 0x4bc500 / 0x4bc520 | sceSifDmaStat / sceSifSetDma (syscalls 0x76/0x77) | stubs at 0x4bbd44-0x4bc5c4 are the syscall thunks (`li v1,n; syscall; jr ra`) |
| 0x4c3000 | loadfile bind (SID 0x80000006) | |
| 0x4c3798 | sceSifLoadModuleBuffer | used by boot, arg = IOP heap address |
| 0x4c3f00 / 0x4c3ec8 | sceSifRebootIop(img) / sceSifSyncIop | boot 0x101de0/0x101df0 |
| 0x4c2d00 / 0x4c2d88 / 0x4c2ef0 | sceSifInitIopHeap / AllocIopHeap / FreeIopHeap | SID 0x80000003 |
| 0x4bff98 / 0x4c0220 / 0x4c05d8 / 0x4c0848 / 0x4c03a0 | sceOpen / sceClose / sceRead / sceWrite / sceLseek (SID 0x80000001, fno 0..4) | used for LEVELS.RRC and screenshots (0x1d7d40 writes `ScreenShotNN.tga`) |
| 0x4b8af8 / 0x4b8fd0 | sceCdInit / sceCdMmode (libcdvd, SIDs 0x80000592-0x8000059a) | only used in the reboot path |

Game-level RPC wrappers:

| Address | Role |
|---|---|
| 0x4af310 | `RpcClient* Bind(sid)` - cached by SID in a list (head @0x51538c), takes a lock, prints "Unable to bind to IOP RPC function 0x%x!" (@0x553bb8) on failure |
| 0x4af5d8 | `int Call(client,fno,mode,send,ssz,recv,rsz)` - locks client semaphore, calls sceSifCallRpc with an iSignalSema end callback (0x4af488) |
| 0x4af4f0 | `SdrCmd(cmd,a,b)` - binds 0x80000701 on first use then `sceSdRemote(1,cmd,...)` |
| 0x4ae4e8 / 0x4ae448 | File ctor (bind 0x1201) / File::Open |
| 0x101310 | helper: construct File, Open(path,1), read whole file (used by DB/RRC loaders); other Open callers 0x12a850, 0x1f228c, 0x4a3868, 0x4a3c88 |
| 0x1efdc0 | resource subsystem init (servers 0x1301-0x1303 + bind 0x1201/0x1202) |
| 0x1f0a10-0x1f0e48 | resource batch submit (builds 28-byte wire entries, `Call` with mode 1) |
| 0x1f0110 / 0x1f0550 / 0x1eff90 | EE servers 0x1301 / 0x1302 / 0x1303 handlers |
| 0x1e5768 / 0x1e6484 / 0x1e5d64 | sound init (bind 0x1203) / sdBatch flush (fno 0) / fno-2 call |
| 0x1e5668 / 0x1e5618 | LEVELS.RRC decrypt / key guard |

Client binds seen (`jal 0x4bee98` with SID): 0x80000100, 0x80000101 (libpad, 0x4b0944 PadInit), 0x80000592,
0x80000593, 0x80000595, 0x80000597, 0x8000059a (libcdvd, 0x4b8xxx), 0x80000701 (0x4b9090 libsdr), 0x80000400
(0x4b95b0 libmc), 0x80000001 (fio 0x4bfd30), 0x80000003 (0x4c2d00), 0x80000006 (0x4c3000); via the game
wrapper: 0x1201, 0x1202, 0x1203.

> **Correction (see docs/reference/input.md):** Ghidra function entries are 4 bytes later than listed below: scePadInit 0x4b0948, scePadPortOpen 0x4b0ba8, scePadRead 0x4b0ef8, scePadGetState 0x4b0f78, scePadSetMainMode 0x4b1490, scePadSetActDirect 0x4b1548.

libpad entry points (EE) [V by arg patterns]: `scePadInit` 0x4b0944, `scePadPortOpen(port,slot,buf)`
0x4b0ba4 (called for port 0/1 with buffers at pad-mgr+0x500/+0x600), `scePadGetState` 0x4b0f70 (state 6 =
STABLE), `scePadRead(port,slot,buf32)` 0x4b0ef4, `scePadSetMainMode(port,slot,1,3)` 0x4b148c,
`scePadSetActDirect(port,slot,6 bytes)` 0x4b1548 (vibration). Game pad manager: init 0x1e37d8, per-frame
0x1e3970.

---------------------------------------------------------------------------------------------------

## 5. Game data and file access

### 5.1 How files are opened

* **Everything is read through the IOP** (`File Command` + `Resource Request`), not through `sceCdRead` from the
  EE. The EE never sees LSNs: the IOP resolves names with `sceCdSearchFile` and reads with `sceCdRead`.
  So **no EE-side file-to-LSN map is needed**; the PC runtime needs a *name -> bytes* service.
* The only EE-side file I/O is: `LEVELS.RRC` at boot (fio/ioman via loadfile RPC `0x80000001`), screenshot TGA
  (`sceOpen/sceWrite`), memory card (`libmc`), and a `host:%s` debug path (@0x41fd38).
* EE strings holding paths are *long names with Windows separators* and **no** language dir: e.g.
  `\Common\Interface\Interface` (@0x42a8a8), `Drakan 2.rrc` (@0x42b618), `LevelList.txt` (@0x420630), `%s/%s.rsg`,
  `.lvl` (@0x44f680); database dependency lists use `..\..\World Common\...`. Extension table @0x4dac20:
  `.odb .mod .txd .adb .sdb .ssd .xdb .vdb`.
* RSRCIOP prepends the language root (`\English`, arg 2) and applies the 8.3 mangler per component
  (5.3), appends `;1`. Exact prefix handling for `cdrom0:` paths is in disc `0x1ea0`; **[?] log the first
  ~100 `open` paths at runtime to confirm whether the EE sends `\Common\...` or `cdrom0:\...` forms** (HLE
  should accept both: strip a leading `cdrom0:`/`cdrom:`, strip `;1`, split on `\` and `/`).

### 5.2 Disc layout (926 files, 177 dirs; list them locally with `7z l <iso>`, never commit the listing)

```
SYSTEM.CNF  (BOOT2 = cdrom0:\SCES_500.06;1, VER 1.04, VMODE PAL)      SCES_500.06 (4.5 MB)
IOP\IOPRP23.IMG                       IOP\MODULES\*.IRX  (8 files, 0.5 MB total)
ENGLISH\ FRENCH\ GERMAN\ ITALIAN\ SPANISH\   (each ~685-693 MB; same structure)
   DRAKAN2.RRC  LEVELS.RRC  LEVELLST.TXT  AEON/DODO/MUSH/RIOT/SPOT/WYRM.HSD
   COMMON\{DEBUG,FX,INTERFCE,MAGIC,NPC,RESOURES,SOUNDS,SYSTEM}\<NAME>.{DB,ODB,MOD,TXD,ADB,SDB,SSD,XDB,VDB}
   LEVELS\LEVEL<n><xx>\LEVEL<n><xx>.{DB,LVL,MOD,ODB,SDB,SSD,TXD,XDB,ADB}  + LEVEL<n>TS.{DB,TXD}  (+ OUT_SPTS.DEF in LEVEL0RO)
   MUSIC\PS2MUSIC\PS2MUSIC.{DB,MOD,ODB,SDB,TXD,XDB}
   WORLDCON\{WORLDCON,MOUNTAON,DESERTON,PROCTEES}\*.{DB,MOD,ODB,TXD} (+MODELPHS.REF)
```
The five language trees are nearly independent copies (localised `*.SDB` speech, `INTERFCE.VDB` video/credits,
`*.XDB` text, `LEVELLST.TXT`, `.ODB`). A PC build needs only one language tree + the executable; ~689 MB
(English). Per-type volume (English): SDB 251 MB, VDB 219 MB, MOD 98 MB, TXD 72 MB, ADB 34 MB, LVL 13 MB.

### 5.3 8.3 name mangling (RSRCIOP disc 0x2398) [V]

Per path component: drop every char except `[0-9A-Za-z_.]`, upper-case; if the base (before the last `.`) is
longer than 8 chars, keep the first 6 and the last 2. Examples: `Interface`->`INTERFCE`, `Resources`->`RESOURES`,
`Level 5 - Shiverbane`->`LEVEL5NE`, `Mountain World Common`->`MOUNTAON`, `Drakan 2.rrc`->`DRAKAN2.RRC`. Implementation +
validation against all 111 DB dependency paths and 14 `LEVELLST.TXT` entries: `analysis/scripts/iop_mangle.py`
(`python -I iop_mangle.py` -> "unmatched 0"). Level dir naming (`LEVEL0RO, 1NA, 1OR, 1BS, 1ED, 1IR, 2RE, 3ES, 3NE, 3ON,
4LD, 4RA, 4ON, 5NE, 6EN, 8OS`) falls out of this rule; some levels have only `.LVL` + `LEVELnTS.TXD` (their `.DB`
points at shared databases).

### 5.4 Container: SRSC [V]

`analysis/scripts/iop_srsc.py`. All `.TXD .MOD .ODB .ADB .SDB .XDB .VDB .SSD .LVL` and `DRAKAN2.RRC` start with:

```
+0  'SRSC'  +4 u16 version=0x0101  +6 u32 index_offset  +10 u32 count  +14 u16 pad      (16-byte header)
data blocks start at 0x10 (8-byte aligned), index runs from index_offset to EOF: count x 14 bytes
index entry: u16 type, u16 id, u16 sub, u32 offset, u32 size       (offset+size <= index_offset, checked on all samples)
```
Resource *types* seen (high byte = class): `0x01xx` objects (ODB), `0x02xx` models/skeleton/geometry (MOD),
`0x0040` texture (TXD), `0x04xx` string/locale tables (`0x402` present in every file = per-file header/strings),
`0x05xx` animations/sequences (ADB) and XDB text (`0x400`), `0x03xx` level/sound-set (LVL/SSD). The per-file
names ("Old Text", "Arokh", "Spheres and Boxes") are inside type-0x402/0x400 payloads.
`DRAKAN2.RRC` additionally holds zlib-compressed payloads (`78 01` at 0x64). `LEVELS.RRC` is *not* SRSC
(encrypted, see 2).

### 5.5 File-type purposes

| Ext | Purpose (evidence) |
|---|---|
| `.DB` | text dependency list: `version 1 / dependencies N / <idx> <relative path to another DB>` (e.g. SYSTEM.DB -> Resources, FX, NPCs, Magic, Sounds, Music) - defines which sibling databases a level pulls in |
| `.ODB` | object database: entity/object definitions & behaviour parameters ("ODB Time" debug string, references `Drakan 2.rfl`) |
| `.MOD` | model database (geometry, skeletons: "Arokh", "Rynn", "Snow Dwelling/Towers") |
| `.TXD` | texture database ("Riot Engine Texture Database", 64/128 px bitmaps, palettes) |
| `.ADB` | animation / sequence database ("Opening", "shrug2ff") |
| `.SDB` | sound database: ADPCM/wave banks (SOUND.SDB 51 MB, PS2MUSIC.SDB 48 MB music, per-level/NPC speech); names like "Music Combat loops", "Creepy Sounds" |
| `.SSD` | per-level sound scripts (SRSC types 0x310/0x311/0x312, e.g. "0.01 Intro Stomp"; 13-100 KB; only in the `LEVELS\LEVEL*` trees, none for the COMMON sets) |
| `.XDB` | text/string database ("Old Text" entries = localised text, subtitle strings, music file names) |
| `.VDB` | FMV: INTERFCE.VDB has "Logos / Sony & Surreal Logo Intro_Pal", 512x264, a cumulative per-frame size table then frame data; not plain MPEG start codes -> probably IPU macroblock streams (string "Initializing IPU library...") **[?]** |
| `.LVL` | level data (name "Shiverbane", spawn/sector tables) |
| `.DEF` / `.REF` | texture definition text (`OUT_SPTS.DEF`), model-source reference list (`MODELPHS.REF`), dev leftovers |
| `.HSD` | 11712-byte high-entropy blobs per creature/"RIOT" (not the LEVELS.RRC cipher, not zlib); purpose unknown **[?]** (hit-sphere/skeleton data?) |
| `.RRC` | `DRAKAN2.RRC`: compressed SRSC with 128x128 texture-like first block; `LEVELS.RRC`: IOP module bundle (2) |
| `LEVELLST.TXT` | `\Levels\<Name>\<Name>.lvl` x14 (mangled via 5.3) |

Samples and decrypted artefacts: `iso/sample/**` (ENGLISH tree subset, `LEVELS.RRC.dec`, `INTERFCE.VDB`).

### 5.6 Memory-card / save data

Folder `BESCES-50006` (@0x424668/0x44c2c8) and `BESCES-50006Settings` (@0x44c2d4), files `%s/%s.rsg` (save game),
`%s/%s.rlt` (level state), `%s/icon.sys`, `FolderIcon.ico`, `%s/*.cfg` (settings). Accessed with libmc through
the `File` class (mc backend; strings "!! ERROR %d returned from sceMcRead!").

---------------------------------------------------------------------------------------------------

## 6. Existing runtime assets

`tools/PS2Recomp` already contains `ps2xIOP` (R3000 interpreter + virtual IOP kernel; generic HLE of `MCSERV
0x80000400/480` and `LIBSD 0x80000701`) and `ps2xRuntime`. The recompiler config accepts
`handler@0xADDRESS` stub bindings for stripped games - use the addresses in section 4.

---------------------------------------------------------------------------------------------------

## 7. HLE plan for the PC runtime

### 7.1 Layering

```
recompiled EE code
  |  (hooked at SDK boundary, addresses in section 4)
  v
SifRpcRouter: sceSifInitRpc / BindRpc / CallRpc / CheckStatRpc / RegisterRpc / SetRpcQueue / RpcLoop
  |-- native IOP services keyed by SID: 0x1201 0x1202 0x1203 0x80000701 (+0x80000704 callbacks) 0x80000001/3/6 0x80000400
  |-- EE-registered servers (0x1301 0x1302 0x1303, 0x80000704): router owns their queue, dispatches into guest handlers
  v
host: ISO/extracted-file reader, SPU2 + audio device, input, save folder
```
Do not recompile/run IOP code; do not hook `sceSifSetDma` data paths separately - the router copies between
guest buffers directly.

### 7.2 Router semantics

* `BindRpc(client,sid,mode)`: if `sid` is a known native service -> fill `client` (server data pointer =
  token, `client->serve != 0`), return 0; unknown SID -> return -1 (the game prints the error and spins).
  Mark EE-registered SIDs as bindable for the native side only.
* `CallRpc(client,fno,mode,send,ssz,recv,rsz,endfunc,efarg)`: copy `send` out of guest memory, run the
  service handler (inline for blocking mode 0 / synchronous services; queue for mode 1 `SIF_RPCM_NOWAIT`
  with `CheckStatRpc` returning 1 while pending), copy the reply to `recv`, then invoke `endfunc(efarg)` (the
  game's `0x4af488` thunk does `iSignalSema`) from the router - on the EE thread that issued the call, or a
  host thread that safely injects a guest call (same mechanism used for interrupts).
* `RegisterRpc/SetRpcQueue/RpcLoop` for EE servers: `RpcLoop` becomes a native wait on a per-queue request list;
  when an IOP service needs a callback (7.5) it enqueues `(sid,fno,data,size)`; the loop calls the guest handler
  `fn(fno, data_ptr, size)` (pointer must be in *cached/uncached alias* range: the handlers OR 0x20000000/0x30000000
  into it, so pass a guest-RAM address) and completes the IOP-side call afterwards.
* Latency/ordering: services run on host threads; completion callbacks must be delivered through the EE
  server thread (never from the host thread directly).

### 7.3 Services to implement (in priority order)

1. **0x1201 File Command** (3.1) - 5 trivial calls. Handle table of {FILE*/mmap, size, SRSC index}. On `open`:
   resolve name (7.6), parse SRSC header + index (14-byte entries, 5.4), return `{handle,size}` or a negative
   code. `read`: copy to the guest address (EE physical address -> host pointer), return count.
2. **0x1202 Resource Request** (3.2) + completion server **0x1302** (+ **0x1303** allocation callback when flag 0x80
   is set; **0x1301** progress notification may be skippable - verify by log: if the EE handler is a no-op for
   non-streamed requests, skip it).
   Pseudocode:
   ```
   for each 28-byte entry e in send[]:
       f = files[fno]; ent = f.index.find(e.type, e.id, e.sub)
       if !ent -> status = -1 (continue)
       off = ent.off + e.offset; len = min(e.length, ent.size) ; if len > 0x40800 -> status = -9
       switch (e.flags & 0xf):
         0: dst = (e.flags & 0x80) ? call EE 0x1303(cookie,{len}) -> addr : e.dest; memcpy(guest[dst], file[off..], len)
         1: spu2.ram_write(e.dest, adpcm_fixup(file[off..off+len], e.flags), round_up(len,64))   # flags 0x20/0x10/0x40
         2: IOP-local buffer (ignore/log)
       post {e.cookie, status}
   deliver batches of <=64 completions via EE server 0x1302 (fno = count, data = count*{cookie,status})
   ```
   ADPCM fixups (flags 0x20/0x10/0x40) operate on 16-byte SPU ADPCM blocks (first-block `flags |= 0x06`,
   last-block `|= 0x03` or clear + zero the 0x7777 sentinel); implement exactly as in RSRCIOP `0x4fe8-0x5048`.
   Hard-fail (log) on unknown handle/type. Keep completion order == submission order.
3. **0x1203 Sound Command** (3.3): fno 0 -> feed records to the SPU2 emulator's ProcBatch (with the same
   KON/KOFF throttling semantics being unnecessary on the PC if the emulator applies the batch atomically);
   fno 1/2 -> SPU2 RAM copy ops.
4. **0x80000701 sdrdrv** (3.5 table): map each command to the libsd-equivalent method of the SPU2 emulator
   (7.7). It is simplest to hook `sceSdRemote` (0x4b91d8) itself so EE batch pointers need no IOP DMA bounce.
   Callbacks: `TransIntrHandler` (codes 0xb/0xc) and `Spu2IntrHandler` (0xd) -> EE server `0x80000704`
   (`fno 0`, 0x40-byte buffer `{u32 code, u32 arg,...}`) via the router; dispatcher 0x4b94d0 then calls the
   game's registered callbacks.
5. **Pad**: do not emulate padman. Hook `scePadInit` (0x4b0944), `scePadPortOpen` (0x4b0ba4, remember buffer),
   `scePadGetState` (0x4b0f70 -> 6), `scePadRead` (0x4b0ef4: fill 32 bytes `{status=0, 0x70|(len/2)=0x73 analog
   DualShock, buttons (active low) u16, rx, ry, lx, ly, pressure x12 ...}`), `scePadSetMainMode` (ok),
   `scePadSetActDirect` (rumble -> XInput/SDL). Map keyboard+mouse (the game has mouse-look command binds) and
   gamepad; mouse look can be synthesised into right-stick values, or later into game-level hooks.
6. **Memory card** (`0x80000400`): hook `sceMc*` or implement the RPC. Folder per slot, e.g.
   `saves/mc0/BESCES-50006/{icon.sys,*.rsg,*.rlt}`; `GetInfo` returns a formatted 8 MB card (type 2, free
   clusters) so the game never shows "format card"; `Open/Read/Write/Seek/Close/Flush/ChDir/GetDir/Mkdir/Delete/
   GetEntSpace` map to `std::filesystem`; results are returned through the libmc async protocol
   (`sceMcSync` polls) - complete immediately. `ps2xIOP` already has an MCSERV HLE to borrow semantics from.
7. **fio/loadfile/IOP-heap** (`0x80000001/3/6`): only needed for boot. Simplest: hook `0x101db8` (ReplaceIop),
   `0x101710` (LoadIopModules: skip the bundle entirely - load nothing) and `0x4b8af8/0x4b8fd0` as no-ops
   returning success; keep `sceOpen/Read/Write/Lseek/Close` mapped to host files for the screenshot path.
   If boot is left unhooked, `0x80000006` must at least return success for LoadModuleBuffer and `0x80000003`
   must hand out fake IOP-heap addresses.

### 7.4 Boot shortcuts (smallest first milestone)

Hook, in this order, and the game proceeds to the title with zero IOP emulation:
`0x101db8` -> return 0; `0x101710` -> return 0 and set the language global (0x4deAD0 = 0..4); `0x4b95b0`
(`sceMcInit`) -> 0; `0x4b9090` (`sceSdRemoteInit`) -> 0; `0x4bee98` for SIDs `0x1201/0x1202/0x1203` -> success;
then implement 3.1, 3.2, 3.3 as above. Everything the game does afterwards goes through the router.

### 7.5 EE-side server callbacks (hard part)

The completions use the guest's own SDK server loop, so the router must run guest code at the right time.
Recommended: keep the three EE server threads exactly as the game created them (`0x1f11e8`), hook
`sceSifRpcLoop` (0x4bf748) as `while(true){ req = queue.wait(); result = call_guest(handler, req.fno, req.data,
req.size); req.complete(result); }`. The handler for 0x1302 touches request objects through uncached aliases
(`|0x30000000`), so host code must have written the completion array into guest RAM before enqueueing.

### 7.6 Name resolution

```
resolve(name):
    n = name.replace('/', '\\')            # also accept leading 'cdrom0:' / 'cdrom:' / language dir
    n = strip_prefix(n, 'cdrom0:'); n = strip_suffix(n, ';1')
    comps = [mangle(c) for c in n.split('\\') if c]      # iop_mangle.py
    full = <iso/extract root> + '\\' + LANG + '\\' + '\\'.join(comps)     # LANG from config
```
Two backends: (a) open the ISO and parse ISO-9660 level-1 directory records (about 100 lines; case-
insensitive, `;1` optional) reading `file offset = LSN*2048` - no extraction needed; (b) a folder produced by `7z x`.
Both give "name -> (offset,size)"; build the map once at start.

### 7.7 SPU2 / audio

Expose `libsd` semantics through one class `Spu2` (2 cores x 24 voices, 2 MiB RAM, 48 kHz):
`SetParam(entry,val)` / `GetParam` (entry = register id | core | voice bits per `sdrdrv`/`libsd`), `SetSwitch`
(KON 0x1500|core, KOFF 0x1600|core, ENDX, PMON, NON, VMIX...), `SetAddr`(start/loop/NAX/transfer addrs), `SetCoreAttr`
(IRQ, effects enable, MIX/NOISE clock, DMA direction), `VoiceTrans`/`BlockTrans` + status (memcpy into RAM,
fire the trans-end callback), `SetEffectAttr`/`ClearEffectWorkArea` (reverb mode/depth/delay, work area in RAM).
Implementation: SPU-ADPCM decoder (16-byte blocks -> 28 samples; header: shift/filter + flag byte with loop
start 0x04 / loop 0x02 / end 0x01), Gaussian/linear interpolation, ADSR1/ADSR2 envelopes, pitch (0x1000 = 48 kHz),
per-voice and core volume (incl. sweep modes), voice-to-core mixing, reverb (PS2 core-1 effect with preset
buffers), and 48 kHz output through WASAPI/XAudio2 pull from a ring buffer. Report NAX/ENDX/IRQ accurately
because the streaming music logic (PS2MUSIC.SDB loaded in chunks via Resource Requests with SPU destination)
refills SPU buffers based on position/IRQ callbacks. Reference: the Sony libsd/ps2sdk semantics; `LIBSD.IRX`
(export slots 4..27, ProcBatch @0x3128, SetParam @0x36a0, SetSwitch @0x375c, VoiceTrans @0x1850) is available
for side-by-side behaviour checks.

### 7.8 Plan B: run the original IOP code

`tools/PS2Recomp/ps2xIOP` loads original IRXs and provides imports (`sysmem, intrman, ioman, loadcore, stdio,
sysclib, cdvdman, thbase/thsemap/thevent?`, heap, timers, vblank). RSRCIOP(embedded) needs: sysmem, intrman,
ioman, loadcore, sifcmd, sifman, stdio, sysclib, thbase, thsemap, **libsd**, **cdvdman (Init, Read, GetError,
SearchFile, Sync, DiskReady, #37, #75)**. If ps2xIOP supports these (libsd via its LIBSD HLE; cdvdman via
`iop_cdvd.cpp` pointed at the ISO), loading the 7 bundle modules unmodified is a legitimate fallback and an
excellent *oracle*: run it once with RPC tracing and compare against the native services above.

### 7.9 Verification checklist / known unknowns

* Log every `BindRpc` SID, every `CallRpc` (SID, fno, sizes) and every File `open` path on the first full
  boot -> confirms 5.1 path form and the fno sets in section 3.
* [?] Exact meaning of Resource Request `+0x0e`, flag bits other than 0x0f/0x80/0x10/0x20/0x40, and `0x1301`
  payload (0x10 bytes). Behaviour of dest type 2.
* [?] Sound Command fno 1/2 purpose (SPU read-back / SPU->SPU copy) - log first uses.
* [?] FMV (`.VDB`) decode path (IPU). If frames are IPU macroblock data, a software MPEG-2 intra/inter
  decoder in the IPU API hook is required; otherwise skip videos (they are Logo/intro cut-scenes in
  `INTERFCE.VDB`, 219 MB across all VDBs).
* [?] `.HSD` format.
* [?] Whether game code depends on RSRCIOP latency (loading screens use progress callbacks) - complete
  requests asynchronously but promptly.
* The anti-tamper style pattern (`f(1,0xffff)` guard calls such as 0x1e5748 -> 0x1e5618, RSRCIOP 0x1978 / 0x4d0)
  mutates keys/buffers; do not "optimise away" calls to them in the recompiler.

---------------------------------------------------------------------------------------------------

## 8. Artefacts produced

| Path | Contents |
|---|---|
| `analysis/scripts/iop_irxinfo.py` | IRX parser: `.iopmod`, sections, import/export tables |
| `analysis/scripts/iop_dis.py` | IRX disassembler (capstone, annotated imports), run with `tools/pyenv/Scripts/python.exe -I` |
| `analysis/scripts/iop_romdir.py` | IOPRP/ROMDIR lister/extractor |
| `analysis/scripts/iop_rrc.py` | LEVELS.RRC decryptor + inflater (reimplements EE 0x1e5668) |
| `analysis/scripts/iop_bundle.py` | splits the decrypted LEVELS.RRC into its 7 IRX modules (`analysis/iop/embedded_named/`) |
| `analysis/scripts/iop_srsc.py` | SRSC container/index parser |
| `analysis/scripts/iop_mangle.py` | RsrcLoader 8.3 name mangler + validation |
| `analysis/scripts/iop_eedis.py`, `iop_eecallers.py`, `iop_eeapi.py`, `iop_eefuncs.py` | EE disassembly (capstone MIPS64 approx., MMI shows as .word) and call-site tools |
| `analysis/scripts/iop_isohdr.py` | peek ISO file headers through 7-Zip |
| `analysis/iop/irx_info.txt`, `*.asm` | module reports and annotated disassembly (RSRCIOP = disc build, RSRCIOP_embedded = real build) |
| `analysis/iop/embedded/` | the 7 modules extracted from LEVELS.RRC |
| `analysis/iop/ioprp_modules/` | modules inside IOPRP23.IMG |
| `iso/sample/` | extracted small data files and decrypted bundle |
| `tools/pyenv/` | venv with capstone + pyelftools |
