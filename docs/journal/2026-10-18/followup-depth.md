# Depth and combat follow-up — 18 October 2026

Executable SHA-256:
`7B987758130784ECE01461DCB0DB1C36D27F04A9C6E9C15B7D2C6C53976E6CD4`.
Source implementation commit: `186443017ade0186898669dc329c9ec441a74666`.
Nested runtime: `35443440fec23c6fab551e8978ee7929777d2be5`.

## Fixed

SR world rendering now resizes depth together with color and restores full-resolution depth before native UI. This removes the reproduced gray holes under the canopy and restores missing Equipment grid lines with SR Quality and DLSS 5 both enabled. The failure path restores the native depth baseline and disables reduced rendering; spatial recovery keeps color/depth jitter consistent. See [root cause and stage evidence](depth-transitions.md).

## Combined checks

The exact executable above completed both audio-enabled functional runs normally, exit 0:

- Native: **23/23** live checks, 96.41 seconds.
- SR Quality + DLSS 5: **23/23**, 103.04 seconds; **1794 SR / 1676 NR** successful evaluations, including NR toggles.
- Coverage: seven category selections, seven Equipment mouse checks, nine camera/movement/bow/Quick Inventory checks.
- Equipment, Journal and bow-preview screenshots retain the corrected scenery. Thin white orb edges remain.
- Original, fixture and private-copy save hashes remain unchanged. Test input/graphics INIs match the existing dist INIs.

Functional times include screenshots, RAM capture and diagnostic logging; they are not benchmark results.

## Performance

The clean SR Quality + NR, scale4 run completed in120.33 seconds. Its last ten two-second gameplay windows measured **30.03 world submissions/s** over 20.050 seconds. The previous deployed54F082 build measured **29.98** with the same script/input hashes and selected graphics/audio settings. This is effectively unchanged at the sampled cadence; no new performance gain or whole-game60FPS claim is made. Audio output, screenshot capture and verbose DLSS logging were off for this measurement.

[Sanitized aggregate receipt](data/depth-followup-20261018.json). Detailed runs remain local under `build/t/depth-final-*`.

## Still open

- Thin orb-edge lines: the border exists in the original atlas. The native sprite-origin discrepancy is understood, but high-resolution sampling needs a separate renderer regression. No blanket UV offset or atlas masking was applied. See [sampling evidence](sprite-sampling-convention.md).
- Smoke needs a reproducible emitter; torch flame is not smoke validation.
- Original gate-guard shadows, broader hair/texture coverage, occasional HUD/weapon-preview placement, Arokh, complete combos and subjective Order of the Flame parity remain unverified.
- Original asset detail is unchanged. Linux gameplay and a browser runtime remain future work.

All three Sol6.1 agents completed their assigned investigations. Only authored source, tests and sanitized receipts are published; disc data, generated game code, saves, captures and vendor DLLs remain local.

## Final combat follow-up

Four completed runs use the same final executable, scale2/native graphics and fresh copies of the existing hostile675 save. The version2 observer is now live-validated: completed native HP changes match incoming/returned damage, and all save hashes are unchanged.

| Run | Correlated HP updates | Active-window samples | Normal completion |
| --- | --- | --- | --- |
| `depth-final-parry-tap` | 7 | 172 | 44.23s |
| `depth-parry-front-neutral` | 8 | 0 | 44.27s |
| `depth-parry-front-tap` | 2 | 153 | 46.26s |
| `depth-parry-front-tap12` | 7 | 123 | 44.26s |

The three front-facing recipes add a single relative-frame28 mouse turn (1571 counts, about180 degrees at the existing sensitivity). They do not edit guest health, weapon stats, saves or attack state. The phase12 recipe delays tap requests by12 input frames. Two distinct attackers strike from opposite sides. All correlated hits return10 damage and lose10 HP; in these observations the timed window and required facing dot>0.4 do not coincide. Fewer hits in one run are not proof of parry protection. **Successful timed damage mitigation remains unverified**, as do subjective Order of the Flame parity and broader combat progression. No automatic parry repeats or balance changes were added.

The final observer's live receipt supersedes the CPU-only status for version2 in the earlier agent handoff; that handoff's own run identities and limitations remain unchanged.

## Renderer regression checks

Nine CTests passed, including the three depth-transition cases. Full synthetic pixel checks passed at scale1 and scale4: **118/118 each**. The HUD packet/sampling CPU fixtures passed24 checks; the parry observer passed34 C++ checks and8 Python methods. Existing compiler deprecation and ShowCursor/FORCE linker warnings remain. Device-loss recovery has source review, not a forced live-device-failure test.
