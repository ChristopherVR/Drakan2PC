# Order of the Flame sounds and music -> PS2 SDB (2026-10-31)

Code: `editor/packages/formats/src/ootfsound.ts`, tests `formats/test/ootfsound.test.ts` (synthetic data only; 11 tests).
Not exported from `index.ts` yet (another session owns it): import from `./ootfsound.ts`.

## PC format (`build/ootf/cab_disc/**/*.sdb`, SRSC v0x0100)

Records: `0x0300` (4 bytes: entry count, next id), `0x0402`, `0x0301` group (`string name`, id = group id), `0x0302` sound
(id = sound id, sub = group id). 19 SDBs, 1561 sounds, 0 parse warnings. Sound record:

```
string name, u32 flags (0x4 flush, 0x8 loop; seen 0,1,2,4,6,8,12), u16 channels, u16 bits, u32 rate, i32 volume (millibel, 0 = full),
f32 dropoff (0..30), u32 priority (0..10), u32 decompressedSize, u32 compression (0 none, 1 = zlib), [u32 compressedSize], data
```

1432 are 22050 Hz mono 16-bit, 114 are 11025, 12 are 44100, 3 are 8-bit; 390 are zlib-compressed. Total 177.6 MB PCM.
The PC reference to a sound is `(db << 16) | id` with `db` = the level's dependency index (same as the PS2 rule in
level-format.md 12b).

## PS2 format (verified against the disc's INTERFCE / SYSTEM / RESOURES / PS2MUSIC / FX / level SDBs)

SRSC v0x0101. Index order on the disc: `0x300, 0x402 (10 01 00 00), 0x307 (0)`, then `0x301` groups and `0x302`/`0x304` pairs;
data 8-byte aligned, the three trailer records are stored last. `sub` of `0x302`/`0x304` = group id (not a copy count);
`0x301` sub = parent group.

Descriptor `0x302` after the name (`u16 len` includes NUL padding: `len = roundup4(2 + n) - 2`), `base` = 2 + len, always 48 bytes
when there is no subtitle text:

| off | meaning |
| --- | --- |
| +0 | flags: 0x8 loop, 0x2 common on effects, 0x10, 0x2000 subtitle text present (speech) |
| +4 | `0x00100001` = u16 channels 1, u16 bits 16 (same layout as PC) |
| +8 | sample rate (22050 / 44100 / 11025) |
| +12 | volume in millibel (negative; same unit as PC, values of the same size) |
| +16 | small int 0..4 (unknown) |
| +20 | f32 dropoff, 10..30, 15.0 most common (same scale as PC `dropoff`) |
| +24 | f32, 0 mostly, 10.0 on some effects (unknown; written 0) |
| +28 | class byte `0x55`, `0x99` (all music), `0x66` ...; low nibble = priority (sound-debug.md "lowest priority nibble"), PC priority 0..10 -> `pri * 0x11` |
| +32 | `u32 2` (empty text area: u16 len 2 + 2 NUL) or `u16 len + text` |
| +36 | PCM byte count of the source (0 for 22 of 61 sampled; music: 0) |
| +40 | ADPCM byte count = size of the `0x304` record (music: sometimes in +36) |
| +44 | 0 |

Sample record `0x304` (3398 of 3398 on the disc): `[16 zero bytes][ADPCM blocks, flag 0][end block 00 01 + 14x00][16 x 0x77 sentinel]`.
**Loop flags are not stored**: even `Wireloop_3` (descriptor flag 8) has flag 0/1 only; the IOP sets them at upload from the
request flags (iop-and-io.md 3.2: 0x20 loop start on the first block, 0x10 loop end on the last). So a loop is selected only by
descriptor flag 0x8.

The `0x300` pair is (entry count, next id); the disc has stale values (PS2MUSIC 53 vs 87 entries), so the engine
very likely ignores it. The writer stores entries + 3 and max id + 1.

## What was built

* `parsePcSounds(bytes)` -> `{sounds, groups, warnings}`; 8-bit and stereo are folded to mono 16-bit.
* `encodeVag(pcm, {loop})` standard PS-ADPCM: per block all 5 filters, shift = largest without clipping and one below, closed
  loop with the decoder's exact arithmetic (`decodeSpuAdpcm`), best squared error wins. `encodeSdbSamples` adds the disc wrapper.
  `decodeSdbSamples`, `snrDb`, `resamplePcm` (box filter 44100 -> 22050).
* `convertPcSounds(banks, {firstId, groupId, ps2Db})` -> `{map, sounds, group, adpcmBytes, warnings}`; `writePs2Sdb(sounds, [group])`
  standalone, `appendToPs2Sdb(existingSdb, sounds, groups)` keeps the old data byte for byte (use for the level's own
  SDB), `nextSdbIds(existing)` gives free ids.
* **Id map**: `SoundMap` keyed by `(sdbKey(pcSdbPath), pcId)` (`sdbKey('Common\\System\\System.sdb') = 'common/system/system'`);
  `map.mapRef(pcRef, keyOf)` takes a PC field value `(db<<16)|id` plus `keyOf(dbIndex) -> sdbKey` from the PC level's
  dependency table and returns the PS2 ref `(ps2Db<<16)|newId` or null. Plug it into `ConvertOptions`-style hooks (a
  `mapSound?: (pcRef, pc) => number | null`) and pass `map` to the sequence converter as its `soundMap`. All converted
  sounds of a mod go in one new group; ps2Db 0 = the level's own SDB (append there), another index needs a template
  dependency pointing at a separate SDB.

## Validation (real data; scratch script, nothing stored)

* Real disc VAGs (61 sampled from INTERFCE/SYSTEM/RESOURES) decode with `decodeSpuAdpcm`; decode -> re-encode -> decode: SNR min 28.5,
  median 51.7 dB (the re-encode of already 4-bit data mostly lands on the same codes). Tail layout matches 60/61 sampled (the other has no 0x77 block, flag 7).
* All 1561 PC sounds encoded and decoded back: SNR vs the source PCM min 15.1, p10 21.7, median 32.6, max 59.9 dB
  (low-level/noisy 44.1 kHz material is the weak end; ordinary 4-bit ADPCM behaviour). 24 s for 177 MB.
* 177.6 MB PCM -> 50.8 MB ADPCM for all PC SDBs; the biggest single sound is 459 KB, 6 exceed 0x40800 (the per-request cap; the
  game streams longer samples in chunks, see PS2MUSIC below), none are a problem for the format.
* Descriptors rewritten from parsed fields match the disc byte for byte except the unknown fields +16 / +24 / class byte.

## Limits and open questions

* In-game test not done: the PC volume/dropoff are copied as is (same units); +16 and +24 unknown (written 0).
* SPU2 RAM is 2 MB and shared by all effects of the loaded sets; a level SDB with 30+ MB of effects cannot be resident at once on the
  real console, but this runtime streams by request, so only the ids a level plays matter. Prefer converting only sounds that the
  converted level references (via the map) and `maxRate: 22050`.
* Speech (OotF voices are 22050 Hz) would need a subtitle text and the `0x305` lip-sync track (the game treats "no track" as fine).

## Music

* **PS2**: no level property. Music is `PS2MUSIC.SDB` (48.8 MB, 38 mono 22050 Hz tracks, flags 0 / 8 / 0xa, class byte 0x99) plus placed
  objects of `PS2MUSIC.ODB` classes ("Combat Music Loop", "Triggered Music Loop", "Music Ambient Trans", "NPC Counter", "NPC Music Off
  Trigger") whose type-5 sound field is a `(db 14 = Music, id)` ref (Surdana dependency 14). Tracks are up to 4.4 MB (about 6 min);
  the engine streams them in <= 0x40800-byte requests with SPU destination, so the whole-entry limit does not apply to music.
* **PC**: the level class has "Initial Music File" (a DirectMusic segment name); `Music.rrc` is 33 records of type 0x401 = RIFF
  `DMSG` DirectMusic segments (4 B to 1.2 MB: MIDI-like event lists + band/DLS references), not audio. `Dragon.rrc` holds the
  engine's textures (0x40) and tables, no audio.
* **Plan**: (1) cheap and robust: map each OotF level to a PS2 track by world (already in PS2MUSIC.SDB) and emit a "Triggered Music
  Loop" object; (2) full port needs a DirectMusic renderer (DLS banks are not in the install: segments reference the system GM.DLS
  / Roland GS bank, so output would depend on a synth), render each segment to 22050 Hz mono WAV (loop points from the segment's
  loop markers), `encodeVag`, and write with `convertPcSounds` flags 0x8 + priority 9 into a mod SDB. Size is no obstacle: 4.4 MB
  tracks exist on the disc. Not started.
