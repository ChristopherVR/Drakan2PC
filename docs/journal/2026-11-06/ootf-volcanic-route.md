# Issue 5: the Volcanic route to Rift World (sanctum switch, ice Morph Plug)

Follows [fire arrow](ootf-volcanic-arrow.md), [mounting and Volcanic](ootf-mounting-volcanic.md) and
[Stomp Player play order](ootf-stomp-play-order.md). Runs, recipes and helpers are private under
`build/harness/ootf-volroute/` (`run.py` = the ootf-arrow harness that takes `GAME_SLOT.lock` only when it is free and
never waits; `objs.py` registry handles; `chain.py` / `state.sh` sanctum objects and dragon state per dump; `pcnear.mts`,
`pclinks.mts`, `pcboxes.mts`, `morphcounts.mts`, `dlcmp.mts` PC data probes; Ghidra exports under `dec/`, `decpc/`).
Nothing from either game is quoted here.

Executable: `dist/DrakanPC/drakan.exe` of 17:18 (without the runtime half of `dd1557b3`, which only matters for flying
Arokh; the route places him). Up to three other agents' games ran beside every run, so the recipes count engine ticks
(no `clock milliseconds` header): every wait is a tick count or `sync play <n>` (n consecutive gameplay ticks, which a
cut scene breaks), and Volcanic ran at 20 to 45 fps.

## Findings

### 1. The sanctum switch turns the target in front of the ice (no converter defect)

Without it the arrow from launcher 2245 always struck the "ice cover test" 2378 (`DRAKAN_TRACE_FN=0x1fe3c8,0x39ef90`:
the impact names the hit object; the second function is called with it), because 2378 stands on the line between the
launcher and the Arrow Detect Object 2366. The PC object groups explain the design: group 2249 carries the catching fire
object 2259, flame gen 2320 and 2366 on the sanctum central platform 2237, group 2941 the ice (2378, 2241). The "Succ
Switch" 2243 (126003, 53276, 218061) turns the platform (a mover, Rotate -0.25 r per press). Each press turns it 90
degrees in game (`sw1`, `vr10`); after two presses 2366 and the flame stand on the launcher's line on the near side of
the ice, so the arrow reaches 2366 before the ice.

A detour on the way (not kept): with the ice taken out of the arrow's way by test aids the chain worked (`vr7`: no
polygons on the ice model; `vr8`: 2378 dropped), and the PS2 projectile code was read for a converter-side "let the
arrow pass the ice" rule. For the record: the hit loop `0x39d450` sweeps boxes through the object filter `0x125338`
(visible objects with a model resource, per runtime class bits), poly-accurate projectiles (class property 0x1c) then
test every polygon of the hit model (`0x3748c8`, back faces culled unless double sided), and `0x1fe3c8` lets a projectile
pass a Morph Plug (`0x10063`). The switch made all of that unnecessary; the experiments' converter changes were dropped.

### 2. Morph Plugs keep the template's frames and vanish (fixed, formats)

Every converted Morph Plug (engine 99) cloned its template's "Morphing Models" (Crown of Thorns spikes, models of
another level's MOD): none of the 28 plugs of Volcanic was in the registry after load (`objs.py --cls 0x10063`), so the
melting "ice cover" 2241 never appeared, and neither do Rift World's RibDoor and the vertical gate eyes, Volcanic's lava
wall glows, Alwarren's glow or the Intro logo morph. The earlier trial that converted the frames (`ootf-arrow`, `a4`)
corrupted the guest heap; the cause:

* The plug's tick (`0x3a5058` -> `0x3a4818` -> `0x3a3c08`) copies, on each step, the next frame model's vertices **and
  its collision shapes and polygon lists** into the plug's own model instance, sized for what that instance already
  holds. The disc's morph frames have one shape each. The PC's do not: Volcanic's IceCover01 has nine boxes, IceCover02
  to 04 one; the copy overran and the next `realloc` (`0x4acef0`) found a zero block size (`[memcpy] bad size 0xfffffffc
  ra=0x4ad088`, then stray copies from `0x3a4114` and a stall). Vertex counts, polygon counts and display-list layouts
  are equal across every PC morph list of the campaign (`morphcounts.mts`, `dlcmp.mts`), so the shapes were the only
  mismatch.
* Fix (formats `918de7e`; `ootfpipeline.ts`, `ootfclass.ts`, `ootfassets.ts`): the frames of a plug's "Morphing Models" are converted with
  a display list (as on the disc) and `singleShapeBounds` gives each one box over its polygons that lists all of them; the
  class's model field is mapped to the converted frames (only when every frame converted, else the template's stay).
  Tests: `ootfmorph.test.ts`, "gives a Morph Plug frame one collision shape ..." in `ootftex.test.ts`.
* In game (`vr9`, `vr11`): 28 plugs after load, no bad memcpy for the whole run; after the arrow hit the "ice cover" shows,
  morphs and destroys itself at the end, as on the PC.

## Route (`vr11`, pack from formats `918de7e`)

Direct start of OotF 11, god mode, test-aid placements as before (each step placed beside its trigger, then a real walk,
press or breath). Evidence is RAM dumps decoded with `objs.py`, `chain.py` and `dragon_report.py`, and the impact trace.

| Step | Result |
| --- | --- |
| Rune Cage Key, W, E | pass: the key's handle leaves the world (cell -1) |
| Detector 1994 with the key (scene 9.6a) | pass: after `sync play 600` Arokh is free, `+0x94` 9 -> 1, `+0x9c` 0 -> 1 |
| Walk into Arokh | pass: controlled actor `0x1001c` |
| Breath on 2259 over the sanctum | pass: 2259 leaves the registry, flame gen 2320 burns (flame particles at its spot), 2366 shown |
| Cave mouth, Cross held after landing | pass once the hold starts after he has landed (about 900 ticks after the placement; `vr1` held too early and stayed mounted) |
| Succ Switch 2243, E twice | pass: 90 degrees per press, 2366 ends at (113200, 57170, 230658) on the launcher's line in front of the ice |
| Wall button, E | pass: "Arrow No Flame" plays once, the launcher fires |
| Arrow hits the visible 2366 | pass: the impact (`0x1fe3c8`, then `0x39ef90` with the hit object) names 2366's handle; 2366 is gone in the next dump |
| Ice | pass: 2378 hidden about 2 s later (`+4` bit 0 clear), the "ice cover" plug appears, morphs (its model resource changes) and destroys itself at the end (gone 18 to 24 s after the hit) |
| Runeblade, E | fail in this run, a harness artifact (see the November 7 follow-up): she was placed 1,400 units above the platform's central pillar, fell through it and E took nothing |
| Remount by the cave mouth | pass: controlled actor `0x1001c` |
| Mounted in the rift box 2318 | not opened, as it should without the blade (no message box; the run was stopped there) |

Rift World direct start (`rw1`, same pack): arrives mounted with control (W moves Arokh 18,400 units), F5, S, F8 brings
him back to within 23 units of the saved spot (he hovers between the restore and the dump), D moves him; the converted
Rift World Morph Plugs (RibDoor, gate eyes) are all created (10 in the registry), no bad memcpy, clean scripted exit.

Every run of the new pack (`vr9`, `vr11`, `vr12`, `rw1`) logged no `[memcpy] bad size`.

## November 7 follow-up: the floor under the Runeblade, the walked exit, Rift World

### The floor was never missing

The sanctum central platform (PC model with a 62-shape box hierarchy) is a central pillar, 1.45 lu high, inside a spiral
stair; the blade hovers 0.36 lu over the pillar top. Probes on the committed pack (`plat5-g`: placed 1,000 units lower than
before, then released after 30 ticks) stand Rynn on a high stair (56550), a low stair (54912), the pillar top (56670) and
the pillar centre (56670). Every earlier "falls through" placement started 1,400 units over the pillar top: she dropped
through the pillar top. Placed just above it she stays, and the route's blade step passes (below). No converter change
was needed. Two candidate changes were tried and dropped, as neither changed where she stands: bounds lists remapped
to the split triangles, and polygons the PC lists on inner shapes only handed down to the leaf boxes
(`build/harness/ootf-volroute/obb-remap-descend.diff`, with tests). The remap is still a real mismatch (the copied PC lists
name PC polygon indices after quads were split), left for a separate change with its own evidence.

### Walked route (`vr14`, pack from formats `918de7e`)

| Step | Result |
| --- | --- |
| Key, 9.6a, mount, breath | pass (as `vr11`) |
| Cave-mouth dismount | pass on the second try: the recipe places Arokh three times 100 ticks apart and holds Cross after he has landed; controlled actor `0x10009`. Not dependable: in `vr13` and `vr15` he drifted out of the cave-mouth detector after the placement, never landed (Cross held while flying lifts him) and the rest of those runs was played mounted |
| Succ Switch twice, button, arrow on 2366, ice melts | pass (as `vr11`) |
| Runeblade, E | **pass**: placed on the pillar top (116736, 57000, 227728) she stands at 56665; after E the blade's handle leaves the world (cell -1, as the key) |
| Remount at the cave mouth | pass: `0x1001c` |
| Mounted in the rift box 2318 | **pass**: message box (screen 2), save screen (5), loading (14), Rift World gameplay |
| Rift World arrival | **pass**: mounted with control, Cross held lands Rynn on foot (`0x10009`, `+0x94` 0), W walks her 1,600 units |
| F5, move, F8 on foot | **fail in this run**: F5 saves; F8 starts the load and the game hangs (watchdog, guest thread 1 in an eviction walk `0x15e5a0` -> `0x15c820` called from the engine tick `0x139010`, a list at `0x4dff60` that never reaches its head) |

### The Rift World quick-load hang is intermittent and older than the Morph Plug fix

Direct starts of Rift World, dismount, F5, move, F8 (`rwf-*`): the pack built before `918de7e` (no Morph Plugs) hung in
1 of 3 runs (`rwf-base`, `-base2` pass, `-base3` hangs); the committed pack hung in 3 of 5 (`rw1` and `rwf-tc1` pass);
with Rift World's plugs dropped it hung as well (`rwf-r`), and with the campaign texture budget raise turned off
(`DRAKAN_TEX_CACHE_KB=0`) 1 of 2. When it does not hang the restore is exact on foot (`rwf-tc1`: saved (342021, 95320,
315617), moved, loaded at (342021, 95320, 315617), D then moves her). Two converter changes tried against it (Morph
Plug frames given the disc's one-sphere bounds, and world-morph templates with model flags 0) did not stop it and were
dropped (`morph-sphere-flags.diff`). The cause is in the engine's cache eviction during a reload and needs its own
investigation (which list `0x4dff60` is, who links into it during the load).

## Still open

1. The Rift World quick-load hang (above): intermittent, also without converted Morph Plugs; blocks a dependable F8 there.
2. The walked route needs test-aid placements (each trigger, the pillar top); the stair walk to the blade was not played,
   and the placed cave-mouth landing is unreliable (1 of 3 full runs landed, see the table).
3. 2366's model ("invisiblewall") draws as a black box; another agent is on it.
