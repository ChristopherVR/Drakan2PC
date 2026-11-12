# Issue 6 item 3: wrongly textured NPCs at a distance (2026-11-10)

Report (v0.1.10, unchanged on v0.1.11): a few NPCs have wrongly mapped textures on their low-detail model when seen
from far away. The reporter's picture shows a grey-haired male villager on the grass outside Surdana: a brown smear
with flesh-coloured legs when far, correct when a little closer.

Checked again on 2026-11-12 ([issue6-distant-npc-textures-check](../2026-11-12/issue6-distant-npc-textures-check.md)):
one texture list serves every LOD, no sheet on the disc fits the LOD 2 UVs (they are a height projection), the running
engine binds the sheet the record names, and the built-in override works in the game. The evidence paths below
(`build/issue6-3/`) no longer exist; the new evidence is under `build/review/issue6-3/`.

## Result

It is a fault in the disc's model data, not in the port. The renderer, the texture cache and the HD textures are not
involved. The port now works around it for the two affected villager models (`hle::CharacterLodFixOverride`).

## The model

* The NPC in the picture is the Surdana object `Male Villager Fat4` (NPCS.ODB class 5757) at world position
  (439201, 160403, 450978), model `BigBrownGuy`, NPCS.MOD id 0x131d. `BigBlueGuy` (0x131c, classes `Male Villager Fat`,
  `Male Villager Fat3`, `Old Guy 2`, `Fatty big blue (Male)`) is the same mesh with other textures; `Male Villager Fat2`
  and `Old Guy 1` use 0x131d too. Surdana places 0x131d three times and 0x131c twice.
* Textures of 0x131d (NPCS.TXD): index 0 = 0x187e (trousers, boots, 256x256), 1 = 0x1884 (head, 128x128),
  2 = 0x1885 (torso, arms, 256x256). All 8 bit with a palette, like every other NPC sheet; nothing special about them.
* LOD table (0x0208): LOD 0 from 0 units (996 vertices, 1928 polygons), LOD 1 from 6000 (452 / 882), LOD 2 from 12000
  (220 / 440).
* LOD 0 and LOD 1 use all three textures (474 / 420 / 1034 and 326 / 97 / 459 polygons). **LOD 2 names texture 2 for
  all 440 polygons**, head and legs included, and its UVs do not fit the sheets either: against the nearest LOD 0
  triangle, 98 % of the LOD 2 triangles that should use the torso sheet sit more than 0.2 away in UV space (other
  models: 0 to 20 %). 0x131c is the same (all 440 on its texture 2, 99 %).
* The engine draws skinned models from the strip record 0x0211, not from 0x0204. Its leading word is the number of
  lists, one per LOD (new, see level-format.md section 11). For 0x131d: list 0 has batches on textures 1, 0, 0, 2, 2,
  2; list 1 on 1, 0, 2, 2; **list 2 is a single batch on texture 2**. So both records agree and the PS2 draws the same
  thing. A software render of LOD 2 from either record gives the brown figure with one flesh-coloured leg of the
  reporter's picture.
* No port code touches LOD selection (the native selector `0x102c90` runs as recompiled; nothing in `runtime/` or
  `ps2xRuntime` scales LOD distances). At 12500 units the figure is about 80 pixels tall in a 3840x2160 frame, so under 20 lines of a PS2
  field, which is probably why it went unnoticed.

Not checked on PS2 hardware or PCSX2 (neither is set up here). The claim rests on the disc records the engine reads.

## Reproduction in the game

`dist/DrakanPC/drakan.exe` (the current play folder with its own `drakan.ini`), `scripts/harness/play_run.py`
with a recipe that places Rynn 12500, 7500 and 3000 units south of the villager (`place 439901 161103 463478 0` and so
on, `DRAKAN_PLAYTEST_GOD=1`), private output under `build/issue6-3/` (not committed):

* far: brown smear, as reported (`runs/issue6-3-base/A_far.png`);
* 7500 and 3000 units: textured correctly.

The same run with a private mod pack that overrides the two 0x0208 entries (`overrides/NPCS.MOD/0208_4892_1796.bin`,
`0208_4893_1796.bin`), with only the LOD 2 distance changed from 12000 to 1e8, shows the villager textured at 12500
units (`runs/issue6-3-fix/A_far.png`, side by side in `cmp_A.png`, `cmp_A_far_zoom.png`). So the engine takes the
distance from that record through the 0x1202 resource requests and keeps LOD 1 when LOD 2 is out of reach.

## Fix

* `runtime/hle`: `CharacterLodFixOverride` (`hle/ResourceOverride.h`, `src/ResourceOverride.cpp`), a resource override
  for NPCS.MOD entries of type 0x0208. It finds the LOD table by its layout and, only when it is exactly the table of
  this mesh (3 LODs, 996 / 452 / 220 vertices, 1928 / 882 / 440 polygons, distances 0 / 6000 / 12000, one match in the
  record), writes 1e8 into the LOD 2 distance. Matching by shape instead of entry id keeps it safe for the USA disc
  and for packs that replace these models. Cost: 882 instead of 440 polygons for a far fat villager.
* `ps2xRuntime/src/lib/drakan_overrides.cpp` registers it at start-up, after the campaign overrides.
  `DRAKAN_NPC_LOD_FIX=0` leaves the disc's LODs alone (env-flags.md).
* Test: `override_test` (`runtime/hle/tests/override_test.cpp`): the broken table is patched in its four distance
  bytes only, a patched record, a healthy three-LOD villager, a two-LOD model and a cut record are left alone. 73
  checks pass (built in `build/issue6-3/hle`).

The game was not rebuilt here. The in-game proof above used the byte-identical change through a mod pack on the
existing executable. The built-in override was checked in the game on 2026-11-12 (see the check note).

## Other models found by the same audit (not changed)

Audit: every skinned model on the disc, each lower LOD triangle compared with the nearest LOD 0 triangle (texture id
and UV distance). Besides the two above, these lower LODs use other textures than LOD 0. They are coherent models, not
smears, but the character changes look from 6000 units on:

| Model (NPCS.MOD id) | Lower LODs use | Effect at a distance |
| --- | --- | --- |
| `FattyChick` 0x13cb (Old Fat Lady in Surdana, Villager Female Fat 1 / 2, Farmer Fat Lady, Desert Female 3 / 5) | 0x1986, 0x1987 in place of 0x18e2, 0x18e3 | black-haired woman turns red-haired |
| `northman4` 0x125e (Northmen Villager4) | 0x1988, 0x1989 in place of 0x18be, 0x18bf | dark leather vest turns into a fur one |
| `ConcubineNew2`, `ConcubineNew3`, `ConcubineNewBride` 0x13e0 to 0x13e2 | the three `ConcubineNew` sheets 0x18fd to 0x18ff | skin and clothing colour of the first concubine |
| `NicholsTheBold` 0x141c | the `nicholsthebold clean` sheets 0x197f to 0x1981 | the blood disappears |
| `Guard2`, `Guard3` 0x13b9, 0x13b8 | sheet 0x18d2 (Guard1's third sheet) in place of 0x18d1 / 0x18d0 | small difference in a software render |
| `ChainWartokPS2`, `PlateWartokPS2` 0x1146, 0x114b | a second set of sheets | no visible difference in a software render |

Proposal, not done: the same override could keep LOD 0 for the first four rows and the guards (set both lower distances to 1e8;
LOD 0 has 1968 to 2248 polygons, nothing for a PC). I left it out because none of these was looked at in the game,
the reporter's picture shows only the fat villager, and for Nichols the swap may be deliberate. A general "character detail distance" setting would cover all of them at once but changes the look of the
whole game, so it is a decision for the owner.

## Relation to item 2 (view-dependent wrong textures on world geometry and the HUD)

No shared cause seen. This one is fixed per model and per distance, comes straight from disc data and does not depend
on the view direction, the texture cache or HD replacement.

## Tooling used (private, `build/issue6-3/`)

Node scripts on `editor/packages/formats`: `lodprobe.ts` (per LOD texture use and UV ranges), `lodaudit.ts` (the
audit), `lodrender.ts` / `lodrender2.ts` (software render of each LOD from 0x0204 and from 0x0211), `dlprobe.ts`
(multi-list 0x0211 decode), `classmodels.ts` (class to model), `mkpack.ts` (the probe pack), `actors.py` (live NPC
positions from a harness RAM dump).
