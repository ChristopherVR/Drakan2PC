# Multiplayer shared by both games, co-op moved to the campaign (2026-11-01)

Directive: one Multiplayer menu for Drakan 1 (Order of the Flame) and Drakan 2, arena maps from either game selectable; co-op is
shared too but is not a Multiplayer option, it belongs to the campaign start.

* `gameui::mp::MpScope` (`Multiplayer.h`): `Versus` (title Multiplayer row; no Co-op mode card, the Game Type row is fixed, arenas),
  `Coop` (title New Game > Co-op; Game Type fixed to Co-op, campaign levels), `Any` (old behaviour, tests). `selectMode` refuses modes
  outside the scope; `openLobby` picks the scope from the session's mode so a co-op lobby resumes in the Co-op scope.
* Title: `TitleConfig::showCoop` (the host glue turns it on with `showMultiplayer`): New Game opens a Solo / Co-op card
  (`TitlePage::NewGameChoice`, `TitleRow::NewGameSolo/NewGameCoop`, appended ids). Solo sends the ordinary `NewGame` action; Co-op opens
  the Multiplayer pages in the Co-op scope. Netcode, lobby and the co-op runtime are untouched (the match config still carries the
  Coop mode; harness shortcuts `DRAKAN_NET_*` unchanged).
* Map tag: `ArenaDef::game` (`game =` key in `[arena]`, `Drakan 1` / `Drakan 2`; `d1`/`ootf`/`d2` accepted), `MapEntry::game`,
  `LevelEntry::game`; the Host map list shows the tag and the preview shows GAME. Defaults: built-in and the player's `mp_maps.ini` =
  Drakan 2; maps of a pack with `campaign = "ootf"` = Drakan 1 (`ArenaCatalog::add(..., defaultGame)`); other packs untagged.
* OotF maps: `ootfArenaFromPc` (editor-core `ootf.ts`) turns the Rynn launch pads of a `Multiplayer/*` PC level into spawns and
  `buildOotfPack` ships `maps/<mangled>.ini` (`OotfLevelInput.arena`); the import dialog fills it. Not verified in game: no converted
  Multiplayer pack was installed in this tree (see the final report); the parser/serializer round trip is unit tested.
* Editor: `ArenaDraft.game` round-trips through `parseArenaIni` / `serializeArena` so editing an OotF map keeps its tag.
