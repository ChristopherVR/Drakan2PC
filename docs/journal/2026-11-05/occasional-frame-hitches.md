# Occasional frame hitches after the scratch readback fix

## Attribution

In the final 6F8ACB44 Isles combat capture, the 166 / 176 / 200 ms gaps at uncapped / 30 / 60
coincided with the 3840x2160 attack screenshot. The present thread read the current image and encoded and wrote
its PNG before presenting. The earlier 135 ms stationary gap included 122 ms processing a GS
record, with maxima of 88 ms in model motion guides and 32 ms waiting for reconstructed surfaces in the
corresponding profiler window. The guide and drain numbers are profiler-window maxima, not a per-event breakdown.
Ordinary play and screenshot hitches are measured separately below.

## Changes and controlled checks

- The PNG encoder packs its RGB scanlines directly, reduces its Adler sums in bounded blocks, computes the CRC
  four bytes at once and avoids copying whole chunks through a temporary buffer. PNG delivery and on-disk bytes
  retain their existing semantics. In an alternating CPU-only comparison, 4K write time was 135 / 152 ms before
  and 39 / 44 ms after. Five fixtures including 1x1, stored-deflate boundary sizes and 4K were byte-for-byte
  identical, and their PNG chunks passed independent validation.
- Preparing motion guides assigns the texture slot once for each complete triangle using reusable indexed storage.
  Ambiguous duplicate object IDs are always rejected, so sorting the previous instances does not require preserving
  equal-key order. The dynamic vertex ring grows in 8 MiB steps to reuse allocations across small successive peaks.
  A release-mode probe with 98,304 moving vertices and 64 alpha materials measured median preparation at 0.90 /
  0.91 ms before and 0.63 / 0.63 ms after. This is isolated preparation time, not total frame time.
- The render-target upload paths share native and scaled pixel scratch buffers owned by the D3D11 renderer.
  They grow on demand and overwrite the requested rectangle completely. Repeated uploads avoid reallocating and
  zeroing buffers, including when alternating small and large targets. The source rectangle, scaled pixel pitch,
  target update, row stamps and dirty-region decisions follow the same rules as before.

CPU probes are in ignored `build/perf-hitches/`. Game runs use the canonical incremental build and deployed play
folder, private graphics/native settings, 4x scale, DLSS Quality, ray effects enabled, audio on and vsync off.
The test save is isolated per run. The screenshot case is the third-person dragon encounter in Andrellian Isles.

## Game measurements

Receipts are in `build/perf-pacing-fix/`. The earlier comparison build is 6F8ACB44. The PNG and model-motion
build is 5CA6AC54. Build F2587A24 adds the reusable upload buffers. Each timing interval excludes warm-up;
the short combat runs include one mid-fight screenshot and two 32 MiB RAM dumps. To isolate ordinary frame
times, the analysis excludes those events and two neighboring presented frames on either side.

With the same frozen 4x settings, before/after the upload-buffer change (`bench-140752.json` /
`bench-141539.json`), exclusive syncRT time was 0.728 / 0.647 ms per present in Catacombs and 1.103 / 1.004 ms
on the Isles. Worst observed stationary gaps were 21.4 / 16.7 ms and 46.7 / 37.9 ms respectively. These are
short matched scenes and individual maxima vary. Overall FPS in those pairs was 102.1 / 99.2 and 101.5 / 99.6;
this does not establish an average FPS gain.

System load materially affected early combat measurements. Unrelated compilation occupied all CPU cores and
the machine paged heavily during the first candidate sequence. Its first uncapped run fell to 50 FPS.
A later uncapped repeat on the same executable measured 97.9 FPS. No other processes were terminated.
The 30 FPS F2587A24 stress run included a 360 ms gap with two long GS records; the profile recorded a long
wait for the surface builder. Treat this separately from the repeatable cost of screenshot encoding.

## Motion-guide vertex uploads

Temporary per-stage timing on a minute-long Isles fight located 32 model vertex-buffer maps over 5 ms in the
uncapped measurement window (387 ms total, worst 28.6 ms). At 30 FPS one map took 54.7 ms. Tracing the
map mode on the next build identified the waits on WRITE_DISCARD when the ring wrapped. No screenshots or
RAM dumps ran between the timed interval markers in these fights.

The default path now projects the same vertices with the existing CPU routine into reusable CPU storage and
issues UpdateSubresource for the used byte range of a default vertex buffer. The immediate context orders the
range update with earlier and later draws. Invalid projections are rejected before submitting an update.
`GSR_MODEL_MOTION_UPLOAD=0` selects the previous dynamic-buffer map path. An added GPU regression
alternates motion direction across eight large draws to verify the latest range is read when the vertex ring
wraps. The guide suite passed with both paths, including depth, alpha rejection, reset, and context restoration.
The model-motion, world-readback, partial-upload and image-upload suites also passed during the investigation.

Same diagnostic executable 4671CB36, identical 40-second firing sequence and input/configuration, switch order
reversed for the second pair. The table sums only individual upload/map calls over 5 ms. It is not total GPU time.

| Pair | Path | Uploads over 5 ms | Sum / max of those calls (ms) | FPS | p99 frame (ms) | Max frame (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `bench-142216.json` | Dynamic map, first | 29 | 397.2 / 50.45 | 80.82 | 22.59 | 82.56 |
| Same | Queued update, second | 1 | 14.9 / 14.91 | 83.26 | 18.91 | 103.98 |
| `bench-142505.json` | Queued update, first | 3 | 18.7 / 6.72 | 52.93 | 83.41 | 404.46 |
| Same | Dynamic map, second | 22 | 250.2 / 30.16 | 60.83 | 33.23 | 210.58 |

Both orders showed lower exposure to long upload calls. Whole-frame results were mixed. In the second pair the
geometry worker received about 16 ms of CPU during 243 ms of wall time on the new path, and about 16 ms during
193 ms on the old path (Windows thread CPU accounting has coarse granularity). On the first pair the new path
also saw a 66 ms wait with about 63 ms of worker CPU. The captured waits include real geometry work as well as
time the worker could not run. Temporary diagnostic probes were removed before the final build.

Screenshot stalls were measured independently. On 6F8ACB44 the attack capture spanned 166.4 / 175.6 / 199.7 ms
at uncapped / 30 / 60. On F2587A24 the uncapped attack capture spanned 64.1 ms; the 30 FPS capture was
111.0 ms. The screenshot still synchronously reads and writes the requested frame. Its timing includes GPU
readback, PNG encoding, file I/O and the current present cadence.

## Final deployed build

SHA-256 `7aaf661eb2069daf99656262c829b702bb361a9f59a7cf54f19e50aae80c121f`.
The longer fight fired repeatedly for a minute with no screenshots, RAM dumps or detailed GPU/profiler logs
inside the timed segment. All three runs completed on the final executable with unchanged private source settings
(`bench-142917.json`). These are single-run timings on a shared workstation. At the end of the uncapped test
sequence the machine again approached CPU saturation and paged heavily.

| Display setting | FPS | p99 ms | Worst frame ms | Engine time / wall time |
| --- | ---: | ---: | ---: | ---: |
| Uncapped | 51.47 | 55.89 | 195.94 | 0.9973 |
| 30 | 29.99 | 33.62 | 40.35 | 0.9997 |
| 60 | 59.01 | 21.48 | 193.80 | 0.9981 |

Long frames still occurred during gameplay. The per-stage A/B measurements support reducing the model upload wait;
overall FPS and isolated maximum frame times also depend on geometry preparation, scheduling and host load.

The final 13-map sweep (`bench-143512.json`) loaded each playable map, captured its start/end views and
exited by script. At least **93,000 verification calls compared 813,970,112 pixel values with zero mismatches**.
These checks include skipped GPU readbacks and mirrored pixels. The model-motion buffer has its separate
GPU test of guide pixels. The map timings include deliberate synchronous verification, so they are used for
correctness. Both PNGs in every run passed integrity checks. Reviewed end views are in
`build/perf-hitches/final-maps.jpg`; Valley reached its cutscene.

| Map | Compared pixels |
| --- | ---: |
| surdana | 68,803,648 |
| kragmor | 64,769,088 |
| catacombs | 106,322,880 |
| city | 40,163,456 |
| inquisitor | 66,788,096 |
| shadowmire | 46,240,064 |
| isles | 71,966,720 |
| crystal | 51,510,976 |
| ravenshold | 68,334,720 |
| tundra | 46,433,536 |
| shiverbane | 53,747,904 |
| valley | 66,312,384 |
| stratos | 62,576,640 |

## Trace-free A/B on the deployed executable

The final 40-second comparison (`bench-144736.json`) used the same executable and recipe, with
`GSR_MODEL_MOTION_UPLOAD=0` then `1`. FPS was 60.47 / 69.79, p99 was 26.61 / 22.25 ms, and the worst
frame was 69.44 / 85.82 ms. Both runs advanced the engine clock at 0.9997 times real time. A concurrent
one-second CPU sampler (`build/perf-hitches/final-ab-load.json`) estimated median CPU use outside the game
at 72% / 58% respectively. The predefined under-35% background-CPU subset contained zero full seconds
for the old path and only four for the new one. A quiet-to-quiet throughput comparison was unavailable.
The measured decrease in long upload calls across both diagnostic switch orders is the consistent result.
