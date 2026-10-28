# Modern Tab screen: rendering, icons, preview, input (research)

Goal: replace the native Tab screen (GUI screen 0x15) with a host-drawn, full-screen action-RPG menu: item grid with
category tabs, tooltips, comparison, a character panel with Rynn, and Spells / Skills / Journal / Map pages. Mouse,
keyboard and gamepad must all work, and `inventory_screen = native` stays selectable. This is static research only
(Ghidra `build/recomp_ui/alldecomp/all_00414280.c`, current sources). Nothing was built or run. **[unverified]** marks
claims that need a bounded run. It follows `ootf-map-inventory-ui.md` (same day) and corrects one claim in it (below).

## Native facts that drive the design

* **`42fd18` is not only a draw.** It moves the open/close animation `+0x158/+0x15c` (decomp 18048-18065: at t < 0 it
  calls `42f758`, which closes the screen and restores the camera), runs the sound/event loop (18076-18094), zooms the
  player camera (18114-18134: camera `+0x70/+0x74/+0x78` from screen `+0x160/+0x168`), calls the current section's draw
  slot `0x24` (18181-18183), draws the tab records (`42b0a8` / `42b308`, 18185-18199, 18273-18332) and the hints
  (`42faa0`), and finally writes the world crop: `DAT_004fb608 = scale`, `DAT_004fb604 = offset` (18363-18364).
  So it must keep running in the new mode. Only its pixels are unwanted.
* **Correction:** `42f980` does not draw the frame. It is a small state machine (properties `0xe1` / `0xe3`, camera
  `+0x74/+0x78`, the summon state `player+0x1040`; decomp 17770-17826). The page background is most likely the screen's
  only widget, the full-screen image `0x1000` (`mouse-menus.md` section 8), drawn by the GUI manager **[unverified]**.
* **The "Rynn preview" is the live world.** No separate model viewport exists. `42f200` frames the camera on the
  section's subject (bounding sphere, FOV from renderer slot `0x74`, 17583-17628). `42fd18` zooms the camera, and the
  screen manager crop (`2c4400` -> renderer slot `0x8c` = `132f60`, called from `2c4444`) narrows the world to the
  right-hand part. `132f60` writes `+0x100 = 2048 + offset` and `+0x118 = W * s`. Those are the fields seen in the RAM
  dump (`+0x118 = 221`, `+0x100 = 2218` = offset 170, s 0.433; `inventory-viewport-handoff.md`). `132f60` keeps the
  projection centre and the character size fixed while the crop changes, so the crop only reveals or hides world.
  `42f640` sets `4fb608 = 1.0, 4fb604 = 0` (17664-17665): "no crop" is a native state.
* **Icons are the item objects themselves**, drawn in 3D under the UI camera. `437170` walks the 8 x 3 cells and calls
  `434e80(section, item, count, {col,row}, frame, flags)` (flags 2 = selected, 4 = equipped). `434e80` writes the
  item's transform (`+0x14..+0x44`), orientation (fixed `-pi/2`, a special case for class `0x10090`), a fit-to-cell
  scale (`4346d0`, cell spacing `+0x60/+0x64`) and flags (`+0x4 |= 0x8000000`, `+0x104`), sets visibility
  (`17db40` / `17daf8`) and draws through `42a4b0` and `434c00`. The shop list uses the same `434e80` / `4380b0`
  (`drakan_inventory_content_hooks.inl:101-110`). After the cells, `437170` spawns a second object for the selected item
  (`33c0e0`, kept at `section+0x74`) and draws it large through `436b78` (the details model).
* An ordered EE -> GS marker protocol already exists: `drakan_scene_camera` posts `SceneGuideCamera` packets through
  the ordered bridge. The marker `sourcePc 0x430d68, guiScreen 0x15` is sent at handler message 10 **before** any
  inventory geometry and after the world (`drakan_scene_camera.cpp:340-357`). It is enabled whenever the GS bridge is
  attached (`drakan_dlss.cpp:211`). `Backend::noteSceneCamera` handles private markers such as `0x3afd18` (subtitles)
  and `0x139010` (world phase) (`d3d11_backend.cpp:123-185`).

## 1. Item icons

| Option | How | Verdict |
|---|---|---|
| (a) Native off-screen render per item on demand | Call `434e80` / `42a4b0` for any item into a separate GS target | **No.** `434e80` changes the item object (transform, flags, visibility), and its draw goes into the frame's packet stream. A host-initiated call would have to be posted on the EE (nested yielding calls are unsafe, `audio-slider-freeze.md`) and needs a backend redirect anyway. The backend does not expose its render targets: `OverlayContext` only carries device, context, back-buffer RTV and view rect (`gsr.h:142-155`). RTs are internal (`getRT`, `d3d11_capture.cpp:151`). Covered by (b) without the hazards. |
| **(b) Capture from the hidden native page** | The native section keeps drawing every frame. The backend sends the page into an off-screen canvas, and the host samples each cell's rectangle from it | **Recommended.** Equipment draws all 24 cells every frame. Non-selected icons are static (fixed orientation), and the selected one spins, so a live UV crop shows it spinning for free. The 437170 capture already gives exact cell quads (`MenuMouse::captureInventoryGrid`, `drakan_input_glue.cpp:355-369`), as do shop (`captureShopItem`) and spells (`4481e8`). Size: the grid is about 38 GUI units per cell (110..414 / 8), so about 150 px at internal scale 4. That is enough for 4K cells of 120-160 px. |
| (c) Extract models | Offline from the ISO: no model decoder exists (`analysis/scripts` has TXD / XDB / UI tools only), so this means a full format reverse-engineering effort. **At runtime**: `drakan_scene_models.h` already decodes live guest meshes and 128x128 textures for ray reflections, and a host icon renderer could use it | Offline: highest cost. The runtime variant is a good phase-later option for icons of items that are not drawn (shop stock in other sections, an "all items" codex) and for studio lighting. Medium risk: whether inventory item objects take the same mesh path (`189b38` faces, `12b640` buckets) is **[unverified]**. Nothing derived is committed either way: memory only, or a local cache under `dist/` like `textures/`. |
| (d) Hand-authored set | Original art per item class | Committable (not derived), but costs artist time and won't match the 3D look. Use it only for fallback category silhouettes (empty slot, sword, bow, potion, key). |

Cost of (b): about 1-2 weeks backend + glue. Risk: medium. The main unknown is whether the page draws can be
separated cleanly from the world after the `0x430d68` marker; they can be if the order is world -> marker -> page.

## 2. Rynn character preview

Yes, and it is cheaper than expected, because the preview is the world:

* Put Rynn anywhere: overwrite `DAT_004fb604` / `DAT_004fb608` after the original `42fd18` returns, or pass host
  values in the existing `hookCropViewport` (`drakan_widescreen.cpp:290-320`). Rynn lands at `x = W/2 + offset`, and
  the crop must be at least `s >= 1 + 2|offset|/W` to fill the screen. Today's `detail::inventoryWorldCrop`
  (`drakan_inventory_viewport_policy.h:16`) rejects `s >= 1`, so a host-mode branch is needed. The 32:9 run already
  used `s = 1.2025` through that hook, so `s > 1` works.
* Full-screen world with `offset 0, s 1` gives Rynn centred at her native preview size. That is the Assassin's Creed
  layout: character in the middle, gear panels left and right.
* Size: scale the camera zoom that `42fd18` writes (`+0x70/+0x74/+0x78`, from screen `+0x160/+0x164/+0x168`). Write
  the screen fields once on open, so that `42f758` still restores the real camera on close.
* Limits: the preview has world behind it (the level, not a studio backdrop), the camera collision and facing are the
  game's, and a blur must leave Rynn's rectangle sharp (see 4).

## 3. Host drawing layer

What exists (`runtime/settings/src/Overlay.cpp`):

* One ImGui 1.91.9 context, drawn in gsr's overlay callback just before Present, into the back buffer
  (`render`, 421-493). It uses the ImGui DX11 / Win32 backends. Textures come in as `ImTextureID` (SRV) through
  `uploadTexture` (82-105). `GameSkin` (`Skin.h`) is ImGui-only: it 9-slices the brass frame, tiles stone, and draws
  plates, orbs, the fire highlight and the game font 96 baked into the atlas.
* Alpha blending over the game image works (straight-alpha ImGui pipeline). Textured quads, gradients and `AddImageQuad`
  are enough for a game-styled screen. Blur or glow need a backend pass, not ImGui.
* Keyboard and gamepad navigation are on (`NavEnableKeyboard | NavEnableGamepad`, 267). The Win32 backend polls XInput
  itself.
* Input: the subclass consumes keyboard and mouse **only while the settings menu is open** (352-419). HUD, cursor and
  stats layers are draw-only. On open, `drakan_settings_host.cpp:405-414` calls `drakan_pause::overlayOpened()` and
  `InputModule::setSuspended(true)`: neutral pad, key commands and the menu mouse dropped, `injectPadTap` still works.

Gaps for a full-screen game UI:

1. **An input-taking modal layer** next to the settings menu: consume input while the Tab UI is open, and let F1 open
   Settings over it. The current `contentMouseAllowed` requires `!suspended()` (`drakan_inventory_content_hooks.inl:9`),
   and so does `33eaf8` (`drakan_input_glue.cpp:373`). The captures must keep running in host mode, so add an input
   mode `HostScreen` (pad neutral to the guest, captures on, host-issued requests executed) instead of reusing
   suspension.
2. **Fonts at 4K:** the fonts are baked at 28 px and scaled with `FontGlobalScale` (`Menu.cpp:393`, `2195`), which is
   blurry at 2160p. Either re-bake the atlas per output-height bucket, or upgrade ImGui to 1.92.x (dynamic font sizes,
   renderer-owned textures). The game font 96 is a low-resolution bitmap. For large headings, take its HD-pack image
   (look up `textureContentHash` of the decoded TXD, converting alpha back to GS units) **[unverified that the hashes
   match]**.
3. **Fonts and gamepad portability:** body and heading fonts come from `C:\Windows\Fonts` (Palatino / Georgia,
   `Overlay.cpp:272-297`). Vendor an OFL serif in `third_party/`. Feed ImGui gamepad events from `runtime/input`'s pad
   state (which the browser build would source from the Gamepad API) instead of the Win32 backend's own XInput poll.
4. **Scaling for any aspect:** lay out in virtual units with `uiScale = h / 1080`. Keep a 16:9 safe area centred for
   21:9 / 32:9 and anchor side panels to the edges. Use `OverlayContext::viewX/Y/W/H` for pillarboxing.
5. **Threading:** the UI runs on the present thread, the game on the EE. Use a double-buffered immutable
   `InventoryModel` written on the EE (at `430d68` message 10, or `afterPadMgrUpdate`) and an action queue read on the
   EE. Execute actions through the proven MenuMouse request paths (selection substitution at `33eaf8` / `3422c8` /
   `33e960`, then Square / Cross / Right taps; `mouse-menus.md` section 11) so every native rule and refusal stays
   native.
6. **Portability rule:** `Overlay.h` already exposes `ID3D11*` / `HWND` (a known violation). New code must stay
   ImGui-API-only or `std`-only, with textures as opaque `ImTextureID` / `uint64_t` handles. New `OverlayContext` fields
   must be `void*` / handles, like the existing ones.

## 4. Hiding the native screen, world behind the UI

Keep 0x15 open and `42fd18` running, so pause, animation, close path, captures and the selected-item model all behave
natively. Then:

* **World full screen:** the host-mode crop (section 2).
* **Page pixels:** the backend, gated by a host-mode flag, does this when the `0x430d68` marker arrives:
  (1) copy the display RT (the world only) to a `menuBackdrop` texture; (2) send the following draws of that frame
  into a `pageCanvas` RT (cleared to alpha 0, own depth) instead of the display target; (3) present the backdrop,
  optionally blurred, dimmed or vignetted with an unblurred rectangle for Rynn, then the overlay. Expose `pageCanvas`
  (and the native-to-canvas mapping) to the overlay as an opaque handle for the icon crops of section 1.
  Publish `inventoryPageScaleX = 1` and bypass `hud::inventoryMap` in this mode (the page is not shown).
  **[unverified]**: whether all page draws (widget `0x1000`, tabs, text, icons, the `436b78` model) follow the marker
  in the same frame, whether any are deferred to a later DMA kick (the subtitle font is, `d3d11_backend.cpp:129-139`),
  and whether GS alpha after an icon draw gives a usable coverage mask.
* **Cheaper guest-level trims** (optional, saves GPU): no-op `42faa0` (hints) and the tab records `42b308` / `42b0a8`
  when called from `42fd18`. `42b308` is shared with other UI (`MenuMouse.cpp:317`), so gate on the caller. Clear the
  visible flag `0x200` of widget `0x1000` **[unverified that the GUI draw honours it]**. Do not skip the section draw
  slot `0x24`.
* **Modal boxes** (first-visit help, "Buy this item?", "Sell?", Skills "apply points?", START pause): they are separate
  screens stacked on 0x15 (`inventoryCropScreen`). Either leave them out of the canvas redirect (end the redirect at the
  box's handler marker) so they show natively over the host UI, or detect them and answer with host dialogs + taps.
  Native boxes first; it is lower risk.

## 5. Art direction and reusable runtime art

Dark stone panels, brass frames with corner studs, parchment for the journal and tooltips, and the fire highlight for
selection. This already matches the F1 overlay skin. Read from the user's disc at runtime and never stored
(`drakan_settings_host.cpp:61-94`, `ISettingsHost::gameTexture`):

* `INTERFCE.TXD` 118 frame, 88 stone, 128 orb, 197/198 arrows, 103 check, 28 fire, 61 cursor, 96 menu font (already in
  `GameSkin`). Also 94: the inventory icon atlas with the five section icons, ideal for the category tabs
  (`ui-prompts.md` 1.3; its page-switch tile is regenerated, so keep it out of HD lookups). Also 97: the font with
  button glyphs, for prompts. The other candidates inspected in the TXD census are 59-63, 101-102, 117, 126-127 and
  194-196 (`ui-prompts.md` line 79).
* The HD pack (`textures/<hash>.png`, user-generated) can provide 4x versions of these by content hash.
* Parchment: the journal page and the per-level map backgrounds (`DAT_004fbb68`) come from level resources. Capture
  them from `pageCanvas` while those sections draw, or decode the TXD by id **[ids not identified]**.
* Item names and descriptions: source not located. Options: the ODB/XDB properties per item class, or a capture of the
  strings the details pass draws through `286170` (already hooked) **[unverified]**.

## Recommended architecture

**Components**

| Module | Content | Platform |
|---|---|---|
| `runtime/gameui/` (new lib `drakan::gameui`) | `InventoryModel` (items: native index, cell, footprint, class, quantity, equipped channel, icon UV ref; spells; skills; stats), `GameUiScreen` (tabs, grid, tooltip, comparison, character panel, nav, focus), `UiAction` queue, theme on top of `dsettings::GameSkin`. Unit tests drive it headless (ImGui without a backend) | ImGui + std only |
| `ps2recomp/ps2xRuntime/src/lib/drakan_tab_screen.{h,cpp}` + hooks `.inl` | Mode machine (Native / Host, fallback to Native on any missing hook), the model snapshot at `430d68` msg 10, crop / zoom steering, widget / tab / hint trims, action executor over MenuMouse requests and taps, close on Tab / I / Esc / right click (SELECT tap) | glue (EE side) |
| `runtime/input` | Input mode `HostScreen`; captures stay on; pad-state feed for ImGui gamepad | portable core |
| `runtime/gs` | `setMenuCanvasMode(...)`, marker-driven backdrop snapshot + page canvas redirect, blur / dim pass (simple HLSL, WGSL-portable), opaque canvas / backdrop handles + cell-rect mapping in `OverlayContext` | behind the backend interface |
| `runtime/settings` | Modal input layer in `SettingsOverlay`, font re-bake (or ImGui 1.92), vendored OFL font, `[ui] inventory_screen = native|modern` on the General page | glue + ImGui |

**Phases** (each one ships behind `inventory_screen = modern`, default native until verified):

0. Measure (one bounded run): GS draw log with the `0x430d68` marker; the order and deferral of the page draws;
   widget `0x1000`; alpha after icon draws; the effect of writing `4fb604/608` after `42fd18`; box draw order.
1. Shell: host mode, full-screen world, backdrop dim / blur, page in the hidden canvas. Show the whole canvas in one
   panel to prove the plumbing. Close / open parity.
2. Equipment: host grid with per-cell icon crops; select / equip / use / drop / hot-slot through the existing requests;
   mouse, keyboard and gamepad navigation; tooltips (names once a source is found).
3. Character panel: place Rynn with the crop offset and zoom; equipped weapon / armour slots around her; comparison
   from the item records (`+0xed0` / `+0xeac` channels).
4. Spells, Skills, the shop (3-section variant); native modal boxes shown over the host UI.
5. Map (reuse the M view of `ootf-map-inventory-ui.md`) and Journal (text capture, or the native page from the canvas
   in a parchment panel).
6. Polish: HD art lookups, transitions, UI sounds (needs a scheduler-owned sound path; `drakan_settings_host.cpp:374`),
   localisation.

Estimate: 4-6 weeks including verification runs.

## Biggest risks

1. Backend page-canvas redirect: it interacts with the RT cache, temporal / DLSS / NR state and possibly deferred DMA;
   phase 0 decides it.
2. Modal boxes and first-visit help hidden under the host UI, or answered twice.
3. Input mode: suspension disables the content captures and `33eaf8`, and the SELECT / START / Circle meanings
   (`mouse-menus.md` 7, 8) must be reproduced by the host.
4. Item names and descriptions: no source located yet.
5. Parity with native rules (shop refusals, summon lock `player+0x1040`, Skills apply box): mitigated by acting only
   through native paths.
6. ImGui 1.91.9 font quality at 4K; Windows-only fonts and XInput in the current overlay (portability).

## Phase 1 done (library only): `runtime/gameui`

Added the platform-free core `drakan::gameui` (std only; `runtime/gameui/README.md`). Nothing in the game uses it yet,
and the settings key `[general] inventory_screen = native | modern` (default `native`) is parsed but not read.

* **Model + publish**: `Snapshot` (items with origin cell, footprint, type / class / derived category, name, details
  lines, quantity, equipped, hot slot; spells; skills + unspent points; HP / mana / gold / XP / level; quests and
  markers; regions; shop stock; native state) and `ModelExchange` (double buffer, readers hold an immutable copy).
* **Reader**: `readSnapshot` over a read-only `IGuestMemory`; all addresses in `Addresses.h`. Checked against the RAM
  dumps of the data-model note by `gameui_ram_test` (skips without them): fixture dagger / bow (1x2, 200/200), the
  magic5 shop save (gold 376, bow 181/200, club 144/150, Fireball details cached from the Spells pane, 6 quest
  records). Correction to the data-model note: `res` and the string manager are **pointers**, `res = *(ctx+0x320)`
  (`*(ctx+0x344)` = `0x4dbca8`), as the probe scripts already do. Region entry 13 holds unrelated text in every dump,
  so the reader stops at 13 entries.
* **Screen**: pages (Merchant in shops, Inventory, Character, Spells, Skills, Journal, Map), category filters,
  detail panel with comparison against the equipped item (by details label; numbers and Speed / Range words ordered,
  lower is better for "Needed" / "Cost", prices neutral), one directional-navigation model for pad and keyboard,
  hover / click / double click / right click for the mouse, clickable footer hints, refusal toasts. Output is draw
  commands as data plus `UiAction`s; Rynn's area is a `WorldView` hole for the crop / zoom steering of section 2.
* **Actions**: queue + `Expectation` / `evaluate` / `ActionTracker` (effect and time-out per action, table in the
  README). Native refusals become `Refused` after the time-out; boxes count as the effect of Buy / Sell.

Open for phase 2 (glue, renderer, ImGui):

1. Executor: map each `UiAction` onto MenuMouse requests + taps on the EE, refuse when the cell no longer holds
   `UiAction::handle`, one action at a time, feed `ActionTracker` with each new snapshot.
2. Where to read: `430d68` message 10 (inside the screen, `ctl` stable) is the natural point; the snapshot copies
   ~10 KB per frame (fine), or publish only on change (`sequence` is per read).
3. Native section vs host page: the screen emits `SwitchSection` on page changes (Inventory / Character 0, Spells 1,
   Skills 2, Map 3, Journal 4; none for the Merchant page). Leaving Skills with changes opens the native apply box.
4. Spell details exist only for the selected spell (selecting changes the active spell), so unselected spells show
   their cached lines or a hint; decide whether focusing a spell should select it (the native page does).
5. Renderer: an ImGui draw-list translator for `DrawCmd` (Panel styles on `GameSkin`), `ImageRef` resolution (icon
   crops from the page canvas, INTERFCE.TXD 94 section icons, prompt glyphs, map background), and a real
   `TextMeasure` from the font atlas.
6. Input: the `HostScreen` input mode, the key mapping in the README, and double-click detection in the glue.
7. Unknowns still open from the data-model note: armour / shield / key classes (categories fall back by object type),
   the quick-inventory order and capacity, the shop stock list (only the current stock record `0x1b` is read; browsing
   uses `ShopBrowse` taps), and the map image size per level (markers are placed only when it is known).
