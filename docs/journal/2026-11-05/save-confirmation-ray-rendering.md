# Save-to-continue confirmation rendering, 5 November 2026

The user clarified the Rynn symptom occurs when the level-exit save confirmation
opens, rather than after the destination level loads. The native sequence is
message screen 2, save browser 5, then save/load screen 26. The earlier generic
level-cache corrections do not establish the cause of this menu-specific symptom.

## Reproduction and confirmed defects

`build/harness/rt-exit-prompt-20261105/` reached the actual screen-2 confirmation
and screen-5 browser and exited normally (52 seconds). Camera/RT logs show valid
world packets, ray submissions and successful DLSS evaluation throughout screen 2.
Ray tracing was not switched off. The frozen camera nevertheless received changing
Halton jitter (e.g. -0.1875,-0.12963), because `hud::frozenWorldScreen` omitted 2.
It now includes that screen, shared by both renderer backends. HUD policy tests
cover camera-first, host-first and stale host-screen transitions.

Separately, the hardware closed-world readback regression failed before the fix:
a guest screen read immediately after EndWorld received the unprocessed raster
image, while a display-latch-first reference received ray lighting. Both backends
now resolve pending, closed modern worlds before target readback. Active worlds
and late native water streams remain open. The test passes with those negative
controls and an effects-disabled preservation check. See
[closed-world-readback.md](closed-world-readback.md). This verifies an applicable
capture-order bug but does not prove the live prompt used that exact readback path.

The initial forced save-browser mouse recipe did not open the modern save menu;
it remained paused and was stopped. Two farther-away walk attempts showed Rynn
but did not hit the detector; only those owned test processes were stopped.
The successful near-detector recipe froze a camera far from Rynn, so its images
verify the prompt/background, not the reported character appearance. No claim
that all Rynn artifacts are visually resolved is justified by those images.

## Build and tests

- Full runtime build passed, including WebGPU: `build/rt-save-final-build.log`.
- Eight focused tests passed in 8.13 seconds: ray_trace, world_readback,
  partial_upload, temporal_presentation, ray_water, rt_upload, hud_page,
  native_display_commit (`build/rt-save-final-tests.log`). The new readback
  comparison executes D3D11/DXR hardware; WebGPU parity has build and existing
  renderer-test coverage, not an equivalent new readback fixture.
- Standard builder deployed SHA-256
  `7dd25f888a1e5deefb719526295532aa1f97bb30634af7cfba01407d020bf073`;
  build/dist hashes match (`build/rt-save-final-deploy.log`).
- Formatting and repository guard pass (2,313 approved tracked files,
  zero prohibited). Other work and unrelated running games were preserved.

Final verification: uild/harness/rt-exit-confirmation-verified-20261105/ completed in 53 seconds. All 247 screen-2 SR preparations used jitter 0,0; 247 ray passes were queued and 247 SR evaluations succeeded during the prompt. Prompt and save-browser captures were produced and the prompt image inspected. Rynn was obscured by the near-detector camera, retaining the visual limitation above.
