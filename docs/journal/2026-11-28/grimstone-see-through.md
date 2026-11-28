# Grimstone Mines: "textures clipping where we can see through" (not reproduced)

Report (owner, Order of the Flame campaign, OotF 04 - Grimstone Mines): "see our slot 2 save, further over the swinging
boulders, the textures in the area are clipping where we can see through." No defect was found in the data or in the
frames captured; nothing was changed. This note lists what was checked so the next look can start from a picture or a
spot. Runs, cards, recipes and scratch tools: `build/review/grimstone-seethrough/` (`runs/`, `recipes/`, `scratch/`),
not committed.

## 1. Setup

* Private cards: the owner's Slot 1 (20:15, wooden walkway at the wrecking balls), Slot 2 (17:22, pulley lever) and the
  newest quicksave Slot 33 (20:18, on the risen pulley grate), each copied as Slot 33 into `card_s1`, `card_s2`,
  `card_q` and loaded with `play_run.py --save-ootf <card>` (`--save` is the Drakan 2 card: the title then shows no
  Continue and the run stalls), `DRAKAN_GAME=d1`, the `dist` exe and `drakan.ini`.
* The "swinging boulders" are the two `wrecking ball`s (1066, 509) on the `lairswing` ropes (1065, 508) over the rock
  shelf in front of the `Entrance` model (472, PC model 3680, a brick archway at 215040, 47618, 264192) that leads into
  the brick corridors west of it.
* Camera pose per frame: a RAM dump next to each shot; the follow camera's transform is at `0x4dad94` (position,
  rows of the 3 x 3 matrix, position again; the view direction is minus the third column). `scratch/rend.py` draws the
  PC terrain (and a placed PC model, `scratch/objtris.py`) from that pose with a 90 degree horizontal field of view and
  lines up with the game frames (`runs/cam/cmp_terr_r3b.png`, `runs/arch/pairsE.png`).

## 2. Looked at

| Run | Where | Seen |
| --- | --- | --- |
| `s1-look`, `s1-lookd`, `s1-balls`, `s1-pitch` | Slot 1, view turned 130 degrees, pitched down, 12 frames of the balls swinging | walkway, pillar, balls, the Entrance arch on the shelf; no gap |
| `s2-look`, `s2-base`, `s2-farland0`, `s2-farscen0` | Slot 2, view turned; far land and far scenery off | the Entrance arch seen across the cavern; the same picture with either option off |
| `q-look` | Slot 33 on the pulley grate, view turned | no gap |
| `west` (p1 to p3), `arch` (e1, e2) | placed in the brick corridors behind the Entrance, at the three spots of the owner's bow shots in the session log (211057 / 210015 / 209767), and on the shelf in front of the arch | corridors closed; the Entrance's inner faces show stretched textures where they meet the corridor ceiling |
| `s1-flick`, `s2-flick` | 4 and 10 frames 150 ms apart from Slot 1 and Slot 2 | no flicker on static geometry (only the balls and the sword move) |
| `s1-walk` | walking on from Slot 1 | a ball knocked Rynn into the pit below; the camera then stood against the pit wall and a crystal |

No frame showed the clear colour (the pale lilac of the 2026-11-14 wall triangles) or any other hole.

## 3. Data compared (PC `mines.lvl` against the installed pack's `OOTF04ES.LVL`)

* Terrain, 346 layers: every cell's heights and both triangles' hole / invisible state are the same within 16,000 units
  of the balls (2,060 triangles, `scratch/terr.ts`). The visibility lists differ only by layers 198 and 240 (water) that
  the converter adds.
* Terrain draw data (`scratch/blobcheck.ts`): every drawn cell of every layer lies in a chunk that lists its texture with
  non-zero vertex weights, except the 50 cells of the two water layers (drawn by the water pass).
* Objects between x 200000 and 236000, z 250000 and 282000 (`scratch/objdiff.ts`): all present with the PC position,
  rotation, scale and flags; only seven flames are moved, as the converter places them.
* The Entrance model drawn from the game cameras: the faces seen from the cavern, the shelf and the corridor are front
  faces, which the PC draws too. One back face (PC culled, drawn on the PS2 because models are drawn from both sides) is
  in view from the corridor next to the exit.
* View layer: no "camera point from layer -1" decision in these runs. In the owner's running session log the first 16
  view-layer decisions (the trace limit) are all near x 245000 to 252000, outside this area.

## 4. Not found, open

* The spot and the camera the owner means. A screenshot (F12 or the save preview) or a quicksave taken while the
  see-through is on screen would settle it; the preview picture of a save is the camera of that moment.
* Near-plane loss: the VU1 clipper does not clip against the near plane (`graphics.md`), so a triangle that crosses it
  may be dropped. With the mouse camera close to a wall this would open a hole in that wall; no run put the camera that
  close to a wall here except in the pit, where none was seen.
* The PC game was not run; the comparison is of data and of terrain drawn from the same camera.
