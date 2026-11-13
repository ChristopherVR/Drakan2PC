# OotF play check: Islands, Islands Night, Rift Crystal, Rift World (round 5, checker A)

Rounds 3 and 4 changed these four levels from data only (half-hole terrain cells, the 3 x 3 texture rule, water, death
clips, armour, spells). This note says what was played in them on the 14 November `dist` pack and exe, what was found and
what was not checked. Scratch, tools, runs and evidence: `build/review/ootf-check-a/` (private). Nothing from either game
is quoted.

## Set-up

* Play folder: a copy of `build/review/baseline-play` (the owner's `drakan.ini`: DLSS quality, HD textures, 1280 x 960)
  with the `dist` exe and DLLs of 14 November (draw state cache on), the `dist` HD textures added, a private ELF copy, and
  the `dist` pack (`dist/DrakanPC/mods/ootf-levels.zip`, formats `d937581`). Comparison packs: `packs/before` (formats
  `10d8713`, before the half-hole fix) and `packs/nov13` (the pack installed on 13 November, before the water change).
* Harness: the round 3 plan driver (`tools/push.py` over `live_drive.py`, `tools/runf.sh`), direct start, New Game, god
  mode, live keyboard and mouse input. Plan `plans/tour.py`: get on foot, then for each spot **place** Rynn one or two
  cells from a cell next to half-hole cells and **walk** her there with W and mouse steering, turn the view round in eight
  steps, walk on two cells, attack the nearest creature, F5 / walk / F8 once, and walk into the level exit. Spots come
  from the PC data (`tools/spots.ts`: drawn floor cells with the most half-hole cells around them).
* Rynn has no weapon in a direct start, so every fight was with bare hands (2 damage per hit on a Crimson Knight); no
  creature was killed, so the death clips and corpses were **not** checked.

## Per level

| Level | Played | Placed | Result |
| --- | --- | --- | --- |
| Islands | Arokh flown out of the arrival tunnel (W 3 s, Space), Ctrl taps to land and dismount (`is-land`..`is-land4`) | Arokh placed on the ground at (396288, 58049, 556032) before the dismount (`is-tour4`, `is-tour5`); Rynn placed at the spots | **Crash / hang at (400384, 58646, 560128)** in 2 of 2 runs on the `dist` pack, also on `packs/before` and `packs/nov13` (see 1). Ctrl landing always ends on one small plateau where Rynn is stuck on Arokh's back (see 2). Exit not played (see "Not checked") |
| Islands Night | 6 spots walked and looked round, F5 / F8, exit walked into Detector 4928: message, save screen, Succubus Lair loads (`in-tour1`, `in-exit1`) | the spots, the exit approach | F8 back within 20 units of the save. No hole triangles, sheets, flat patches or invisible walls seen at the six spots (dark night pictures, `evidence/islandsnight_s1.png`, `_s3.png`). Two spots were hilltops where Rynn slid into the sea and swam |
| Rift Crystal | 6 spots, F5 / F8, three fights, exit over the WhirlpoolSteps with jumps: message, save, Islands Night loads (`rc-tour1`) | the spots, the creatures' surroundings, the exit approach | Halls, stone corridors, the pool and the caves draw without gaps (`evidence/riftcrystal_looks.png`). One spot lies in the deep pit under Kill Box 2207: with god mode Rynn reaches its floor at 165,500, where nothing is drawn (`riftcrystal_pit_black.png`); without god mode the kill box ends her fall first, so no player sees it |
| Rift World | Arokh placed on the ground, Ctrl taps to dismount, 5 spots, F5 / F8, two fights (`rw-tour1`) | Arokh, the spots | No crash. Walking on at one spot started the Delon scene, its subtitle line is readable at the bottom (`evidence/riftworld_looks.png`). The two spots at x 600,000 and more lie in a copy of the arena terrain (layers 141 / 145 duplicate 140 / 144) that is unlit and drawn black over the lilac sky; probably not reachable in play, not judged |

## 1. The crash and hang in the Islands: terrain draw pages run out of memory

Both runs that walked to (400384, 58646, 560128) ended the game: `is-tour4` with `[memset] bad size 0x20400fe ra=0x157d7c`
and then a jump to address 0 (`[fatal] main thread unwound to pc 0`), `is-tour5` with the same `memset` line and a stall
(guest frame frozen for 8 minutes). The same position stalls on the pack from before the half-hole fix (`is-crash-before`)
and on the 13 November pack (`is-crash-nov13`), so rounds 3 and 4 did not cause it. The same `memset` line from
`0x157d7c` is in four other agents' Islands and Wartok Canyons runs (`ootf-corpses/islands-walk`, `ootf-water2/cr-*`,
`is-sea-b`, `ootf-check-b/hd-mbefore`): it is general, not one spot.

`0x157d7c` is in `FUN_00157a08`, the page writer of the terrain draw-block builder (`FUN_00154060` -> `FUN_00154d98` ->
`FUN_00157a08`, the code of [quick-load LRU](../2026-11-07/ootf-quickload-lru.md)). A trace of those four functions
(`DRAKAN_TRACE_FN=154060,154d98,156518,157a08`, run `is-crash-trace`) shows the failing build: layer index 368 (id 1306,
6 x 13 cells, nine chunks, seven of them 1 x 2 or 3 x 2 cells with 7 or 8 textures) built fine the first time it came into
view, with its pages near `0x1c3...`; the second time its pages were allocated at `0x1efb6d8` to `0x1effe08`, at the top of
the heap under the stack, and inside the 1 x 2 chunk the group pointers became 0, 0x44, 0x88, ... before the bad size.
The draw pages of the visible layers no longer fit in guest memory.

Why: the converted terrain has an order of magnitude more chunks than any disc level. Chunks per visible list (sum of the
chunks of every layer a layer lists as visible, maximum over the level; `tools/vischunks.ts`):

| Level | chunks in the level | most chunks in one visible list | most chunk textures in one visible list |
| --- | --- | --- | --- |
| 14 PAL disc levels | 55 to 1,497 | 26 to 524 | 48 to 1,352 |
| OotF Islands | 16,855 | 5,656 | 40,228 |
| OotF Islands Night | 7,053 | 3,538 | 24,935 |
| OotF Wartok Canyons | 9,027 | 3,098 | 22,222 |
| OotF Volcanic | 10,041 | 1,756 | 12,240 |
| OotF Rift World | 6,400 | 2,368 | 17,005 |
| OotF Alwarren | 7,172 | 2,316 | 16,640 |
| OotF Rift Crystal | 545 | 248 | 1,587 |

The disc has no chunk of 4 cells or fewer with 5 or more textures; the Islands have 14,269 (5,547 of them 1 or 2 cells
with 8 textures). They come from the texture limit of 7 November ([VU1 reserved lower](../2026-11-07/vu1-reserved-lower.md)):
a chunk lists the textures of its cells and of the ring around them, may list at most 8, and a crowded chunk is cut down
to single cells. The PC paints far more textures per area than the disc, so most chunks are cut, and every chunk costs at
least one draw page (the writer rounds a page up to 0x400 bytes, `0x157ca4`). The engine keeps the drawn layers' pages
within a budget (`0x4dff0c`) by evicting layers not drawn this frame, but the pages of the layers drawn in one frame must
all fit, and in the Islands they do not.

Not fixed here: the fix is a converter decision with a visible cost. Keeping chunks at disc size means at most 8 textures
in each chunk window, so the cells using the other textures must take a neighbour's texture (`limitWindowTextures` of
`d937581` does that for 3 x 3 blocks). Measured on the current pack (`tools/repaint.ts`, 8 textures per window, ring
included): 12 x 12 chunks repaint 19.9 % of the drawn Islands cells (868 chunks instead of 16,855), 6 x 6 chunks 14.4 %
(2,453), 4 x 4 chunks 11.1 % (4,905); Islands Night 19.2 / 14.1 / 10.3 %, Wartok 15.1 / 12.1 / 9.6 %, Mines 9.8 / 7.3 /
4.8 %. Which size keeps the heap safe needs the engine's page cost per chunk (or a run per size); Rift Crystal (248
chunks per visible list) never failed. A runtime-side alternative (more guest memory for terrain pages) was not looked at.

## 2. Islands landing: Arokh always lands on one plateau and Rynn is stuck on his back

Flying out of the arrival tunnel and holding or tapping Ctrl (Cross), Arokh is moved in a handful of frames to (389638,
62313, 778893), 13,000 to 39,000 units from where he was, whatever the route (`is-land`, `is-land2`: straight, `is-land3`
/ `is-land4`: turning; he was over the sea each time). That point is a 3 x 3 cell floor layer (index 180) on a rock above
the sea with a 3 x 2 ceiling layer (176) at the same height. Ctrl taps (a press, not a hold) then dismount Rynn, but she
stands on Arokh's back, W and S move her a few units, the harness cannot place her or him elsewhere, and F5 / F8 restore
the same state (`is-tour2`, `is-tour3`). The 6 November flight note already saw "Rynn stands on Arokh" at this spot; it is
not from rounds 3 or 4. A player can probably remount and fly on, which was not tried. Why the landing picks this plateau
over every island was not found (no landing object of the PC level lies there); it needs the engine's landing search.

## Other observations

* Start of the Islands: the arrival tunnel seen from behind Arokh shows the tunnel walls with black star-shaped areas
  around the exit (`runs/is-survey/sheet.png`); not compared with the PC.
* Ctrl held continuously does not dismount; Ctrl pressed again after the landing does (harness note, not a defect).
* Placing Rynn next to a just-dismounted, landed Arokh is unsafe in the harness: one placement onto his position crashed
  (`is-tour4`); later runs placed her 5,600 units away.

## Not checked

* Death clips and corpses (no creature killed: no weapon in a direct start), armour requirements, item pickups.
* The Islands exits (Alwarren is a mounted exit and the harness could not place a flying Arokh; the bell route needs the
  hammer), and Rift World's end (the boss fight).
* The Islands sea-edge water (left to the water agent) and the Islands Night sea edge.
* Most of each level: the spots are where the half-hole cells cluster, several were hilltops; no level was walked end to
  end. Subtitles only in the one Rift World scene (the harness skips scenes).
