# Level copies in the game: direct load and an exit into the copy (2026-11-01)

The editor's "copy a level" export (`buildProjectPack`, `files/ENGLISH/LEVELS/<mangled>/...` + `levels = [...]`) had never been
loaded in the game. Now proven, with two fixes and the exit link.

## Findings

* **Splat files need both names.** A copy of Level 1D ("City Copy", folder `CITYCOPY`) started through `DRAKAN_START_LEVEL` crashed
  right after the intro (`missing-target` JALR in the level load, twice; the same level unmodded loads fine). The pack only had
  `CITYCOTS.*` (copy title). The LVL's dependency table names `Level 1D - City of the Damned_splats`, which the New Game redirect does
  not rewrite (it only rewrites the Surdana name), so the game opened `LEVEL1TS.DB/TXD` in the copy's folder and the missing file
  crashed it. A Surdana clone (the old test) worked because there the redirect does rewrite the stem. Fix: the export ships the splat
  files under `<source stem>TS` and `<copy>TS` (`export.ts`). Entering a copy through an exit needs the source name too.
* **Exit into a copy needs no runtime table.** An exit's destination is a level XDB string (`LEVEL1ON.XDB` string 15 = Surdana's
  City exit, text `Levels\Level 1D - City of the Damned\Level 1D - City of the Damned.lvl`). A pack that overrides that entry
  (`overrides/LEVEL1ON.XDB/0400_15_1.bin`, text `Levels\City Copy\City Copy.lvl` in every slot; the Logic panel's exit picker already
  writes this through the Story session) makes the game's own transition load the copy by that path. The arrival marker works
  because the copy has the source's exit back, whose text equals the old level's name; and the way back works because the same
  string names the copy in Surdana's marker search. The string is shared by every exit using it (per-object overrides were not built).
* **Stale names in the host's exit list.** `drakan_levelchange` named exits from `kLevelGroups` / `kDestTexts` first, so the
  retargeted exit still read "Level 1D - City of the Damned" (it names which level `DRAKAN_LEVELCHANGE=city`, the F1 list and
  netplay follow resolve). Now: the live cached string first, then a mod-pack hook (`setDestinationTextHook`: asks the pack's entry
  overrides for the English text of that XDB string), then the tables. A copy shares its source's group (`registerLevelCopy`,
  from the manifest key `level_copies = ["Copy = Source"]`, `ExtraLevel::copyOf`). `portal` texts are still refused.

## Proven in the game (private card, PAL, `build/final-game/bin/drakan.exe`)

Pack `build/copylevel/mods/test.levelcopy` (made by `build/scratch/copylevel/make_pack.mjs` + `link.mjs`, scratch, not in the repo).

| Run | Result |
| --- | --- |
| `build/harness/cl-direct` (`DRAKAN_START_LEVEL=City Copy`) | New Game loads the copy, Rynn on the arrival teleporter, 176 fps (`shot1_600.png`) |
| `build/harness/clz-city` (Surdana, fire the City exit) | exit list names it `City Copy`; `afterLevelLoad level='City Copy' path=.../City Copy/City Copy.lvl`, Rynn on the marker (`s1.png`) |
| `build/harness/clr-custom` (`city,0`: Surdana -> copy -> back) | both loads, Rynn back at the Surdana City exit (`s2.png`) |

Not proven: a real **walk** into the exit (`level_change_walk.py --exit city`, `build/harness/clw-walk-city`: the touch never fired,
the known open problem of walk-ins for stock exits too); the editor UI itself (the pack was made with scripts that mirror
`buildProjectPack`; the editor code is type-checked and unit tested, the desktop Play button for a copy was not clicked);
`dist/DrakanPC` was not refreshed (the build's clean step was blocked by a running game from that folder, so the proofs ran
from `build/final-game/bin` with a private play folder).

## Editor changes

* `playtest.ts`: `setCopyPlayProvider`; when the open level is a project copy the play-test pack is the whole level pack
  (`buildProjectPack` for that level with the play-test document), not a patch of a disc level. Registered in `project/install.ts`.
* `project/export.ts`: both splat names, `levelCopies` in the manifest, optional document override for the play test.
* `mod.ts`: `ModMeta.levelCopies` -> `level_copies` key. Logic panel exit picker lists the project's copies as destinations.
* Mangled-name clashes between copies and disc levels were already validated (`title-clash`, `copy-title`).
