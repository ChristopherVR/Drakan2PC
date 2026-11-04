# Ray GPU stage timing and attribute uploads

The stationary and all-map performance notes identify two separate limits:
ray-frame-slot waiting in some views, and renderer readback-map waiting in
several other maps. CPU times on separate threads overlap. The existing DXR
benchmark's wall time includes shared-fence and readback synchronization; it
does not isolate opaque transport shader cost.

`GSR_RAY_GPU_TIMING=1` now records five D3D12 timestamps per pass, reporting
upload/acceleration-structure work, sky reduction, opaque transport, final
composition, and their total as `[ray-gpu]` lines. Each frame slot owns its query
heap and readback buffer. Results are read only after its existing completion
fence, or after the normal final drain. Profiling adds no new completion wait.
The GPU interval excludes the producer-fence wait, CPU preparation, presentation
and final shared-resource transitions. Logging/query overhead is enabled only
for measurement. Device scheduling contention can still affect GPU timings.

The source review also found that cached native attributes were copied into
staging and uploaded in full every pass. The DXR material buffer belongs to one
ordered queue, so an exact CPU shadow of its last submitted dynamic attribute
region can identify unchanged bytes safely. `GSR_RAY_UPLOAD_CACHE=1` compares
128-triangle blocks, merges adjacent changed blocks, and uploads only those
ranges. Changed static-region size or dynamic-region length invalidates the
comparison. Static attributes and texture texels retain their existing separate
invalidation rules. `GSR_RAY_UPLOAD_CACHE=0` restores complete dynamic uploads.
Timing lines include uploaded bytes and the complete dynamic-region size.

This cache adds CPU comparisons and retained attribute storage. Its performance
benefit depends on how much of a scene changes; lower upload byte counts alone
do not prove a frame-time gain. Use the same executable, fixture, effect flags,
warm-up and background load for off/on comparisons. Validate changed materials,
geometry, texture-table bases, level changes and unchanged recaptures before
deployment. Build, hardware regression and benchmark results are recorded by
the integration run; no measured performance gain is claimed here.

## Initial validation and instrumented sample

Cache-off and cache-on hardware regression snapshots are byte-identical,
SHA-256 `e9bdda5e9947a72c1bcaa74baf899d552e5c67a04c1d7e3549ce80c220ad430d`.
The regression includes 514 frame triangles with independently visible material
changes in separated 128-triangle blocks, simultaneous restoration, exact
recaptures, shrink/grow and adding/removing a static triangle prefix. These
checks exercise both dirty-range detection and the dynamic destination offset.

An instrumented moving sample at 1364-pixel render extent retained 683 samples
after discarding the first 100 passes. Stage medians were:

| GPU stage | Median |
| --- | ---: |
| Upload and acceleration-structure build | 0.7521 ms |
| Opaque transport | 5.9406 ms |
| Composition | 5.0811 ms |
| Complete measured ray interval | 11.868 ms |

Separate stage medians do not sum to the median complete interval. Dynamic
material uploads were 45.54% of the complete dynamic attribute bytes: 54.46%
were skipped. Another Drakan process (PID 50480) and a concurrent CTest run were
detected during this sample. It identifies substantial transport/composition
cost and demonstrates reduced upload volume; it establishes no controlled
speedup or representative uncontended frame rate.

The pre-final complete CTest run contained 109 entries: 103 passed, six skipped,
zero failed, in 229.55 seconds (`build/rt-quality-full-tests.log`). This run
preceded the transport representation correction below. Final tests and
captures for that correction are still pending; the earlier result must not
be presented as validation of the later shader revision.

## Transport filtering correction

The first half-resolution reconstruction stored a signed RGB correction:
incoming light minus reflected energy removed from the sampled native pixel.
Filtering that correction transferred one texture's colored subtraction onto
a differently colored neighboring texture. Even neutral incoming illumination
could therefore produce complementary red/blue patches. This is a representation
defect rather than evidence that the scene contains colored bounced light.

The revised intermediate stores incoming RGB and scalar reflected energy
separately. Reconstruction filters those quantities, then applies attenuation
to the full-resolution receiver's own native color. The half-resolution pass
also stops evaluating shadow/ambient occlusion: the existing lighting path
already owns it, so it need not be repeated to construct transport. DXR keeps
its shadow evaluation at final resolution; WebGPU retains its existing separate
shadow reconstruction.
Depth, normal, material and color rejection still bound the spatial filter.
This change is intended to prevent neighboring texture-color subtraction and
remove duplicated half-resolution shadow work. Its final image quality and
performance require the pending hardware tests and gameplay captures.

## Final integration and gameplay checks

- Final renderer tests pass: `build/rt-quality-final-ray-tests.log` (DXR and
  WebGPU, 2.67 seconds). The colored-stripe WebGPU fixture initially had a gap
  in its constant-radiance enclosure; fixing the ceiling vertex order reduced
  error to 1 LSB without relaxing the 3 LSB assertion. DXR verifies local hue
  preservation against a black reflector. The complete runtime build passes
  (`build/rt-quality-complete-build.log`).
- Final cache-on/off captures are byte-identical (6,815,744 bytes each), SHA-256
  `86d9867f2a776b4faf9357c8bb145b1bbb9edd86bed65f5e93e74b6249eb0d2c`.
  Logs/snapshots: `build/rt-quality-final-cache-{on,off}.*`.
- Standard incremental builder deployed executable SHA-256
  `05b47b6da96eee5ea289902a6e36e0c69fa96eaa63f43715e778705f8c9117d3`;
  build and dist hashes match. Receipt: `build/rt-quality-radiance-deploy.log`.
- Same-frame Surdana comparison completed cleanly in
  `build/harness/rt-quality-final-retry-surdana-20261104/`. Final DLSS Quality
  moving-camera capture also completed cleanly (53 seconds), in
  `build/harness/rt-quality-final-dlss-moving-20261104/`; screenshots inspected.
  Colored terrain reflection bands remain visible. GI-only isolation is clean;
  do not claim the signed-filter fix resolves all scene reflection artifacts.
- Final moving-run medians after 100 warmup passes, 682 samples at 1364x1364:
  build 0.7522 ms, transport 3.7475 ms, composition 4.7444 ms, total 9.2609 ms.
  Dynamic uploads were 45.01% of their full byte count. Earlier same-recipe
  transport median was 5.9406 ms and total 11.868 ms, but concurrent games and
  test workloads changed: these are diagnostic observations, not a controlled
  speedup claim. Synthetic all-effects GPU medians similarly changed from
  2.76982 to 2.29955 ms; benchmark validates 524,288 affected opaque pixels.
- First final gameplay attempt failed during startup, before any ray dispatch:
  movie-screen skip handler called a null target at guest source 0x440708.
  Only this task's stalled capture process was stopped. The retry succeeded;
  unrelated sessions were left running. This is recorded separately from RT
  correctness and does not establish an RT regression.
- Repository guard passes (2,294 approved tracked files, zero prohibited), and
  touched C++ formatting/diff whitespace checks pass. A concurrent backend
  signature change required updating the native-display test fake's new
  `written` argument; no production behavior was changed by that test fix.
