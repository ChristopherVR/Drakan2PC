# Door proof: a lever, plate, counter and timer open a placed door in game (2026-11-07)

Closes the gap open since 2026-11-02 (`puzzle-messages.md`, `editor-round7.md`): the editor's puzzle wiring is measured in the
game, not judged from screenshots.

## Why the earlier runs saw nothing

* They compared the world dump's `flags` (entity `+0x9c`) and position. A door swings about its origin, so its position never
  changes, and `+0x9c` is not its state. What changes is the entity's rotation matrix (yaw), entity `+4` (bit 8 while it moves)
  and the door's instance data.
* The door handler (class `0x10013`, `0x45ba20`, decompiled with `analysis/ghidra_scripts/InputDecompile.java`; event `0x20` =
  message, `*(msg+4)` = engine id) reacts as follows:

| Engine id (ordinal) | Door does |
| --- | --- |
| `0x1001` (1 On, the editor's "Opens") | opens (state `data+0x10` 0 -> +-1, the door turns by its Opened Rotation Angle at Open Speed) **only if it is not locked** (`data+0x40 == 0`); sent again while open it does nothing for swing doors |
| `0x1002` (0 Off, "Closes") | closes (state back to 0) |
| `0x1101` (2 Lock) | `data+0x40 = 1` |
| `0x1102` (3 Unlock) | `data+0x40 = 0`; **nothing moves**, the door still needs Auto-Open (proximity) or an On message |
| `0x11` / `0x12` | disable / enable Auto-Open (`data+0x18`) |

So the editor's ordinal -> message mapping (Opens = On, Closes = Off, Unlocks = Unlock, Locks = Lock) is correct. Two things
were wrong around it: the guide told modders to set **Locked on** for puzzle doors (a locked door ignores On, nothing opens),
and nothing warned about it. Fixed in `3b6a1b55` (guide text, template note, new validation warning
`door-locked-ignores-open`, test in `logic.test.ts`).

## Test aid: `DRAKAN_PLAYTEST_WATCH`

`class=10013,player=1,words=0,data=20` prints once a second (stderr, `[watch t] ref= h= cls= pos= yaw= | e+.. | d+..`) the
door (and Rynn) from guest RAM; documented in `env-flags.md`, formatter and filter tested by `world_watch_test`
(`runtime/world/include/world/Watch.h`). Door open = yaw 0 -> about 1.7 rad and `d+10` 0 -> 1, `d+40` is Locked. The test packs
come from `scripts/harness/story_puzzles_author.mjs` (editor-core templates, node, no browser; options `locked`, `autoOpen`,
`then`, `leverMode`) in Stratos (`--level 8`), Rynn placed at 651612 147580 961906 facing +z, door at +4500 z, lever 700 ahead.

## Results (`play_run.py --exe build/door/play/drakan.exe`, placed `GF Wood Door01`, Locked 0, Auto-Open 0)

| Wiring | Measured |
| --- | --- |
| Lever "Opens" (`key tap E`) | lever gets use `0x4010e1`, door gets `0x1001`, flags `0x45` -> `0x49` (moving) -> `0x45`, yaw 0 -> 1.330 -> 1.697, `d+10` 0 -> 1 within 2 s. Screenshots `before` / `after` (fixed camera): gate face-on, then swung edge-on |
| Walk-through control | Rynn placed at x 652600 walking +z for 6 s. **Without the press**: stops at z 966030 (door at 966406) for the rest of the walk. **With the press** (door open): z 963642 -> 965990 -> 968587 -> 972301, through the doorway |
| Plate -> counter -> door | counter and door receive `0x1001`; door yaw 1.732 |
| Counter, two plates | door stays at yaw 0 after the first plate (about 8 s), opens (yaw 1.73) after the second |
| Timer (volume -> timer 2 s -> door) | timer `0x1001`, door `0x1001`, yaw 1.708 |
| Locked 1 + lever On | `d+40 = 1` kept, yaw stays 0: ignored, as decoded |
| Locked 1 + lever Unlock, Auto-Open 0 | `d+40` cleared, yaw stays 0: Unlock only unlocks |
| Locked 1 + lever Unlock, Auto-Open 1, Rynn walks up | opens (yaw 1.71) |

## Still failing / not run

* Lever second press: the lever receives the use message again but sends nothing, the door never closes (tried 4.5 s and 12 s
  later). Close by lever (Off) is unproven; the Off handler itself is decoded only.
* Key lock (no key item class in Stratos to test with; it only sends Unlock, so it needs Auto-Open, as above), trigger ->
  enemy spawn (the editor has no spawn class; "Move to WayPoint" / "Blow Up" to a creature was not run) and kill-all-gate
  (needs kills) were not run in game.
* Plate trigger needs Rynn to touch the plate (a scaled 0.4 stock plate is still a solid block in Stratos).
* Does Use (E) on an unlocked door open it? Not tested.
