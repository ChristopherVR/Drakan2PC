# Startup and New Game intro skipping

With Skip intro videos on, the previous PAL build showed all 264 frames of the boot logo at an increased rate. A
single Esc tap with the setting off left the logo screen active. The input hook changed playback pacing while Esc
was held and sent Start taps to the New Game movie. The Order of the Flame opening had its own playback path.

The PAL movie screen tick now takes the screen's close path before the game creates a movie job. If Esc is pressed
after playback starts, the tick uses the game's stop request and lets the pending reads and soundtrack finish
shutting down before closing the screen. The Order of the Flame opening uses its existing level exit when skipped.

Verified in the rebuilt Windows play folder with a PAL disc and fresh test cards:

| Case | Result |
| --- | --- |
| Skip intro videos on, The Ancients' Gates | The title and first level loaded. Neither the logo nor the New Game movie started. |
| Skip intro videos off, The Ancients' Gates | A single Esc press closed each active movie. The logo showed 11 frames and the New Game movie showed 10. |
| Skip intro videos on, Order of the Flame | Ruined Village loaded without opening the movie or presenting any movie frames. |
| Skip intro videos off, Order of the Flame | A single Esc press during the movie took the exit to Ruined Village. |
| Skip intro videos off, Order of the Flame with the pre-rendered movie disabled | A single Esc press during the live opening took the exit to Ruined Village. |

## Startup-only setting

Skip intro videos now applies only to the startup logo. With the setting on, the rebuilt PAL game reached the title
without starting the logo. The New Game movie in The Ancients' Gates, the pre-rendered Order of the Flame opening, and
its live fallback each played for at least 2.5 seconds before a scripted Esc tap ended the intro. Both Order of the Flame
runs reached Ruined Village.
