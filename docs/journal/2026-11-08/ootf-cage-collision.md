# OotF: Rynn clips into cages (2026-11-08)

User report: "Rynn's animation / body can clip through cages causing game-breaking problems."

Probes, packs and runs are private under `build/ootf-cage/` (`cages.mts` / `cagemodels.mts` list the PC cages,
`cagemodel.mts` prints a model's PC and converted 0x0207, `scope.mts` measures the whole campaign, `cage_walk.py` is a
`live_drive.py` mission that places Rynn on a side of a cage and walks her through its centre, `movecage.py` moves objects
of a converted LVL as a test aid). Nothing from either game is quoted here.

## Cages in the campaign

| Level | Object | PC model | PC 0x0207 |
| --- | --- | --- | --- |
| Wartok Canyons | Cage(L) 816, Cage Body(L) 813, 814 (hanging from a post) | Cage02Empty, Cage02wPerson | one sphere |
| Grimstone Mines | Cage(L) 329 | Cage02Empty | one sphere |
| Islands | Cage 2347 (rfl 16, dropped by its rope) | Cage | OBB: root listing all polygons, 5 plain boxes (walls, roof) |
| Volcanic | Cage 02 794 | Cage01wPerson2 | OBB: root and 3 leaves listing polygons (313 memberships over 255 polygons) |
| Volcanic | Cage 02 795 | Cage02wPerson2 | one sphere |
| Volcanic, Succubus Lair | Dragon Platform (Arokh) with Brace / Neck Brace props | finalplatform; brace1/2, nekbrace1 | OBB: root and 6 plain boxes; braces one sphere |

Every cage hangs: model y runs from the origin down (Cage01wPerson2: hanger 0 to -334 units, bars -323 to -828, the
prisoner's legs to -1,117), and the levels put the origin 1 to 2 lu above the floor (Volcanic 794 at 135917 over a floor
at 133928, so its bars are above Rynn's head; Wartok at 17139 over the post foot at 15000). Where the terrain under a
cage is higher, or Rynn jumps or climbs, the bars are at her body.

## Two converter defects

**1. Lists of display-listed props (all rigid props).** PC object collision is the model's 0x0207 hierarchy: boxes, and
per box the polygons tested inside it. The PC lists a polygon in every box it crosses (Cage01wPerson2: 313 entries for 255
polygons) and uses a box without polygons as a solid block. Rigid props (PS2 rfl 17) get a strip display list, and that
upgrade (`rigidDisplayList` in `ootfassets.ts`) rebuilt the lists geometrically: each polygon went to the one leaf box
holding its centre (`rebuildObbHierarchy`), and a hierarchy whose leaves list no polygons fell back to one box over the
whole model, throwing away the lists `pcToNeutral` had remapped from the PC (formats `f5d3c6b`). Across the 13 campaign
levels 634 rfl 17 models have an OBB hierarchy; 303 lost list entries (74,147 in all, for example Ship 1,154 of 1,684,
sanctum dome 2,211 of 2,891, Dragon Gate 805 of 1,055, Cage 02 79 of 387) and 83 had their blocker boxes collapsed into one
box (Volcanic's and Succubus Lair's Dragon Platform, crates, the Volcanic lock, the ice cover). Fixed in formats `c6ad739`:
`buildDisplayList` reports `polygonSource` (output to input polygon; the chunked rigid path splits and reorders) and
`rigidDisplayList` maps the PC lists through it with `remapObbPolygons`; the geometric rebuild is only the fallback.

**2. Cages are not solid on the PS2 in either PC form.** Even with exact lists, Rynn walking at Cage 02's bars at body
height pushes 100 to 150 units past them into the cage (table below), and with a sphere (the PC form of Cage02Empty,
Cage02wPerson, Cage02wPerson2) nothing stops her. The disc itself almost never relies on listed polygons for props: 716 of
its 863 OBB hierarchies (host MODs) list none, so their boxes are solid. Cage models (`/^cage\d/i`: the four above; not the
Islands rope cage `Cage(H)`, which already has PC blocker boxes and is no display-listed prop) now get one solid box over
their polygons (`SOLID_BOX_MODEL` in `ootfassets.ts`). Tests in `ootftex.test.ts`: a triangle listed in two leaves stays
in both, plain blocker boxes stay, a cage gets one box without polygons (each fails on the old code).

## In game

Private copy of the play folder (`build/ootf-cage/play`, no HD textures), direct starts of Volcanic, god mode, placement
hold 30, each run holding `build/ootf-run/game.lock`. Packs: the shipped `dist/DrakanPC/mods/ootf-levels.zip` (build
`76c09ab2d9bada3f`) and the 13 campaign levels converted from the working tree with fix 1 (build `5bbbc44f2359211d`) and
with both fixes (`mods-fix2`). The cages hang out of reach where the levels put them, so a test aid (`movecage.py`) lowers
the cage under test by 859 units in every pack (Cage 02 794 to y 135058: bars 300 to 800 units above the floor, the
prisoner's feet on it) and moves the other one away; the packs then differ only in the converted models. `cage_walk.py`
places Rynn 1,000 units from the cage on 8 sides and holds W toward a point beyond it for 200 samples, steering by the
mouse. Cells: nearest approach of her origin to the cage axis / the most she got inside the cage's footprint (the bars
stand about 250 units from the axis; positive = inside), samples on the floor only (the placement aid sometimes put her
on the cage or the crates, and the floor south of the cage drops into a pit).

| Run | Pack | W | E | N | S | NW | NE | SW | SE |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `vlow-base` | shipped | 289 / 40 | 711 | 313 | 176 / **117** | | | 833 | |
| `hangA-base` | shipped | 301 | 357 | 135 / **112** | | | | | |
| `hangA-fix` | fix 1 | 280 / 40 | 798 | 306 | 159 / **102** | | | | |
| `hangB-fix` | fix 1 | 334 | 804 | 174 / 48 | 136 / **126** | | | | |
| `c794-fix2` | fixes 1 and 2 | 404 | 450 | 369 | 745 | 444 | 413 | 399 | 359 / 9 |
| `c795-base` (Cage 02 795, sphere) | shipped | 255 / 67 | 303 / 9 | 246 | 303 | 250 | 640 | 809 | |
| `vsph-fix` (795) | fix 1 | 335 | | 394 | **30 / 199** | | | | |
| `c795-fix2` (795) | fixes 1 and 2 | 373 | 412 | 395 | 413 | | | 798 | 372 |

* Shipped pack and fix 1: walking into the bars she ends up to 126 units inside the footprint of 794 (her body through the
  bars, the prisoner's legs in her), and inside 795 down to 30 units from its axis.
* Both fixes: on every side she walked she stops 359 to 450 units from the axis of either cage, outside the bars, and
  slides round them. Per-sample positions: `build/ootf-cage/runs/<run>/drive.jsonl`, shots `c_<x>_<side>[_end].png`.
* Fix 1 is not seen here (the cage is replaced by the solid box); it restores the PC lists of the other 302 props and the
  blocker boxes of 83, which no run here walked.

## Open

* The user's exact spot was not reproduced: every campaign cage hangs above the floor where it is placed, so the runs move
  one down. Wartok direct starts (mounted) stayed on the loading screen with the world at 0 fps in this harness
  (`wartok-*`), and Grimstone's opening scene puts Rynn back on her start, so those cages were not walked in place.
* Other sphere-bounded rigid props are unchanged (the Arokh braces brace1 / brace2 / nekbrace1, many small props): Rynn
  likely walks through those as well; not checked against the PC.
* Needs a regenerated pack: the shipped `ootf-levels.zip` still carries the old collision.
