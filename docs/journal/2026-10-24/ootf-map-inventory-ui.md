# Order-of-the-Flame style Map (M), bottom inventory (I) and a modern Tab screen: feasibility

Request: M opens a fit-to-screen map like Drakan: Order of the Flame (OotF), I opens an OotF-style inventory strip at the
bottom of the screen, Tab keeps the original screen; and look at replacing Tab with a modern full-screen UI (Assassin's
Creed style). This note is static research (Ghidra decompile `build/recomp_ui/alldecomp/all_00414280.c`, existing
hooks, `docs/reference/mouse-menus.md` sections 8 and 11). Nothing was built or run.

## How the screen works today

* Tab = pad SELECT, and `I = ACT:0x17`, both reach command `0x4030`: the shipped PC keymap (table `0x54b6c8`) already has
  DIK `0x17` (I) and `0xcf` (End) -> `0x4030`. So today **I and Tab do the same thing**. M (DIK `0x32`) is unbound.
* `0x4030` opens GUI screen `0x15` (ctor `430b48`, handler `430d68`). `+0x154` is the current section (0 Equipment,
  1 Spells, 2 Skills, 3 Map, 4 Journal; 3 sections in a shop), `+0x1a8` the section objects.
* Section switching is `4309d8`: key `0xd1` / `0xd3` step `+0x154`, then call the old section's slot `0x4c(0, dir)` (it
  may refuse, e.g. the Skills "apply points?" box) and the new one's `0x4c(1, dir)`. There is no "open at section N";
  M would open the screen and step to section 3 through the same native path (R1 taps or posted `4309d8` calls).
* Per-frame draw `42fd18`: installs the UI camera (`430e90`), moves the whole page left with renderer slot `0x8c`
  (`-(W/2) * 0.33333`), draws the frame (`42f980`), the current section (slot `0x24` with the frame at `+0xa0..`), the
  five tab icons (`42b308`) and the corner hints (`42faa0`). It ends by setting the world crop for `2c4400`:
  `DAT_004fb604 = W/2 * 0.66667 * t` (offset) and `DAT_004fb608 = 1 - 0.66667 * t + 0.1` (scale), `t` = open animation.
  That is the "page on the left, shrunken world on the right" layout; `inventoryWorldCrop` already adjusts it.

## M: fit-to-screen map

Map section draw `439b28` (key handler `43a588`):

* The background is one image (resource from `DAT_004fbb68`, region table entry `+0xc`) drawn by the sprite method
  (texture vtable `+0xbc`, scale `f12 = f13 = 1`) at the projected section origin (`4314a0`, frame point
  `y = -490, z = 610`) at its **native texture size**. That is why it is small: nothing scales it to the screen.
* Markers (`DAT_004fbb70`, 0x20-byte records, count `DAT_004fbb78`) are drawn at `origin + record{x, y}` with their own
  texture size (`f12 = 1, f13 = -1`, call returning to `43a330`, already hooked for the mouse), the selected one with a
  frame (`4398f8`) and its caption at `origin.y + 279`. A 3D "you are here" object (`+0x68`) is placed from the same
  origin in UI-camera units.
* The map has no pan / zoom; arrows only cycle markers.

Approach (guest level, so textures, HD packs, the GS path and the mouse capture all follow on their own):

1. A map-view flag set by the M binding: open `0x4030`, step to section 3, mark the screen as "map view".
2. While the flag is set and section 3 draws: replace the sprite scale and position of the background and markers with
   `s = min(screenW / texW, screenH / texH)` about a centred origin (hook the `+0xbc` calls from `439b28`, as
   `hookMapImage` already does for return `43a330`), scale the `+0x68` object's placement the same way, and move the
   caption below the scaled map.
3. Skip the page offset, the frame, the tab icons and the hints in `42fd18` for that view, and set the world crop to
   none (or darken the world) so the map is the whole screen.
4. M / Esc / right click close (SELECT path); Tab during map view switches to the normal screen.
5. MenuMouse's map capture reads final submitted vertices, so marker hover / click keeps working after scaling.

Unknowns to measure in a run: the background texture size per level, whether the 3D marker reads `+0x58/+0x5c` from
elsewhere, and how much upscaling the original map art tolerates (the HD texture pack helps here).

Risk: low to moderate. Everything stays inside the native screen; a failed hook falls back to the normal map.

## I: OotF bottom inventory strip

OotF's inventory is an 8 x 3 style grid along the bottom over the paused world. ATG's Equipment section is already an
8 x 3 grid of 3D icons (`437170`, cells 0..23, spacing `+0x60/+0x64`, centres `+0x68/+0x6c`), so the content matches.

Approach: open `0x4030` on section 0 in a "strip view" and, in `42fd18`:

* no page offset and no world crop (full-screen world behind, optionally dimmed at the bottom);
* move the Equipment grid to the bottom: the grid is placed through the frame passed to slot `0x24` and the UI camera,
  so shift that frame (or the UI camera) down and centre it; hide the character preview, the tabs and the frame art;
* keep the item name / details line above the strip (it is drawn by the same section; position to be measured);
* section keys (R1 / L1, wheel) disabled in this view; I / Esc / right click close.

The Equipment mouse hit test projects the live grid at `437170` with the installed camera, so it follows a moved grid.
The existing GS-side `inventoryPageScaleX` contract must be published as 1 in this view.

Unknowns: whether the details / description text and the preview are separable draws (needs one bounded draw log of
section 0), and the first-visit help boxes, which are positioned for the old page.

Risk: moderate. More native draw paths to steer than the map.

## Tab: a modern full-screen inventory (Assassin's Creed style)

What exists and could be reused:

* **Actions**: the content mouse work already drives every section through the game's own navigation and confirms
  (`33eaf8` Equipment target, `3422c8` Spells, Skills rows, shop `33e960`, then Square / Circle / Right). A host UI could
  issue the same requests, so equip / use / drop / summon / spend points / buy / sell stay native and save-compatible.
* **Data**: player inventory controller `player+0x584`, item records with the known XOR / rotate quantity encoding,
  equipped channels `+0xed0` / `+0xeac`, spells, skills, journal text and map regions are all readable.
* **Drawing**: the F1 overlay's ImGui layer (`runtime/settings`, `drakan::imgui`) draws over the game, and
  `runtime/ui` has fonts and prompt glyphs.

What does not exist and is the real cost:

* **Item icons**: ATG's icons are spinning 3D models, not 2D textures. A modern grid needs either renders of those
  models into textures (drive the native icon draw into an off-screen target per item) or a hand-made icon set.
* **Names and descriptions**: the strings must be located in the resource data per item class.
* A new layout, controller navigation, tooltips, comparison, and parity with every native rule (shop refusals, summon
  lock, the Skills apply box). Hiding the native screen while it keeps running underneath.

Estimate: a multi-week project, best done after the M and I views, which build the "custom view of screen 0x15"
machinery it needs. A cheaper middle step: keep the native 3D content but re-lay the native screen full width (bigger
grid, details panel on the right, larger preview) with the same guest-level technique.

Note: a separate inventory sidebar was built and rejected on 2026-10-18 (`docs/journal/2026-10-18/native-inventory.md`).

## Suggested order

1. M map view (smallest, proves the "view flag + steer `42fd18` / section draw" machinery).
2. I strip view on the same machinery.
3. Tab: decide between the native full-width re-layout and the host-drawn modern screen.

## Implementation: M map view (built and run)

Reference: `docs/reference/mouse-menus.md` section 12. Runs: `build/harness/view-map1..5` (private copy of the
`inv-sections` Surdana save, 3840 x 2160, 16:9, page scale 0.75).

Measured / found:

* Surdana's map background is one **256 x 256** sprite, natively at x = -237 .. -164 during the open animation and
  y = 103 (window coordinates before the page offset). Fit scale 1.75 (1.797 with a 40 px caption band; 52 px keeps the
  caption clear of the bottom edge), markers 1.32. Marker sprites are 16 x 16 (`image+0x24/+0x28`).
* `42f980` is not the page frame (it runs the camera "flash" state, `+0x13c/+0x140`), and `42faa0` is the tab glow,
  not corner hints. The page art is the section base draw `42b5c8`: two 3D panels through `42a2c0`, then the hint bar
  (slot `0x54`, text through `431a10`) and the gold count `42b988`. Section vtables: base `54de18`, Equipment `54e108`
  (draw `433610`: `42b5c8`, `4330b0`, slot `0x6c` = `437170` grid + item preview `436b78`, details text `433850`), Map
  `54e220` (draw `439b28`).
* The section help id comes from the entered section's slot `0x5c` through `42b850` into `screen+0x150`, and is shown
  by `42fd18` once the open animation ends. Entering only the target (preset `+0x154`, one native "previous" step)
  avoids the intermediate sections' first-visit boxes.
* Moving the UI viewport right (slot `0x8c` offset `W/2 * (1/s - 1)`) centred the map's origin but cut it off at native
  x 512: the GS front end culls and scissors in native frame coordinates before the D3D page map. The working fix is
  renderer-side: a `CentredPage` flag on the inventory draw packet and a centred `inventoryMap` for that frame.
* The click log's view fields (`view=... valid=0`) are only filled on the Equipment path; its GUI values (x 222 / 250
  for client x 1734 / 1890 of 3840) are the centred inverse, and the two clicks selected "The Palace" and "Brekk the
  Blacksmith" (screenshots `build/harness/view-map5/click_yellow.png`, `click_blue.png`).

Verified live: M opens straight on the map (`inventory section -1 -> 3`, no intermediate help box), map fit and
centred over the full world (`build/harness/view-map4/map_b.png`), markers / caption / "you are here" follow the
scale, M / Esc / right click / Tab close it, Tab afterwards opens the normal Equipment page unchanged
(`build/harness/view-map5/after.png`, `view-map1/tab_b.png`). Not done: dimming the world beside the map (the gameplay
HUD orbs stay drawn below it, as on the normal screen); `map_view = 0` covered by the parse test only.

## Implementation: I strip view (built and run)

Runs: `build/harness/view-strip1..5`, `view-new1` (new game, first-visit help boxes).

* Equipment: plane cells 218 x 218 units, grid centre natively at plane (-725 + 3.5 cw, 520 - ch); moved for the draw
  to land at GUI 256, 412 (grid 104..408 x 331..494). The first frame of the open animation solves to a different
  plane point (the frame moves), later frames are stable.
* Writing the world crop globals after `42fd18` returned did not work: the draw does not always return to the hook (the
  log showed the native crop, widened to GUI x 315 * 0.75 by the widescreen correction, so x = 0.46 of the output).
  Overriding at the crop call itself (`132f60` from `2c4444`) does.
* With the whole world shown, the region above the walls kept stale gameplay pixels (no sky is drawn behind the
  inventory; the camera there is the inventory's own camera in front of Rynn). Restoring the camera zoom alone did not
  help; clearing the world target at the first world draw after a view frame does (`view-strip4/strip_b.png`).
* Verified live: `view-strip5` hover / click / equip, wheel blocked, Esc and right click close; `view-new1` help boxes
  over both views. Not done: slot frames and a darkened band (the slots are part of the page panels).

## Polish: strip bar, map shade, hidden orbs, and the 6 fps reading

Runs: `build/harness/polish-*` (private copy of the `inv-sections` Surdana save in `build/harness/polish-save`).

**The 6 fps in `view-strip4/strip_b.png` was the display, not the strip.** All `view-strip*` and `view-new1` runs
(and every run in this session) opened a 1904 x 993 window (`[gsr] ... window 1904x993`): the game window was on the
1920 x 1080 display driven by the AMD iGPU while the renderer runs on the RTX 5080, so every present is a
cross-adapter copy. The `view-map*` runs had a 3818 x 2032 window on the 4K NVIDIA display and showed 60 fps. In the
same strip4 run the closed world showed 5.9 fps and the normal Tab screen 7.9 fps. Sustained measurement with
`PS2X_PERF=1` over ~90 s per phase (`polish-perf-before`, `scripts/perf`-style per-second lines, summarised per phase):
world 12.1 flips/s, strip 12.7, Tab 12.8, map 12.9 (guest vblank ~25/s, EE thread 2-3 %); with the new bar
(`polish-perf-after`) strip 12.7, Tab 12.6, map 12.7. The strip costs nothing measurable; no fix needed. Only the
1080p display was attached in this session, so nothing was run on the 4K output.

**Native 2D draws from a hook.** The bar uses the game's own GUI draws `2c8e98` (tinted 32 x 32 white tile, the
equipped cell's dark fill) and `2c8ff8` (the thin slot / selection frame), both `(x, y, w, h, t0 = 4 colours)` in
native window pixels. First attempt, as nested host calls (copy of the context, `$ra = 0`, no preemption): the first
draw blocked once (duplicate first call in the log, the hook restarted from its entry), and after a few frames the EE
stopped for good (`[perf]` flips 0, EE 0 %). A blocking draw (GIF buffer kick, texture load) throws
`EeDispatcherTransfer` through the nested call; the copied context is lost and, without RAII, the no-preempt counter
leaked. Fix: queue the draws as guest invocations of the current thread (`EeScheduler::invokeCurrentSequence`, as the
HLE IPU init does) and unwind; the thread resumes its own context at the hooked entry, the hook runs again (per-frame
stamp) and chains to the original. Two more traps: `SET_GPR_U32(c, 4 + i++, v)` increments twice (the macro evaluates
the register index twice: first run drew with x = a stack address and a 24 M pixel width), and the brass message-box
frame `2c94a8` never returns from inside the inventory (left out; a double `2c8ff8` frame instead). GS vertex colours
are 0x80 = full, so the first colours (0x96 brass) came out washed-out pale gold; halved.

**Who draws the orbs and the strip's "646".** A sprite log of `1c1e90` during a view frame: the health orb pieces come
from `3ac008` (x 28..110, y 379..494) and the mana orb from `3ac540` (x 417..481), both outside `42fd18`; `3ac540(0x510150,
frame)` is the gameplay HUD draw (from `3b2178`). Skipping it while a view's screen is up hides the orbs under the map
and in the strip. The "646" left in the strip was not `42b988` (already skipped) but `4330b0` from the Equipment draw:
`sprintf` of the gold into the text box `42ae00` plus the spinning bag `4316c8`; skipped in the strip.

**Details panel.** Strings of the controller `section+0x70`: name `+0x244`, `+0x2c4` (blank `" "` for weapons),
`+0x284`, `+0x304`, `+0x450`, `+0x344`, `+0x384` (indented by an icon). Unused lines hold `" "`, so lengths are counted
to the last visible character (`Health Elixir`: one line). Widths of about 7.7 / 6.0 native pixels per name / stat
character fit Wood Club, Short Wood Bow (6 lines) and Health Elixir.

Verified live: 16:9 `polish-final/strip.png`, `strip_right.png`, `map.png`, `tab.png`; 21:9 `polish-219/strip.png`,
`map.png` (shade covers the whole 21:9 picture); mouse `polish-mouse2` (hover potion / bow, click equips the bow, Esc
closes); Tab in the strip closes it (`strip_tab_closed.png`) and a later Tab is the normal screen with orbs and gold;
`map_view = 0` / `inventory_strip = 0` via `DRAKAN_INPUT_INI` (`polish-off`): normal Map and Equipment pages.
Not verified: a 4K output, a new game's first-visit help boxes over the new bar.
