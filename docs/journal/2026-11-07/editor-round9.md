# Editor items from the outstanding list (round 9)

## Shop: the editor's new shop sells in game (proved)

`editor/scripts/vendor-pack.ts --new-shop` (committed) runs "Add shop here" headless, then edits that new shop with the usual
options. `scripts/harness/vendor_shop.py --new-shop --play-dir build/ed-a/play --shop 4 --no-buy` / buy run on a private play folder:
Surdana shop 4 (the new Alchemist, keeper handle 4676) opened as screen 21 and sold the first item, gold 1000 -> 900.

**Correction (see `shop-stock-limit.md`): this finding was a harness artifact; the edit had been written to the Blacksmith, not the new Alchemist. There is no stock limit and a weapon sells fine from an Alchemist.**

Finding: adding an Iron Mace (`--add RESOURES:8569`) to the new Alchemist made the shop screen fail (the game logs `stock=4`
although five entries were written, the shop UI never draws its items and the harness never gets past `sync screen 21`; two attempts).
The same shop without the extra entry works. A shop class seems to hold a fixed number of stock slots, or a weapon in an alchemist's
list is rejected. Not chased; the editor's Vendor form should be checked for a slot limit per shop class.

## New level exit

Already proved in round 8 (`editor-round8.md`, "New exit fires in the game"): the exit handler got message 0x20 on the new exit and
Level 1E loaded. The outstanding list still said "not run"; it is run. Not run: the project map chain with a second level copy.

## ui-sweep

Rebuilt the app and ran `npm run ui-sweep -w @drakan/editor-app`: 192 states in 3 sizes, 0 palette entries missing, 0 findings.

## Save As in the desktop app: NOT done

The debug desktop exe starts and exposes the page over WebView2 debugging (a small CDP helper worked). The remaining step is
Ctrl+Shift+S (the native menu accelerator) and the native "Choose where to create the project folder" picker, which needs the
real foreground window. Another session's full-screen game was in front, so keystrokes could not be aimed safely and the attempt
was stopped (one Ctrl+Shift+S was sent while that game may have had focus; no effect seen). Redo it on a quiet desktop: start the exe with
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9340`, open a project, press Ctrl+Shift+S, type a name in the ask
dialog, pick a folder in the picker, then check the new folder, the toast and that closing the window saves into the copy.

## Not touched

A11y with a real screen reader, the campaign wizard focus trap in a browser, deeper 1920x1080 polish, quota overflow, Play with
two editors: unchanged from the outstanding list.
