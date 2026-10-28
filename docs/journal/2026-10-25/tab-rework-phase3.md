# Tab rework phase 3: the other pages, fonts and sounds — 2026-10-25

Follows `tab-rework-phase2.md`. Reference: `docs/reference/mouse-menus.md` section 13 (native side bullets "Skills
working copies", "Character page icons", "Map page", "UI sounds", "Shops stay native"). Commits `b87bd4a` (gameui),
`f987e79` (renderer), `2043b57` (font), `773b9db` (game glue).

## Findings

* **Silhouettes behind the icons.** The faint diagonal swords behind the club and the dagger were the screen's own
  category silhouette, drawn under every item at 40 alpha (the Melee glyph is a diagonal sword). Images now carry it as
  a `fallback`, drawn only when no crop exists.
* **Equipped row.** It already followed the slot `+0` flag: after equipping the bow (`tab3-base/inv_bow.png`), the club
  and the dagger (`tab3-v3`) the row and the card's "Equipped" changed with the native state. Nothing to fix.
* **Header under the fps meter.** Rynn's name and level moved to a nameplate at the bottom of her preview column; the
  header keeps the page tabs and the gold.
* **Skills.** The first live run spent points natively (the apply box listed Melee 2, 2 unspent) while the host showed
  nothing and reported "No skill point could be spent": `4468e0` changes only the Skills section's working copies
  (`+0x5c` points, rows at `+0xfc`, `0x1b4` each: level `+0`, level on entry `+4`); the stats block changes on "Yes".
  Five RAM dumps around spend / spend / spend archery / return (`build/harness/tab3-v2/ram_k*.ram`) confirmed the
  layout; the reader now takes the copies while the Skills section is current. `tab3-v3`: spend Melee, spend and return
  Archery, Q -> apply box -> Yes -> Character shows Melee 2/8 and 2 points left.
* **Spells.** The Slot 1 Surdana save knows no spell and has 646 gold: Jade's sells Fireball for 270, Ice Blast costs
  540 and needs Magic 2, so the runs buy Fireball through `DRAKAN_DEBUG_OPEN_SHOP=2` and a click (one known spell).
  Focus selects natively (as the native arrows do); Confirm summons (`cast-or-summon(0) -> confirmed`). No save with
  several spells was available, so browsing between known spells is not run live.
* **Map.** Map captions are resident only once the native map has shown them (the reader never loads strings): two of
  three Surdana markers read "Location n". The native map's GUI units are not square (the page is compressed by
  `hudScaleX`), so the drawn map is 4:3 on screen; the host fits it with that aspect and rings the focused marker
  (checked against the native markers at 16:9 and 4:3).
* **Shops** stay native; reasons in the reference.
* **Font.** Gelasio (OFL, no Reserved Font Name, metric-compatible with Georgia) replaces the Windows Georgia /
  Palatino lookup: variable font instanced at 400 / 700 and subset to Latin-1 / Latin Extended-A with fontTools,
  compressed into `third_party/fonts/gelasio/*.inc` with ImGui's `binary_to_compressed_c -base85` (built in
  `build/b2c`). Libre Baskerville was rejected (Reserved Font Name: a subset would have to be renamed).
* **UI sounds.** The overlay's settings host keeps its clicks silent (a nested play can block in the sound manager).
  The Tab screen plays its tick at the screen's draw message instead, the play as a guest invocation (the InvDrawBatch
  pattern); `tab3-43` logged `ui sound 2a: id 12004d via 1b7f30` with audio on, and the run went on normally. Audible
  output was not checked (harness run).

## Runs (1920 x 1080 output on the iGPU display, about 6-8 fps; save `build/harness/polish-save/mc0`)

| Run | What | Result |
| --- | --- | --- |
| `tab3-base` | before the changes: shop, Tab, all pages | silhouettes, header overlap, empty Map panel, skills not shown |
| `tab3-v1` | first build: Character, Spells, Skills, apply box, Map | map image placed; skills refused (working copies); Character alternatives not reachable |
| `tab3-v2` | RAM dumps on Skills, Character, Map with aspect | skills layout found; map fills the panel, ring on the marker |
| `tab3-v3` | shop, equip club, Character compare and equip dagger, Skills, apply box, Spells, Summon | all confirmed (`char_alt.png`, `char_equipped.png`, `sk_archery.png`, `sk_returned.png`, `char_after_points.png`, `summoned.png`) |
| `tab3-43`, `tab3-219` | `DRAKAN_WS_ASPECT` 4:3 / 21:9: Inventory, Character, Spells, Map | icons, Character crops and the map aligned; at 4:3 the game picture's pillarbox cuts the left of Rynn's column |

## Not done / next

* Spell icons: the native Spells section's icons are not cropped (they draw over the world, not over a black fill);
  known spells show a star glyph.
* Map: no native marker steering from the host list (the native selection glow stays where the native map has it),
  captions of unseen markers are placeholders.
* Shops (Merchant page) not routed; the Merchant page code is unused while shops stay native.
* Only game-picture aspects were run on the 16:9 output; a 21:9 or 4:3 *output* (window) and 4K were not available.
