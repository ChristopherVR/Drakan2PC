# Multiplayer performance: is MP slower than single player? (2026-11-08)

Question: the overlay in the MP inventory shows `World 0.0 fps, Present 51.8 fps 19.30 ms`, and in-game MP feels worse than
single player. Result: **no MP-specific cost was found; the slowness is the heavy arena levels plus the engine redrawing
the 3D world under a paused menu. No code was changed.**

## Method and what was measured

All runs: `dist/DrakanPC` exe copies (harness private folders), RTX 5080, `PS2X_PERF=1`, `DRAKAN_RUNTIME_PROFILE=1`,
`DRAKAN_SHOW_FPS=2`, audio off, uncapped world rate, vsync off in the harness. Another game and another agent's build were
running on the machine part of the time, so absolute numbers carry the usual shared-machine spread
(`docs/journal/2026-11-08/perf-leftovers.md`). Windows: `play_run.py` honours the window size only partly (the SP runs came out at 3840x2160).

| Run | Setup | World fps | Notes |
| --- | --- | --- | --- |
| Arena, two instances (`arena_two_instances.py`, inquisitor-depths) | A host + B client, 1280x720 | 13 to 40 flips2/s per instance in the fight | two clients share one GPU and one CPU; the client waits in `handoff`/`drainTransfer` (1.0 to 1.5 s of every 2 s) for the GS worker |
| Arena, solo host (one instance, no B) | 1280x720 | 55 to 70 | 1.0 M GIF packets/s, 340 M VU1 pairs/s, EE thread 63 to 86 % |
| SP Level 1E at the same spot (`place 579166 128750 440175`) | 3840x2160 | 55 to 66 (overlay 62.7) | 0.6 M GIF packets/s, EE 93 to 97 %, GS thread 101 % |
| SP Level 1E start spot | 3840x2160 | 123 | 0.3 M GIF packets/s (a close rock view, not comparable) |

Same level, same spot: the arena (solo) and single player run at the same world rate. The campaign removal in the arena
does not add work and no second world/viewport pass exists in MP (the ghost hooks `hookRenderWorld` / `hookRenderObject`
only insert one body into the single native render; there is no split screen). The netplay thread sleeps 2 to 4 ms per
loop and is below the profiler's noise.

## Inventory / paused world

Reproduced on one instance: `Tab` in the arena gives `World 0.0 fps, Present 63.5 fps 15.74 ms, GS 96 %` (1280x720). Single
player at 4K gives `World 0.0 fps, Present 81.8 fps 12.22 ms, GS 100 %`. So the effect is not MP-only. While the world is
paused the guest still draws the whole 3D scene behind the menu every frame (`flips/s` stays near 115, GIF 0.4 to 0.8 M
packets/s, VU1 200 to 320 M pairs/s, `clip_fans` 20 to 25 k/s). The sampler (`PS2X_HOSTPROF`, 12 s inside the inventory)
shows the VU1 worker about 50 % busy and the NVIDIA driver in the GS thread; the frame is limited by that serial chain,
not by a single hot function.

## Hotspots

1. Guest draw of the cave scene: 15 k GIF records per frame in the depths arena, VU1 worker plus the GS thread.
2. Redraw of the unchanged world under the inventory (above).
3. Running two clients on one GPU: each waits on the GS worker handoff.

## Not done (candidates, none safe enough to ship blind)

* Reuse the previous world frame under a paused menu (skip the guest world draw while `World 0.0 fps`): needs a guest
  hook on the world render call and a cached GS frame; the menu composes into the same framebuffer, so it needs its
  own change and visual verification.
* Per-level cost of the arena maps (1E depths): trim far geometry only with a map-specific cull; not an MP bug.

The shared WorkDir was owned by another build until 17:55, so no rebuild was run; `PS2X_HOSTPROF` was resolved against the
fresh `build/final-game/bin/drakan.map`.
