# Issue 1: remote character detail

The follow-up reports other players looking low polygon even nearby. The existing native selector and archived paired
RAM do not establish a permanently coarse remote LOD. No LOD override was added. The report's current executable,
character choice, camera distance and visible result remain unconfirmed.

## Native path

Read-only PAL Ghidra review, with generated investigation output kept under `build/issue-review/remote-lod/`:

- `0x18b0c8` is the native visibility path; `0x18b5d0` computes squared distance from the current camera position and
  calls `0x102c90` for skinned actors. Selection is enabled by actor `+0x104` bit 23, unless bit 21 disables it.
- `0x102c90` compares that distance with the resident model's LOD table, using a 1048576 squared-distance hysteresis.
  It requests a controller transition through flags rather than simply overwriting the active slot. Actor flag bit 22
  can request finer detail. Ghost pose writes do not write this selector or its slot.
- `drakan_scene_models.h` reads the active controller `+0x10` slot, obtains that slot's vertex count from the model's
  descriptors, and includes the slot in its cache key. The modern scene capture follows native LOD; forcing its slot
  independently would mismatch pose vertex buffers and geometry.
- Default on-foot Rynn ghost creation uses the controlled actor's record index. While mounted, it resolves Rynn's
  record `0xa50` with the level's SYSTEM prefix. Selected roster bodies use their explicit character records. None of
  these paths selects a far-LOD resource instead of the character's normal resource.

## Archived RAM evidence

These are the **2026-11-04 position/inventory acceptance captures**, not a fresh reproduction of the follow-up or an
acceptance run of today's diagnostic change. Vertex counts below come from the resident resource's LOD descriptors,
not screenshots or an assumption that slot zero looks sufficiently detailed.

| Capture under `build/harness/` | Ghost body | Model resource | Active slot | Available vertex counts |
| --- | --- | --- | --- | --- |
| `coop-position-fixed/a/ram_before.ram` | `a96670` remote default Rynn | `20f25` | 0 | 1628 / 208 |
| `coop-position-fixed/a/ram_inventory.ram` | `a96670` remote default Rynn | `20f25` | 1 | 1628 / 208 |
| `coop-position-fixed/b/ram_before.ram` | `dacfb0` remote default Rynn | `20f25` | 0 | 1628 / 208 |
| `coop-position-fixed/b/ram_partner_inventory.ram` | `dacfb0` remote default Rynn | `20f25` | 0 | 1628 / 208 |
| `coop-position-fixed/b/ram_after.ram` | `dacfb0` remote default Rynn | `20f25` | 0 | 1628 / 208 |
| `coop-position-final-npcs/a/ram_before.ram` | `dad1f0` selected local body | `51323` | 0 | 1056 / 424 / 108 |
| `coop-position-final-npcs/a/ram_before.ram` | `daecf0` selected remote body | `51262` | 0 | 1020 / 352 / 176 |
| `coop-position-final-npcs/b/ram_before.ram` | `dacfb0` selected remote body | `51323` | 0 | 1056 / 424 / 108 |

The ordinary Rynn gameplay samples demonstrate highest available detail on a remote default Rynn. The inventory
sample demonstrates that its native slot can change, not that inventory LOD is itself faulty. The selected NPC samples
demonstrate highest detail for those particular resources only: their highest-detail meshes contain fewer vertices
than Rynn's. They do not prove the reporter used those characters, or justify replacing their source art.

## Diagnostic and remaining acceptance

`DRAKAN_GHOST_LOG=1` now reports each live ghost's initial native LOD and subsequent changes, with body handle, model
resource ID and selector flags. It is read-only, uses the existing opt-in log, and prints only changes; it does not
change pose playback, LOD requests, geometry, resource choice or network state. The parent integration task owns the
game compilation. No shared game/runtime build or new live instance was launched for this investigation.

A fresh close-up reproduction should pair the screenshot and camera distance with that log and a RAM dump, naming
the character and executable revision. Inspect the matching model's descriptor count and active slot. A nonzero slot
stuck near the camera would justify investigating selector eligibility, distance/bounds, pending transition flags or
resource availability; slot zero with the correct resource would establish a source-model/detail limitation instead.
Archived slot-zero evidence cannot certify the follow-up's visual result on its current executable.

## Fresh native near/far readback on the final build

`issue1-remote-motion-baseline2` and `issue1-remote-motion-adverse` captured fresh
native RAM, skeleton poses, LOD transition logs and near screenshots from final
executable SHA-256
`fce25c698b06cc77a233aea741c3f46c318a3c06fa40b565df9813002ed02503`.
All four native processes completed scripted quit with exit 0; deployed settings,
source fixture card and executable remained unchanged. No LOD override or network
production change was added.

Descriptor readback uses the resident model resource's LOD table and the live
skeleton/controller's selected slot, matching `drakan_scene_models.h`. Distances
below use the actual native selector camera origin at `4dadc4/8/cc`, independently
checked against the active camera handle transform. Native root/spine clips and
clip times were retained too; the verdict does not infer mesh detail from pose,
texture resolution or a screenshot alone.

| Run / peer / stage | Live remote body | Model | Slot / active vertices | Available vertices | Selector distance |
| --- | --- | --- | --- | --- | --- |
| Baseline A near | `8a67c0` | `20f25` | 0 / 1628 | 1628 / 208 | 2339.1 |
| Baseline B near | `dfd680` | `20f25` | 0 / 1628 | 1628 / 208 | 1094.3 |
| Baseline B later far | `dfd680` | `20f25` | 1 / 208 | 1628 / 208 | 6579.1–6634.7 |
| Adverse A near | `dfbba0` | `20f25` | 0 / 1628 | 1628 / 208 | 2272.6 |
| Adverse B near | `df99c0` | `20f25` | 0 / 1628 | 1628 / 208 | 1129.3 |
| Adverse B later far | `df99c0` | `20f25` | 1 / 208 | 1628 / 208 | 6618.3–6727.0 |

The useful visible close-up is
`build/harness/issue1-remote-motion-baseline2/a/near.png`: it frames the local Rynn
in the foreground and remote Rynn to the right, consistent with the actual
highest-detail native mesh. B's near RAM is valid but its remote subject is behind
the camera; B's near image is not visual proof of the remote mesh. Original
screenshots were inspected without upscaling or editing.

`lod-readback.json` retains all stage results under each run. Baseline B's earlier
startup body `dfb280` was explicitly destroyed and replaced before the near
captures. Its stale handle has no live skeleton and is recorded as an error,
not mistaken for a low-LOD subject. The valid rows identify the actual replacement
body and matching model descriptors.

The fresh data **does not reproduce an always-coarse nearby remote Rynn**. The
native selector supplies all 1628 vertices nearby and can choose 208 vertices at
longer range. This is still original source-model geometry; high-resolution
textures do not add vertices. Other roster characters, replacement models and
the reporter's exact character/settings were not covered by this native pair.
Earlier NPC descriptor examples remain character-specific evidence, not a promise
that every source model has Rynn's vertex count. Motion/lifecycle analysis and
impairment limits are recorded in [the motion journal](issue-1-remote-motion.md).
