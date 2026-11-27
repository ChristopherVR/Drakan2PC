# Modern inventory while riding Arokh

Report: Tab while flying took a few seconds, then showed the original inventory, very zoomed in. The modern screen never
appeared.

Cause: the gameui reader (`runtime/gameui/src/Reader.cpp` `findRoots`) returned `ReadStatus::Dragon` as soon as the
controlled actor (ctx+0x4b0) was Arokh, before looking at the open inventory screen. The snapshot was never valid, so the
overlay never drew (`tabDraw` needs `native.valid`), and the EE watchdog put the native page back after 2.5 s
(`watchdog: host UI not drawn for 296 frames` in the user's drakan.log, Wartok Canyons). The zoomed look was the native
page with the camera's open animation zoom.

RAM (Ravenshold, mounted start, `build/harness/dragontab1/ram_open`): screen 0x15 +0x14c is Rynn's player data
(0xc96120, not the dragon's 0x9ed360), and its controller holds her items. The native inventory works on the dragon; only
Use / spells are refused natively.

Fix: the reader takes the player from screen 0x15 first and reports `Dragon` only when there is none. While a native box
(the first-open help) is stacked on the inventory, the mounted player data is kept in `ReaderState::mountedPlayer` and
dropped when the inventory closes. `native.onDragon` is still set.

Evidence (`build/harness/dragontab2`, Ravenshold on Arokh, the user's drakan.ini): `host screen 0 bound`, `snapshot ok: 2
items`, the help box comes and goes, `open_mid.png` shows the modern Inventory page, the Select close ends the screen and
`closed.png` shows the normal flight camera. gameui_test / gameui_screens_test cover the mounted, box and closed cases.
