# Issue 5: the Volcanic fire-arrow puzzle (launcher 2245) and the ice over the Runeblade

Follows [mounting and Volcanic](ootf-mounting-volcanic.md) ("Still open" 1). Runs and helpers are private under
`build/harness/ootf-arrow/` (`run.py` = the mounting harness with its own slot-lock text and PID, `objs.py` live registry
handles with runtime class, `gen.py` generator state, `ps2obj.mts` / `ps2idx.mts` converted objects, `newcls.mts` a pack's
new class records, `pccls.mts` PC classes, Ghidra exports under `dec/`). Nothing from either game is quoted here.

## PC logic (Volcano.lvl)

* Dragon breath destroys "catching fire object" 2259 (health 10). Its links: vis 2263 shows the hidden "Arrow Detect Object"
  2366, Timer 2321 lights "burning flame gen" 2320 in front of it, Timer 2534 / vis 2540 shows the second wall button 2538,
  Timer 2542 / vis 2541 hides the first one 2261.
* Either button starts Timer 2325 (1.75 s, On) and Timer 2530 (2 s, Off) to "Arrow launcher" 2245 (generator, Spawn Object
  "Sword Sanctum Arrow", class 95, damage 10,000, 5 lu/s, no gravity; Release Period 0.5 s, On Trigger 4, Run Mode 1, yaw 45).
  Each button also plays "Arrow No Flame" through its Stomp Player.
* 2366 has health 10,000, so one arrow destroys it; its "Message To Send" On goes to Timer 2368 (fires the "Flaming Arrow
  launcher" 2367 and starts Timers 2380 / 2383 / 2384), 2369 (puts the flame out) and 2373 / 2379. 2380 (2 s) hides the static
  "ice cover test" 2378, 2383 (2 s) shows the hidden Morph Plug "ice cover" 2241, 2384 (6 s) starts its morph (four models,
  3 s each), which destroys it at the end. 2378, 2241 and the sanctum platform ride together (object group 2941).

## What the PS2 engine does with the launcher

Generator (runtime `0x1003f`) handler `0x2cdc60`: event `0x20` is a message (`0x2ce120`), event 4 the tick (`0x2cf130`).
With On Trigger 4 an On (`0x1001`) puts it on the update list and an Off (`0x1002`) takes it off and zeroes its release timer,
so the 0.25 s window between the two timers releases exactly one arrow on the first tick (timer 0). The spawn (`0x2cfb28`)
has its own branch for class `0x1005f` (95): it builds the arrow's frame from the generator's and launches it along it.

| Run | Pack | What | Result |
| --- | --- | --- | --- |
| `a1` | `mods2` (pack1 + List Play Order 2) | full route, button, 23 RAM dumps 0.3 s apart from the press | no arrow in any dump, generator never seen running |
| `a2` | same, trace `0x2ce120`, `0x2cfb28`, projectile factory `0x1fdbd8` | first button, no breath | On reaches 2245, one spawn (arrow resource `0x31f86`), the arrow gets its init event, one movement step and is destroyed; no impact call (`0x1fe3c8`) |
| `a3` | `pack3` (fix 1 below) | same | the arrow flies (velocity 7,240 / 0 / -7,240 wu/s, 10,240 wu/s = 5 lu/s, towards +x / -z, i.e. at 2366), and its impact (`0x1fe3c8`, damage 10,000) lands about 0.2 s of log time later on a flame particle (class `0x10001`) of 2320 at (120347, 60779, 224234), the spot where 2366 stands; 2366 itself was still hidden (no breath in this run) |

So the launcher fires, the arrow is lit by nothing (it is the plain arrow; the "fire" is flame gen 2320 in front of 2366, which
the breath lights) and it flies the PC direction. It died at once in the earlier packs because of the converter (fix 1).

## Fixes (converter, formats)

1. **Model-less generators get a model-less template.** 2245 has no PC model; the class plan cloned it from the closest PS2
   generator, "Bridge Generator", which carries a model (FX model 4676); the clone carried it, and the arrow, spawned inside the
   generator, ended on its first movement step (`a2`). With a model-less template the same press gives a flying arrow (`a3`).
   (The large dark panel in front of the button in the screenshots is not that model: it is still there with the fix.) Generator classes
   (kind `spawn`) now follow the rule mechanisms, scenery and FX already had: a PC class without a model is cloned from a PS2
   class without one. In the campaign 10 of 133 converted generators carried a template model (Volcanic: Arrow launcher, Ice /
   Gas / Lava Lightning, Lava Flare, Solve Magic; others: Gib Spray Pumpkin, Small Dart Hole, Barrel Fire, Dragon Fire Ball);
   those without a PC model now show nothing. Test: "clones a model-less generator from a model-less generator template".
2. **Withdrawn: Morph Plug models.** Model-typed class fields are never carried, so the "ice cover" Morph Plug 2241 (and
   every converted Morph Plug, e.g. Volcanic's 20 lava-wall glows) keeps its template's "Morphing Models" (Crown of Thorns
   spikes). A trial that converted the Morph Plugs' own models (52 more models, `pack4`) logged negative-size guest memcpys
   right after Rynn was placed by the Rune Cage Key (`a4`, 15 lines, `[memcpy] bad size 0xfffffffc ra=0x4ad088`) and then
   stalled in the allocator (watchdog at 194 s). The same route on packs without it (`a1`, `v-run2`) logs none. The machine was
   also running other agents' games at the time, so the cause is not proven, but the change was withdrawn
   (`build/harness/ootf-arrow/morph-*.diff` keep it). The ice therefore still morphs through the template's models, and whether
   the hidden 2241 is instantiated at all was not settled (no live handle at its position in `a1`).

## In game (full route)

Not completed. The two full-route runs on the fixed pack could not be judged: `a4` (`pack4`, see 2) stalled, and `a5` (`pack3`)
ran at 2 fps while up to five other agents' `drakan.exe` (perf sweeps under `scripts/harness/play_run.py`, which do not take
the game slot) ran beside it. At that rate the timed recipe fell apart: scene 9.6a never released Arokh (`+0x94` 9 in every
dump), so there was no breath, and the placed Rynn did not move under W at the button. A rerun waited for a free machine for
about 40 minutes and was then withdrawn. So the arrow reaching a *visible* 2366, 2366's timers, the melted ice, taking the
Runeblade, the mount and the Rift World exit remain to be played on `pack3` (or a pack from the committed converter): recipe
`build/harness/ootf-arrow/recipes/a4.txt` is the v-run route with RAM dumps every 4 s after the press and the blade step
moved 10 s later (the ice needs about 17 s after the hit: 2 s to hide 2378, 6 s to start the morph, 3 x 3 s of morph).

What to check there: 2366 hit by the arrow (`DRAKAN_TRACE_FN=0x1fe3c8`, impact a3 = the struck handle), Timers 2368 / 2380 /
2383 / 2384 firing (they vanish from the registry: Destroy After Timeout), 2378 hidden, the blade taken with E.

## Evidence

`build/harness/ootf-arrow/runs/`: `a1` (pack1 route, no arrow), `a2` (trace: spawn and instant death), `a3` (fix 1: flight and
impact), `a4` (full route on `pack4`, heap errors and stall), `a5` (full route on `pack3`, invalid: 2 fps under contention). Packs:
`pack3` (formats `5745259` with the Stomp Player play order fix, plus fix 1 = formats `8605c83`), `pack4` (plus the withdrawn
Morph Plug change).
