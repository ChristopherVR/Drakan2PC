# Tab rework phase 2: the modern screen in the game — 2026-10-25

Phase 2 of `docs/journal/2026-10-24/tab-rework-rendering.md`: the gameui screen drawn over the native inventory for
the Inventory / Equipment page, end to end. Reference: `docs/reference/mouse-menus.md` section 13. Commits
`b798c69` (input host-screen mode), `84d5e66` (gameui Inventory layout), `67fafee` (overlay renderer, screen layer,
fonts, F1 option), `78d785b` (game glue).

## What was decided against the research

* **No backend page canvas.** The research recommended redirecting the page draws into an off-screen canvas at the
  `0x430d68` marker. Not needed for the Equipment page: the page art is removed with the views' draw skips, and the
  icons are taken from the presented frame. The overlay copies the back buffer before it draws, the native grid is
  scaled and moved so its icons land exactly under the host grid's cells (inside the native 4:3 window, where the GS
  front end does not cull them) over a native black fill, and the host draws each cell's crop additively. The crop
  is 1:1 in output pixels, so it is as sharp as the native render; the selected icon spins in the cell and in the
  detail card. The same frame copy gives the backdrop blur. Risk: the copy is the image of the previous GS frame
  while the cells are this EE frame's; the grid is static after the open animation, so it does not show.
* **The native black fill is hidden by the host grid panel.** Because the native grid sits under the host grid, the
  black rectangle and the native icons are covered by the grid panel and the crops; on host pages without the grid
  the grid draw `437170` is skipped.
* **Message boxes: step aside instead of punching a hole.** The plan was to draw the box region of the frame copy on
  top. The box screen's widgets do not give its outline (a full-window type 10 widget, label widgets 256 wide while
  the drawn text spans GUI 32..480), so the host screen fades out while a box is up and the box shows over the live
  world, natively interactive. Run `tab2-r4b`: the Equipment help on a new game, OK, back to the
  modern screen.
* **Details rebuild.** The details builder `342f90` runs from the selector `33eaf8`; for the selection made during
  the open animation it leaves `ctl+0x4ac = 0` (labels still loading). Re-steering with arrow taps fails when the
  grid holds one item (no arrow event reaches `33eaf8`). Fixed by invoking `33eaf8(ctl, cell)` as a guest invocation
  (`invokeCurrentSequence`), which rebuilt the dagger's lines on the first try (`tab2-r4b` log: `details of cell 0
  empty: selector run again` -> `[Rynn's Dagger][Skill Needed: 1][Damage: 4][Speed: Fast][Indestructible]`).
* **Scripted input.** Harness scripts feed the input module, not window messages, so the overlay never saw them. The
  input module now queues the script's key / mouse / pad edges while the host screen is up; the overlay reads that
  queue in scripted runs. This made the mouse, keyboard and pad paths testable headless.

## Runs (all on the 1920 x 1080 iGPU display: only that monitor was attached, about 6-8 fps)

Save: private copy `build/harness/polish-save/mc0` (Surdana; bow, Wood Club, Health Elixir, Rynn's Dagger, 646 gold),
`--env DRAKAN_INVENTORY_SCREEN=modern`. Recipes in `build/tab2/`.

| Run | What | Result |
| --- | --- | --- |
| `tab2-r1` | open, hover, arrow, Esc | page hidden, world whole, Rynn left, real icons in the grid; details missing (fixed later) |
| `tab2-r2` | hover + double click club, Left + Enter bow, F on elixir, Esc | club equipped (`E`, Weapon card), bow equipped (on Rynn's back in the preview), elixir quick-slotted, closed with Rynn holding the bow |
| `tab2-r3` | pad: D-pad, RB Character, RB Spells, LB LB, B | pad prompts, Character page with Rynn centred, Spells page (section 1 switched natively), back to Equipment, closed |
| `tab2-r4b` | new game: Tab, help box, Enter, Esc | help box shown, OK natively, modern screen back with the dagger's details |
| `tab2-r5` | `native` setting | the original screen unchanged |
| `tab2-r6` | pad: A equip club, Y drop elixir, Select close | equipped, dropped, closed |
| `tab2-final` | mouse equip, F quick slot, E / Q pages, Tab close | all confirmed; Rynn holds the club in the world afterwards |

Executor timings from the logs: selection steer 1-8 frames, equip confirmed 6 frames after Square, drop 1 frame,
quick slot 1 frame, section switch 1 frame, close 10 frames.

## Not done / next

* No 4K run: only the 1080p display was attached in this session (fonts are baked per output size, so 4K should be
  crisp, but it is unverified).
* Not routed: Skills spend / return (the native row selection is unknown), shop pages (shops stay native: three
  sections), map / journal pages show the host's data pages only (no map image).
* Character page: no native icons (the grid is not drawn there): category silhouettes for the equipped slots.
* Rynn drifting off the preview column's centre with her idle animation: fixed, see `tab-preview-centring.md`.
* The quick-slot corner tag the native icon draw adds (`4315b0`) shows in the crops; the host's own `Q` marker was
  dropped for it.
* Not checked: 21:9 / 4:3 outputs (the grid target uses the centred page map with `hudScaleX`, the same as the views).
