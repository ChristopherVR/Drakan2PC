# World panel verification (commit 7118bf2 follow-up)

Runs: `build/harness/world-panel*`, `world-sl1`, `world-sl2` (own card dirs under build/, a copy of the play folder
`build/harness/world-exe` so a concurrent build of `dist/DrakanPC` does not interfere; dump files therefore land in
`build/harness/world-exe/dev/world`, the same `exeDir/dev/world` rule). Driver: `settings_check.Driver` command inbox
(`toggle` = F1, `shot` = overlay screenshot) plus `PostMessage` mouse clicks on `DrakanWindow` for the panel widgets.

1. Panel visual: the window was 760x520 px at 4K (about 20% of the frame, text clipped). Now sized from the font and
   display. Filters, player highlight (orange when selected, green otherwise), distance, details, Dump JSON verified by
   screenshots (has HP: 4602 to 108 rows; Dump JSON writes `world_Surdana_000286.json`). Label `actor` for type 4 removed.
2. Save / load: F5 / F8 with `DRAKAN_WORLD=1` and `=2` do not crash; `[world] sample status` log lines (new) show
   recovery. `=2` dumps work at the title (`Intro` level has a player, 375 entities). The pause-menu Save Game / Load
   Game screens were not driven (only the quick pair).
3. USA: not enabled, see `docs/reference/world-model.md` section 7 (no USA ELF/ISO/RAM in the tree).
4. Creature HP: found in plain ints at `inst+0x24c/+0x250` for types 6 and 7 (the combat harness already read it);
   `HpKind::Creature`, tests in `world_test` and `world_ram_test` (`pal-combat-melee1/ram_hit3.ram`: 137 creatures).

Tooling note: `dist/DrakanPC` could not be refreshed while another session ran a game from it (Access denied clearing
`avcodec-61.dll`); `build/final-game/bin/drakan.exe` holds the new binary, the next build refreshes dist.
