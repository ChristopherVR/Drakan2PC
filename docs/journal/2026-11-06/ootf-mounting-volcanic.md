# Issue 5: mounting Arokh, and the Volcanic route to the Runeblade

Follows [ootf-progression-2.md](ootf-progression-2.md). Runs, recipes and helpers are private under `build/harness/ootf-mount/`
(`run.py` as in progression 2, `dragon_report.py` dragon / rider fields from a RAM dump, `pcrfl.mts`, `pclogic.mts`,
`pcseq.mts`, `refns.mts`, `pcrefns.mts`; pack `pack1` regenerated from the working tree with the fix below). Decompiles
were read in Ghidra (scratch only, nothing verbatim here).

## How mounting works on PS2 (D2 PAL)

Mounting is not a button. E / L1 (command `0x4009`) is Use: it only ever acts while Rynn is the controlled actor and
goes to items, climbing and NPC talk. The mount check runs in Rynn's on-foot controller every frame she is moving
forward (forward-move bit of `player+0x504` set; for a digital forward press it also needs a scripted-input flag, an
analog stick forward, which is what the PC keyboard W sends, does not) and the camera is not in free-look. It passes when:

* Rynn's state allows it (`player+0x94` zero or only its bit 2) and she has a linked dragon (`player+0x4d8`);
* neither actor is in its guarded animation state;
* the dragon's Mount clip (dragon field 109) and Rynn's mount clip are resident;
* the dragon is closer than about 1,448 units (squared distance under 2,097,152) and in front of her;
* the dragon is **landed**: `dragon+0x9c == 1`.

So walking or running into a landed Arokh mounts; nothing needs to be pressed. Dismounting is holding Cross (C); it needs
him landed as well. The dragon's start state comes from its `Initial Mode` (field 145), read when Rynn and the dragon are
linked at level start: 0 mounted and flying, 1 on foot with Arokh taking off (2,048 units up, airborne bit `0x8` in
`+0x94`, `+0x9c` 0; he lands by himself near her), 2 mounted and landed. Messages `0x12` / `0x11` to the dragon mount and
dismount from script.

Baseline in Drakan 2 (`tundra3`, Level 4B, no mods): Arokh takes off, lands near Rynn within 12 s (`+0x94` 1, `+0x9c` 1);
Rynn placed 3,000 units away facing him, W held: the controlled actor becomes Arokh (`0x1001c`) within a second; W walks
him; Cross held 2 s dismounts. Converted Wartok (`wartok-remount`): Cross dismounts, Rynn placed facing him walks back in
and remounts, he walks under W, Cross dismounts again. No input mapping change is needed.

## Why Volcanic's Arokh could not be mounted

He is caged by design. The opening scene "9.1" takes script control of him (sequence action type 5 with 0) and never
gives it back; "9.6a" gives it back. Until then his tick does not run: in every earlier dump he still had the take-off
velocity (`+0x18` = -2048 / 1.4) and the airborne bit, so `+0x9c` stayed 0 and the mount check failed. The PC level frees
him through Detector 1994 at the dragon platform: Task Play Sequence 9.6a, "Trigger Only If Carrying Item..." = the Rune
Cage Key (object 2785, lying at 208384, 160083, 449920), and it enables the mounted-only box 2543 (scene 9.6b).

Two converter gaps kept that detector dead:

1. **Class references in object parameters were read in the wrong numbering (fixed).** The key reference is PC
   `(2 << 16) | 3174`. Its database number is an index into the `.db` of the object's class ODB (Volcano.db: 2 =
   Resources), exactly like class defaults; the converter read it in the LVL header's numbering (2 = the level itself)
   and wrote it the same way, so it became 0. A survey of every class-reference object parameter (PC: all 23 levels; PS2:
   all disc LEVEL*.LVL) shows the class-ODB `.db` numbering resolves every reference that resolves at all, on both
   games; the header numbering resolves almost none (and in places picks a wrong class, e.g. an orc's Weapon Object
   becoming an "object group"). The converter now maps these parameters through the PC class ODB's `.db` to the
   planned PS2 class and writes the index the PS2 class ODB's `.DB` gives it (formats `6332a2b`, test
   `ootfobjectclassrefs.test.ts`). In the regenerated pack Detector 1994 asks for Resources class 18097, the converted
   Rune Cage Key. This also restores creature drops, weapon objects and the like wherever the referenced class is part of
   the conversion.
2. **Its scene was dropped**: detectors live in FX, which has no SSD. The concurrent sequence work (detector scenes given
   a Stomp Player, not part of this change) covers it; with both in the working tree 9.6a plays.

## Volcanic by play (`v-free`, `v-mount2`, `v-blade`, `v-full`, `v-cave*`; pack1, god mode, placements)

| Step | Result |
| --- | --- |
| Rune Cage Key: placed by it, W, E | taken |
| Detector 1994 with the key | scene 9.6a plays (15 s, `ref 80058`), Arokh released: `+0x94` 9 -> 1, `+0x9c` 0 -> 1, he walks off the platform |
| Walk into Arokh | mounted (controlled actor `0x1001c`), W walks him, Cross dismounts |
| Native F5 / F8 after that | S moved her, F8 restored (269053, 151498, 503932) exactly |
| Fire puzzle, step 1: Arokh breathes on the "catching fire object" 2259 (shown when he is in box 2898 over the sanctum) | 2259 is destroyed and its links fire: the Arrow Detect Object 2366 is switched on (handle flags change) |
| Over the sanctum (box 2898), dragon placed 4,000 units west of the "catching fire object" 2259 facing it, breath (LMB) x5 | 2259 is gone from the registry afterwards and the second wall button 2538 is switched on (its handle flags change), so 2259's links fired |
| Dismount in the sanctum | not possible: the floor there is object geometry, the dragon's ground probe finds the terrain 9,000 units below and he stays airborne. The PC route is the Cave Entrance detector 2846 at the sanctum mouth (117781, 54244, 214016): placed in it he lands on the corridor floor, and Cross held 2.5 s once he has landed dismounts (`v-cave4`; held while still landing it is ignored) |
| Wall button by the sanctum floor, E | presses; the button plays "Arrow No Flame" through Stomp Player 2527 and starts the arrow launcher timers. The second button 2538 is pressed as well (its handle moves) |
| "Arrow No Flame" | **loops**: with the PC "List Play Order" 1 (In Order Loop) the PS2 Stomp Player restarts the scene every 5 s for the rest of the run (70 s logged), which holds the camera and ignores input. With the value patched to 2 (One Scene Per Trigger And Looped, `mods2`) it plays once per trigger (`v-run2`) |
| Ice cover, Runeblade | **not solved**: the ice objects (handles of 2378 / 2241) never change and E at the pedestal takes nothing, so the arrow from launcher 2245 never registers on the Arrow Detect Object 2366 |
| Remount by the cave mouth, place in the rift box 2318 | remount works (`v-run2`); the mount-only rift box stays closed without the blade, as it should: no exit message |

Rift World was therefore not reached from Volcanic in this pass.

## Still open

1. **Arrow launcher to Arrow Detect Object.** The last link of the fire puzzle: launcher 2245 (PC class "Arrow launcher",
   Spawn Object = PC class 5076, Initial Velocity 0 lu/s at class level, Run Mode 1, On Trigger 4) is a converted FX
   generator; whether it spawns a projectile at all, in which direction, and whether a hit on 2366 (a 10,000-health prop,
   "Message To Send" 1) reaches its links (timers 2368 / 2384 that show and melt the ice) is unchecked. Next: watch the
   registry for a projectile from 2245 after the press, then check the spawned class and 2366's damage handling.
2. **Stomp Player "In Order Loop".** PC 1 means "play the next scene of the list on each Message To Play Next"; the PS2
   player with 1 keeps replaying on its own. Mapping it to 2 fixed the loop in a patched pack; the converter does not do it
   yet (the stomp-player conversion is part of the concurrent sequence work). A D2 survey: 7 of 98 disc Stomp Players use
   1, one of them Initial State On (an ambient loop), which fits the auto-replay reading.
3. Scene 9.2 (meeting caged Arokh, Detector 978) did not play when Rynn walked past the cage; 9.6a did. Not traced.
4. Flight: a flying, placed Arokh did not move under W (as in the Islands direct starts), so the routes were placed, not flown.

## Evidence

`build/harness/ootf-mount/runs/`: `tundra3` (D2 baseline mount / dismount), `wartok-remount`, `v-free` and `v-mount2`
(key, 9.6a, mount, F5 / F8), `v-blade` (breath on 2259), `v-cave4` (cave-mouth dismount), `v-run` (scene loop), `v-run2`
(patched List Play Order, remount, closed rift box). Packs: `pack1` (working tree incl. `6332a2b`), `mods2` (pack1 with
Stomp Players 2527 / 2536 List Play Order 2).
