# OotF high sound-resource playback

A private native-sequence fixture verifies an imported sound at Resources id 609, above the former conservative 608 allocator ceiling. It does not validate all imported speech, subtitle synchronization, long-clip fidelity, or a full campaign conversion.

The fixture starts from the audited header-corrected CD09A5 pack. It adds the already converted `I_don't_trust_them` descriptor/sample pair from the private full-speech candidate under Resources ids 600 and 609. Both ids contain byte-identical descriptors (68 bytes) and ADPCM payloads (89,136 bytes). It inserts two ordinary type-3 sound actions into Intro1's existing camera actor, at source seconds 2 and 8, using that SSD's existing Resources dependency index 4. Other sequence actions, actor identity, and normal title/NewGame route remain intact. This is a diagnostic substitution, not a production pack. Its SHA256 is `F557CA81BC778AD2B9B31CF3CC7A6651C9CAB60A1C596A40E61B2EEDBE625445`.

Both runs use staged executable `6D747DB43DFD7C50C39781F5DDD973C6834654C7BF409D8C87CA0A1E0C116D46`, D3D11, ray effects off, world rate 60, fresh private cards, `--audio`, and `DRAKAN_AUDIO_DUMP`. The first run exits 0 after 52 seconds; the traced repeat exits 0 after 54 seconds. Evidence lives under ignored `build/scratch/ootf-audio-native/sound-low-high` and `sound-low-high-logged`.

The traced repeat uses the existing opt-in `DRAKAN_SND_DBG=1` and `DRAKAN_SND_LOG=3`, with no native behavior changes:

| Observation | Low id 600 | High id 609 |
| --- | --- | --- |
| Native resolved sound id | 0x30258 | 0x30261 |
| Play timestamp | 25.358 | 31.358 |
| Sequence sound caller | 0x1a5df0 | 0x1a5df0 |
| Created instance result | 0x8000000c | 0x8000000d |
| Final gain | 0.278612 | 0.278612 |
| Voice | core1 voice23 | core1 voice22 |
| SPU key-on timestamp | 25.375 | 31.375 |
| SPU start address | 0xa9840 | 0xc2840 |
| Pitch / left-right volume | 0x75a / 0x11d4 | 0x75a / 0x11d4 |
| Mixed-output frame at key-on | 1,218,720 | 1,506,720 |

The key-on frame difference is exactly 288,000 output frames, or six seconds at 48 kHz, matching the authored event interval. The first run's mixed WAV also matches the decoded source speech at offsets 25.39075 and 31.39075 seconds, six seconds apart. Normalized correlations over the tested source segment are approximately 0.730 and 0.603 with other native audio mixed in. The lower value is not a quality score; this check establishes source-content presence rather than bit-exact reproduction. Together, the resolved high id, allocated instance, nonzero gain, distinct SPU voice/key-on and captured source content establish actual playback of sound 609.

This removes the specific unverified-high-id blocker for the sound allocator expansion. It does not promote the separate full-speech/animation candidate or alter the coherent CD09A5 progression baseline. Natural campaign speech timings and the remaining conversion warnings still require their own acceptance.
