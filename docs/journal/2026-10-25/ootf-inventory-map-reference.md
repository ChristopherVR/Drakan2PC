# Drakan: Order of the Flame (PC, 1999): inventory and map reference

Research note for restyling the port's "I" inventory strip and "M" map view to look like the 1999 PC game's.
No code changes. All measurements come from the screenshots listed below. Colours are sampled from
compressed PNG/JPEG captures, so treat them as approximate (about ±10 per channel).

## Sources

| # | What | URL |
| --- | --- | --- |
| S1 | MobyGames Windows screenshot gallery (49 shots; captions used to find the UI ones) | https://www.mobygames.com/game/337/drakan-order-of-the-flame/screenshots/windows/ |
| S2 | MobyGames "Armor and items are being used by simply point and click." (inventory open, 16:9) | https://www.mobygames.com/game/337/drakan-order-of-the-flame/screenshots/windows/886283/ |
| S3 | MobyGames "World map" (map open, 16:9) | https://www.mobygames.com/game/337/drakan-order-of-the-flame/screenshots/windows/886282/ |
| S4 | MobyGames HUD-only shots (health orb, breath orb) | .../886277/, .../886280/, .../886281/, .../234103/ (800x600, older capture) |
| S5 | Original manual, scanned with OCR (Internet Archive `Drakan_Order_of_the_Flame`) | https://archive.org/details/Drakan_Order_of_the_Flame (text: `.../download/Drakan_Order_of_the_Flame/Drakan_Order_of_the_Flame_djvu.txt`) |
| S6 | Prima's Official Strategy Guide (1999), Internet Archive `drakan-guide`; Fig. 3-6 *Game Interface* (leaf n61, p. 46), Fig. 3-7 *Inventory Inset* (leaf n62, p. 47), Fig. 5-81 *Map of the Islands* (leaf n195, p. 180) | https://archive.org/details/drakan-guide (page images: `https://archive.org/download/drakan-guide/page/n62_w2000.jpg` etc.) |
| S7 | Wikipedia (inventory is limited in size; weapons wear down) | https://en.wikipedia.org/wiki/Drakan:_Order_of_the_Flame |
| S8 | Cheat sites describing dragging items out of the "inventory box" | https://www.cheatcc.com/?p=96118 , https://www.supercheats.com/pc/drakan-order-of-the-flame/15652/duplicate-potions-or-crystals/ |

The Prima guide figures are greyscale print captures at 4:3, from the 1999 retail build. The MobyGames 1200x675 shots
are 16:9, so they come from a modern widescreen setup (probably a community patch; MobyGames does not say which).
They are the only colour references I found.

## Local reference images (not committed; `build/` is gitignored)

- `build/ootf-reference/moby-6121063-drakan-order-of-the-flame-windows-armor-and-items-are-being-used.png`: inventory open, in colour (main reference).
- `build/ootf-reference/moby-6147704-drakan-order-of-the-flame-windows-world-map.png`: map open, in colour (main reference).
- `build/ootf-reference/prima-guide-leaf62-inventory-fig3-7.jpg`: 4:3 inventory with numbered callouts (greyscale).
- `build/ootf-reference/prima-guide-leaf61-game-interface-fig3-6.jpg`: 4:3 HUD (map-change scroll icon, orbs).
- `build/ootf-reference/prima-guide-leaf195-islands-map-fig5-81.jpg`: 4:3 map with struck-through objectives.
- `build/ootf-reference/moby-6111519-...useful-items.png`, `moby-6114335-...arrow-types...png`, `moby-6121066-...former-owner.png`, `moby-405944-...treasure-chest.jpg`: HUD only.
- `build/ootf-reference/ia-Drakan_Order_of_the_Flame_djvu.txt`, `ia-Drakan_Guide_djvu.txt`: OCR text of the manual and the guide.

## Inventory

### Behaviour (manual S5, guide S6)

- **Key:** the manual's default for "Inventory" is the **End** key or a letter key. The OCR reads "End or ti" or "END nr t",
  which is most likely **I**, but I could not confirm this from the scan. The guide's controls table is the
  author's custom layout (Inventory = 2) and does not give the defaults.
- **Not a separate screen.** It is an overlay on the live 3D view. When you open it, "Rynn will spin around in place to face you"
  (manual): the third-person camera stays where it is and Rynn turns to face the camera. The level stays fully visible
  behind the panel, and Rynn stands centre-screen as a live **3D paper-doll**.
- It cannot be opened while swimming or while flying on Arokh (manual, guide).
- **Pause:** I found no statement that the game pauses, and nothing that says it keeps running.
- **Mouse:** a standard arrow pointer appears (Fig. 3-7 callout 8: a white arrow at the lower right).
  - Left-click an item to select it. "A red line will outline the currently selected item", and its stats
    appear "in the small window at the right side of the screen" (the Detail Inset).
  - Drag an item onto Rynn's 3D body to equip it. The item she was holding goes back into the grid, which needs enough free space.
  - Right-click or double-click a grid item to equip it as a shortcut. Double-clicking a weapon swaps it with the active weapon.
  - Drag an item out of the panel into the world (the space left and right of Rynn) to drop it on the ground.
  - Click and hold on the weapon or armour on Rynn's body to show its stats.
  - You can also drag items to new positions inside the grid.
- **Use without opening:** the H key drinks a health potion. Other keys cycle weapons.

### Layout

- **Grid: 8 columns x 3 rows** of square cells. Both S2 and S6 show this. There are no other panels: no separate
  equipment slots, gold counter, name plate or stats page. What is equipped is shown on Rynn's 3D model.
- **Items have multi-cell footprints** (guide). Potions, crystals and each arrow type take 1x1. Short and medium weapons
  take **1 wide x 2 tall**. Bows and two-handed weapons take **1x3**. Armour takes **2x2** (seen in S2). There is a
  Tetris-style free-space rule.
- **Items are 2D icons**, painted sprites drawn at a slight angle, with weapons diagonal or vertical. They are not 3D models.
- **Stack counts** are small light-grey/white numerals (about `#e0e0e0`) in the **top-left corner** of the cell, for example "38", "14", "3".
- **Position:** the panel is centred horizontally and sits **flush with the bottom screen edge**. The bottom border of the
  grid touches or is clipped by the screen edge, and there is no frame strip below the last row.
- **Frame:** an ornate cast-metal / gilded bronze frame with relief scrollwork, gold with dark recesses.
  - The top edge rises in a shallow arch to a **pale blue-grey oval cabochon gem** at top centre.
  - The two top corners have raised scroll "horns".
  - Each side has a **crescent / axe-blade wing** in bone-ivory with dark filigree that flares outward beyond the frame.
  - The same crescent motif frames the health orb (bottom-left) and the breath orb (bottom-right), so the whole HUD shares one style.
- **Detail Inset (stats window):** on the **right side, at or a little below mid-height**, not attached to the grid.
  - A dark, semi-transparent rounded rectangle with a thin **orange-gold double border**.
  - There are small inward notches (leaf points) at the middle of each side and at the corners.
  - A **bone-coloured crescent "horn" arcs over its top and right side**.
  - The text is left-aligned, small, light-grey **sans-serif** (Arial-like bitmap font), one stat per line:
    - Armour: `Banded Mail` / `Armor Rating: 40` / `Durability: 100/100`.
    - Weapon: `Lightning Axe` / `Damage: 30` / `Durability: 11/150` / `Speed: Slow` / `Range: Long`.
  - In Fig. 3-7 the low-durability line is printed **dimmer** than the others. It could be a different colour, but the figure is greyscale, so the colour is unknown.
- **Selection:** a thin (about 1-2 px) **pure red line that traces the selected item's silhouette**, not the cell rectangle.
  It sits on the armour in S2, and the inset shows that item.

### Proportions

At 4:3 (Prima Fig. 3-7, measured on the 840x650 crop of the screenshot):

| Element | Size and position as a fraction of screen size |
| --- | --- |
| Grid (8x3) | about 0.42 W wide x 0.20 H tall; cell about 0.052 W (about 0.067 H), square; left edge at about 0.295 W |
| Frame incl. gem, without wings | about 0.51 W wide; top (gem top) at about 0.75 H; bottom = screen bottom |
| Frame incl. crescent wings | about 0.57 W |
| Detail Inset | about x 0.74–0.99 W, y 0.48–0.74 H (including the horn) |
| Health orb with crescent | about x 0.01–0.16 W, y 0.81–0.99 H |

At 16:9 (S2, 1200x675) the HUD is noticeably smaller relative to the screen. It looks fixed-size, or at least not scaled with resolution.

| Element | Size and position at 16:9 |
| --- | --- |
| Grid | x 487–712, y 588–673 px: 0.19 W / 0.33 H wide, 0.126 H tall; cells 28 px = 0.041 H |
| Frame | top about y 565 (0.84 H); wings span x 437–765 (0.27 W) |
| Detail Inset | about x 1048–1185, y 448–562 (0.87–0.99 W, 0.66–0.83 H) |

For a port at 16:9, I recommend sizing the panel in terms of screen height. The original 4:3 look corresponds to a
cell of about **0.067 H**, and the widescreen capture uses about 0.041 H. Pick something between the two.

### Colours (S2)

| Element | Hex |
| --- | --- |
| Cell background | `#1b0800`–`#220500` (near-black, warm red-brown, very slightly mottled) |
| Inner grid lines (about 2 px, dark-centred with a lighter edge) | `#9c6715` / `#643c0c` |
| Grid outer edge (bright gilt line) | `#ffff9b` highlight, `#bc8f24` / `#ae801f` body |
| Frame metal | highlights `#f8c46e`–`#ebc063`, mid `#b87626` / `#946327`, recesses `#583f1f` / `#4a3a16` / `#130d0a` |
| Top gem | `#748085` grey-blue with a white specular highlight |
| Crescent wings / horns | bone-ivory, `#dfd8c0` / `#cac093` (sampled), with dark filigree `#725e3e` |
| Selection outline | `#d40000` (sampled `#cb0000`–`#d70404`) |
| Detail Inset fill | about `#282b1f` over a dark wall (semi-transparent black, about 60–70 % opacity; the opacity is a guess) |
| Detail Inset border | `#b87626` / `#946327` (orange-gold) |
| Detail Inset text | `#bbbdc0` (light grey) |
| Health orb | red `#860d1d`, glossy |
| Breath orb (empty) | `#181613` with a grey number in it |

## Map

### Behaviour

- **Key: Tab** ("Open Map", manual S5 controls table; the guide says "press the Tab key to check your map at any time").
  OotF has no M key for the map.
- **HUD hint:** a small rolled-parchment **"Map Change Indicator" icon in the upper-left corner** of the gameplay view
  flashes whenever the map is updated, either with a new level map or when "Rynn sketches any newly found locations or goals
  onto the map" (guide Fig. 3-6).
- **Objectives live on the map.** They are written in a hand-lettered ink font in the empty parchment margins, mostly
  along the top edge. Completed goals are **struck through with an ink line** (Fig. 5-81, for example "~~Find Rimril~~" and
  "~~Find the Bellhammer of Alwarren~~"). Newly found places appear as small ink pictograms (buildings, a circle glyph).
- **Zoom/pan:** none visible. Every capture shows the whole level map at once, filling the screen. I found no evidence of zooming or
  scrolling, and no evidence against it.
- **Player marker:** a small **steel-blue metallic arrowhead / dart with a round purple-violet gem in its centre**, about 0.06 H across.
  - In S3 it points toward the left (west).
  - In Fig. 5-81 a similar icon sits at the top centre.
  - It is drawn over the ink map; next to it in S3 there is a black ink sigil (a circle with a spiky surround).
  - Whether it rotates with Rynn's facing or updates live: **not established**. One still points sideways, which suggests it
    shows a heading, but that is not proven.
- **World behind:** **not visible.** The map is a full-screen image on **solid black** (`#000000`) with the torn parchment
  edges showing the black. Whether the game is paused underneath: no evidence.
- **Legend:** no legend or key box. The only fixed decoration is a **compass rose**: an 8-point star around a circular
  medallion with a Drakan glyph, drawn in ink. Its position changes per level map (top-left on the Rift Crystal map in S3,
  bottom-left on the Islands map in Fig. 5-81). It is part of each level's painted map art.

### Look

- **One sheet of torn, crumpled parchment** almost filling the screen. In S3 it covers about x 0.01–0.99 W and y 0.01–0.99 H.
  In the 4:3 guide figure there is a slightly wider black margin, about 3–5 %.
  - The edges are ragged and deckled, with small tears.
  - Crease and fold shading shows across the sheet.
- **Drawing style:** hand-inked in dark sepia/black.
  - Mountains are rows of shaded cone "bumps".
  - Water is fine hatched wave lines.
  - Buildings are drawn as top-down wall outlines.
  - There are swirl symbols for portals and whirlpools.
  - Everything is monochrome ink on parchment; the only colour is the player marker.
- **Objective text:** a hand-lettered, brush-like font with a mix of upper and small-capital forms ("Recover the Rift Crystal"),
  about 0.035 H cap height in S3, ink `#2a1e10`–`#655028` (anti-aliased).

### Colours (S3)

| Element | Hex |
| --- | --- |
| Parchment | `#c69d50` (shadowed) – `#d5a854` (mid) – `#dfb15a` (lit) |
| Mid-tone ink shading | `#a7833f` / `#c49a4c` |
| Line ink | about `#2a1e10` (the darkest antialiased cores) |
| Background | `#000000` |
| Player marker | steel blue `#3b3a81`–`#5a6f9a` with a violet gem and a white specular highlight |

## Original vs. patched versions

- **Original 1999 retail** (Prima figures, 4:3): the HUD panels are large relative to the screen (the grid is about 42 % of the width).
- **Modern widescreen setup** (MobyGames 1200x675 captures): the same art and layout, but the HUD is proportionally smaller.
  - The inventory still hugs the bottom centre.
  - The map parchment is stretched or fitted to 16:9 with almost no black margin.
- I found **no patch notes** that describe changes to the inventory or map UI in any official patch, the "10th anniversary" patch,
  or the Drakan Community Patch (https://archive.org/details/drakan-community-patch). Differences beyond scaling and aspect ratio are unknown.

## Gaps (no evidence found)

- The exact default inventory letter key. The manual OCR is garbled; End is certain, and I is likely.
- Whether the game pauses while the inventory or map is open.
- Map zoom/pan, whether the marker rotates with Rynn's facing, and whether it animates.
- The exact font family of the inset text (it looks like a small Arial-like bitmap sans) and of the map lettering (a custom hand-lettered face).
- The colour of the dim "Durability" line in the inset (the source is greyscale).
- The original-build colours. All hex values come from the widescreen MobyGames captures.
