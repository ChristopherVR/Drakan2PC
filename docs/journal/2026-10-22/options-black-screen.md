# Options menu black for 1-4 s on its first visit

Report: Main menu -> Options showed a black screen before the background appeared; Back and Options again was fine.

## Repro
`scripts/harness/play_run.py` with `--env DRAKAN_AUTOPRESS=0 --env DRAKAN_INPUT_AUTOSTART=0` and a `clock milliseconds`
recipe: `key down/up DOWN` at 9 s (title is up by ~6 s), `ENTER` at 11 s, `shot` every 250 ms, `ESC`, `ENTER` again.
Mean brightness of the shots: first visit 0 for 1-4 s (varied per run), then the game's own ~1 s palette fade-in;
second visit at full brightness at once. The Controls submenu behaved the same.

## Cause
Not the renderer: black with `DRAKAN_HD_TEXTURES=0` and `GSR_PALETTE_HD=0` too. While black the game kept flipping
(PS2X_PERF ~200 flips/s) but drew almost nothing; it was waiting for its first-visit resources (six INTERFCE.XDB item
texts and the six 64 KB background tiles INTERFCE.TXD 160-165), requested as one 0x1202 batch at a time.
Each batch waits `DRAKAN_RES_LATENCY_US` of *guest* time (`HleHost::waitGuestLatency`, 2000 µs). A timing probe showed
30-230 ms of wall time per wait (738 ms once at boot): in the uncapped menus the EE clock advances at a few percent of
real time, so the "skipped at once while idle" assumption does not hold. With `DRAKAN_RES_LATENCY_US=0` or `=50` the
screen fades in immediately (waits 0.1-7 ms).

## Fix
Default latency 2000 -> 50 µs (~14.7k guest cycles, still far beyond the requester's bookkeeping after a submit). The
latency is no longer load-bearing for the listener crash it was added for (`../2026-10-17/crash-listener.md`, fixed by
the unwind fix and the listener guard). Checked: Options first visit fades in at once, second visit unchanged; three
`play_run.py` boots through New Game, intro and level load with no `missing-target` / `trace-ring` / `did not return`.
Not run: the `listener_repro.sh` stress loop.
