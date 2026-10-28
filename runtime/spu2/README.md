# drakan_spu2 - standalone SPU2 + libsd (C++20)

Implements `hle::ISpu2` (runtime/hle/include/hle/Interfaces.h) with a software SPU2, the libsd API used by
`sdrdrv`, and audio output: WASAPI on Windows x64, ALSA on Linux, and a shared stereo PCM ring consumed
by AudioWorklet in the browser. Windows needs no third-party audio dependency; Linux needs ALSA
development headers/libraries. Browser mixing runs on a pthread and continues when audio is suspended,
so guest voice-position polling can still complete. Links `drakan_hle` for `hle::logf`.

```
include/spu2/Spu2.h         class Spu2 : hle::ISpu2  (+ libsd API, render, events)
include/spu2/Libsd.h        SD_* constants, SdBatch, SdEffectAttr, sdrdrv command ids (ps2sdk libsd-common.h values)
include/spu2/Sdr.h          SdrServer: dispatcher for SID 0x80000701 (+ callback messages for 0x80000704)
include/spu2/AudioOutput.h  native output thread (48 kHz float), shared gain/pause/watchdog/WAV dump
include/spu2/Adpcm.h        adpcmDecodeBlock()
src/  Spu2.cpp (regs, libsd, effects)  Mixer.cpp (voices, cores, reverb, ADMA)  Sdr.cpp  Adpcm.cpp  GaussTable.inc
tests/spu2_test.cpp, tests/adpcm_ref.py
```

## Build / test
```
cmake -S decomp/runtime/spu2 -B decomp/build/spu2/vs -G "Visual Studio 17 2022" -A x64
cmake --build decomp/build/spu2/vs --config Release
decomp/build/spu2/vs/Release/spu2_test.exe [--sdb SOUND.SDB] [--out dir] [--live] [--python exe]
```
For the root Linux build, install `libasound2-dev`, then use `cmake --preset linux-runtime-tests`,
`cmake --build --preset linux-runtime-tests --parallel 2` and `ctest --preset linux-runtime-tests`.
`audio_output` and `audio_output_alsa_null` exercise silence, mixer progress, pause and shutdown without
disc data or speakers. The ALSA backend accepts PCM names (including `default` and `null`), retries
unavailable devices and keeps voices progressing silently; enumeration filters out unusable plugin hints.
WASAPI device handling is unchanged. Common output logic lives in `src/AudioOutput.cpp`.

Defaults: SOUND.SDB = `decomp/iso/sample/ENGLISH/COMMON/SOUNDS/SOUND.SDB` (extracted from the ISO),
WAVs -> `runtime/spu2/test-output/` under the selected CMake binary tree (separate for each platform).
Result: **77 checks passed, 0 failed** (the live gain / device section is [10]). Tests: ADPCM vs the Python reference (16800 samples of a real SOUND.SDB
sample, 0 mismatches) + embedded vector; one-shot on core 0 and core 1 (WAV), ENDX/NAX/voice stop; ADSR phases and
release; loop + IRQ event + TransDone event + noise; Hall reverb tail (reverb_hall.wav vs reverb_off.wav);
`hle::SoundService` fno 0 batch driving Spu2; sdrdrv dispatch (Set/GetParam, Note2Pitch, VoiceTrans, ProcBatch,
Set/GetEffectAttr); AutoDMA streaming; command queue under thread load; `--live` plays 3 s through WASAPI.

SOUND.SDB entry types: `0x302` = sample header (u16 nameLen, name, then at `roundup4(2+nameLen)`: +8 rate (22050 seen),
+0x28 ADPCM byte size), `0x304` = raw PS-ADPCM blocks (same id/sub). Data ends with an `0x77`-flag sentinel block
(loop-to-itself silence) and one-shots carry flag 1 on the last real block.

## Usage
```cpp
spu2::Spu2 spu;  spu.sdInit(0);                       // libsd init (voices parked on silent block @0x5000, AVOL1=0x7FFF)
hle::RsrcIop iop(fs, mem, ee, spu);                   // Spu2 *is* the ISpu2: RPC 0x1203 and SPU2 resource uploads work
spu2::AudioOutput out(spu); out.start();              // or call spu.render(int16 stereo, frames) yourself
spu2::SdrServer sdr(spu, resolver);                   // for SID 0x80000701: sdr.call(fno, data, size, reply, cap)
```
* **0x1203 / SoundService**: `setParam/setSwitch/setAddr/setCoreAttr` are queued and applied in order at the next 64-frame
  boundary; getters and `ramWrite/ramRead` first apply everything queued. `ramWrite` is the resource-upload path (ADPCM
  into SPU RAM, byte addresses, wraps at 2 MiB, fires address IRQs).
* **sdrdrv**: `SdrServer::call` mirrors ps2sdk `sdd_com.c` (word0 = self pointer, args in u32 slots from word1).
  `resolver(iopAddr, n)` must map IOP-side addresses (ProcBatch/VoiceTrans/BlockTrans buffers) to host pointers - in a
  native port hook `sceSdRemote` (EE 0x4b91d8) and return EE-memory pointers. Reply = s32 (GetEffectAttr: 20-byte attr;
  ProcBatch2/Ex2: s32 + rets). `pollCallbacks()` converts events to EE 0x80000704 messages (`mode` flags
  DMA0INT/DMA1INT/IRQINT, game codes 0xb/0xc/0xd, `voiceBit` = core mask); post each as a 64-byte SdrEECBData RPC.
  Events are produced only if enabled via cmd 0x8160 (trans handler, per core) / 0x8170 (SPU2 IRQ handler).
* Entry encoding (exactly libsd): `entry = (reg<<8) | core | voice<<1`; `SD_VOICE(core,v)`. 24-bit switches; addresses
  are byte addresses (`SSA` 16-byte aligned, `EEA` only the upper nibble writable), as in freesd.

## What is modelled
2 cores x 24 voices, 2 MiB RAM (16-bit word addressing internally), VOL L/R with sweep (all modes), PITCH (0x1000=48 kHz),
pitch modulation, noise (Dr. Hell), ADSR1/2 state machine (PCSX2), KON/KOFF (KOFF before KON, one sample latency), ENDX
(init 0xFFFFFF, cleared by KON / any write), NAX/LSAX/SSA incl. LOOP_START capture, PS-ADPCM (4 filters, loop flags,
END without LOOP stops the voice), 4-tap gaussian interpolation (512-entry table sourced from GPL-licensed PCSX2 interpolate_table.h), VMIXL/EL/R/ER,
MMIX gating, master/effect/ext/input volumes, core0 -> core1 external routing (AVOL), reverb (PCSX2 algorithm incl.
39-tap 48k<->24k filters), libsd effect presets (Room..Pipe, read from the disc's LIBSD.IRX; Echo/Delay params, ESA from EEA, work-area clear),
IRQ address matching (voice reads, DMA writes, ADMA ring; libsd behaviour: IRQ-enable bit auto-clears, event `SpuIrq`),
voice/core output written back to RAM areas (0x400.. / 0x1000.. / 0x800..), BlockTrans AutoDMA input (planar
256 L + 256 R blocks, double-buffered halves, loop mode, TransDone per consumed half), VoiceTrans (synchronous),
Note2Pitch/Pitch2Note, ProcBatch/ProcBatchEx.

## Gaps / TODO
* Core MUTE bit (CORE_ATTR bit 14) ignored like PCSX2; `setHonorMute(true)` enables it.
* BlockTrans READ (output capture), real DMA timing/STATX, SPDIF, PS1 mode, ADMA input sync is simplified (ring primed at the
  next ring wrap, so first output is delayed <= 10.7 ms). IOP WRITEIOP batch op is ignored; WRITEEE/EERETURN need `setEeWriter`.
* Reverb register offsets treated as relative to ESA as in PCSX2; no reverb IRQ checks. PCSX2's core-1 DIFF_L/R_SRC swap is kept.
  Effect presets are not built in: `findLibsdEffectPresets` (src/LibsdPresets.cpp) reads them from the disc's own
  IOP/MODULES/LIBSD.IRX, recognised by a hash of the table, and the runtime passes them to `Spu2::setEffectPresets`.
  Without them every mode other than OFF leaves the reverb silent (Drakan never enables reverb).
* ADPCM shift > 12 uses PCSX2 arithmetic (not the PS1 "9" rule). Voice 0 cannot be FM-modulated (hardware).
* Not verified against the real game yet: how Drakan streams music (doc says SPU-destination resource requests + IRQ /
  NAX polling, not ADMA) - the NAX/ENDX/IRQ paths needed for that are implemented and tested synthetically only.
* Interpolation/ADSR numerics are PCSX2's; no bit-exact comparison against hardware captures was done.

Licence note: mixer/ADSR/reverb/ADPCM are ports of PCSX2 (GPL-3.0+), including the Gaussian table (see
THIRD_PARTY.md). The libsd register layer follows ps2sdk freesd (AFL-2.0); Note2Pitch/Pitch2Note are an independent
rewrite (checked identical to the previous code over all 0-127 note/centre and full fine ranges), and no libsd data tables are included.

## Live output controls (`spu2::AudioOutput`, settings overlay)
```cpp
out.setMasterGain(1.0f); out.masterGain();      // linear 0..4, thread safe, initial value = Options::volume
out.setMuted(true);      out.muted();           // mute keeps the SPU2 running (the game polls voice positions)
auto devs = spu2::AudioOutput::enumerateDevices();     // vector<OutputDevice{id, name, isDefault}>, default first, any thread, no start() needed
out.setOutputDevice(devs[1].id);                // "default" | endpoint id | part of the friendly name; re-opens the stream on the render thread
out.outputDevice(); out.currentDeviceId();      // what was requested / the endpoint actually playing ("" while none)
out.renderBlock(float* stereo, frames);         // the single render path: SPU2 render + gain/mute + levels + WAV dump (offline use without start())
```
* Gain and mute are applied in `renderBlock`, i.e. to the device buffer **and** the WAV dump (`Options::dumpPath`, also in the device-less wall-clock mode) **and** the
  level meter, through `spu2::GainRamp` (a 10 ms linear ramp per full 0..1 swing; the first block jumps to the target, so there is no fade-in at start-up). No clicks on
  volume changes, mute or unmute. `Options::volume` still works as the initial gain.
* Device switching: `setOutputDevice` bumps a generation counter; the render thread notices it between buffers, closes the WASAPI stream, opens the new endpoint
  and keeps rendering the SPU2 on a wall clock meanwhile (a short gap, no stalled voices). An unknown id / name falls back to the default endpoint. "default" keeps following the
  Windows default device. `enumerateDevices` initialises COM itself (MTA) and uses the same endpoint ids `setOutputDevice` accepts.
* Tests: `[10]` in `spu2_test` (offline renderBlock at gain 0.5 / 2 / mute / unmute compared sample by sample against an identical unity scene, GainRamp step bound,
  gain applied to the WAV dump of the device-less thread while it runs, enumeration, switching to every device while running).

## Pause and stall watchdog (`AudioOutput::setPaused`, `setHeartbeat`)
```cpp
out.setPaused(true);                       // freeze: the SPU2 is NOT rendered (voices / ADMA rings / envelopes stand still), silence after a 5 ms fade
out.setHeartbeat(&vsyncTick, 250);         // guest liveness: if the counter does not move for 250 ms the output pauses itself (out.stalled())
```
* Why: a streamed voice (the New Game narration, music rings) is refilled by the EE. If the EE is held or blocked (settings overlay, pause_when_unfocused,
  a blocked host call, a hung guest) while the SPU2 keeps rendering on its own thread, the voice replays the last buffered chunk forever ("though the order held
  strong for centuries" on repeat). Pausing the render instead freezes the stream and resumes seamlessly.
* The runtime passes `PS2Memory::gs().vsyncTick` (advanced by the EE scheduler thread every vblank); `DRAKAN_AUDIO_STALL_MS=<ms>` changes the 250 ms threshold, 0 disables it.
  `drakan_pause.cpp` additionally calls `setPaused()` immediately for the overlay and for `pause_when_unfocused`.
* Test: `[11]` in `spu2_test` (pause silence + frozen frame counter + seamless resume against a never-paused twin; heartbeat stall -> silence -> resume).

## Runtime integration (PS2Recomp `drakan` branch, `ps2xRuntime/src/lib/drakan_overrides.cpp`)
`Spu2` is the `hle::ISpu2` for SID 0x1203 and the SPU-destination resource uploads; `sceSdRemote` (EE 0x4b91d8) is hooked natively and served by `SdrServer`
(buffers are EE pointers); `AudioOutput` plays it (`DRAKAN_AUDIO=0` = silent wall-clock render thread; the game polls NAX through the 0x1310 snapshots).
`Spu2::takePeak()` is an output level meter used by `DRAKAN_SND_LOG=1`. Measured in the real game: movie soundtrack = one-shot on core 1 voice 23 (SPU RAM 0x15040..),
menu/level music = looped region on core 0/1 voice 23; ADSR/loop/NAX behave as the game expects (voices end, snapshots park at 0x5000).

## Verified against the real game (2026-10-17, docs/reference/sound-debug.md)
Scripted level-1 run with `DRAKAN_SND_LOG=3`, `DRAKAN_AUDIO_DUMP`: the game uses 48 voices (it allocates from the top: core1 v23, core0 v23, v22 ...), all with ADSR `1e0f/1fdf`, pitch `0x75a`
(22050 Hz; some 0x6af / 0x664 / 0xe46 / 0xecd), volumes written every ~43 ms through 0x1203 `SETPARAM` records. Key-off arrives sample length + ~0.1 s after the key-on (`snd_voice_life.py`), the
streamed voices (music, narration) are refilled through the resource uploads into the buffers the game chose (0x35840 / 0x49840, 0x96040 / 0xaa040, narration 0xf3840), the 500 ms NAX snapshot
(`soundPump`, SID 0x1310) is what the game's voice allocator reads. No defect found in the SPU2 model; the silent positional SFX were the recompiler's SQRT.S bug. Diagnostics:
`DRAKAN_SND_LOG=3` prints `KON c<core>.v<voice> ... f=<frames>` (f / 48000 = time in a `DRAKAN_AUDIO_DUMP` wav) and every VOLL/VOLR write.
