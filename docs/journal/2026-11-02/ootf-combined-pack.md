# One pack for the campaign and the arenas

Problem (see `ootf-unproven-creatures-chains.md`): a campaign pack and an arena pack converted separately collided in 67 paths
(`RESOURES.MOD/.TXD`, `WORLDCON.MOD/.TXD`, `PROCTEES.TXD`, new class ids from the same first free id), so the later pack won.

Fix (formats `ff2a03d`): `convertOotfLevels` keeps `Multiplayer/*` levels (`isOotfArenaPath`) out of `planCampaign`. Arenas get the default
`OotF - <name>` titles, no exits, and sort after the chapters in `mod.toml` `levels` (the first entry stays the campaign start). Running
`ootf-convert.ts --campaign 1` with no level paths now yields one pack: 13 chapters + 10 arenas, `maps/*.ini` for the arenas, shared assets
allocated once, so nothing can conflict.

Check (build/ootf-run/combined, ~1 min convert): `DRAKAN_GAME=d1` loads `OotF 00 - Intro` with control; `DRAKAN_START_LEVEL="OotF - Relic (ground)"`
loads Relic (playable, netplay/co-op listeners up). No crash or conflict lines; only one pack is mounted. Not done: a full arena match and a
level-to-level campaign walk from this combined pack (the coop campaign harness converts its own pack; point it at the combined one to cover it).
The harness recipe path was relative to the wrong cwd in my ad-hoc runs, so those runs ended on `PS2X_MAX_SECONDS`, not a scripted quit.
