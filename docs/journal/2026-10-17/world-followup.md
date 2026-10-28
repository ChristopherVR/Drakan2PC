# Independent world/culling audit

Audit baseline: outer `7a3625b`, runtime `2c863bd`, public runtime export
`dc1bdd62c2332f2b50f1a856e3099a95b5309bae505e9884e127cfc72df4b2e0`.
Isolated worktrees: `tools/world-followup` and `tools/PS2Recomp-world2`.
No new sky/Arokh geometry defect was proved. No runtime behavior, renderer,
public headers, shader, settings, or private game assets were changed.

## Projection and culling findings

The current engine hooks already address the previously reported width mismatch:

| Engine entry | Current handling / source evidence |
| --- | --- |
| `0x139010` | Latches the requested aspect for the engine world, sets renderer dirty, and emits an early world phase before original draws. |
| `0x1480e0` | Applies the scoped X scale before constructing the camera matrix from `0x4dad58`; UI outside that scope uses the native scale. |
| `0x132d68` | Reapplies the scoped scale after viewport calculation. |
| `0x199ff0`, caller return `0x133508` | Corrects the first local frustum input width to `focal / -projectionX`. The generated `0x133498` caller passes its stack vector and renderer `+0x84`, matching the guard. |
| `0x199ff0`, caller return `0x133668` | The extended frustum inherits the already modified stack width; the hook does not widen it again. |
| `0x147690`, caller return `0x139344` | Ends world projection before native GUI callbacks. Early return cleanup uses the same scope close. |
| `0x1e8c78`, `0x1e93b8` | The camera packet reconstructs the engine matrix and viewport. World eligibility additionally requires the authoritative engine scope, preventing a reused inventory context from reopening World. |

For forward depth `d=-viewZ`, VU horizontal acceptance is
`abs(projectionX * viewX) <= d`. The CPU side-plane slope is `width/focal`.
The existing correction makes these equivalent, including when focal length
changes for zoom. The test uses independent inside/outside predicates at both
edges, avoiding exact float rounding ties.

Nested commit `e3ac9a4` extends `ps2xRuntime/tests/widescreen_policy_tests.cpp`:
4 aspects (4:3, 16:9, 21:9, 32:9), 5 FOVs (15, 30, 60, 80, 120 degrees),
3 viewport half-widths, 3 forward depths and 5 edge positions. All 900 side-plane
witnesses agree. Each combination also verifies native projection restoration
and the prohibition on modifying UI frustum inputs. Total: **2,171 checks PASS**.
These are synthetic policy tests, not a VU program replay or a scene capture.

## Moving-object/camera checks

Fresh MSVC `/O2 /W4` CPU runs against the isolated runtime:

| Test | Result |
| --- | --- |
| `run_widescreen_policy.ps1` | 2,171 checks PASS; warning-free compilation. |
| `run_scene_camera_policy.ps1` | 30 checks PASS, retaining scope/preview/GUI/tick/cut thresholds. |
| `run_object_motion.ps1` with saved combat RAM begin/end | 67 checks PASS; warning-free compilation. |

The saved RAM pair resolves owner `0xa5b6a0` through controller `0xbb75a8` to
pose source `0x14d61d0` with 1,628 vertices. The measured squared distance between
vertices 500 and 1200 changes from `23923.880571` to `25013.740366`, corroborating
deformation beyond a common rigid transform. Indexed and packed ordering hashes
remain equal across the pair (`8f937deeb23e74ac`, `664b771b7954a7e1`). This does
not identify a named actor, prove live draw coverage, or cover a complete flight.

Source review also confirms the CPU-clipping/direct-GIF path at `0x18bb90`
remains diagnostic-only for motion identity: there is no immutable per-face
transport certificate. VIF clipping rejection uses feature bit 0x1 before
MSCAL/MSCNT, and `Cursor::clipping` clears `TopologyStable`. Keep that rejection;
no new certificate or animated-actor attribution was justified by this audit.

## Clean ISO source preparation audit

`scripts/stab_world_sources.py` independently checks patch hash, all exported
source hashes, private-asset exclusions and generated hook entry ownership.
Run against the previously prepared upstream clone and the actual public-config
generated sources: **150/150 canonical source matches**, 147 byte matches (three
known CRLF-only differences), and **21/21 distinct hook entries** present with
the correct owning generated function. Eight helper regression tests PASS,
including missing/changed sources, wrong entry owner, missing hook, unsafe path,
private-asset inclusion and patch hash mismatch.

The earlier fresh NR standalone compilation at this same export succeeded;
see `build/source_validation/nr-public-source-validation-150.json`. This audit
does not repeat a full generated-game build or establish final game linkage.

Evidence is private under `build/world-followup/{widescreen,camera,motion}` and
`build/world-followup/source-audit.json`. The saved input pair is
`build/t/combined-off/ram_combat_{begin,end}.ram`. No ISO/ELF/VU instruction bytes
or icon are copied into the authored tests, report, or public patch.

Example source check from the outer root:

```powershell
python -B scripts/stab_world_sources.py --runtime-source build/source_validation/PS2Recomp --active-runtime tools/PS2Recomp --generated build/source_validation/wrapper-source-only/generated
python -B -m unittest discover -s scripts/tests -p test_stab_world_sources.py
```

## Integration and remaining limits

1. Optional runtime test-only cherry-pick: `e3ac9a4` onto runtime `2c863bd`.
   It needs no generated-code rebuild or public-header update. Run
   `ps2xRuntime/tests/run_widescreen_policy.ps1` in the runtime checkout.
2. Cherry-pick the separate outer audit helper/tests/document commit.
3. If publishing the changed runtime test, regenerate the existing public patch
   and its manifest hash. Keep the VU image and icon excluded. This work does not
   update the publication patch or dist.

No GPU/game launch was performed. Remaining scene-dependent questions require a
coordinated native-vs-wide capture with the same save, camera and zoom. A sky or
Arokh hole should first be localized to absent guest submissions versus VU
clipping versus raster coverage. Default-camera FOV consumers, sector/portal
visibility, near-plane intersections and VU rounding remain unproved scene
risks. No threshold was relaxed and no speculative geometry fix was made.
