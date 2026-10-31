# On-screen checks: World panel HP column and ghost spawn/destroy - 2026-10-31

Build: a copy of `build/final-game/bin/drakan.exe` (2026-10-31 11:35:39 +1000, after commit 954e458 at 11:27:40) in
`build/ui-check-play` (top-level files of dist/DrakanPC + the exe; the copy was taken once at the start). Run with
`DRAKAN_WORLD=1 DRAKAN_GHOST=1`, empty card `build/ui-check-card/mc0`, run folder `build/ui-check-run`. Level 1A - Surdana.
Driver: the settings-harness inbox (`toggle`, `shot`) for F1 and screenshots; clicks by PostMessage to `DrakanWindow`.
Earlier stop note (binary "older than 954e458") was superseded: the 11:14 binary was replaced by the 11:35 build.

## Results
1. Surdana gameplay reached; F1 opens the World panel (entities 4602, player 00a4cf10 HP 100/100).
   Screenshot `build/ui-check-run/world0.png` (no filter).
2. has-HP filter on: 245 rows. Creature rows show HP cur/max, e.g. `7 creature 1009d 100/100`, `7 creature 10031 10/10`,
   `10 weapon 1000e 30/30`; the player row shows `100/100`. Screenshot `build/ui-check-run/world_hp2.png`.
3. Ghost Spawn: entities 4602 to 4603, panel line `ghost 1 handle 00761fa0 ref 3002 res 2`, JSON dump
   `build/ui-check-play/dev/world/world_Surdana_006958.json` lists two class 0x10009 rows (player 0xa4cf10 with hp 100,
   ghost 0x761fa0 with no hp field). Screenshot `build/ui-check-run/ghost_spawned2.png`.
   Note: the ghost has no HP, so it does not appear under has-HP; the 245-row count stayed the same.
4. Ghost Destroy newest: entities back to 4602, ghost row gone from the panel, JSON `world_Surdana_008090.json` has one
   class 0x10009 row (0xa4cf10). Screenshot `build/ui-check-run/ghost_destroyed.png`.

Notes
* Dump JSON adds a status line that pushes the buttons down about 43 px (client coordinates; click at Spawn y=526 after a dump).
* Window client is 2560x1440 at 144 DPI; screenshots are 3840x2160. Clicks are client coordinates (screenshot px / 1.5).
* Not checked: the player row's `hp` in the first screenshot is 100/100 at all three samples (no damage taken during the run).
* The game instance was closed (taskkill). No code changed, nothing committed.

Reviewer note: in `world_hp2.png` a weapon row (type 10) shows `-1/-1` under the has-HP filter. That value means
"no durability" and should be hidden or filtered out (`drakan_world.cpp` / `HpKind::Durability`). Not fixed yet.
