# Harness runs from a save stalled at the title

Symptom: `play_run.py --save <card> --recipe <recipe>` runs (recipe F8 quick load at ~45 s) stopped reaching gameplay in the
afternoon; the log showed DRAKAN_AUTOPRESS=3000 tapping Cross every 3 s and the game bouncing between the title and the
Load screen. `--env DRAKAN_AUTOPRESS=0` worked around it.

Cause: play_run.py always sets DRAKAN_AUTOPRESS=3000 (+ DRAKAN_INPUT_AUTOSTART) so a no-save run walks New Game. The modern
title on a card that has saves now has Continue as its first row and a Load screen, so the blind Cross taps open the Load
screen and back out of it again, and the recipe's F8 never lands in a state where it can load. Morning runs predate that
title behaviour on a card with saves.

Fix (harness only, scripts/harness/play_run.py): with `--save` and no `--level`, the run loads the save itself, so
DRAKAN_AUTOPRESS is set to 0. `--env DRAKAN_AUTOPRESS=<ms>` still overrides. No-save runs are unchanged. The runtime is
untouched (autopress is a test aid; making it Load-screen aware was not needed).

Proof (default settings, exe dist/DrakanPC/drakan.exe):
- `--name hts_save2 --save build/scratch/saveitems2/cardP --recipe build/scratch/rec_simple.txt` (F8, sync play, shot,
  Tab, shot, quit): attempt 1 ok, 58 s; b_tab.png shows the inventory over the loaded save.
- `--name hts_nosave --level 1a`: attempt 1 ok, 30 s.

Note: recipe and card paths given to play_run.py must be absolute when `--out-root` is not the default; the game resolves the
recipe relative to its own working directory.
