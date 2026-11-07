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

## Follow-up (same day)

* **Lever second press, solved.** Lever params: `Switch Type` is `One_Time,Toggle,Crank,Reset` and defaults to One_Time (0); the use
  handler (`0x415198`) only flips the state for Toggle. The lever template now writes Toggle (commit 7151b2fc "Make the lever
  template a Toggle"). Measured: press 1 yaw 0 -> 1.714 (open), press 2 yaw back to 0.014 (closed). Pressing E on an unlocked door
  (Auto-Open off) did nothing: doors only react to messages and proximity.
* **Held plate.** Plate params `Send Message When` (Reached Top, Reached Bottom, Both, Never), `Message To Send At Top / Bottom`.
  "Pressure plates" has a `hold` option (On at the bottom, Off at the top, straight to the door). Unit-tested; not driven in game.
* **Template defaults.** "Pressure plates" and "Kill all" sent Unlock by default, which does nothing to an unlocked door: they now
  send On to an unlocked door and Unlock to a locked one.
* **Ambush (spawn on trigger).** The shipped levels use Object Generators (class rfl 0x3f, `Spawn Object`, `Run Mode`, `On Trigger`) for
  effects (dust, flames, rubble); no placed generator spawns creatures. Creatures have `Wait For Trigger` (field 27): dormant until a
  message. Two Stratos clones of a skeleton, trigger volume sending On: before the walk they stay put; after it the message
  `0x1001` reaches each, one walked 2800 units to Rynn. New template "Ambush" (dormant copies plus one volume). A volume centred on
  the floor never fired for Rynn walking through it (the same volume 1000 units higher did), so all detector templates now lift
  their volume by `VOLUME_LIFT = 1000`.
* **Kill all.** Volume sends Blow Up (`0x10a3`) to two creatures, each sends I'm Dead (`0x1003`) to the counter, the counter sends On to
  the door: door yaw 1.714. Melee / bow kills were not driven (the new-game Rynn has no weapon fixture); the death message path is
  the same.
* **Key lock.** Key items are rfl 0x21 (`Lock Code`), locks rfl 0x22 (`Lock Code`, `Trigger To Send`). The lock handler `0x353550`
  reacts to message `0x10e4` (event 0x20), which the key item's use action sends from Rynn's hand; E at the lock sends nothing. The
  Shiverbane locks (`LEVEL5NE`, Lock Plate Iron codes 51 to 54, keys 52 to 54 on the floor, each lock feeding a timer that switches a
  lightning barrier off) are the natural test; not driven yet because the key has to be held. Open.
* `scripts/harness/puzzle_door_check.py` repeats the measured scenarios with PASS / FAIL.

## Third round

* **Held plate, driven.** Pack "Pressure plates" with `hold`: the plate sends On when Rynn stands against it and Off when it rises
  again; the door opens (yaw 1.73) and closes again (0.017) by the plate's own messages (`0x1001`, then `0x1002` at the door).
  Rynn cannot step onto the stock plate (a solid block), so "released" is the plate rising while she is still beside it, not her
  walking off. Scenario `plate_hold` passes.
* **Kill all with real kills.** New-game Rynn carries a sword; with `DRAKAN_PLAYTEST_AUTOAIM=10031` (turns her to the nearest living
  skeleton) and tapped attacks she kills two dormant copies of a skeleton (HP 40 to 5 per hit to dead); the counter receives `0x1003`
  twice and the door opens (yaw 1.73). Scenario `killall_melee` (about 100 s) passes. A bow kill was not run.
* **Key lock, still open.** Standing on or next to a placed key (Shiverbane key 52, `LEVEL5NE`) and tapping E, or walking over it,
  did not pick it up (the key entity stays); without the key in hand the lock handler sees no `0x10e4`. Getting a key into Rynn's
  hand needs the Use handler's pickup conditions (`0x3168a0`) or a save with the key (the save editor only copies item classes already
  in the save). The editor side is done: the key-lock template sends Unlock, and Validation now warns when no key with the lock's
  code is placed in the level (`key-lock-no-key`).

## Still failing / not run

* Key lock with a key in Rynn's hand (see the follow-up and the third round below), bow kills for the kill-all gate.
* Plate trigger needs Rynn to touch the plate (a scaled 0.4 stock plate is still a solid block in Stratos).
