# Optional DLSS integration: actual status

The DLSS 5 neural rendering bridge (`gsr_nr`, `nr_prototype`) was removed on 28 October 2026: it depended on an options
layout NVIDIA does not publish. DLSS Super Resolution / DLAA are unchanged. Sections below that describe the neural path
are history.

## Still world under the pause menu, 24 October 2026

Behind the pause menu (0x17) and the pages it opens (Load 4, Save 5, Options 9, Controller 10, Audio 13) the
game holds the world still but submits it every frame, so SR evaluated a static image with a new Halton jitter
each frame and thin alpha-tested edges (grass, leaves) crawled. Without DLSS those frames are bit-identical.
Both world entries (`enterWorld`, `enterTemporalWorld`) now use zero jitter while `hud::frozenWorld` names
such a screen; history is kept. Surdana pause frames 30 samples apart: mean difference in the world region
0.25 -> 0.0007 (max 15 -> 10 levels, the remainder is the HUD text fade).

## Water and shadow shading upgrade, 23 October 2026

The DXR pass now shades water with an eight-wave world-space spectrum (sub-pixel waves become GGX
roughness), glossy reflections, slope-driven refraction, hue-aware Beer–Lambert absorption, bed
caustics, crest scattering and shoreline foam. `ray_shadows` gives soft directional and point-light
shadows from per-pixel rotated samples (resolved temporally by DLSS), a terminator bias against
low-poly self-shadow stripes, and ray-traced ambient occlusion of the native ambient term (1 m
reach, at most 60% of it, open ground unchanged). Native models neither shadow nor occlude themselves, and
occluders across a shallow crease within 2 m are ignored, so faces and terrain show no facets. Direct light uses
welded per-vertex normals (7 float4 attributes per ray triangle), and receivers drawn at a coarser terrain LOD are
matched to the resident grid vertically
([ray-shadow-facets.md](../journal/2026-10-24/ray-shadow-facets.md)). World scale is about 500 units per metre. Details, test changes and
matched gameplay frames: [water-shadow-upgrade.md](../journal/2026-10-23/water-shadow-upgrade.md).

## Reflection and model-guide follow-up, 19 October 2026

The current source review found a runtime shader compilation failure in the
new model-motion path: `texture` is reserved by the D3D11 HLSL compiler. Renaming
the shader member to `textureIndex` restores guide compilation, including the
shared preparation shader used by SR/NR. Nonfinite current or previous instance
opacity now rejects model motion instead of bypassing the opacity check.

Ray-traced water now fades screen-radiance confidence over relative reverse-Z
error rather than abruptly switching to exported material color. The absolute
depth epsilon was removed so faraway unrelated surfaces cannot pass on that
epsilon alone. A GPU regression crosses the old cutoff: maximum display-channel
jump is 0.000244141, while a valid screen sample still contributes 0.251709.
This addresses a shading discontinuity; it does not prove that the previously
observed snowy-river coverage transition is resolved.

Point-light visibility now traces one ray instead of four identical rays.
Directional lights retain four samples. This reduces that operation's ray count,
not a measured whole-game frame-time result.

Validation: six focused Release renderer tests pass (scene geometry, scene
models, model motion, temporal presentation, DXR water/shadows, and neural
guides). The separate hardware guide run passes with no D3D11 debug warnings,
including posed-model motion, alpha cutouts, jitter, depth rejection and cuts.
The model raster test uses the existing moving-axis 0.003-pixel tolerance on
both axes because WARP leaves a 0.000195-pixel interpolation residual; reset and
camera-only zero-motion assertions remain exact. Evidence is saved under
`build/graphics-followup-20261019/`.

The subsequent requested deployment waited for the competing build to end,
removed its two empty AOT object outputs, and successfully rebuilt the complete
Release game. The broader synthetic renderer suite passed all 187 checks.
(The ShowCursor/FORCE linker warnings of that build are fixed since: strict linking, no `/FORCE`.) The deployed executable is
SHA-256 `cc55bc7f8fb350e579d1c55c3aac9d4c854bcd274c2ffbc7085bba5a524d9b94`.
Prior executable, settings and build metadata are preserved in
`build/graphics-followup-20261019/deployment-backup/`.

Three private-save gameplay runs completed normally: snowy river water,
Surdana pond shadows, and walking/strafe/turn movement with SR Quality and NR.
Same-frame effect comparisons exclude HUD/host overlays using the world ROI
`(300,100)..(1620,800)`. Water changed 175,160 pixels; shadows changed 388,093;
neural rendering changed 923,467. These are operation checks, not quality scores.
The motion capture at world epoch 200 shows 30,966 pixels changed by the native
model-motion pass, with Rynn's moving silhouette visible in the difference
image. Both SR and NR color readbacks are finite. The actual 1920x1080 D3D12
swapchain matches the finished D3D11 presentation with zero differing bytes.
All original, source-fixture and private save hashes were unchanged.

Visual inspection still shows the snowy river's foreground water coverage
boundary. This is not fixed by smoothing reflection shading confidence. Model
motion is now demonstrated during this movement sequence, but comprehensive
animation/material coverage, long-session stability and performance remain
unverified. Deployment enables Quality, neural rendering, modern water,
reflections and shadows; it preserves other settings. Evidence and reproduction
scripts are under `build/graphics-followup-20261019/`, with the final installed
executable smoke run under `dist/t/graphics-followup-deployed/`. That fourth
run also exited normally with unchanged saves, finite neural output, four
native shadow lights, matched moving-model geometry and a zero-byte swapchain
comparison. `deployed-gameplay.png` is the installed executable's actual
swapchain capture. Instrumented captures are not a performance benchmark.

## 19 October 2026 review

The current renderer builds with native-light DXR shadows, resident and proven
posed model geometry, water reflections, SR Quality and neural rendering in the
same pipeline. These are separate rendering stages; neural output processes
the rendered models and does not replace their meshes.

This review fixes two reproduced defects. Model-motion guides now match GS
depth within four float ULPs, preventing the old absolute tolerance from mixing
distant reverse-Z surfaces. Near-plane saturation also rejects object motion.
Fully faded model instances are omitted from the ray scene, so they cannot
consume transparent reflection layers or become primary shadow receivers.
Both added GPU regressions fail on the previous implementation and pass after
the fixes, including one-ULP valid motion, cuts and multiple invisible props.

Evidence is under `build/dlss-review-20261019`: Release game build; nine selected
renderer tests; 187 synthetic checks with zero failures; hardware guide tests
with no D3D11 debug warnings; real RAM/ISO model export and pose-change checks.
The synthetic suite initially exceeded a 60-second timeout and passed in 103
seconds with a 180-second limit. The combined coast gameplay run quit normally,
preserved all original/source/private saves and produced finite neural RGB.
The actual 1920x1080 swapchain matched the finished presentation image with
zero differing bytes at evaluation 200. Logs confirm native-light shadows and
DXR water in that run. These checks do not establish whole-game performance.

The snowy river still has a sharp reflection transition in the foreground.
Animated coverage requires proven pose ownership; the stationary coast run
did not establish moving-character motion-vector coverage. Full material,
environment and whole-game visual quality remain incomplete. Ray Reconstruction
and frame generation are not implemented.

The [resident model reflection follow-up](../journal/2026-10-18/reflection-scene-review.md)
adds offscreen visible props, native UV/material export, read-only texture
loading, alpha cutouts and instancing. It removes hidden helper-box reflections
and fixes CLUT sampling. Final DLAA/Quality + neural swapchain audits pass;
animated-model, material and later-map limits remain documented.

The [native water and resident geometry follow-up](../journal/2026-10-18/water-global-review.md)
removes the Surdana tint restriction, adds offscreen terrain/ceiling geometry and
textured ray hits, and fixes late water overwriting the processed shoreline.
It includes final-build matched gameplay captures and another swapchain audit.
Unseen props and full material coverage remain incomplete.

The [18 October presentation/water review](../journal/2026-10-18/dlss-water-review.md)
adds presentation-backed runtime status, a same-world neural off/on comparison,
actual swapchain readback verification, and an optional experimental DXR water
pass. Its material and reflection coverage limits apply; DLSS alone does not
create new water or ray-traced reflections.

Updated 2026-10-18. **DLSS 5 Neural Rendering is integrated and has evaluated live Drakan gameplay at 2048x2048 on the RTX 5080.** Super Resolution / DLAA can now run before NR in the merged renderer. The matching NVIDIA-signed Streamline 2.13.0 / NGX 310.8.0 runtime bundle is used for coexistence. Both features remain optional and off by default.

## Enable neural rendering

The 18 October build retains the orb-addressing fix, adds GPU texture-cache
coherence and reduces framebuffer upload work. A longer matched SR Quality + NR
comparison measures **29.976 versus 28.633 unique world submissions/s** over
the final 20 seconds, a 4.69% gain. This is one scene at scale 4, not a whole-game
60 FPS claim. Initial catch-up frames are excluded. See the
[latest receipt](../journal/2026-10-18/followup.md) and
[measured comparison](../journal/2026-10-18/data/performance-20261018.json).
Host ticks, GS vblanks and unique world submissions are reported separately.

The 18 October replacement build completed **1,794 SR and 1,692 NR evaluations**,
four NR off/on toggles and **23/23 inventory/movement/bow assertions** with audio
enabled. A separate melee/audio encounter records 420 successful evaluations of
each feature and enemy damage. The receipt records remaining visual artifacts;
successful evaluations do not establish artifact-free output.

Open F1 in gameplay, then Video → NVIDIA DLSS → **DLSS 5 neural rendering**. The switch applies live and saves as `[video] dlss_neural = 0|1`. It does not select an SR quality mode. The four matching DLLs belong in `<exe>/nr`; the local dist deployment includes the user's supplied bundle. A different folder can be selected with `[video] dlss_neural_runtime` or `DRAKAN_DLSS_NEURAL_DIR`. `DRAKAN_DLSS_NEURAL=1` overrides the initial switch.

The bridge verifies the four audited hashes and Authenticode signatures before loading. An unsupported GPU, absent bundle or initialization failure leaves ordinary rendering available and reports a reason. Present/resize failures retire the bridge and restore ordinary presentation where the device permits it. No vendor binary is tracked in source control.

For combined operation, also select an SR quality mode in the same Video menu
(`[video] dlss = quality|balanced|performance|dlaa`). The matching supplied
`nvngx_dlss.dll` belongs beside the executable; the four NR DLLs stay in `nr`.
The SR SDK/link library is 310.9.16, while the validated loaded runtime is 310.8.0.
The separate 310.9.16 SR DLL rendered with NR, but intermittently failed full NR
reinitialization in the isolated lifecycle matrix; it is not the combined runtime
selection. Model presets use runtime defaults unless explicitly changed.

## Live integration

- The existing D3D11 renderer feeds a same-adapter D3D12 NR session through shared textures/fences. The upgraded D3D12 swapchain handles every Present, including NR-off frames after initialization.
- Engine hooks capture the true world view/projection and viewport, excluding object-local MVPs and previews. The precise world-to-overlay call at `0x147690`, entry RA `0x139344`, orders NR before game HUD callbacks. Earlier asynchronous VU work drains before scene records enter the GS queue.
- Exact-size RGBA16F color/output, R32F reverse-Z depth and RG16F pixel motion come from the active world target. GS FTOI4 Z, actual XYOFFSET and infinite reverse-Z projection are included. SR evaluates at the reduced render size, reconstructs output-sized depth/motion guides, then NR evaluates at output size (default; `dlss_neural_resolution = render` instead runs NR on the reduced raster before SR, see "Neural rendering resolution"). RGB composition preserves GS alpha; the SR path reconstructs current input alpha separately. Native HUD draws follow both features.
- Stable engine object/mesh/part identities, pool generations and immutable VIF-job sidecars associate projected vertices across accepted temporal samples. A separate depth/alpha-matched velocity pass adds eligible geometry motion to camera fallback. Perspective interpolation uses current/previous clip-W ratios. Unproven correspondence is rejected.
- Capture is dormant when neither NR nor SR is requested. Enable, cuts, resize, geometry changes and failures reset history. A bounded renderer/window-thread handoff presents the completed world and HUD before another submitted world can overwrite the latch. Present and ImGui retain their host thread. HUD preview cameras no longer erase the real world-camera anchor.
- The isolated backend passed 72 evaluations / 96 presents, repeated toggles, resize, output preservation and same-window reinitialization. Synthetic guide tests checked known camera shifts, finite output, all 256 alpha values and context-state restoration. Live combined test details are recorded in the release integration receipt.

### Neural rendering resolution

`[video] dlss_neural_resolution = output | render` (env `DRAKAN_DLSS_NEURAL_RESOLUTION`; F1 → Video → NVIDIA DLSS →
*Neural rendering resolution*; live, resets NR/SR history; default `output`). It only matters with a reduced SR mode.

- `output`: SR, then NR on the SR output (scale 4: 2048x2048). NR GPU 6.85 ms in Surdana.
- `render`: NR on the reduced raster (SR Quality: 1364x1364) before SR; SR upscales the NR image and keeps the GS
  alpha (`sr_combined::Inputs::alphaRender`). NR GPU 3.68 ms (−3.2 ms/frame). Stills are very close (slightly
  coarser grain); motion not yet compared.
- The NR bundle (sl.dlss_nr 2.13 / nvngx_dlssnr 310.8) has no working internal upscale: performance modes 1–4 fail or
  are unsupported with a reduced input, a reduced input subrect is processed 1:1, and mode 3 costs the same as mode 6.
  NR keeps the raster jitter (its output follows the sample positions; `jitterOffset` has no effect), so a jittered NR
  image is a valid SR input. No frame-skipping mechanism is used.
- `GSR_NR_GPU_TIMING=1` prints the NR GPU time (`[nr-gpu]`).

Details and measurements: [nr-resolution.md](../journal/2026-10-23/nr-resolution.md).

### Remaining limits

Moving-object coverage is partial. Verified indexed/sequential paths can include
bone-deformed sources; CPU-clipped direct-GIF paths, unmatched inline groups,
particles and mixed transparency retain conservative camera/invalid fallback.
Fast motion and disocclusion can still produce artifacts. Live readback measures
motion distinct from camera fallback; it is not a ground-truth optical-flow or
image-quality comparison. SR reconstructs guides and alpha from its input, not
new full-resolution geometry. Runtime-default NR controls are used; no unverified
model-letter mapping is exposed. Frame Generation and Ray Reconstruction are not
added. Combat evidence and its remaining gaps are recorded in
[combat-encounters.md](../journal/2026-10-17/combat-encounters.md). Long runs and device-loss recovery
remain unverified. The older prototype limitations below describe the earlier
path, which is no longer used for combined world rendering.

Browser portability: the camera packet is standard C++; all NVIDIA, Win32 and D3D types remain in the optional native renderer/bridge. A browser renderer can leave NR unavailable. No browser build is claimed.

## NVIDIA version and SDK evidence

NVIDIA describes DLSS 5 as a final neural rendering stage using engine color and motion information to enhance lighting/material appearance, with developer controls. This is a separate integration from Super Resolution, DLAA, Ray Reconstruction and Frame Generation. See the [September 22 NVIDIA developer article](https://developer.nvidia.com/blog/whats-new-for-game-developers-dlss-5-with-3d-guided-neural-rendering-nvidia-ace-updates-and-new-rtx-kit-capabilities/).

NVIDIA's [October 13 update](https://www.nvidia.com/en-us/geforce/news/dlss-5-faster-ai-model-hogwarts-legacy-rtx-spark-laptops/) says DLSS 5 launched in September in NBA 2K27 and announces a faster neural rendering model for October 16. Its RTX Remix update is announced for October 23, still in the future at this audit date. DLSS 5 exists; describing it as a rumor or relabeling this SR prototype as V5 would be incorrect.

The [NVIDIA developer download page](https://developer.nvidia.com/rtx/dlss) separately lists SR/DLAA, RR and frame generation. Its October 2026 Unreal package note identifies DLSS 4.5 with SL 2.14.16 / NGX 310.9.16. An NGX/DLL version does not establish a DLSS product generation.

Earlier public SDK audit: `tools/dlss/dlss_sdk`, commit `9981c6350dc58c0ce21f024fd65e31e851c31689`; release `nvngx_dlss.dll` file/product version `310.9.16.0`. Streamline checkout: `087f076eaa34e89010269b90d472234953670d3a`. Those inspected packages contained SR, RR and FG components, without the later supplied NR bundle. The SR code calls `NGX_D3D11_CREATE_DLSS_EXT` / `NGX_D3D11_EVALUATE_DLSS_EXT`; the new NR bridge uses the separate Streamline feature 1004.

**Earlier V5 investigation:** the local-bundle probe and subsequent continuous/game tests supersede its missing-runtime and integration gates. The public-package findings below are historical artifact audits. Replacing the SR DLL or selecting an SR preset does not implement V5.

## DLSS 5 SDK/API follow-up (2026-10-17)

Earlier bounded research inspected public packages and community source. The later user-supplied package and passing support probe above supersede its local-runtime blocker; the package inspections below remain accurate for the particular public archives examined.

| API/package | Finding |
| --- | --- |
| Vulkan NR | NVIDIA's [pinned sample CMake](https://github.com/nvpro-samples/vk_gltf_renderer/blob/4e02cfe5a759adb5d274e1ed88486aab5829e8df/CMakeLists.txt#L342-L350) requires the external beta header `nvsdk_ngx_helpers_dlssnr_vk.h`. Its [NR source](https://github.com/nvpro-samples/vk_gltf_renderer/blob/4e02cfe5a759adb5d274e1ed88486aab5829e8df/src/dlss.cpp#L459-L619) uses NR-specific Vulkan create/evaluate helpers. This is concrete API evidence, but the inspected public packages do not supply that beta dependency. |
| D3D11/D3D12 NR | At the public-package audit stage the NR contract was not established. The later supplied plugin explicitly declares D3D12/Vulkan, and the isolated D3D12 NR evaluation now passes. Native D3D11 NR is unsupported in that runtime. [Community source](https://github.com/MotionflowOffical/UniversalDLSS5/blob/d483b6f0416fcad7ecd5ef703eb1eafc3fb3c96b/src/neural/ngx_nr.cpp) suggests shared textures/fences for interoperability; its caller/forwarder workarounds were not used in our passing probe. |
| Public packages | Downloaded Streamline 2.14.16 x64: 511 entries, NR identifier/changelog but no NR-specific files. Current UE 5.8 v8.9.0 ZIP directory: 1,104 entries, no NR-specific filenames. Public NVIDIA/DLSS source tree likewise had no NR-specific paths. These bounded artifact findings do not assert that no private SDK exists. |

Earlier SDK follow-up recommendations (not prerequisites for the implemented local path):

1. Request the custom-engine NR evaluation package through [NVIDIA Developer Contact](https://developer.nvidia.com/contact), category **NVIDIA GameWorks Support**, or ask in the [DLSS developer forum](https://forums.developer.nvidia.com/c/gaming-and-visualization-technologies/dlss/493). These are verified general channels; no NR-specific application/eligibility process was found.
2. Request the matching NR headers, guide and distribution terms for the supplied runtime; confirm supported interoperability, registration and color/depth/motion/mask/reset requirements. The local runtime and synthetic evaluation prerequisites are already satisfied; the remaining published-contract and game-integration questions are separate.
3. Recheck the [official DLSS downloads](https://developer.nvidia.com/rtx/dlss) and [Streamline releases](https://github.com/NVIDIA-RTX/Streamline/releases) for a published NR contract. NR remains opt-in in the combined build.

The research reports and the unsent access-request draft were kept in the former owned runtime checkout (`tools/PS2Recomp-dlss`, not part of this repository). They described the earlier public-package audit. The subsequent authorized local probe results are recorded above. No community binary was executed and no request was sent.

## Earlier renderer SR/DLAA prototype (historical)

- Configuration: `[video] dlss = off|dlaa|quality|balanced|performance`, `dlss_preset = auto|j|k|l|m`, `dlss_sharpness = 0..1`. Presets are SR hints subject to runtime support; none enables V5. Unknown modes become off; nonfinite sharpening is sanitized.
- NGX sits behind `Upscaler` in `runtime/gs/src/temporal.h`. Default `GSR_DLSS=OFF` links `dlss_stub.cpp`. Missing SDK also selects the stub and prints OFF during configuration. The off build needs no NVIDIA SDK/DLL.
- The preliminary probe checks adapter vendor and an actual DLL file in `DRAKAN_DLSS_DIR` (UTF-8), the executable directory, or `<exe>/dlss`. This is not proof of RTX/driver/feature support. NGX capability query, feature creation, camera validation and target allocation happen before reduced-resolution drawing or jitter.
- VIF1 forwards copied VU rows 0..6 in the GIF bridge queue. Destination tracking accounts for skip/fill/wrap. The core callback uses standard C++ types and atomics. Off installs no callback and queues no camera records. Live enable installs it; disable/detach removes it. Detach waits for in-flight callbacks without holding a mutex across queue/renderer calls.
- World classification uses GS draw/depth/scissor heuristics. Jitter is Halton; camera motion vectors are reconstructed from depth and captured matrices. Resolve precedes detected HUD drawing, display/readback and end-of-frame cleanup. Each target stores its camera/jitter snapshot; pending worlds finish before sharing temporal targets.
- Pending draws flush before live changes. Mode/preset/sharpness changes release the old pipeline/NGX session; scale changes and guest reset discard history. First/new features, geometry changes, explicit `Renderer::upscaleCut()`, long gaps and failed resolves reset history. Cut flushes pending geometry; guest reset clears captured cameras.
- Missing prerequisites retain full-resolution drawing. Evaluation failure stretches the already reduced current world spatially and disables SR at the next frame boundary. Failure text remains in renderer status; explicit toggle permits retry. Feature/parameter release precedes NGX shutdown/device teardown. Another active D3D11 device is rejected rather than shutting down its NGX session.

## Earlier SR/DLAA prototype limits (separate from NR)

The earlier SR tests did not validate image quality, correctness or speedup. HLSL compiles when the optional pipeline initializes; a C++ build alone does not execute or compile those shaders. The NR path above uses the separately validated guide implementation and RGB-only composition.

VU row layout, matrix interpretation and depth reconstruction are inferred from existing analysis. Camera motion does not track animated characters, moving objects, particles or transparency. There are no object motion vectors, reactive masks, exposure integration or robust automatic camera-cut detection. World/HUD/menu/movie separation is heuristic; unusual effects, render-to-texture and GS depth/alpha conventions can produce artifacts. Resolve writes conventional GS alpha 0.5, which can affect destination-alpha effects. The cut API exists, but no new level-load/cutscene hook was added.

Output is the detected native frame region times internal scale, not the swapchain viewport. Scale is not automatically tuned to display size. Configurations share one feature; geometry changes reset history. Device-loss/memory-pressure behavior needs live validation. No device recreation system was added. The process-global NGX session supports one D3D11 device at a time.

Browser portability: NGX and `temporal.*` stay inside the existing Windows GPU backend; VIF core gains no Windows/NGX types. A future WebGPU backend can leave this feature unavailable. No browser build was attempted; existing abstraction gaps remain in `docs/reference/portability.md`.

## Earlier SR build and integration handoff

The runtime now lives in this repository (`ps2recomp/`, `runtime/gs/`); the old `tools/PS2Recomp-dlss` checkout is obsolete. Historical build tree: `build/recomp_dlss/rt`; executable: `build/recomp_dlss/bin/drakan.exe`. Current equivalent, from the decomp root with CMake on PATH (single root, see `BUILDING.md`):

```powershell
cmake --build build/recomp_dlss/rt --config Release --target ps2EntryRunner -- /m:2 /v:m
cmake --preset runtime-tests -B build/recomp_dlss/gs_off -DGSR_DLSS=OFF
cmake --build build/recomp_dlss/gs_off --config Release --target gsr -- /m:1 /v:m
```

The native configuration has `GSR_DLSS=ON` and tests disabled. The prototype bin's SR DLL predates this continuation. For a separately authorized runtime, use the documented search locations. The project GUID is a development identifier; confirm NVIDIA registration/distribution requirements before shipping.

For later explicitly authorized gameplay validation: compare off/DLAA/Quality; exercise toggles, scale changes, reset/load/cuts, HUD, menus, movies, resize and shutdown. Debug controls: `DRAKAN_DLSS_LOG=1`, `GSR_UPSCALE_TIMING=1`, `DRAKAN_DLSS_DEBUG=mv`. Renderer startup overrides: `GSR_UPSCALE`, `GSR_UPSCALE_PRESET`, `GSR_UPSCALE_SHARP`; INI environment overrides: `DRAKAN_DLSS`, `DRAKAN_DLSS_PRESET`, `DRAKAN_DLSS_SHARPNESS`. Remove overrides when checking INI persistence. Timing measures resolve only; no screenshot/performance results are claimed.

Historical SR handoff: owned runtime commit `026da3d` was subsequently cherry-picked by the integrator; the combined build was tested and deployed by the integrator. The agent's NR continuation changes only this shared document and its owned isolated prototype. It does not modify shared renderer/settings sources or dist. The parent repository ignores tools, so its diff omits nested checkout changes. Preserve concurrent worker edits. Starting owned-file snapshots are under `build/recomp_dlss/resume_baseline`.

## Continuation file inventory and build evidence

Files edited during this continuation (relative to the decomp root):

- `runtime/gs/CMakeLists.txt`: configuration-specific NGX CRT library selection.
- `runtime/gs/include/gsr/gsr.h`, `runtime/gs/src/gs_internal.h`, `runtime/gs/src/gs_core.cpp`: capability API, synchronized diagnostics, flush before toggle/cut.
- `runtime/gs/src/d3d11_backend.cpp`: lifecycle/reset, target snapshots, frame safety, allocation/feature preflight and failure disable.
- `runtime/gs/src/temporal.h`, `runtime/gs/src/temporal.cpp`: prepare/reset API, checked allocations/maps/states, explicit matrix packing, new-feature history reset and fallback.
- `runtime/gs/src/dlss_ngx.cpp`: UTF-8 path buffer fix, parameter cleanup, session retry/shutdown and precise probe wording.
- `runtime/settings/src/Menu.cpp`, `runtime/settings/src/Store.cpp`: experimental SR naming, accurate preset help and accessible Off option.
- `ps2recomp/ps2xRuntime/CMakeLists.txt`: native DLSS glue compiled only in the Windows GSR branch.
- `ps2recomp/ps2xRuntime/src/lib/drakan_dlss.cpp`, `drakan_dlss.h`, `drakan_vu1_hook.h`: toggleable atomic hook, detach lifetime, capability forwarding.
- `ps2recomp/ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp`: atomic callback load and actual destination row tracking.
- `ps2recomp/ps2xRuntime/src/lib/gs/gs_gsr_bridge.cpp`: attach before settings capability snapshot.
- `docs/reference/dlss.md`: this status/evidence/handoff.

Inherited DLSS changes in `gs_shaders.h`, `dlss_stub.cpp`, settings `Host.h` / `Types.h`, bridge header and settings-host adapter were preserved. Stability/combat scripts, other worker checkouts and their documentation were not edited.

Compile/link evidence: Release `ps2EntryRunner` and a separate Release `gsr` with `GSR_DLSS=OFF`, `GSR_BUILD_TESTS=OFF`. Build logs are in `build/recomp_dlss`; final logs are `resume-portable-build.log` and `off-final-build.log`. Earlier logs are retained, including an intermediate VIF tracking scope error that was corrected before the final build. The executable is `build/recomp_dlss/bin/drakan.exe`; the off library is `build/recomp_dlss/gs_off/Release/gsr.lib`.

At the time, native linking reported `LNK4006` (`ShowCursor` from raylib/user32) and `LNK4088` (`/FORCE`, image may not run); both are fixed since (strict link, raylib's `ShowCursor` renamed by a compile definition). Existing conversion warnings also appeared. A successful build is compile/link evidence only, not executable, shader or gameplay validation. Both parent and nested `git diff --check` were clean after the source changes (line-ending conversion notices only).


The 2026-10-19 movement follow-up retains proven sky-background exclusion across camera movement, interpolates native terrain lighting correctly, and fades mismatched terrain shadow receivers. See [water edges and movement validation](../journal/2026-10-19/water-edges.md) for the deployed build, three-area neural/motion checks, clean performance samples and contended timing limitations.
## Further reflection filtering and motion texture validation

See [water-polish.md](../journal/2026-10-19/water-polish.md) for the next filtering,
shoreline and moving-model texture-array pass, with scope and remaining limits.

## Shallow shoreline and directional sky follow-up

See [water-sky.md](../journal/2026-10-20/water-sky.md) for projected-pixel shoreline
coverage, retained world-direction sky colours, and the remaining native geometry
limitations.

See [render-limits.md](../journal/2026-10-20/render-limits.md) for full-viewport sky
observation, the shared sky reduction, fixed-scene timing methodology and the
remaining terrain, moving-model and whole-game coverage limits.

See [shore-shadow.md](../journal/2026-10-21/shore-shadow.md) for the correction to
shadow continuity at partially covered water edges and expanded fixture checks.

See [dlss-overhead.md](../journal/2026-10-21/dlss-overhead.md) for motion-guide
alpha-array capacity reuse, cutout correctness tests and performance limits.

World-triangle preparation caching and stage profiling: [21 October validation](../journal/2026-10-21/dlss-world-cache.md).

A [deferred ray CPU-wait experiment](../journal/2026-10-21/dlss-ray-wait.md) passed image checks but regressed full-game performance and was rejected.

A [GPU-local ray material experiment](../journal/2026-10-21/dlss-material-residency.md) also
passed image and movement checks, but measured 2.78% slower and was not deployed.

[Packed ray textures](../journal/2026-10-21/dlss-packed-textures.md) reduce texture storage
by 75% in a private candidate. Hardware tests and three-area movement validation
pass; uncontended performance measurements are required before deployment.
