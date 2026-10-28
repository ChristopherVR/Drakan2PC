# Sound: the "missing attack / footstep / ambient sounds" bug, how it was found, and how to debug the sound path

Status 2026-10-17 (branch `drakan-snd`; the fix itself is `drakan` commit 997ee7a). User report: "some in-game audio is not playing, like attacking things".

## 1. Verdict

**Root cause: the recompiler translated the R5900 `SQRT.S` with the wrong operand.** The EE's instruction is `SQRT.S fd, ft` (the source is the `ft`
field, `fs` is unused and 0 in compiler output; PCSX2 `SQRT_S` and the EE Core manual agree). `ps2xRecomp/src/lib/fpu_translator.cpp` emitted
`ctx->f[fd] = FPU_SQRT_S(ctx->f[fs])`, so **every `sqrt.s` in the game (329 sites) returned `sqrt($f0)`** whatever the source register was.
(`RSQRT.S fd, fs, ft` = `fs / sqrt(ft)` and the VU0 macro `VSQRT` were already right.)

Why this silenced the sound: the game's distance gain for a positional (3D) sound, `FUN_001b1cf8`, does

```
d2 = |emitter - listener|^2 ; if (r*r <= d2) gain = 0                  (r = descriptor range, e.g. 30720 for Rynn's voice)
d  = sqrt(d2)                                                          <- sqrt.s $f21, $f1 : the bug made d = sqrt(r*r) = r
if (d > 0.06*r) gain *= 10^(((0.06 r - d) * 0.1) / (0.06 r))           (-> 0.027)
if (d - 0.9 r > 0) gain *= 1 - (d - 0.9 r) / (0.1 r)                   (d = r  ->  1 - 1 = 0)
```

so every positional sound that was inside its range got **gain 0** (the sound was created, a voice was allocated and key-on'd, but VOLL/VOLR were
written as 0 for its whole life). Non-positional sounds (music, menu, the wind gust, speech flagged 2D) were unaffected, which is why "some" audio
worked. Player sounds are positional (emitter = Rynn), so footsteps, "Rynn Attack ..." grunts, "Short Swish ..." weapon swings, NPC idles, birds and
every other world sound were silent. The same bug broke the footstep trigger itself (no `PlaySound` was even attempted while walking), the
camera/third-person logic (**Rynn was not drawn**, the camera sat inside her: "Camera Distance Test/Undo Alpha Player Out") and the **inventory
screen** (rendered half-empty). All three work with the fix.

### Fix and what it needs
* `ps2xRecomp/src/lib/fpu_translator.cpp`: use `ft`. **Already in `drakan`** (commit 997ee7a from `drakan-geo`, found there from the geometry side, merged in RECOMP_STATUS section 9); this branch
  (`drakan-snd`) found it independently from the sound side with the same one-line fix, then dropped its copy on rebasing and keeps only diagnostics / stereo / subtitle changes. **The generated sources must be regenerated** (`ps2_recomp config.toml`, ~1 min) and the whole
  runtime rebuilt (every generated TU changes where a `sqrt.s` was emitted: 329 sites in about 45 of the 82 files). A runtime-only rebuild does not fix it.
* A second, small bug found on the way: the sound manager's **stereo flag** (`0x4db930` = manager `0x4db4f0` + `0x440`, read by `SetGain` 0x1e5470 to pan
  voices with `L = g(1 - 0.8 pan)`, `R = g(1 + 0.8 pan)`) is only written by `Options_ApplyAll` (0x3f9e20) or when the "Stereo Sound" box is toggled. A start
  without a settings load never calls it, so everything was **mono** although the option showed "on". `soundPump` now keeps the flag equal to the option
  word `0x4fb744` (`DRAKAN_STEREO_SYNC=0` disables).

### Evidence (same scripted run, `scripts/harness/snd_test_run.sh`, level 1 start, walk W for 5 s then 8 dagger swings)
| | before | after |
|---|---|---|
| voice volume (max VOLL written during the voice's life), footsteps "Grass Short 1..5", "Dirt Gravelly 2..4" | 0 (never started: no `PlaySound`) | 0x0d0c .. 0x3fff |
| Rynn idle "Look at nails 01" (emitter = Rynn, distance 1.1k of range 30k) | 0 | 0x237e |
| birds (BirdWren, BirdCanary, "Birds Loop") | 0 | 0x01c8 .. 0x106f (attenuated by distance) |
| `[setgain]` for sounds inside their range | gain=0 | 0.2 .. 0.55 |
| dagger swings: "Short Swish 011/02/03/04", "Rynn Attack Long 01", "Rynn Attack Short 02/04" | not testable (no inventory) | maxvol 0x22ce .. 0x375b |
| audio dump, swing moments (50.6, 52.6, ... 64.7 s) | - | RMS x4..x8 and peak 0.25-0.33 vs 0.03-0.08 between swings |
| L != R voice volumes (panning) | none (mono) | 482 of ~2300 volume pairs differ in the run |

## 2. What the other hypotheses turned out to be (checked, nothing wrong)
* **`.SSD` / type 0x305 "absent on the disc"**: partly a misunderstanding. `.SSD` files **do exist** for the level trees (`ENGLISH/LEVELS/LEVEL*/LEVEL*.SSD`,
  also FRENCH/GERMAN/...; 13-100 KB; SRSC with types 0x310/0x311/0x312, names like "0.01 Intro Stomp", sound scripts of the level) and are opened and
  parsed when present (`LEVEL1ON.SSD` -> handle 36 srsc 1/63). They do not exist for the COMMON sets (INTERFCE, SYSTEM, FX, NPCS, RESOURES, SOUND, MAGIC,
  DEBUG, music, WORLDCON): the real IOP returns "not found" there too. Path mangling, language root and case are correct (all 8.3 keys resolve).
  **Type 0x305** is the **lip-sync track** of a speech sample, not "sound-set" data: `u32 0, u32 rate (22050), u32 882 (= 22050/25: one entry per 1/25 s),
  u32 frame count, u32 byte count, 4-bit mouth shapes`. It exists only for speech in SYSTEM.SDB (136), NPCS.SDB (392) and LEVEL SDBs (16 in LEVEL0RO).
  The game requests `0305/<id>` for every sample of a set (INTERFCE 26/32/71/77 ...) and treats -1 as "no lip sync". Reporting them as success was not needed.
* **SPU2**: key-on of short one-shots, voice allocation (48 voices: the game takes them from the top, core1 v23 first; the free list is intact, priority
  stealing works), ADSR (`1e0f/1fdf` for every sound), sample upload addresses (the game's allocator, no overlap with the streaming buffers found), loop /
  end flags, voice lifetimes (every key-off arrives sample length + ~0.1 s after the key-on: `snd_voice_life.py`), core0 -> core1 routing, MMIX/VMIX: all fine.
* **sdrdrv / 0x1203**: after init the game uses only 0x1203 `fno 0` batches (SETPARAM / SETSWITCH / SETADDR); no unknown sdrdrv command, no unhandled
  batch function, no `GET*` batch result is needed. The voice-position snapshots (`0x1310`, every 500 ms) are what the allocator reads to find parked voices.
* **Distance culling** (`PlaySound` returns 0 when the emitter is farther than `*(float*)0x4de96c` = 2.36e10 units^2 from the listener; NPC idle sounds
  only start inside their descriptor range, e.g. 8192) is by design; the listener (`DAT_004e0b1c + 0x24` handle -> camera object) follows the player.

## 3. How the sound engine works (EE side, addresses of SCES_500.06)
```
game object / animation event / ambient emitter
   -> PlaySound  FUN_001b7f30(mgr 0x4db4f0, id, pos*, flags)   id = (set << 16) | sample id (set 1 INTERFCE, 2 SYSTEM, 3 RESOURES (weapon swishes 0x301c2..), 4 FX, 5 NPCS; music / level sets use other numbers)
        range cull (0x4de96c), instance object (+0xc id, +0x20 base gain, +0x30 range, +0x48 pos), sorted into class lists (FUN_001b83c8 limiter)
   -> per tick FUN_001b1370: pan (FUN_001b17d8: dot of the unit direction with the listener axis), distance gain (FUN_001b1cf8), SetGain (vslot 0x3c = 0x1e5470)
   -> start voice FUN_001e4fa8: FUN_001b2200 resolves the 0x302 descriptor / waits for the 0x304 upload (state 3 = loaded), FUN_001e5910 takes a voice
        (free list at 0x4e0b1c+0x10c, steals the lowest priority nibble of descriptor+0x48), queue ADSR / SSA / KON
   -> batches of 8 byte {func, entry, value} records (ring at 0x4e0b1c+0x200, flushed by FUN_001e6460) -> RPC 0x1203 fno 0 -> SoundService -> Spu2
```
Sound sources: `FUN_004030c8` (ambient emitters, `ra=0x403498`), `FUN_00103928 -> FUN_0010d100` (sounds attached to animation events: NPC "Sweeping",
"Shovel", "Hammering wood", Rynn's idle "Look at nails"; picks one of two ids with `FUN_0012b448`, preferring a loaded one), the footstep generator
(`ra=0x3d1458`, ids `0x201d9..` = SYSTEM "Grass Short", "Dirt Gravelly" ...), `FUN_002bf568` (menu), `FUN_001a4c50` (placed Sound objects).
Attack input: `Game_OnInputCommand` (0x23ce18) command 0x4006 -> `FUN_00307198` sets bit 1 of player+0x90 -> player state machine (`FUN_0031da90`,
`FUN_003016e8`) -> animation + weapon swing sounds. Rynn starts without a weapon: equip one first (TAB, Enter, E).

## 4. Diagnostics (all env gated, nothing changes behaviour)
| env | effect |
|---|---|
| `DRAKAN_SND_LOG=2` | key-on / key-off, `setAddr` of voice start, every SPU RAM upload, 500 ms voice snapshots |
| `DRAKAN_SND_LOG=3` | + every voice VOLL/VOLR/pitch/ADSR write; `KON c<core>.v<voice> ssa pitch vol adsr f=<SPU2 frames>` (f/48000 = position in a dump) |
| `DRAKAN_SND_LOG=4` / `5` | + every 0x1203 batch record (5 = also the per-frame volume writes) |
| `DRAKAN_SND_DBG=1` | hooks of the game's sound manager: `[play]` PlaySound (id, flags, emitter, result), `[gain]` distance gain inputs, `[setgain]` final gain per instance |
| `DRAKAN_AUDIO_DUMP=<wav>` | 48 kHz stereo dump of what is sent to the speakers (works with `DRAKAN_AUDIO=0`) |
| `DRAKAN_HLE_LOG=100000` | needed so that every resource request is in the log (the key-on -> sample name mapping uses it) |
| `DRAKAN_STEREO_SYNC=0` | do not copy the Stereo option into the sound manager |
| `DRAKAN_TRACE_FN=addr,...` | existing: arguments / result of up to 8 guest functions |

Tools (`analysis/scripts/`): `snd_kon_names.py` (key-on -> upload -> SDB name), `snd_vol_life.py` (max volume per voice life: **a started sound with maxvol 0 is inaudible**),
`snd_voice_life.py` (played length vs. sample length), `snd_dump_check.py` (RMS around each key-on in a dump). Runner: `scripts/harness/snd_test_run.sh <exe> <input script> <name>`;
`scripts/harness/recipes/snd_attack_test.txt` equips the dagger and swings it. Needs the ISO extracted with 7-Zip for the SDB names (`7z x ... ENGLISH\COMMON\*\*.SDB ENGLISH\LEVELS\*\*.SDB`).

Workflow used here: key-on log -> volume 0 for positional sounds -> `[setgain]` gain 0 with a nonzero `scale` -> disassembly of `FUN_001b1cf8` (`.word 46010544` that
capstone does not decode = `sqrt.s $f21, $f1`) -> generated code `FPU_SQRT_S(ctx->f[0])`.

## 5. Open / not done
* Not listened to by a human; verified by key-on volumes, `[setgain]` and RMS in the dump only.
* Reverb / effects: the game does not use them (no `sdrdrv` effect call after init).
* Other `sqrt`-dependent behaviour (collision, AI, camera) was not re-audited; the fix changes it everywhere, which is the intent (visible: Rynn, inventory).
* The first title-menu entry with a save is "Load Game": scripted runs press Cross on it (a save from decomp/iso/mc0 is loaded); "New Game" needs DPAD_DOWN first.

Decompiled sound code used for this (Ghidra, `analysis/input_run.sh InputDecompile.java analysis/sound <addr> ...`): `analysis/sound/*.c`.
