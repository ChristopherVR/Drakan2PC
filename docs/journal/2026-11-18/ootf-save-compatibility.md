# Order of the Flame saves across the creature replacement

The installed pack had build `493b53de681b8791`. Several real saves, including the Wartok Canyons save in slot 14,
still used `da1e598e3d3adfdd`. Comparing the installed layout with the archived pack under
`build/review/cpu-offload/mods` found four removed NPC classes and 45 placed objects switching to their disc
counterparts. All other layout keys matched. The disc replacements have the same engine class and field count.

| NPC class | Earlier ID | Disc ID |
| --- | ---: | ---: |
| Crimson Dragon | 7084 | 3040 |
| Blade Dragon | 7096 | 2676 |
| Ice Dragon | 7109 | 5604 |
| Scavenger AnimWay Point | 7130 | 3134 |

The migration runs on a copy of the save opened by the memory card reader. Its build check applies to this pair of
Order of the Flame builds. The reader resolves the database by the save's own table and rewrites the class ID in
each matching record. Slots and references keep their order. The normal pack revision upgrades also run.

The compiled game restored copies of slots 13, 14 and 33 and accepted movement input. Slot 14 replaced nine Blade Dragon
records. Slots 13 and 33 restored the statue while its generator was present. SHA-256 comparisons against the copies
confirmed the original save records, level states and sidecars kept their bytes.

The final build loaded a copy of slot 14, saved it, and loaded it again. It wrote the current build for the new
save and kept the old build in sidecars for three earlier level states, whose contents kept their original bytes.
If a required save upgrade fails, the memory card reader refuses the open. The deployment also preserved all 103
files on the installed memory cards by SHA-256 comparison.

The converter now checks compatibility before replacing a pack. A visual class split can retain the build when the old
class stays available with the same signature and the new class shares that signature. The level's dependency IDs are
explicit and sparse. For example, slot 3 in Ruined Village names Resources even though it is the second dependency
listed. The complete 23-level rebuild for the tree fix retained build `493b53de681b8791`. The guided builder uses the
installed pack as its baseline and the installer rejects a differing build or a lower save revision.

## Earlier packs

The exact 9 November pack was rebuilt from the editor at `09ff9e11` and formats at `48f6c928`, using the locally
extracted PC installation and the Drakan 2 `LEVEL1NA` template. The full conversion reports content build
`d248f6c701888bf8`, matching the older pack stamp in slots 2, 3 and 4. The current pack differs at 1,852 layout keys.
The recovery launcher under `build/legacy-save-recovery/nov09` uses the matching pack and a separate copy of the
memory card. Loading a copy from that card through the native Load Game screen returned to gameplay with the pack
checks enabled.

The tested 14 November snapshots produced different content builds from the one stamped on slot 1
(`daf6d8d09586c549`). That slot remains preserved on the original card. Its exact source pack has not been recovered.
