# Issue 1 follow-up integration

Source: [reporter follow-up](https://github.com/ChristopherVR/Drakan2PC/issues/1#issuecomment-6637936574).
Starting revision: `892dede`, clean working tree. The user requested one GPT-6.1 Sol agent per bullet.
The twelve bug bullets and CPU-affinity suggestion are assigned separately, with at most three workers active.
All agents share the existing checkout; the root coordinates overlapping edits, runtime builds and deployment.
No GitHub replies, commits or releases are part of this task.

## Scope and evidence

| Item | Outcome | Investigation journal |
| --- | --- | --- |
| Drakan 1 exported levels and campaign selection | Complete conversion pipeline, required assets and campaign metadata wired into editor; real German install untested | `issue-1-d1-loading.md` |
| Template Level dropdown behind editor | Modal popup host fixed; Chromium interaction verified | `issue-1-template-dropdown.md` |
| German ISO replacement causes stuttering | Existing language fix removes need for workaround; modified-image stutter unverified | `issue-1-german-stuttering.md` |
| WASD directional attacks | Movement bindings project D-pad directions while attacking; pad-output regression passes | `issue-1-directional-attacks.md` |
| A/D spell directions | Native horizontal axis restored during casting state, including after button release; routing regression passes | `issue-1-spell-directions.md` |
| Multiplayer pickup freezes | Co-op stale inventory-reference safeguard plus native-owner guard; reported freeze not reproduced | `issue-1-mp-pickup.md` |
| Surdana Meadow opening cutscene | Existing pre-initialization campaign filter confirmed by ISO tests and menu launch | `issue-1-mp-cutscene.md` |
| Inquisitor's Lair movement | Actual menu-hosted spawn 0 moves forward/back without rotation; current nonreproduction | `issue-1-mp-movement.md` |
| Singleplayer to multiplayer loading freeze | Normal SP -> title -> menu-hosted Surdana match and movement succeed; current nonreproduction | `issue-1-sp-to-mp.md` |
| Host departure leaves clients playing | Manual quit, automatic client title/error, new host session and selected-avatar rendering verified live | `issue-1-host-disconnect.md` |
| Remote lag and teleporting | Existing cadence/preview fixes confirmed; added cadence-switch and loss regression | `issue-1-remote-motion.md` |
| Remote players always low polygon | Prior remote Rynn/NPC captures use highest LOD; added opt-in transition diagnostics, visual report unconfirmed | `issue-1-remote-lod.md` |
| Build CPU affinity | Optional Windows mask in both builders and installer; validation/inheritance/restoration tests pass | `issue-1-build-affinity.md` |

## Validation

Before edits, `python scripts/run_tests.py` passed all 256 tests. Private output:
`build/issue1-followup-python-baseline.log`.

Per-item notes distinguish observed defects and tested fixes from plausible safeguards and unverified live symptoms.

Editor integration: all 968 tests across 96 files passed, followed by the complete workspace typecheck and production
editor build. The build retains its bundle-size advisory. Root inspected the dropdown screenshot showing the popup
above the modal; the agent additionally checked mouse/keyboard selection, filtering and cleanup in Chromium.
Native Tauri/WebView2 verification and an actual German D1 conversion/playthrough are not established by these tests.

Other work appeared in this shared checkout during the task (`drakan_overrides.cpp` alignment,
`test_harness_asset_links.py` fixture setup and `compiler-ci-fixes.md`). Those edits are preserved and are not attributed
to these issue agents.

The combined runtime configuration/build succeeded. CTest passed 107 executed tests, with six data-dependent skips
(113 registered, zero failures). This includes input attack/spell routing, replicated inventory cleanup, changing
network cadence/loss, confirmed-quit policy, dedicated-server departure and UDP teardown. The final Python run passed
259 tests; repository-wide Ruff lint, changed Python format checks, changed C++ clang-format checks, UTF-8 checks and
the repository guard passed. The guard reported 2329 approved tracked files and zero prohibited files.

The first canonical incremental build deployed successfully to `dist/DrakanPC` with SHA-256
`2C41BAF586763FDC660B2366B8EFB2168BF78805CE25666B819BDA94685B15C2`. Root's final review then identified that
replicated inventory cleanup needs a native owner-class guard before interpreting controlled-actor data (mounted
Arokh has no player inventory, but shares actor type 3). Both adapters now resolve class and require native Rynn;
the shared helper rejects other classes without reads/writes. The updated inventory test was rebuilt and passed.

The next canonical incremental build/deployment succeeded with SHA-256
`F75B99238530B474C8503B61E15A33D553BC501850AE4A191084E13C4D8D9ECB`. Subsequent source adjustment only wraps
the `nativePlayer` declaration to satisfy clang-format; it does not change compiled behavior. Host-departure live
validation uses this executable and isolated cards/settings under `build/harness/issue1-host-disconnect*`.

Actual menu-hosted Inquisitor verification on the preceding deployed executable reached the assigned spawn and
equipped loadout. W moved 3444.59 units, S moved 1763.94 units in the opposite direction, and all five sampled rotation
matrices were identical. A separate normal-SP -> title -> Surdana arena run loaded successfully, moved 4959 units and
exited through its script. These establish current nonreproduction in the documented settings, not universal closure
of the reporter's movement/reload failures.

## Host departure integration

The first valid two-instance run on `F75B9923...` confirmed manual host Quit to Title, client disconnect and automatic
client return to the title; both exited cleanly. Root inspected the client title screenshot. Follow-up review found
the failed session was not resumed by the title's multiplayer handling, leaving its cleanup/error acknowledgment
pending. The title now resumes an active rejected session, runs the existing Leave transition and preserves only
the readable error after network teardown; the next Host/Join resets that message. The title-resume policy and
multiplayer UI tests passed after rebuilding the policy target.

The acceptance build deployed through the canonical script with SHA-256
`8F86076BFFC2084CDD41D9856155A05ECA4DDD842508A08762F3D03611C230B6`, superseding the earlier hashes. There
were no source mutations during its final live run. Evidence is under `build/harness/issue1-host-disconnect-final`.

That acceptance attempt and one same-hash retry stalled at the native title/render boundary before a completed
modern-title overlay. They are failures, not successful cleanup checks. A separate ghost audit found a concrete
lifetime defect: a dead local cosmetic body was erased while the published view survived an early failed pose read;
RenderWorld could copy into that destination before validating it. Owned-body removal now clears the view first, and
render entry validates the bound skeleton/resource identity, vtable and non-dying flag before writing. The render-side fallback only
clears EE-owned view state; it adds no ghost lock or native-call lock cycle. The rebuilt `character_roster` test passes
identity-reuse, invalid-vtable and dying-body rejection. Detailed evidence/limits are in `issue-1-local-avatar-lifetime.md`.
Whether this explains the observed stalls still requires the new live run; no failed-run RAM or stack was retained.

A first lifetime-check build (`C2ED9A75...`) completed title/error presentation and a new host session with clean
shutdown, but incorrectly rejected selected NPC bodies and displayed Rynn. This is not accepted as a complete fix.
Archived NPC RAM established that a living stand-in can have a zero registry reference, so the unsupported
registry/reference condition was replaced with the bound skeleton/resource identity. The lifetime reset on actual
owned-body removal remains in place. The updated roster test passes. The corrected canonical deployment has SHA-256
`AFF3EE9E1017054CE2AE8C8E73E4FC120A0D6DF8CCD8264F2CABB16BCEDE7DDE`; its live acceptance is recorded below.

Both instances logged exactly one selected/visible/binding event, with no per-frame invalidation. The corrected two-instance run under `build/harness/issue1-host-disconnect-avatar` completed Playing, manual host
Quit to Title, automatic client return, persistent connection-loss message and a fresh host lobby. Root inspected
`b/client_playing.png` (selected Sevoth is visible), `b/client_loss_error.png` and `b/client_rehost_lobby.png`. Both
recipes exited 0 after scripted quit and ordinary renderer shutdown; neither logged a boundary stall or shutdown
timeout. Pre-departure state snapshots were logged; the recipe omitted the ram_ naming prefix, so no full RAM dump was produced. This is bounded localhost acceptance, not WAN stability
or a live abrupt-process-loss test; no claim is made that the earlier stalls' cause has been conclusively proven.

Final source remains uncommitted. The detailed item journals preserve remaining limits: actual German D1
conversion/playthrough and modified-ISO stutter, the reported pickup freeze, WAN lag and the low-poly appearance
are not fully reproduced or closed. Inquisitor movement and SP-to-MP loading passed their controlled reproductions.
The optional Inno installer field was source-reviewed but no local Inno compiler was available.
