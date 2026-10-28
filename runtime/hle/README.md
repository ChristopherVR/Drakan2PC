# RSRCIOP HLE library (`drakan_hle`)

Standalone C++20, no dependencies (standard library only), no knowledge of PS2Recomp. It services the three
RPC servers of Surreal's RSRCIOP IOP module natively (see `docs/reference/iop-and-io.md`, sections 3 and 7):
`0x1201` File Command, `0x1202` Resource Request, `0x1203` Sound Command, reading game data straight from the
`.iso` (or an extracted folder).

```
include/hle/Interfaces.h       IEeMemory, IEeRpcPoster, ISpu2, logging, SID constants
include/hle/IsoFs.h            ISO9660 + folder backend, 8.3 mangler, language root, path resolution
include/hle/Srsc.h             SRSC index structs
include/hle/FileService.h      SID 0x1201
include/hle/ResourceService.h  SID 0x1202 (+ completions to EE 0x1302 / 0x1303 / 0x1301)
include/hle/SoundService.h     SID 0x1203 (forwards to ISpu2; no SPU2 emulation)
include/hle/RsrcIop.h          facade: RsrcIop::call(sid, fno, send, sendSize, recv, recvCap)
tests/hle_test.cpp             test program;  tests/gen_expected.py  ground truth from the Python scripts
```

## Integration

```cpp
#include "hle/RsrcIop.h"
hle::IsoFs fs;
fs.openIso("...Drakan - The Ancients' Gates.iso");   // or fs.openFolder("C:/extracted")
fs.setLanguageRoot("\\ENGLISH");                      // the arg RSRCIOP receives; user-selectable language
MyEeMem mem; MyEeRpc ee; MySpu2 spu;                  // implement IEeMemory / IEeRpcPoster / ISpu2
hle::RsrcIop iop(fs, mem, ee, spu);
```

Hook points in the router (`doc` 7.2):

* `sceSifBindRpc(sid 0x1201/0x1202/0x1203)`: succeed (`RsrcIop::isRsrcSid`).
* `sceSifCallRpc(client, fno, mode, send, ssz, recv, rsz, endfunc, efarg)` for those SIDs: copy `send` out of
  guest RAM into a host buffer, `n = iop.call(sid, fno, hostSend, ssz, hostRecv, rsz)`, copy `n` bytes of reply into
  guest `recv`, then run `endfunc(efarg)` as the real SIF would.
* **0x1201 / 0x1203 may be run inline** (cheap, synchronous).
* **0x1202 must run on a worker thread** (mode-1/no-wait call on the real IOP; `CheckStatRpc` should return
  "busy" until the call returned). `call()` returns 0, and during the call it posts completions through `IEeRpcPoster`.
* `IEeRpcPoster::post(sid, fno, payload, size, reply, replyCap, blocking)` must copy the payload into guest RAM,
  enqueue `(fno, data, size)` on the EE-registered server for `sid` (0x1301/0x1302/0x1303 are created by the game via
  `sceSifRegisterRpc` + `sceSifRpcLoop`, doc 7.5), and when `blocking` wait for the guest handler and copy its
  return buffer into `reply`. 0x1302 and 0x1303 are posted blocking (like the IOP's mode-0 CallRpc), 0x1301 is not.
  If the EE server is not registered yet, return `<0`.
* Addresses go to `IEeMemory` exactly as the game provided them (cached/uncached aliases possible); mask with
  `0x1FFFFFFF` in your implementation if needed.
* Boot hooks (skip LEVELS.RRC module loading etc.) are not part of this library; doc 7.4.
* Logging: `hle::setLogSink(fn)`, `hle::setVerbose(true)` logs every open and resource request (use the first boot
  to confirm the open-path form, doc 7.9).

## Behaviour summary

* **IsoFs**: reads PVD (LBA 16), walks the whole directory tree once into a case-insensitive (upper-case) map
  `ENGLISH\COMMON\INTERFCE\INTERFCE.TXD` -> {lba,size}; `;1` stripped. Raw 2048-byte sector `.iso` only (no Joliet,
  no multi-extent). `resolveKey()` = strip `cdrom0:`/`cdrom:` and `;N`, split on `\` `/`, fold `.`/`..`
  (the IOP does not; harmless), mangle each component (`mangleComponent`, exact port of `iop_mangle.py` including the
  `k > 0` / no-dot-or-dot-at-0 branches), prepend the language root unless the first component already is one of the
  five language dirs. `lookup()` falls back to the path without language root (for `\IOP\...`, `SYSTEM.CNF`).
* **FileService** (layouts exactly as doc 3.1): open `{isWrite,0,path[0x100]}` -> `{handle,size}` (parses the SRSC
  header/index immediately; handles are the lowest free slot, 32 slots, -7 when full; write-open is refused),
  close (-4 bad handle), read (clamped to file size, advances position, copies to EE memory in 1 MiB steps),
  fno 3 no-op, lseek (whence 0 abs, 1 cur+off, **2 -> size-off** quirk), fno 5 meminfo (fixed 0x400000).
* **ResourceService**: 28-byte entries; `fno` = file handle; exact `(type,id,sub)` lookup; `length` clamped to
  `entry.size - offset`, `length==0` means "rest" (TODO unverified), `> 0x40800` -> status -9, missing -> -1, bad
  handle -4. Destination type (`flags & 0xf`): 0 EE memory, 1 SPU2 RAM through `ISpu2::ramWrite` (length rounded up to
  64, with the ADPCM fix-ups for flags 0x20/0x10/0x40 ported from disassembly 0x4fe8/0x4ffc/0x5048), 2 logged no-op.
  Flag `0x80`: ask the EE via blocking `post(0x1303, fno=cookie, {roundup4(len),0,0,0})`, destination = reply word 1
  (0 -> status -6). Completions: `{cookie,status}` in submission order, <= 64 per `post(0x1302, fno=count, ...)`.
* **SoundService**: fno 0 = `SdBatch{func,entry,value}[]` dispatched to `ISpu2::setParam/setSwitch/setAddr/setCoreAttr`
  (func 1..4; get commands 0x11..0x14 called and discarded; WRITEIOP/EERETURN logged); fno 1 = `{spu,ee,size}` SPU2 RAM
  -> EE read-back; fno 2 = `{src,dst,size}` SPU2 -> SPU2 copy. Reply is an s32 status (0).
* LEVELS.RRC decrypt/inflate: **not included** (not needed when module loading is stubbed).

## Tests

```
cmake -S decomp/runtime/hle -B decomp/build/hle -G "Visual Studio 17 2022" -A x64
cmake --build decomp/build/hle --config Release
python -I decomp/runtime/hle/tests/gen_expected.py <iso> decomp/runtime/hle/tests/expected.txt   # needs 7-Zip + repo python
decomp/build/hle/Release/hle_test.exe <iso> decomp/runtime/hle/tests/expected.txt [extracted-folder-root]
```

Result: **434 checks passed, 0 failed** on the real ISO (1103 entries). Covered: mangling vs `iop_mangle.py`
(18 vectors + all 14 LEVELLST.TXT paths resolve to real files); path forms (`cdrom0:\..;1`, long names, `/`, `..`,
language-less `\IOP\IOPRP23.IMG`); File RPC open/read/lseek/close/error cases/handle exhaustion with SHA-1 of
LEVELLST.TXT, DRAKAN2.RRC, INTERFCE.TXD (5.6 MB), SYSTEM.DB, SOUND.SDB (51 MB, first 4 MiB) against 7-Zip bytes;
SRSC index (count, offset, SHA-1 of all index bytes) of DRAKAN2.RRC (297), INTERFCE.TXD (98), SOUND.SDB (455) against
`iop_srsc.py`; Resource requests (first/middle/last entry data SHA-1, partial offset/length, 0x80 alloc path, SPU dest
with fix-ups, -1/-4/-9 statuses, 150 requests -> 64/64/22 completion batches, Rpc1301 mode); ADPCM fix-up unit tests;
Sound fno 0/1/2 on a fake SPU; folder backend (using `iso/sample`). Note: `SOUND.SDB` contains entries > 0x40800
bytes (e.g. 0304/639, 833280 B) so the game must request those in chunks via `offset`/`length`.

## Findings and open questions

1. **How type-0 data really reaches the EE (important).** Disassembly of the loader thread (RSRCIOP 0x3700-0x39a0) and
   EE server 0x1f0110 shows that for EE destinations the IOP does *not* only DMA: it sends each request to EE server
   **0x1301** (`fno` = request pointer = cookie, payload = 16-byte header `{hdrSize 0x10(+align), len, ?, checksum}`
   followed by the data); the EE handler copies the data into the request buffer (`req+8`), verifies the checksum
   in header word 3 only if non-zero (so HLE sends 0), then runs the request's completion virtual call and sets the
   `0x80000000` done flag. 0x1302 runs the same completion code on `{cookie,status}` entries. The default
   (`Delivery::Direct`) writes straight to `entry.dest` and completes via 0x1302, as the task brief specified.
   `Delivery::Rpc1301` is the faithful variant (successful requests complete via 0x1301, failures via 0x1302). If the
   game does not complete / data is missing in Direct mode, switch it. The 0x1301 header words 2 and the
   0x1303-reply word 2 are not understood (set to 0 / ignored).
2. Error codes: only -7 (no free handle), -4 (bad handle), -1 (resource not found), -9 (too big) are documented.
   "File not found" on open (we return -2), I/O errors (-5), alloc failure (-6), zero-size (-2) are guesses.
3. Open paths: unverified whether the EE sends `\Common\...` or `cdrom0:\...`; both are accepted. Log the first opens.
4. `length == 0` and `offset >= entry.size` in resource requests are unverified; sub is compared as u16.
5. Dest type 2 (IOP buffer), request `+0x0e` aux, and flag bits other than 0x0f/0x80/0x10/0x20/0x40 are ignored.
6. Sound fno 1 direction (SPU -> EE) is inferred. sdBatch GET results are discarded (no rets array is evident).
7. Threading: `FileService` is internally locked; `ResourceService` only reads shared state through copies, so a
   dedicated worker thread per call is fine. `IsoFs` reads are serialized by a mutex (one FILE*).

## Measured corrections (2026-10-16, first real boot; see docs/status/recomp-status.md)

These replace the corresponding statements above / in `docs/reference/iop-and-io.md`; all are implemented and covered by `tests/hle_test.cpp`.

1. **Open record**: some opens send a short record (`8 + strlen + 1` bytes, e.g. 57-58) and ask for only a 4-byte reply (the handle).
   `FileService` accepts `replyCap >= 4`.
2. **Not found is -1** (`ERR_NOT_FOUND`), not -2. With -2 the game retries the whole open group forever and leaks handles.
3. **Resource wire entry**: `id` is at +8 and `type` at +0x0a (`ResourceRequestWire` order fixed). Real entries: flags (u32 at +4) 0x80 = EE
   allocates (0x0a80 for movie frames, 0x0800 for sound sets, 0 for texts), `length` = 0xFFFFFFFF means "rest of the entry".
4. **`sub == 0xFFFF` means "any instance"**: `Srsc::find` returns the first entry with that (type,id).
5. **Alloc flag (0x80)**: the allocated address (0x1303 reply word 1) is also stored at the request's `dest` field (a slot descriptor of the
   requester, e.g. the movie player's frame ring); the 0x1301 header carries the allocation offset (reply word 2) as word 2. Verified with the
   movie player: data is DMA'd straight into the allocated buffer, the 0x1301 call only carries the 16-byte header and completes the request,
   and the slot pointer must be (re)written after the completion ran (the EE completion code clears it).
6. **Default delivery is `Delivery::Rpc1301`**. `Direct` remains for tests and only works for requests with a real destination.
7. **0x1202 completes immediately on the EE side**: the real IOP server function only queues the batch; the client's end function must run right
   away and the loader thread answers later through 0x1302/0x1301. Running the end function after the batch deadlocks (a completion handler
   submits the next request through the still locked client). The host adapter queues the batch to one worker thread.
8. Absent entries: the COMMON sets have no `.ssd` (`INTERFCE.SSD`, `SYSTEM.SSD`, ...; the per-level `LEVEL*.SSD` files DO exist, are found and parsed), and type 0x305 (the
   **lip-sync track of a speech sample**: rate, 882-sample frames = 25/s, frame count, 4-bit mouth shapes) exists only for speech samples in SYSTEM / NPCS / level SDBs. The game
   requests `0305/<id>` for samples of a set regardless (INTERFCE 26/32/71/77 ...); "not found" (-1) / completion status -1 is the real IOP's answer and the game continues
   (docs/reference/sound-debug.md section 2). The "missing sound" bug of 2026-10-17 was not in this layer (it was the SQRT.S translation).
9. **0x1301 header word 0 = data offset.** For allocated destinations (flag 0x80) the IOP sets header word 0 to the 0x1303 reply word 2 (offset of the allocated
   buffer from the receive buffer 0x574880); the EE handler uses `data = buf + word0` and (for word0 >= 0x14) treats the stream as owning the buffer (the loader
   frees it). Inline data keeps 0x10. Verified with the texture parser; `{0x10,...}` made every TXD parse fail and stalled the game after the logo movie.
10. **64 file handles** (RSRCIOP file table = 0x40 entries); `FileService` default is 64 now (the test expects -7 at the 65th open).
11. **Completion latency**: the host must not complete a batch within microseconds of the request; see docs/status/recomp-status.md 4c item 6 (adapter side, 2 ms).
12. `ResourceService::skipFailureCompletions` (experiment flag, default off): skipping the -1 completions of absent optional entries makes the game stall.
13. SID 0x1310 (EE server of the game's sound system) is fed by the IOP sound thread, not by the resource/sound services: see docs/reference/iop-and-io.md 3.4.

## Resource overrides (2026-10-16, branch drakan-ui)

`include/hle/ResourceOverride.h`: `IResourceOverride { wants(ResourceId), apply(ResourceId, vector<u8>&) }`, registered with
`ResourceService::addOverride()` (before the first request; called on the 0x1202 worker thread). `ResourceId` = `{file (upper-case name, e.g. INTERFCE.TXD), path, type, id, sub}`;
`sub` is the sub id of the matched index entry. When any override wants a request the service reads the whole entry, applies the overrides in order and then
applies the request's `offset`/`length` to the result (the replacement may have any size; `offset >= size` -> -1 as before).

* `DirectoryOverride::load(root)`: `<root>/<FILE>/<type hex>_<id dec>_<sub dec|x>.bin` = verbatim replacement payload (HD textures, mods).
* `XdbTextOverride`: text entries (type 0x400 of `*.XDB`: 8 slots, English/German/French/Italian/Spanish + 3 spares, `xdb::decode/encode`),
  `addRule(callback)` and `<root>/<FILE>/text.ini` (`<id>[.<lang>] = text`, escapes `\ \n \r \t`).
* `tests/override_test.cpp` (self-contained, builds a folder game tree in the temp dir): codec, folder files, offset/length on replacements, wildcard sub, text rules, untouched
  non-text entries. `hle_test` is unchanged (509 checks with the ISO, one more build target `override_test`).
The UI prompt generator built on top of it is `runtime/ui`, see `docs/reference/ui-prompts.md`.

## Random-access disc sources

`IsoFs::openSource` accepts an immutable, shared `IsoSource` with bounded `readAt` operations.
The native factory uses a seek/read mutex around its private file handle; the browser factory reads
1 MiB ranges from the local launch server on a game pthread, with a bounded per-worker block cache.
ISO parsing and resource services share the same implementation across those sources. `iso_source_test`
uses a synthetic in-memory ISO and a native file round trip; it requires no game data.
