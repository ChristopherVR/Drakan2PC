# Issue #1: pitched tone from HLE parking voices

## Baseline and attribution

2026-11-04, deployed `dist/DrakanPC/drakan.exe` SHA256
`6001EB904A6FBB29608ED64D48610A2966D33CBB4ECBE2491DD6D9FE1F696F75`.
Ran `scripts/harness/play_run.py` with private cards/options/logs, the ignored
`build/issue-review/audio/walk.txt` recipe, `DRAKAN_AUDIO_DUMP`, `DRAKAN_SND_LOG=3`,
`DRAKAN_SND_DBG=1` and resource logging. One clean scripted exit; Surdana loaded
at 24.325 seconds. Capture: `build/issue-review/audio/baseline.wav` (27.32 seconds),
log: `build/harness/issue1-audio-baseline/stdout_1.log`.
This machine uses Realtek 48 kHz float AUTOCONVERTPCM; the reporter uses AE-7.

Narrow spectral lines in the internal PCM correspond to *parked* voices:

| PCM interval (seconds) | Spectral peak | Parked pitch | Predicted frequency |
|---|---:|---:|---:|
| 24.5–25.5 | 10494 Hz | `0x6ff` | 10494.14 Hz |
| 24.5–25.5 | 11854 Hz | `0x7e7` | 11853.52 Hz |
| 26.5–27.2 | 10196 Hz | `0x6cc` | 10195.31 Hz |

Formula: `pitch / 4096 * 48000 / 2`. The final division is the alternating
sample pattern's Nyquist frequency. The log shows voices re-keyed onto SSA/LSA
`0x5000` after their samples finish while retaining pitch and nonzero volume.
`Spu2::sdInit` labelled this a silent loop, but populated every halfword in its
first block with `0x0707`. Its payload decodes to alternating 224 and 0, rather
than silence. Different parked pitches explain changing tone frequencies.
This locates the reproduced tone before WASAPI and independently of rendering.

## Targeted correction

Initialize the HLE-owned parking loop with its existing `0x0707` loop header and
zero payload. Preserve the adjacent zero block and addresses. Do not special-case
SSA in the mixer, silence arbitrary flag=7 data, alter ADPCM decoding, filter
frequencies or change device output. The change is to the HLE's synthesized
parking audio, not a claim to reproduce the original hardware's initialization
transfer behavior. The old array follows the public freesd initialization data;
that alone does not establish that this HLE render of it is silent.

Sources checked: [ps2sdk freesd initialization](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/libsd/src/freesd.c)
and [PCSX2 ADPCM and loop handling](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp).
No original asset data or translated code is added to the repository.

## Regression and checks

Expanded asset-free `libsd_test` with cold/warm/repeated initialization, both
cores and pitches `0x6ff`, `0x7e7`, `0x6cc`, `0x75a`, `0x1000`, `0x3fff`. Each
case first renders an uploaded synthetic tone with the same loop flags, then
re-keys the same voice onto the parking loop without clearing gain/pitch. It
requires exact zero PCM after the short interpolation history, bounded parking
NAX and ENDX. Uploaded tone remains audible, covering accidental general muting.

* Regression before production change: **36 parking-silence failures**.
* After change: **0 failures**.
* `audio_output_test`: passed.
* Existing data-dependent `spu2_test` with local disc data: **88 passed, 0 failed**,
  including ADPCM reference, one-shot/loop playback, music sample, streaming,
  reverb, output gain/dumping, pause/watchdog and device switching.
* Logs and generated WAVs remain under `build/issue-review/audio/`.

Live rebuilt deployment comparison and system loopback capture pending at this
point. No human listening or AE-7 validation has been performed.

## Rebuilt deployed validation

The standard incremental build deployed executable SHA256
`19EBEDA6A3E876836E38D2455B90CE59F0C0DFAE096CEDF24E4EAA56194CE31E`.
Repeated the same recipe/environment from the same play folder with private
cards/options/logs. Run `build/harness/issue1-audio-after/` exited cleanly (32 s),
WAV `build/issue-review/audio/after.wav` contains 26.53 seconds. Timing varies
slightly, so this is an event-aligned check rather than a bitwise full-run A/B.

The first parked c1.v23 at pitch `0x75a` now has **exact zero PCM** from 13.1 to
13.6 seconds (48,000 interleaved channel samples). The corresponding old parking
gap had a clear 11027 Hz line, -75.5 dB/Hz in its 12.9–13.9 s Welch window. The
new parking-window spectral estimate around that frequency is below -224 dB/Hz
(the longer window includes a small boundary transient); in the exact-zero
subwindow the signal is absent. Music, effects and walking output remain present
later in the new dump. No general frequency filtering was introduced.

Also compiled the existing `loopback_rec.cpp` as an ignored probe and recorded
42 seconds of the default Realtek endpoint in `after-system.wav`. The game
soundtrack is present in system capture. Approximate alignment by soundtrack
cross-correlation places the parking gap around 16.85–17.35 system seconds. The
11027 Hz line is absent there (local PSD about -131 to -134 dB/Hz); the system
stream still has a decaying broadband tail and is not sample-identical to the
internal PCM. This is after-fix output evidence, **not a before/after system
capture comparison**: no before-fix loopback was collected. A separate external
game process started after our own game exited, during the final seconds of the
recorder; the analyzed interval predates it. Other processes were not changed.

This confirms removal of the reproduced internal parking-tone mechanism and
its absence at the same frequency in local system output. The reporter's AE-7,
subjective listening, sustained title-idle/menu navigation, long gameplay and
speech parity remain release acceptance checks; this work does not claim those
were all perceptually validated.
