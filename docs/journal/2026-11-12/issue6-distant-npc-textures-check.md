# Issue 6 item 3: check of the "broken disc data" verdict (2026-11-12)

Second look at [the 2026-11-10 note](../2026-11-10/issue6-distant-npc-textures.md): the claim was that the fat villagers'
last LOD is mapped wrongly on the disc itself. The alternative tested here: lower LODs have their own texture list or
dedicated low-detail sheets, and the port resolves "texture 2" of LOD 2 to the wrong sheet. Evidence, tools and
images stay private under `build/review/issue6-3/` (not committed).

## Result

The verdict holds: the disc data is at fault. The port binds exactly the sheet the record names, and no texture on the
disc fits the LOD 2 UVs. The built-in workaround (`hle::CharacterLodFixOverride`) is kept unchanged. It matches the
two affected models and nothing else on the disc. Checked in the live game for two of the five Surdana villagers,
with and without HD textures.

## Disc data

* **One texture list per model, for every LOD.** The 0x0200 list of the healthy multi-sheet characters simply grows:
  FattyChick 0x13cb lists 0x18e2, 0x18e3, 0x18e4, 0x1986, 0x1987. Its LOD 0 uses indices 0 to 2, LODs 1 and 2 use
  3, 4 and 2, both in 0x0204 and in the 0x0211 batch words. northman4, NicholsTheBold, ConcubineNew3 and Guard2 are
  the same. So an index means the same list entry in every LOD. BigBrownGuy 0x131d and BigBlueGuy 0x131c list only
  three sheets (0x187e, 0x1884 / 0x187f, 0x1885 / 0x1880), and their LOD 2 (0x0204 and 0x0211 list 2) uses only
  index 2, which is the torso sheet.
* **The LOD 2 UVs are a projection, not an unwrap.** A least-squares fit of the UVs to the vertex positions gives
  v = 2.39 y + 0.61 with rms 0.000 (0x131c: 0.007): v is the height. u follows x and z (rms 0.18). No other lower LOD
  of the 57 multi-LOD models on the disc (every MOD) has a u or v rms below 0.03; all lower-LOD texture indices are
  within their list. The UV plot over sheet 2 is a whole-body silhouette (`evidence/renders/131d_uv.png`).
* **No sheet on the disc fits.** LOD 2 was rendered with each texture of NPCS.TXD and of every other TXD on the
  English tree (SYSTEM, INTERFCE, MAGIC, FX, RESOURES, the level and WORLDCON TXDs; LEVEL1TS of LEVEL1NA is not SRSC
  and was skipped). Each render was scored against the LOD 0 render (mean RGB distance over covered pixels, front
  and back). LOD 1 as on the disc scores 25.9 (0x131c: 21.8), the disc's LOD 2 58.6 (100.5). The best candidate of
  all sheets scores 47.3: plain brown swatches that only match the average colour
  (`evidence/renders/131d_lod2_best.png`). A dedicated low-detail sheet would score near LOD 1.

## Run time

Baseline play folder (about v0.1.11, owner's `drakan.ini`), `play_run.py` recipe placing Rynn 12500, 7500 and 3000 units
from the villager, RAM dump at each stop:

* The resident model resource of 0x131d keeps one texture library (`model+0x38`): 0x5187e, 0x51884, 0x51885. Its
  prepared LOD slots list strip parts with materials 1, 0, 0, 2, 2, 2 (slot 0), 1, 0, 2, 2 (slot 1) and 2 (slot 2):
  slot 2 binds 0x51885, the torso sheet, as the record says. The LOD distances are stored squared (6000², 12000²).
* The far villager object (resource 0x5131d) is on slot 2 at 12500, slot 1 at 7500, slot 0 at 3000.
* `GSR_TEXREPLACE=0` (no HD textures; the paving visibly changes) gives the same smear. The HD pack and the texture
  cache are not involved.

## Affected NPCs in the game

Placements (all levels, `lvl_objects.py`): the classes on 0x131c / 0x131d (`Male Villager Fat`, `Fat2`, `Fat3`, `Fat4`,
`Old Guy 1`, `Old Guy 2`, `Fatty big blue (Male)`) are placed in Surdana (5), Shadowmire (1), Andrellian Isles (1)
and Valley of the Fallen (6).

| Where | NPC | Seen in the game | Same cause |
| --- | --- | --- | --- |
| Surdana (439201, 160302, 450978) | Male Villager Fat4, 0x131d (the report's NPC) | far smear, slot 2; fixed | yes |
| Surdana (428007, 160615, 443025) | Male Villager Fat, 0x131c | far pale smear with rust legs, slot 2; fixed | yes |
| Shadowmire (222184, 210645, 453331) | Male Villager Fat4, 0x131d | RAM: slot 2 at 12500; no line of sight in 7 tries (swamp, fog) | yes (record) |
| Valley of the Fallen, Andrellian Isles | 0x131c / 0x131d | not reached (Valley: still in the opening cut scene after 9000 samples; Isles not tried) | yes (record) |
| Surdana (565590, 152511, 474261) | Old Fat Lady, FattyChick 0x13cb | far: behind a fence, not judgeable | no: an authored unwrap on its own sheets (red-haired head), a look change, not a mapping fault |

The other three Surdana fat villagers were not checked one by one; they use the same two models.

## Fix (unchanged)

`hle::CharacterLodFixOverride` keeps LOD 1 for the two models by writing 1e8 into the LOD 2 distance of their 0x0208
record. Its shape match (3 LODs, 996 / 452 / 220 vertices, 1928 / 882 / 440 polygons, 0 / 6000 / 12000) hits 0x131c
and 0x131d in all five language trees and no other model on the disc. Nothing else needs it: no other model has a
projected or out-of-list lower LOD. `override_test`: 73 checks pass (`runtime/hle` built alone).

Live game, full build of 2026-11-12 (`build/review/play`, same `drakan.ini`):

* Surdana Fat4: slot 1 at 12500 and 7500, slot 0 at 3000; with `DRAKAN_NPC_LOD_FIX=0` slot 2 again
  (`evidence/A_far_before_after.png`, `A_mid_near_after.png`).
* Surdana Fat (0x131c), seen from the north: slot 2 to slot 1 at 12500, HD and no HD (`B_far_before_after.png`,
  `B_mid_near_before_after.png`).

Run settings that differ from the owner's play: a 1280x960 window (`DRAKAN_FULLSCREEN=0`, ini: borderless fullscreen),
audio off, `DRAKAN_NO_ACTIVATE=1`, `DRAKAN_PLAYTEST_GOD=1`, a New Game on an empty card through the autopress. The
play folders carry the mod packs `ootf-levels` and `drakan-character-motion`; they replace FX, RESOURES and LEVEL1ON
files and patch NPCS.ADB and NPCS.ODB, but not NPCS.MOD or NPCS.TXD.

## Not established

* Real PS2 hardware or PCSX2: not checked (neither is set up here). That a PS2 draws the same smear is inferred from the
  disc records and the recompiled engine's own choice of slot and library entry.
* Each Surdana run shows a second object (resource 0x51323, MediumGuy, flags 0, slot 0) at the same position as the
  villager. It does not change with distance or with the fix; what it is was not looked into.
