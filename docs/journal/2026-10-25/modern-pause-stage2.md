# Modern pause menu, stage 2: host Options / Audio / Controller pages and the Quit card — 2026-10-25

Integration of the gameui `PauseScreen` stage 2 / 3 pages (library commit `593ec92`) into the game, plus three UI
fixes. Reference: `docs/reference/mouse-menus.md` section 14; the action table: `runtime/gameui/README.md`, "Pause menu
actions". Glue: `ps2xRuntime/src/lib/drakan_pause_screen.inl` (`pauseExecuteStage2`, `HiddenDraw`).

## Setup

`scripts/harness/play_run.py`, Surdana card copy `build/harness/polish-save/mc0`, private `drakan.ini` /
`drakan_input.ini` in `build/harness/ps2cfg` (`DRAKAN_INI`, `DRAKAN_INPUT_INI`), a private copy of the player's
`drakan_native_options.ini` per run (`DRAKAN_NATIVE_OPTIONS_INI`, new; `play_run.py` now sets it into the run folder by
default), `DRAKAN_PAUSE_MENU=modern`, 1600 x 900, scripted input only (recipes `build/harness/ps2cfg/*_recipe.txt`).
F1 was closed by posting `WM_KEYDOWN VK_F1` to the game window from a watcher on `DRAKAN_OVERLAY_SHOT_DIR/state.txt`
(no focus change). The SHA-256 of the player's `dist/DrakanPC/drakan_native_options.ini`, `drakan.ini` and
`drakan_input.ini` were taken before and checked after all runs: unchanged; the player's `mc0` and log were not used.

Script note: `cursor <x> <y>` in a recipe is in a 640 x 512 space (the headless script client size), not window
pixels: the first drag (window pixels) landed on the status panel.

## The two open questions

1. **Does message 1 on a check box store the field?** Yes. Static: `441440` (Audio's message 1) reads the check box's
   flag `widget+0x10 >> 7 & 1` into `*0x4fb740` / `*0x4fb744` (Stereo also runs the sound manager's `vt+0xc4`); the
   widget's own press flips the flag first. Live: focus (`2be528`) + Cross stored Subtitles, Stereo, Vibration and
   Reverse Pitch 2 frames after the press in every run (`[pause-screen] SetSubtitles 0 stored 2 frames after the
   press`). The fallback (flip the flag, post message 1) was not needed.
2. **Do the hidden option screens behave under the `2bd1b0` wrapper?** Input, focus, transitions and applies work on
   hidden 9 / 13 / 10 (every Set* confirmed, LeavePage / EnterPage in 3-5 frames). Two things needed changes:
   * *Draws that block.* The trace (`DRAKAN_DEBUG_PAUSE_DRAWLOG=1`, `ps2-quit1`) showed the Quit box's first draw
     entering the wrapper and never returning to it: the draw blocked and the runtime unwound the host stack with
     `EeDispatcherTransfer`. The wrapper's restore and `ps2xNoPreemptLeave` were skipped, so the thread-local
     no-preempt count leaked (that host thread is never preempted again) and the widgets kept `+0x3c = 0`. The phase 1
     pause wrapper had the same exposure. The first full run froze right at Quit to Title (`ps2-run1`), probably from
     that (not proven). Now a guard object restores the fields and leaves the no-preempt section during the unwind;
     the resumed rest of that one draw shows the page (one frame at most). Logged once per Quit to Title
     (`hidden draw of ... unwound (blocked inside): fields restored`).
   * *Timer messages run inside the draw.* `2bd1b0` sends a screen its timer message 9 (`+0x48`). Default Settings'
     native Yes does not restore: it clears the screen's "asked" flag `+0x9c` and arms that timer, whose message 9
     repeats message 1 on `0x2344` from the draw, so the restore (sound manager calls, which may wait) would run inside
     the hidden draw without preemption. In `ps2-run1` it happened to complete (RAM: 100 / 80 / 1 / 1 / 0 / 1 / 0), but
     the glue's confirm fired before it. Now the glue clears `+0x9c` itself and presses `0x2344` once: `4424e0`
     restores at once on the GUI dispatch and no box opens.

## Results (`ps2-run4` unless noted)

| Check | Evidence |
| --- | --- |
| Pages hidden, values from guest memory | `audio.png`, `options.png`, `controller.png`, `pitch.png`, `contact.png` (no native page, cursor or box visible) |
| Master 50 -> 25 (five Left steps) | log `SetMasterVolume 45 .. 25 stored after 2 frames`; RAM `ram_audio_after` (run3) master 25 |
| Music 25 -> 67 (click + drag) | log `SetMusicVolume 33 / 46 / 60 / 67 stored`; `music_set.png` |
| Audio level follows | `ps2-audio`: Master 50 -> 10, Music 25 -> 0 in the pause; gameplay RMS of `DRAKAN_AUDIO_DUMP` about 183 before (t 20.0-21.0 s) and 37-71 after resuming (t 24.5-26.6 s) |
| Subtitles, Stereo, Vibration, Reverse Pitch | `stored 2 frames after the press`; `subtitles.png`, `stereo.png`, `vibration.png`, `pitch.png` |
| Scheme A -> B | `scheme 1 after 1 presses`; `scheme_b.png` |
| Key Bindings | F1 opened on its Controls page (`ps2cfg/ov1/f1_1.bmp`), closed, action confirmed, the host page back (`after_keybindings.png`) |
| PC Settings | overlay open / closed, back on Options (`after_pcsettings.png`) |
| Default Settings -> Cancel | `defaults_cancel.png`, no action |
| Default Settings -> Restore | `defaults restored 2 frames after the press: volume 100 / 80 ...`; `defaults_restored.png` |
| Quit card -> Cancel | `quit_card.png`, `quit_cancel.png` |
| Quit to Title | box hidden, `[in] Quit to Title (modern pause menu): the title menu stays`, binding ended; `title.png`, `title2.png` (also `ps2-quit1`) |
| Persistence | `[menus] native options automatically saved`; the private ini and the card's `Drakan 2.cfg` hold 10 / 0 after `ps2-audio`; the relaunch `ps2-desktop` read 10 / 0 (`ram_start`) |
| Quit to Desktop (last) | `ps2-desktop`: `quit to desktop: closing the window`, `[gsr] exit`, exit code 0, no later shot |

Runs that failed to close F1 (`ps2-run2`, `ps2-run3`) were most likely a test-harness problem (more than one F1 watcher alive at once,
each toggling); with one watcher bound to one process (`ps2-run4`, `ps2-kb`) it closed every time.

## Small fixes

* **Arrow prompts.** The "Adjust" prompt drew `"↑"` with the screen font (Gelasio has no arrows: a missing-glyph
  mark), the pad variant a bullet. `Prompt::Adjust` (left / right) is new beside `Navigate` (up / down); the renderer
  draws both as arrowheads in a key cap (keyboard) or on a D-pad (pad). `PauseScreen` uses Adjust for sliders,
  toggles and the scheme. Seen in every Audio / Controller screenshot.
* **Strip placement.** Not a regression: before and after the restyle the grid alone was centred and the item card
  hung to the right, so the plate's centre sat 184 design units right of the screen's
  (`build/harness/aui-pairs/strip_before_after.png`). The whole plate is centred now (`ps2-strip/strip_hot.png`: plate
  x 425..1175 of 1600); the layout test checks the plate's centre.
* **Quick-slot corner mark.** The marker is the object at `section+0x80`, drawn by `42a4b0(section, object, ...)` from
  inside the item draw `434e80`. That one call is skipped while a host view draws the grid (return address inside
  `434e80`, object = `section+0x80`); no guest memory is written. `ps2-strip`: after F on the elixir the host shows its
  "1" badge and no native triangle (`elixir_zoom.png`), Tab screen too (`tab_hot.png`); log `quick-slot marker draw
  skipped (host view, ra 435a98)`.

## Not verified

Real keyboard / mouse / pad input (scripted only), 4:3 and 21:9, a watchdog fallback while an owned screen is open
(the native screen then shows natively: by design, not run), Save / Load from the modern pause after the stage 2 pages
(phase 1 paths unchanged).
