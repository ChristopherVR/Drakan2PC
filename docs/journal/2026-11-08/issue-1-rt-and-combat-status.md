# Issue 1: ray tracing artifacts and combat WASD, status check

Rabe's latest unresolved items on issue #1 were checked against git history before any new work.

| Item | Status | Where |
| --- | --- | --- |
| (a) Milky reflections | Fixed in `02dcb8e9` (only glossy materials reflect; native default rough dielectric no longer traces a sky lobe) | `docs/journal/2026-11-07/issue-1-ray-artifacts.md` |
| (b) GI colours along seams | Fixed in `02dcb8e9` (bounce rays skip cutout and blended foliage cards; per-cell terrain projection). Thin dashes along some terrain cell edges in debug view 4 and the missing dirt-path overlay in the ray scene stay open, neither shows in GI output | same |
| (c) Shadows turning black | Fixed in `02dcb8e9` (sun occluders fade out between 8 m and 16 m; the cliffs shadowed the whole valley floor) | same |
| (d) Combat WASD hijacks movement | Fixed in `49089458`: `[keyboard] directional_attack = modifier` (default, hold Left Alt, rebindable `DIRECTIONAL_ATTACK`), `tap`, `always`, `off`; unit tests in `runtime/input/tests/directional_attack_mode_tests.cpp`, 87 strict deployed checks passed | `docs/journal/2026-11-07/issue-1-directional-attack-modifier.md`, `docs/reference/input.md` |

## Still open

- WebGPU ray path performance ("notably worse" than D3D11/DXR) has never been measured; no profile exists. WebGPU is covered
  by `gsr_test_ray_trace` only, and the artifact fixes were verified live on D3D11 at the Surdana start only.
- Reporter should confirm the fixes with the next release build; no reply was posted (not asked).
- No new code was changed in this pass.
