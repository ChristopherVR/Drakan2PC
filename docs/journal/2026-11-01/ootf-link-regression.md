# OotF link-position fix: 13-level regression, Book slow load, exit boxes (2026-11-01)

Follows [ootf-puzzles-ending.md](ootf-puzzles-ending.md) (object links are table positions, `pc.ts` translates them),
[ootf-full-level-runs.md](ootf-full-level-runs.md) and [ootf-campaign-walkthrough.md](ootf-campaign-walkthrough.md).
Code: `editor/packages/formats/src/ootftex.ts`, `ootfterrain.ts`, `ootfcampaign.ts` (`exitBoxes`), `ootfpuzzle.ts` (`quietCloudRings`),
`ootfpipeline.ts` (test aids). Harness (private, `build/ootf-run/g/`): `all.sh` (13 levels, convert outside the game lock, run inside it, trap
release), `bv.sh` / `bvid.sh` / `bvseq.sh` (Volcanic bisect), `ex.sh` (place Rynn in an exit of the campaign pack `ct-camp7`), `hop.sh`.

## 1. Regression run: all 13 levels with the corrected links

`build/ootf-run/g/all.sh g` (one level per pack, `recipe_long.txt`, `PS2X_WATCHDOG=60`, Intro first as the harness does), binary
`dist/DrakanPC`. "Control" = `autostart: player under control` and `gui screen 14 -> -1`; "min flips" = smallest `completedFlips` per 2 s after
control (about 270 is a healthy level here; 100 to 350 is normal for the big ones).

| # | Level | Entities (was) | Loads | Rynn control | Crash | Min flips / 2 s |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Intro | 112 (115) | yes | yes | no | 1835 |
| 1 | Ruined Village | 2359 (1669) | yes | yes | no | 317 |
| 2 | Ruined Village Book | 24 (24) | yes | yes | no | 1422 (**81 s to control, see 2**) |
| 3 | Wartok Canyons | 2039 (1990) | yes | yes | no | 121 |
| 4 | Grimstone Mines | 1656 (1542) | yes | yes | no | 265 |
| 5 | Grotto | 547 (486) | yes | yes | no | 121 |
| 6 | Islands | 2709 (2350) | yes | yes | no | 136 |
| 7 | Alwarren | 2038 (1470) | yes | yes | no | 97 |
| 8 | Rift Crystal | 3010 (1294) | yes | yes | no | 354 |
| 9 | Islands Night | 726 (713) | yes | yes | no | 277 |
| 10 | Succubus Lair | 1036 (1022) | yes | yes | no | 103 |
| 11 | Volcanic | 1463 (1457) | yes | yes | no | **3: 2 frames per second (regression)** |
| 12 | Rift World | 166 (164) | yes | yes | no | 72 |

No level lost control, crashed or stalled at load; the screenshots at the start and after the W walk differ in all 13. The campaign exit chain
still works: a new campaign pack (`ct-camp7`, all 13 levels) from a New Game plays the Intro, the 44 s timer takes the hand-over (`exit taken in
'Level 0 - Intro'`), Ruined Village loads and Rynn gets control (`intro_c7`).

### Volcanic: 2 frames per second after control (fixed)

After `autostart` the guest ran at 4 to 7 flips per 2 s (about 2 fps) instead of 270 (a1-volc, written after the link fix, shows the same; the
pre-fix b-volc2 ran at 520+). The guest sits in a busy loop (`pc 0x14b968`). Bisect (each run: convert with a test aid, short recipe, flips after control):

| Run | Dropped | Flips / 2 s |
| --- | --- | --- |
| g-volc | nothing | 4 to 7 |
| bvA | classes `timer`, `Rift Timer`, `counter`, `detector`, `trig`, `trigger button` | 270 |
| bvB | `timer`, `Rift Timer` only | 270 |
| bvC / bvE | detectors etc. only / the 12 zero-delay timers only | 4 to 7 |
| t1 / t3 | first 24 / first 12 of the 56 timers (ids) | 270 |
| t5 / t6 | timers 4..9 / 0..3 | 4 to 7 |
| u1 | only object 1633 (**Rift Timer**, `Start Mode` 0) | **270** |
| v1 / v3 | the 4 Lightning Object Generators / Vortex + Inner + Outer Rift + flare | 4 to 7 |
| v2 | the Cloud Rings (`Cloud Ring 01..03`, `clowdRing*`) | 90 |
| w1 | fix below | 85 |

Cause: with positional links the Rift Timer (starts at once) reaches the 17 rift effect objects it was built for (Inner / Outer Rift, Vortex
x2, Cloud Rings, flare, lightning generators). They are movers cloned from the template "Flaming Bolt test" (rfl 35), the PC engine culls them
by distance, the PS2 engine animates them everywhere. Before the fix none of this was linked, which is why the effect never ran.

Fix (`quietCloudRings` in `ootfpuzzle.ts`): links to objects of the classes `Cloud Ring NN` / `clowdRing[N]` are removed (11 links in Volcanic); the
rings stay as scenery. Volcanic then runs at about 85 flips per 2 s (about 42 fps) with the rest of the rift effect live; dropping the vortex and
rift movers too would restore 270 but removes the whole effect, which Rift World's finale also uses, so it was not done. Test aids added:
`DRAKAN_DBG_DROPIDS=<id,id>` (drop objects by PC id).

## 2. Ruined Village Book slow load (fixed)

Not a hang: the level does load, but control came at guest `t=81 s` (Ruined Village: 24 s, Intro: 21 s). The Book's 33 object textures are
256 x 256 with more than 256 colours; the converter wrote them as 32 bpp (`truecolor`: 8 bpp only for at most 256 colours, else 32 bpp), and the
guest builds mip maps of every texture it uploads (17k profile samples in `0x1c14a0`).

| Pack | Texture mode | Time to control (guest t) |
| --- | --- | --- |
| g-book (default before) | 32 bpp | 81.3 s |
| bk-indexed | 8 bpp, quantised | 21.2 s |
| bk-auto | 16 bpp 1:5:5:5 | not reached in 150 s (16 bpp is even slower than 32) |
| bk-new (fix) | 32 bpp up to 128 x 128, else quantised 8 bpp | **21.2 s** |

So bit depth matters, not just the byte count: 8 bpp with a CLUT is the fast path. Fix (`ootftex.ts`): option `maxTruecolorPixels` (default
128 x 128). In `truecolor` mode a texture with more than 256 colours above that size is quantised to 8 bpp (median cut, alpha kept in the
palette); textures with at most 256 colours stay exact 8 bpp; 128 x 128 and smaller stay 32 bpp. Terrain passes `Infinity` (its 256 px 32 bpp
textures load in time, Ruined Village is unchanged at t=24.3 s and carries the visible detail). Offline error of the 256-colour quantise
against the PC source (see ootf-texture-quality.md): mean absolute 0.2 (0.4 over the 256 px textures) of 255. Test aid committed:
`OOTF_TEXMODE=auto|indexed|truecolor`, `OOTF_TEXMAX=<edge>`.

**The HD pack must be regenerated** for converted mods: texture hashes follow the decoded textures, and the large object textures changed
(`python -I analysis/scripts/ootf_hd_pack.py`).

## 3. Exit boxes

Findings that change the earlier picture (links are correct now, `zz` probe of `pcExits` + incoming links):

* Islands to Rift Crystal: the PC exit (id 4132) lies at y = 0, but its real trigger is **Detector 4131** at (656384, 75736, 474112), 1.5 x 1 x 1.5
  cells, `Detect Only Once`; the terrain under the exit is at y 77832.
* Volcanic to Rift World: exit 1237 at y 115000 (terrain 73222 type 0, 91658 type 2 under it), real trigger **Detector 2318** at
  (294144, 100498, 137941), 7 x 8 x 3 cells, `Detect Which? = 1` (Rynn mounted): flown into.
* Succubus Lair to Volcanic: exit 1394 at y 219000 (doorway layer 219000, floor 209000) is linked from the scenery object `ship2` (237568,
  219000, 247808); Succubus to Islands Night: "Detect and Execute" 1827.

`exitBoxes` (`ootfcampaign.ts`) now puts the detector boxes of an exit where a walker can stand: unchanged within 1500 units of the floor;
spanning floor to exit (+ a default box at floor + 700) up to 16000 above; for a higher exit a default box on every surface of the 60000 below it
plus one that spans them; boxes over the surface for an exit under the ground. The terrain comes from `pc.terrain`.

In-game checks (campaign pack `ct-camp7`, Rynn put with `DRAKAN_PLAYTEST_POS`, then the prompt chain):

| Exit | Rynn at | New pack | Old pack (`ct-camp6`) |
| --- | --- | --- | --- |
| Succubus 1394 | floor 209800 | exit taken, Volcanic loads | no touch |
| Succubus 1394 | doorway layer 219800 | no touch (Rynn is not held there) | n/a |
| Islands 4132 | surface 78632 | exit taken, Rift Crystal loads | no touch (`exI0`) |
| Volcanic 1237 | surface 74022 (and 115800) | exit taken, Rift World loads | no touch (`exV6`) |

Harness catch: Volcanic needs `--settle 12000` and `DRAKAN_PLAYTEST_HOLD=2700` (the placement at t = 7 s landed before the level was up and
looked like a missed box); the first two Volcanic tries without them were wrong.

## Volcanic hops (timeouts 420 s, recipe 330 s)

Both directions now load within seconds of the host-fired exit (the old note: Succubus Lair <-> Volcanic stalled for 170 s each way; the
slow Volcanic load was the 2 fps rift effect, not the hop):

* Succubus Lair to Volcanic (`hopSV2`, `DRAKAN_LEVELCHANGE=1@600`): Volcanic loaded 13 s of guest time after the exit, Rynn under control.
* Volcanic to Succubus Lair (`hopVS2`, `0@600` = the arrival-marker exit): Succubus Lair loaded 1.5 s after; Rynn stands on the Succubus start
  (60416 221400 170944), not on a marker (the known fall-back).
* Name form (`DRAKAN_LEVELCHANGE="OotF 10 - Succubus Lair@600"`) does nothing in the OotF pack ("no exit in this level leads to"): the exit
  scan cannot resolve level-local destination strings of converted levels (`dest` text empty). Use the exit index from the
  `[levelchange] exit N:` lines.

## Open

* Volcanic runs at about 42 fps (85 flips per 2 s) with the rift effect live; the Vortex / Inner / Outer Rift movers are the rest of the cost.
* The real PC triggers (Islands Detector 4131, Volcanic Detector 2318 mount-only, Succubus `ship2`) were not walked or flown; the exit
  boxes placed by `exitBoxes` are the walk-in path.
* The `ootf-full-level-runs.md` entity counts differ from the table above (links now reach many more objects, plus the puzzle stage).
