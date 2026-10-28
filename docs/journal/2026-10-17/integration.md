# Combined build continuation — 17 October 2026

## Sources and build

The `drakan` tools branch integrates stability `34455b5`, narration `c2f2b5e`, combat audit `6c91c57`, optional DLSS SR glue `323301e`, and opt-in frame timing `d9a7e5d`. Parent commit `54a4805` contains the combat input changes. Matching narration assets and the optional renderer/settings module are in parent commit `23ef1a3`.

Rebuilt the recompiler, regenerated the translation from the local ELF, then built the Release runtime with `GSR_DLSS=ON`. A final incremental build included the opt-in subtitle frame logger. The existing duplicate `ShowCursor` / `/FORCE` linker warnings remain; this continuation does not resolve them.

The first final-diagnostic build had SHA-256 `4B0AC391E1241834CFD27B6F7B72D9A2430E036130793D2AD7FED104FD7CE08D`. The subsequently discovered narration-request restart fix is tools commit `a44712e`; its rebuilt executable has SHA-256 `43C7C30123F053E2C93EA85B18DF80E7FD4B2FDD57A9F93AE10F9D68F1C110F2`.

## Checks

- Input accumulator/command diagnostic: 9/9 pass against the integrated shared sources.
- Input suite: 641 checks, zero failures.
- Settings suite: 201 checks, zero failures.
- Temporal HLSL: CSMotion, VSFull, PSStretch and PSMvDebug compile with the Windows SDK shader compiler.
- `combined-off`: copied save loaded; scripted inputs, pause/resume and shutdown completed at input frame 1099, exit 0. No crash, missing-target, reserved-op, bad-resume or bad-size markers. Final audit reports neutral buttons/sticks and no held commands.
- `combined-quality`: same script completes, exit 0. **This is not successful DLSS evaluation**: uploads reached the hook, but no world resolve or NGX feature creation was observed. The scene continued through the full-resolution path. SR/DLAA remains experimental and off by default.
- First full-cue narration capture: all 25 cues, zero interior blank intervals; only the initial/final fades. The script deadline was too short to reach its requested quit after the slower movie, so this is **not** counted as a completed boot. It also exposed a second request without voice key-on falsely restarting the subtitle timeline after 123 seconds.
- Narration restart correction: an already-started narration can restart on a real voice key-on, but not on the request-time fallback for a repeated resource. A movie completion resets this guard for future playback.
- Final guard-build capture: 2,110 submitted subtitle frames, all 25 cues, zero interior blank intervals and no interior fade dips. Only the initial fade was sampled (125 ms media / 123 ms wall); the movie stopped the track before its final fade. Movie screen 29 closed at game time 139.96 s; the script quit at frame 3600, exit 0. No crash markers or restarted narration were observed. This run completed the movie earlier than the prior slow capture, so it did not itself exercise a late duplicate-resource timeout. Evidence: `guard.{input,stderr,stdout}.log` and `guard.wav` in the final narration directory.

The first two runtime runs used the build before the final diagnostic-only subtitle logger rebuild. The final narration run uses the final executable hash above. The superseded initial narration capture was deliberately stopped to restart with that logger; it is not counted as a pass or a spontaneous failure.

Evidence is local and ignored: `build/recomp_merged/integration_*.log`, `build/integration_input_*`, `build/integration_settings_*`, `build/integration_shaders/`, `build/t/combined-{off,quality}/`, and `build/integration_narration_final/`. Tests use copied/isolated memory cards. Desktop screenshot captures can select another same-title game window, so they are not treated as authoritative rendered-frame comparisons.

## Limits and next work

Combat input delivery is verified, not equipped attack animation/damage, combos, parry, bow/magic or Arokh. Quick Inventory stalled in the agent's earlier run and remains under investigation. The outro timing was checked offline, not in a live endgame. No fifty-boot integrated stress claim is made.

DLSS 5 Neural Rendering is a real released feature, distinct from this SR/DLAA prototype. Public official sample code references a separate NR beta package; matching headers/runtime and the supported custom-engine integration contract were not found in the inspected public packages. The user then supplied `~/Downloads/SL 2.13/SL 2.13`: it **does** contain matching `sl.dlss_nr.dll` and `nvngx_dlssnr.dll`, with valid NVIDIA signatures (Streamline 2.13 / NGX 310.8). This removes the missing-local-runtime condition. The isolated Streamline initialization, device binding, NR-support query and function/options probe report `eOk` on the RTX 5080. Actual evaluation, D3D12 interoperability and output validation remain a separate ongoing task. See `docs/reference/dlss.md`. No DLSS 5 implementation or image-quality improvement is claimed by this deployed build.

Deployment must preserve the user's INI files, saves and existing HD pack. Replace the executable and matching English narration SRTs together; retain the previous executable for rollback and record hashes in `dist/DrakanPC/BUILD_INFO.json`.

Deployment completed to `dist/DrakanPC/drakan.exe` with the guard-build hash above. The matching two English SRTs and optional public SR DLL were copied with it. Both INI files retained their pre-deployment hashes; save folders and HD textures were not modified. Prior executable, player notes, INI files and SRTs were backed up under `build/release_backups/20261017-175120/`. `PLAY.txt` and `BUILD_INFO.json` describe the actual limits, including inactive SR evaluation and ongoing full NR work.
