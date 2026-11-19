# Ruined Village CPU profile at 4K

The [GPU optimization](ootf-ruined-gpu-filter.md) freed GPU capacity in the Order of the Flame opening view. This profile keeps the same game and camera pose, campaign pack, Direct3D 11 settings, 3840 × 2160 output, automatic internal scale 8, HD textures, and 16× anisotropy. Vsync, DLSS, Modern Water, and the frame cap are off.

The first uninstrumented launch measured 87.0 world FPS and 95.5% GPU utilization. Separate instrumented launches put the main game thread at 10.8–11.2 ms of CPU time per completed world frame, the GS worker at 9.8–10.2 ms, and the VU1 worker at 10.1–10.2 ms. These threads overlap. The main game thread regularly waits at the handoff to the workers.

| Thread | Samples | Main paths in the trace |
| --- | ---: | --- |
| Main game thread | 2,917 | Terrain draw 56.3% inclusive, wide terrain chunk draw 24.4%, DMA chain send 10.6% |
| GS worker | 2,090 | GIF packet handling 68.3% inclusive, unpacking individual packed values 55.9%, draw 27.8%, texture resolution 12.5% |
| VU1 worker | 2,118 | VIF1 command processing 71.5% inclusive, vector microprogram execution 57.8% |

The two screening pairs for the existing texture coherence and refresh shortcuts overlapped. Their medians were 81.0 FPS with the shortcuts disabled and 82.0 FPS with them enabled. A separate instrumented pair replaced the guest's busy FINISH poll with an exact blocking read. It cut the poll rate from about 1.5 million to 77 per second. Throughput stayed around 76 FPS in that pair. The build retains the existing choices for both options.

A DMA prototype reused the address and fields already decoded for each tag, then reused the register slot when updating D_STAT. Its two pairs measured a median of 81.5 FPS with the original path and 81.9 FPS with the prototype. The prototype was removed.

## Decode complete packed vertices together

In three sampled Ruined Village capture frames, 615,892 of 630,530 packed GIF payload words belonged to the same four-register sequence. The registers carry perspective texture coordinates, color, fog, and position. The renderer now decodes complete groups through the existing register handlers with the register numbers known at compile time. The generic parser still consumes fragments, other register sequences, and the remaining words at the end of a tag. Each GIF path continues to track its own register cursor.

The initial two screening pairs measured 82.0 FPS with generic decoding and 83.8 FPS with grouped decoding. The comparison below uses a revised control that selects the decoder once for each incoming packet, so the generic control compiles without an added check on each payload word.

## Live comparison

The benchmark alternated five pairs in the same release executable and used a private save card for each launch. It counted completed world frames during the five seconds before the final screenshot. The recorded level, output resolution, and internal scale were checked on every launch. CPU profiling was off during these ten launches.

| Measurement | Generic decoder | Grouped decoder |
| --- | ---: | ---: |
| Median world FPS (range) | 77.6 (76.2–87.8) | 83.5 (79.2–87.8) |
| Median 99th-percentile frame time | 17.23 ms | 17.25 ms |
| Median GPU utilization | 86.0% | 93.5% |

Median FPS rose 7.5%. Four pairs gained 1.6–7.5 FPS. The pair at 99% GPU utilization measured 87.80 and 87.83 FPS.

In two separate instrumented pairs, the GS worker's CPU time per completed frame fell from 9.46 to 8.14 ms at the median, a 14% reduction. Both pairs improved. The scene still averaged about 447 DMA chains per completed frame.

The Direct3D 11 WARP replay produced identical frame hashes with the two decoders. It rendered a textured scene with perspective coordinates, fog, color, and an ADC-suppressed triangle. It split the commands four ways and interleaved an unfinished tag on a second GIF path during one of the frames.

## Installed build

Grouped decoding is enabled by default. `GSR_PACKED_VERTEX_GROUPS=0` restores the generic parser. A separate launch of the installed build confirmed the decoder was active and recorded 86.7 FPS at the same 4K settings.

Five renderer tests passed, and the rebuilt replay produced the same five frame hashes with either decoder. The installed build played from a copy of the mounted Wartok cave save and started a fresh Surdana game. Checksums for all 16 protected save, settings, and campaign pack files matched the pre-test snapshot.

Scripts, symbolized CPU profiles, frame timings, and captures are in `build/ruined-cpu-20261119/`.
