# Browser player, game-led redesign

The user rejected the browser shell's landing-page appearance and requested the
Claude frontend design skill. Applied the official Claude frontend-design skill.
The references are the original title/load screens observed in the running game:
aged metal frames, warm lettering, a dark surround and a clearly selected action.

## Design plan

| Token | Value | Role |
| --- | --- | --- |
| Black stone | `#0a0d0e` | Page surround |
| Deep water | `#152022` | Recessed launcher surface |
| Bronze | `#80643e` | Frame and control boundaries |
| Worn gold | `#c3aa73` | Lettering and frame highlights |
| Parchment | `#e1d6b8` | Readable primary text |
| Ember | `#e7ad54` | Selected action / keyboard focus |

Type: Palatino / Book Antiqua / Georgia for the player and controls, with a
plain sans-serif fallback for small help text. The wordmark and launch action
carry the personality; no external font or game-derived image is added.

The centered screen sets the layout. It fits the available desktop height rather
than pushing the game below a marketing header. Help and logs stay collapsed.

```text
Drakan / The Ancients' Gates       Mouse look  Fullscreen  Quit game
+---------------------------------------------------------------+
|                                                               |
|                Actual 4:3 game / launch action                  |
|                                                               |
+---------------------------------------------------------------+
Session status                         Controls & saving
Save in the game's pause menu.          Session log
```

The game owns New Game, Load Game, inventory, pause and Save Game. The browser
only starts/stops the runtime, captures the mouse and provides fullscreen. Saved
games already created in the game persist automatically; a separate "Store
saves" button suggests an ability to save current progress that it does not have.

## Review before implementation

A fantasy illustration, large promotional headline and decorative feature cards
would repeat the rejected website treatment. They are omitted. The distinctive
element is the original-style framed game screen, not another fantasy landing
page. Frame ornaments are authored vectors, not extracted game assets. Effects
remain static so the shell does not compete with rendering. Keyboard focus,
reduced motion, narrow layouts and preservation of the original aspect ratio
are part of the implementation checks.


## User clarification: the game is the interface

The user clarified that the browser must not show a control scheme or a launcher
interface. The preceding framed-shell proposal was rejected and removed. The
revised design uses the actual game as the entire viewport: no header, toolbar,
control legend, help panel, footer or visible session log during play. A minimal
branded Play gesture is needed to initialize browser audio; it disappears once
the runtime starts. The game's own title, load, pause and save menus own the flow.
Normal focus, mouse capture, menu cursor release and fullscreen behavior are
platform integration, not a second set of on-page controls. Saved games must
persist from the in-game save operation without a separate browser button.


## Game-only implementation and validation

The shipped HTML now contains only the canvas, the initial Play gesture/audio
startup state, and a normally hidden storage-error dialog. No control scheme,
header, toolbar, help, footer or on-page diagnostic log is displayed. During
actual title, gameplay, pause and save screens, document.body.innerText was empty.
Those visible screens are rendered by the game itself. The original 4:3 image
retains its aspect inside the full browser window and fullscreen.

The guest executor publishes menu state through an atomic read-only browser
export. A gameplay click captures the mouse; opening the native pause menu
releases it, and returning through the game's menu reacquires it while the user
gesture is active. This was checked live, along with fullscreen transitions.
An initial pointer-lock attempt while the test window was not foreground failed;
bringing it forward and clicking captured normally, with no error in the fresh run.

In-game Save Game wrote empty Slot 4. All twelve existing Slot 1–3 files retained
their SHA-256 hashes. All four new Slot 4 files in IndexedDB matched the VFS bytes,
without any manual drakanSync call or external save button. Reloaded the browser
without stopping the runtime first, then loaded Slot 4 through the original game
menu successfully. Automatic file-close persistence shares the periodic/exit queue.
A failed initial restore refuses write-back; asynchronous failures do not poison
the queue. The Node regression fixture covers those failure and ordering cases.

The explicit ?metrics=1 development URL enables read-only metrics without adding
player UI or relying on injected test scripts. A non-instrumented timing attempt
was rejected as incomplete. A subsequent instrumented sample ran under substantial
Call of Duty CPU contention and failed pacing; it is retained separately from
clean-run evidence. The user was asked whether they could close that game before
an uncontended rerun; no unrelated process was stopped.
