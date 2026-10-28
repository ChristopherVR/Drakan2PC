# Controller prompts -> keyboard/mouse prompts

Status (2026-10-16, polished): implemented and verified in the running game (title menu -> Options -> Controller screen, button glyphs in text).
Branch `drakan-ui` of `tools/PS2Recomp` (worktree `tools/PS2Recomp-ui`, build dir `build/recomp_ui/`), libraries `runtime/hle` (override layer)
and `runtime/ui` (generator). Screenshots: `build/screenshots/prompts_*.png`.

Everything the player sees about the DualShock comes from four places. All four are rewritten **at load time from the original disc data**
(the override sees the entry the game requested), so no copyrighted asset is stored and the prompts follow `drakan_input.ini`.

| What | Where | How it is replaced |
|---|---|---|
| 8 button glyphs inside text (`\030`..`\037`) | `INTERFCE.TXD` 0x40/97 (plain UI font) | the 8 icon boxes are repainted with key caps / mouse icons |
| Controller diagram of Options > Controller | `INTERFCE.TXD` 0x40/124 | pad body erased, leader lines kept, key caps drawn at the line ends |
| "R1 / L1" page-switch tile of the inventory | `INTERFCE.TXD` 0x40/94 (icon atlas) | the two labels are repainted with key caps |
| Button names written as words (`L1`, `R2`, `START`, `SELECT`, "left analog stick", "directional buttons", "Controller") and the menu confirm glyph | `*.XDB` type 0x400 | string rewrite rules, all 5 languages |

## 1. How the game renders prompts (reverse engineering)

### 1.1 Text escapes and the glyph table (Ghidra, `build/recomp_ui/alldecomp/all_00267cf0.c`)
* Text layout/measure function `FUN_002865d8` (0x2865d8): a `\` followed by up to **3 octal digits** is one character code (`\033` = 27).
  Strings in the XDBs contain the *literal* characters backslash-0-3-3 (see `xdb_dump.py`), not a control byte. `\t` (9) = tab stop (30 px),
  0x20 = space.
* Code -> glyph: printable codes use `code - 0x21`; codes `< 0x20` and `0x7f..0xa0` go through the 35-entry table at **0x4eac30**
  (`0x8c 0x9c 0x8a 0x9a 0x9f 0x9d 0x9e 0x83 0x88 0x98 0x96 0x97 0x91 0x92 0x82 0x93 0x94 0x84 0x86 0x87 0x95 0x85 0x89 0x8b 0x9b 0x99`
  = Windows-1252 specials, then `0x17..0x1f` = 9 control codes) and are mapped to glyph index `0xde + position`, i.e. the glyphs after the Latin set.
* The 8 button glyphs (font 97, same order as the end of the table):

| Escape | Code | Glyph | Replaced by (action -> default binding) |
|---|---|---|---|
| `\030` | 0x18 | Circle | CIRCLE -> RMB (mouse icon) |
| `\031` | 0x19 | Square | SQUARE -> LMB (mouse icon) |
| `\032` | 0x1a | Triangle | TRIANGLE -> Q |
| `\033` | 0x1b | Cross | CROSS -> Left Ctrl ("CTL") |
| `\034` `\035` `\036` `\037` | 0x1c-0x1f | arrows left, right, up, down (boxed) | left stick directions A, D, W, S |

  (`\027` is a tiny glyph before Circle, not a button.) Used as e.g. "Press \033 to crouch" in `LEVEL1ON.XDB` (and German/Italian/Spanish inventory help
  `INTERFCE.XDB` 148: `\034\035\036\037` select, `\031` use, `\032` drop, `\033` hot slot).
* Font loader `FUN_00285aa8` (0x285aa8): glyph boundaries are **marker pixels** inside the font texture itself. Line `k` of the font is
  22 rows; the row below it contains pixels of palette entry 7 (`01ffff00`) at the left edge of every glyph (row 35: the 4 arrows at x = 3, 26, 49, 70 (+91 end);
  row 57: the 15 specials incl. circle 136, square 161, triangle 185, cross 214, end 240; row 79 ... = text lines). The loader compares **palette indices/colours of
  texels**, so a texture edit must not renumber the palette and must not touch the marker rows: `TxdEdit` only writes into palette slots no pixel uses any more.
  The textures are stored **upside-down** (row 0 = bottom of the picture); glyph lines therefore run bottom-up.
* Font 97 icon boxes (stored coordinates, x y w h): circle 136,36,22x21; square 161,36; triangle 187,36; cross 215,36 (all 22x21);
  arrows: the original art is only 15x13 / 13x15 px at 5,18 / 28,18 / 51,17 / 72,17, **but the glyph cells are much bigger**: the marker pixels of row 35 sit at x = 3, 26, 49, 70 (+91) and a
  line is 21 content rows high (stored rows 14..34 below the marker row 35, like the 21-row button icons). The generator therefore draws the W/A/S/D caps as **19x21 px** at x = 5, 28, 50, 71 (stored y 14)
  with 1-2 px margin to the neighbour glyph, which allows 2x letters (~11x14 px, embolden) instead of 1x. No relocation was needed, the markers stay untouched. Font 96 is the decorative menu font (no button glyphs).

### 1.2 Controller screen (Options > Controller)
Texture 0x40/124 (256x256, 8 bpp, 120/128-alpha palette) is the PS2 pad outline **with the leader lines**; the labels ("Jump", "Crouch",
"Inventory/Map", ...) are plain text from `INTERFCE.XDB` ids 7..28 (group names "Controller Text"/"Inventory Text"/... in the 0x330 entries) drawn by the
game at fixed positions that line up with the line stubs left/right of the texture. Anchor points of the lines (upright texture px):

| Button | Leader end | Label (standard scheme) |
|---|---|---|
| L2 / L1 | (50,51) / (47,68) from the left | Next Target/Strafe / Call Arokh/Use |
| R2 / R1 | (208,51) / (212,68) from the right | Toggle Targeting / Quick Inventory |
| Select / Start | (105,131) / (151,134) | Inventory/Map / Game Menu |
| Triangle / Circle / Cross | (217,114) / (233,132) / (212,154) from the right | Jump / Use Magic/Parry / Crouch |
| Square | (190,138) via (190,160) | Attack |
| D-pad | (50,134) from the left | Special Moves |
| Left / right stick | (84,182) / (165,182) | Turn/Move / Pitch/Freelook |

The generator erases the pad body (keeps blue leader pixels and the long brown edge stubs), removes the two stick rings and draws key caps at these anchors
(WASD cluster at the left stick, arrow keys at the D-pad, a mouse at the right stick). The label text is not scheme-aware in the texture (the game picks
labels per scheme), the key caps always show the DualShock-button -> key mapping, as the labels are placed per button.

### 1.3 Other
* `INTERFCE.TXD` 0x40/94 (128x128, 32 bpp) is the inventory icon atlas; the stone tile at upright (49..85, 85..128) shows "R1" above "L1" (inventory
  section switching: "Press L1 and R1 to access other sections of the Inventory").
* Page-switch buttons (2026-10-24): the two arrow sprites show only part of the tile, upright texels
  right x 56.1..71.3 / y 90.4..105.7 (tip to x 76), left x 58.5..74.1 / y 106.3..123.3 (tip to x 52)
  (`kPageTileNext` / `kPageTilePrev` in `ui/FontAtlas.h`). The native caps fit these windows and the old "R1"/"L1"
  ink is painted out. In game the tile is an HD overlay: `buildPageTileOverlay` draws "PgDn"/"PgUp" caps at 4x from
  Arial Bold, and `gsr::registerRuntimeOverlay` keeps the rest of the atlas, the section icons, from the HD pack image
  of the disc texture. The hook (`PromptOverride::Generated`) passes the id, the original pixels and the captions.
* Tex 102/103 (box with arrow / empty box) are check boxes, not prompts. Only `INTERFCE.TXD` carries the fonts (ids 96/97) and the pad texture.
* **Search for further button art (done, nothing found)**: all 3282 textures of all 35 ENGLISH TXDs (common, 8 levels incl. the `*TS` tile sets, worldcon, music) were exported as contact sheets
  (`analysis/scripts/ui_txd_sheets.py`, `ui_txd_small.py`, sheets in `build/ui_polish/sheets/`), plus a colour scan for the PS2 icon colours sampled from font 97
  (`ui_txd_colorscan.py`: only one false positive, a DESERTON terrain tile). Candidates inspected: INTERFCE 28/59-63/101-103/117/118/126-128/194-198 (cursor, debug font, sword, arrows, frames, check boxes),
  SYSTEM 5450 (red-triangle "A" compass marker), FX 5952 (debug "TB21"), loading/title art 72-77/183-187 (title, promo, copyright). Level signs (LEVEL1ON 818-821) are painted runic text, not buttons.
  The 19 textures LEVEL0TS 1-18 and one more use a compressed layout the decoder rejects (terrain tiles). All level TXDs are identical in the 5 language trees except `LEVEL*TS.TXD`
  (terrain/map tile sets); XDBs are identical in all trees (each XDB holds all 5 languages). So **the only controller art on the disc is font 97, pad 124 and tile 94**.
* Text: `INTERFCE.XDB` 35/36 ("\033 Load, START to Exit"), 37/38, 73/74/85 (memory card prompts: "Press \033 to continue"), 130/138/141 ("Controller"),
  148 (inventory help), 157 ("directional buttons ... START button"); `LEVEL1ON.XDB` tutorial hints 18-52 (glyph escapes **and** words: "the L1 button", "R2",
  "SELECT", "left analog stick", "right stick") in all 5 languages (L1/R1/R2/SELECT/START are the same tokens in every language).

## 2. File formats (implemented in `analysis/scripts/txd_decode.py`, `xdb_dump.py`; C++: `runtime/ui/src/Txd.cpp`, `runtime/hle/src/ResourceOverride.cpp`)

TXD texture (SRSC type 0x40; all 97 INTERFCE textures decode with exact size match):
```
+0 u32 width  +4 u32 height  +8 u32 stride_bytes  +0xc u32 bpp (8/16/24/32)  +0x10 u32 flags(0,1,2,4,7,8)  +0x14 u32 0xffffffff ... (0x40-byte header)
bpp 8 : +0x40 {u32 kind, u32 count, 0, 0}; kind 1: 256 x B,G,R,A (alpha 0..0x80), kind 2: 256 x u16 ARGB1555 (tex 9 only); then w*h palette indices
bpp 16: ARGB1555 (R low bits)      bpp 24: R,G,B      bpp 32: R,G,B,A (alpha 0..0x80)
rows are stored bottom-up; pixel data is linear (not GS-swizzled, the game uploads it with plain IMAGE transfers)
```
Palette byte order is **B,G,R,A** (R/B swapped compared with the 24/32-bit formats).
Sizes seen: 2128 (32x32x8), 5200 (64x64x8), 17488 (128x128x8), 66640 (256x256x8), 196672 (256x256x24), 65600 (128x128x32), 32832 (128x128x16).

XDB text (SRSC type 0x400): 8 slots; slot = `u16 len` + `len` bytes with `len = roundup4(chars + 2) - 2` (NUL terminated only when padded). Slots 0..4 =
English, German, French, Italian, Spanish; 5..7 are empty spares. Type 0x330 gives the group name of the same id. All 694 text entries of the disc decode
and re-encode byte-exact. Dump: `python analysis/scripts/xdb_dump.py FILE.XDB [lang|all]`, textures: `python analysis/scripts/txd_decode.py FILE.TXD outdir [id]`.

## 3. Resource override layer (`runtime/hle`, section "Resource overrides" of its README)

`hle::ResourceService::addOverride(std::shared_ptr<hle::IResourceOverride>)`; for every request the service builds a `ResourceId {file, path, type, id, sub}` (`sub` is
the matched index entry's sub, never the 0xFFFF wildcard) and asks `wants()`; if any override wants the entry, the **whole entry** is read, passed through
`apply()` in registration order and then sliced with the request's offset/length, so a replacement may have a different size. Provided overrides:

* `DirectoryOverride`: `overrides/<FILE>/<type hex>_<id>_<sub|x>.bin` replaces the entry verbatim (`overrides/INTERFCE.TXD/0040_97_0.bin`; any file can be an HD texture,
  a model, a sound ...). A user file wins over the generated prompts (`PromptOverride::wants` returns false for it).
* `XdbTextOverride`: rule callbacks (the prompt rewriter) and/or `overrides/<FILE>/text.ini` lines `<id>[.<en|de|fr|it|es|*>] = text` (escapes `\\`, `\n`, `\r`, `\t`; to
  write the game's glyph escape `\033` put `\\033`); `text.ini` is applied after the rules.
* Tests: `runtime/hle/tests/override_test.cpp` (18 checks, no ISO needed) + the existing `hle_test` (509 checks on the ISO, unchanged and green).

`runtime/ui` (`drakan_ui`): `drakan_ui::install(res, Options::fromEnvironment())` registers [user overrides] + [prompt textures] + [XDB text]. Environment:
`DRAKAN_INPUT_INI` (default `drakan_input.ini` next to the exe, then the working directory; missing -> built-in copy of the default bindings),
`DRAKAN_OVERRIDES` (default `overrides/` next to the exe), `DRAKAN_UI_PROMPTS=0` (keep the original PS2 prompts, only the override folder is active).
The wiring in the game is 3 lines in `drakan_overrides.cpp` plus a `drakan::ui` link in `ps2xRuntime/CMakeLists.txt`; the log shows `[drakan] ui: prompts keyboard/mouse (bindings: ...)`.

## 4. Binding -> prompt mapping

The generator reads the `[bindings]` (and `[menu_bindings]`) sections of `drakan_input.ini` itself (no dependency on the input module): for each DualShock action the **first source
listed** is the prompt (`SPACE = L1 / E = L1` -> "Space"). Key cap text is at most 3 characters (`SPC CTL SHF TAB ESC ENT BSP`, `F5`, `N5` numpad, `LMB RMB MMB M4 M5`, `WHU/WHD` wheel; unknown keys use their
first 3 letters); sentences use the long names ("Left Ctrl", "Right Mouse Button"). Direction glyphs use `LSTICK_*` (fallback `DPAD_*`). Mouse buttons are drawn as a mouse with the
button highlighted in the font glyphs and as text caps in the diagram. Unbound actions show a "?"/"-" cap.
Menu confirm prompts: the escape `` in the menu strings `INTERFCE.XDB` 35, 36, 73, 74, 85 is written out as the **menu** binding of CROSS ("Enter") because the
glyph itself can only show one key.

Text rules (`runtime/ui/src/TextRules.cpp`), all five languages, tokens `L1 R1 L2 R2 SELECT START` (+ French "Select") and `` in the strings above. Names are localised per language
(`SPC` Leertaste / Espace / barra spaziatrice / barra espaciadora; `CTL` Strg; `SHF` Umschalt/Maj/Maiusc/Mayus; `ENT` Eingabetaste/Entree/Invio/Intro; `ESC` Echap in French; mouse buttons
"linke Maustaste" / "le bouton gauche de la souris" / "il tasto sinistro del mouse" / "el boton izquierdo del raton"), and the sentence noun is made grammatical:

| Lang | Source | Result (key E) |
|---|---|---|
| en | "the L1 button" / "Press L1" | "the E key" |
| de | "die L1-Taste", "mit der L1- oder der R1-Taste", bare "" | "die E-Taste", "mit der E-Taste oder der V-Taste" (dative: "mit der linken Maustaste"), "die Eingabetaste" |
| fr | "Appuyez sur L1", "la touche R2", "sur Select" | "Appuyez sur la touche E" (mouse: "le bouton gauche de la souris", the old noun "la touche" is dropped) |
| it | "Premi L1", "il tasto SELECT", "premendo L1 e R1" | "il tasto E", "premendo il tasto E e il tasto V", "la barra spaziatrice" |
| es | "Pulsa L1", "el boton START", "Manten pulsado R2", "los botones L1" | "la tecla E", "la tecla Esc", "Manten pulsada la tecla F", "las teclas E y V" (the disc string 148/4 is truncated at "L1": completed to "L1 y R1") |

Hint bars (`INTERFCE` 35-38: " Load, START to Exit") stay bare ("Enter Load, Esc to Exit", "Eingabetaste Laden, Esc zum Verlassen"). Movement: "left analog stick" -> "W A S D keys"
(en only, the string exists only in English), "right stick" -> "mouse", "directional buttons" -> "arrow keys" (es: "botones de direccion" -> "teclas de direccion"; de/fr/it
"Richtungstasten / touches directionnelles / tasti direzionali" are already correct for a keyboard). Options menu: "Controller" -> Controls / Steuerung / Commandes / Controlli / Controles,
"Controller Configuration" -> Control Scheme / Steuerungsschema / Configuration des commandes / Configurazione dei controlli / Configuracion de controles.
Left alone on purpose: `143 Vibration` (still valid for a gamepad; fr/it/es mention "manette/controller/mando analogico"), `147 Dual Control` (scheme name), "Select Skill/Location" (verb).
Full before/after list of the 110 rewritten strings: `build/ui_polish/text_report.txt` (regenerate: `ui_preview <iso> out ini text`).
Preview without the game: `runtime/ui` target `ui_preview <iso> <outdir> [ini] [text]` writes before/after PNGs (3 textures) and, with `text`, lists every rewritten string.

## 5. Open points / notes for the input owner
* The default ini comments name Q = "sidestep" and Space = "jump/use", but the game's own Standard scheme (shown on the controller screen) is Triangle = **Jump**, Cross = **Crouch**, Square = Attack,
  Circle = Magic/Parry, L1 = Call Arokh/Use. The prompts follow the ini literally ("Press Q to jump"), so rebinding Space to TRIANGLE (and Ctrl to CROSS stays) makes the prompts read naturally.
* Inventory/store/load menus use Square/Triangle/Cross (use, drop, hot slot) but `[menu_bindings]` only maps CROSS/CIRCLE/START/SELECT; the glyphs there show the *gameplay* keys.
* The 4 direction glyphs (`\034`..`\037`) are tiny (13-15 px boxes) so single letters are drawn at 1x; the other icons use 2x letters.
* Other control schemes (Alternate, Dual Control) change the label placement text, not the texture; the key caps stay attached to the button positions.
* Not done: live regeneration without restart (prompts are generated when the game requests the resources, i.e. once at start / level load), `\027` glyph, memory-card strings 67-72 (they name no buttons).

## 6. Polish pass (previews: `build/ui_polish/`)
`build/ui_polish/drakan_input.ini` = the new defaults (Space=TRIANGLE, Ctrl=CROSS, LMB=SQUARE, RMB=CIRCLE, E=L1, V=R1, F=R2, R=L2, Tab=SELECT, Esc=START, menu Enter/Space confirm).
Before/after PNGs: `after/tex97_orig.png|tex97_new.png` (font), `after/tex124_*` + `compare_controller_screen_tex124.png` (controller screen), `after/tex94_*` + `compare_inventory_tile_tex94.png`
(inventory: V over E replaces R1 over L1), `compare_font_glyphs_old_vs_new.png` (old 13-15 px vs new 19x21 WASD caps; `before/` = previous generator). Known cosmetic point: on the controller screen the Square
leader (LMB) runs under the CTL cap. Item list: 1 enlarged arrow glyph caps, 2 no further button art exists, 3 localised text rules, 4 verified in the preview, 5 vibration wording left as is.

## 10. Memory card wording -> PC save wording, menu-layer prompts (controls branch, 2026-10-17)

* `memcardText()` in `runtime/ui/src/TextRules.cpp` replaces the INTERFCE.XDB strings 6, 67, 70, 71, 72, 74, 107-111, 129, 167, 170, 171 in all five languages (before / after list: `build/ui_polish/memcard_text_report.txt`; Load Game screen "Reading save data...": `build/ui_polish/memcard_text_load_game.png`). The Cross glyph escape of id 74 keeps going through the normal confirm-key rewrite.
* The disc UI texts (INTERFCE.XDB) that name L1 / R1 (inventory help: "Press L1 and R1 to access other sections") now use the **menu layer** bindings (shipped default: Page Up / Page Down first) instead of the gameplay layer, where L1 / R1 are E / Q, which in the inventory are Square / Triangle. The inventory page tile (tex 94) does the same. The Controller screen diagram stays on the gameplay layer.
* The built-in default bindings of `runtime/ui` now match the shipped `drakan_input.ini` (Space = Triangle ...).


## 11. Linux/browser bindings and prompt activation (2026-10-26)

Portable hosts now publish keyboard scancodes, mouse and controller snapshots to
the shared input binding module. The in-game generator reads that same
`drakan_input.ini`; the browser no longer forces `DRAKAN_UI_PROMPTS=0`. Enter is
confirm, Escape is pause/back, and configured gameplay bindings drive pad/command
input rather than the old hard-coded Z/X/C/V fallback. Physical controls and idle
direct-camera ownership remain separate so focus loss clears input without
recentering camera pitch. Pointer capture stays on the portable window thread.

The browser Controls screen visibly shows keyboard/mouse caps and no console-pad
body in `build/browser-keyboard-controls.png`. This changes the game's own
resources and wording, not the browser shell. Windows generation is unchanged.
Native-resolution text/geometry and the remaining portable settings/host-screen
work are not claimed equivalent to the Windows HD renderer yet.
