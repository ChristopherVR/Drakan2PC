# Campaign map click-through in the desktop editor (2026-11-02)

Setup as in `editor-desktop-clickthrough.md`: debug Tauri build, private WebView2 profile, CDP on 9397, real
`Input.dispatchMouseEvent` input, private play folder, `settings.json` backed up and restored. Project "Map Mod" with
1A Surdana, 1B Kragmor, 1C Catacombs, 1D City of the Damned and a copy "Surdana Copy". Scripts and screenshots:
`build/scratch/mapclick/` (`shots/`).

| Step | Result | Shot |
| --- | --- | --- |
| Create project, add 4 levels + copy | PASS | `02-levels.png` |
| Switch to Map, exits load: 5 cards, wires 1A to 1D/1B/1C and 1B/1C/1D back to 1A, stubs (2, 6, 1E, 3A, 5), warnings on the copy and on 1A's 3A exit | PASS | `04-map-view.png`, `05-zoom-1A.png` |
| Click card (1B) opens the level | PASS | `06-card-click.png` |
| Click wire 1A to 1B: Surdana opens, exit #12250 selected, Logic panel shown | PASS | `09-wire-after.png` |
| Drag card, Auto arrange (restores the layout), drag again, saved in `mapLayout` | PASS | `13-dragged.png`, `14-arranged.png`, `15-dragged-1B.png` |
| Restart app, project reopens, card positions kept | PASS | `16-map-after-reopen.png` |
| Double-click 1C: game starts straight in Catacombs (`[playtest] New Game opens 'Level 1C - Catacombs'`, level load, player under control) | PASS | `18-game-catacombs.jpg` |

## Bugs fixed

* Stub labels (up to 214 units wide) overlapped the next column of cards after Auto arrange (`05-zoom-1A.png`).
  `MAP_GAP_X` 160 to 260, `MAP_STUB_W` 190 to 216 (`editor-core/src/campaign-map.ts`).
* The first fit ran before the exits arrived (cards only, 100 % zoom) and never again, so the copy was off-screen
  until Fit was pressed. The map now keeps fitting on each update until the user pans, zooms or drags
  (`app/src/editor/project/campaignmap.ts`, `touched`).

## Left over

* The copy's exits all warn "no exit leading back to Surdana Copy"; expected for a fresh copy, but 9 warnings flood
  the Checks list.
* Fit leaves a lot of empty space because back-wire lanes widen the bounds.
* Row labels truncate ("Transition from 1..."); only the tooltip shows the full name.

## Polish

Screenshots in `build/scratch/mappolish/` (before: `build/scratch/mapclick/shots/16-map-after-reopen.png`).

* Copy warnings: `validateLogic` (per-level Validation, `logic.ts`) now takes `copyOf` and passes it to
  `exitArrivalProblem`; the Logic panel supplies it from the project's copy entry. `validateProject` and the map
  already used it via `levelExits`. A copy is fine when the destination leads back to the copy or its source; a real
  warning remains when neither does. The "No exit of the project leads to ..." note says "Nothing leads to the copy
  ... Point an exit at it in the Logic panel, or start it from the level list." for copies. Tests in
  `logic.test.ts` and `project.test.ts`.
* Fit frames cards and stubs with a 24 px margin (was 56) and lets the back-wire lanes overflow slightly
  (`campaignmap.ts`).
* Exit rows show the destination as the main text (left, normal colour) and the exit's own name as small faint
  secondary text on the right (`campaignmap.ts`, `campaignmap.css`); tooltip unchanged. Mock of the rows:
  `after-exit-rows-mock.png`.
* Browser build (static `vite build`, private Playwright context, seeded project, no disc): map renders three cards,
  Fit gives 95 %, card drag works, List/Map toggle works, no Play buttons (only the Start badge), "Open your disc
  image to see the exits." note shown. `after-browser-map.png`, `after-browser-dragged.png`. Real exits and lanes
  were not re-shot (needs the desktop app with a disc).
