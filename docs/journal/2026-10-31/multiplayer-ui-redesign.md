# Multiplayer UI redesign: full-screen two-pane front end and HUD - 2026-10-31

Request: "The UI needs a lot of work for multiplayer and bigger - Order of the Flame type style big, with preview etc."
Layout inspiration only (the section 2 facts of `ootf-multiplayer-ui-spec.md`); no OotF art or code. Owner of the code:
`runtime/gameui` (`Multiplayer.{h,cpp}`, std only) and the glue in `drakan_settings_host.cpp`.

## What changed

All pages are laid out on the 1920 x 1080 design canvas (`computeFrame`: scaled by the viewport height, so 1080p, 1440p and
4K are the same layout, pillarboxed on 21:9). Each page has a big title with the gilded rule and crest, a left working pane
and a right preview / details pane. The row model (`RowId`, `rows()`, focus, `MpAction`s) is unchanged, so keyboard,
controller and mouse behave as before; `MultiplayerScreen::layoutRows` only places the rows.

| Page | Left | Right |
| --- | --- | --- |
| Multiplayer (player setup) | name, colour, human skin, dragon skin, speed; big Host / Join / Back buttons | "Your character": hooded figure tinted with the colour (idle sway), name, skin, a dragon-skin chip |
| Host | five game mode cards (clickable; Dragon's Duel / Dragon Master show "Coming"), server name / password / map group, the **map list** (8 visible, scroll bar, click or Left / Right to pick); second column with game type, players, kills, time, teams, friendly fire, master servers, a one-line rules summary, Begin Game / Back | **map preview**: picture, name, description, players, supported modes, spawn points, mod pack, and a red line when the map cannot run the chosen mode (Begin Game greys out) |
| Join | server table: name, map, mode, players, ping, mods; Refresh / Add Server (address entry) / Server Info / Password / Connect / Back | selected server: map preview, address, map, mode, players, ping, password, mods |
| Lobby | roster with colour swatch, host mark, ping and READY / WAITING pills; team games split into Fire / Water columns with counts | map preview, chat log and chat line, Ready up / Start Match / Leave; a "Match starts in N" card when `MpState::countdownSec >= 0` |

In-match HUD (`drawHud`): full-screen scoreboard (rank, colour swatch, kills / deaths / ping / time, self row outlined, limit
chips; team games get two team panes with the team totals), results screen (winner banner, standings, "Next match in N" /
`resultsText`), kill feed (bigger, fading, own kills outlined), respawn overlay with a large countdown, limits chip, and a
bigger chat box (name in gilt, input line over a panel).

## Multiplayer maps (arenas)

The Host list is built around **multiplayer maps**, not campaign levels. `MpState::arenas` (`LevelEntry` with `arena`,
`description`, `modes` bitmask, `maxPlayers`, `spawns`, `author`, `previewId`) feeds it; Co-op always lists the campaign
levels (`MpState::levels`), and when `arenas` is empty (no definitions) the Host page falls back to the campaign list with
the old group names ("All Levels" / "Disc levels" / "Custom levels"; with arenas: "All Maps" / "Arenas" / "Custom maps").
Glue (`mpArenas()` in `drakan_settings_host.cpp`) fills `arenas` from `drakan::net::ArenaCatalog` (the compiled-in
`configs/mp_maps.ini` plus a `mp_maps.ini` next to the exe); `DRAKAN_MP_ARENAS=0` hides them. A custom (mod pack) map shows
"CUSTOM" in the list and its source in the preview pane.

## Previews (nothing from the disc is stored)

* Always: `drawMapArt` draws a **generated overhead sketch** (layered ridgelines in a biome tint taken from the map name,
  faint grid, spawn points marked) so every map has a distinct picture; it is labelled "OVERHEAD SKETCH". It is not the
  level's terrain.
* When `previews/<map name with non-alphanumerics as _>.png` exists next to the exe, the glue loads it on a worker thread
  (`updateMapPreviews`, same path as the save-slot pictures), gives the entry a `previewId`, and the renderer draws
  `ImageKind::LevelPreview` over the sketch (centre-cropped, label "MAP PREVIEW"). These files are the player's own (a
  screenshot, a mod pack's image); the repository holds none. Producing them from the disc automatically (a loading
  screen picture or a terrain render through `runtime/world`) is not done; the hook above is where it plugs in.
* Characters: the Multiplayer page draws a stylised figure from rectangles (no model render in the title menu). The
  `ImageKind::SkinOrb` glow sits behind it when the renderer has it.

## Model / tests

New testable state in `gameui_multiplayer_test.cpp`: arena list vs campaign fallback, Co-op campaign list, `selectMode`
(cards, also by mouse click), map list click, `previewRequests`, mode support disabling Begin Game, lobby team columns,
chat entry producing `MpActionKind::SendChat`, countdown card, Join selection, team scoreboard / results / 4K command parity.
`gameui_preview` writes the synthetic scenes `mp_*` (offline renders via `render_drawlist.py`, no game data).

Other API: `MpState::{arenas, chat, countdownSec}`, `LanServer::mods`, `RosterRow::colour`, `HudState::nextMatchSec`,
`MpAction::text`, `MpActionKind::SendChat`, `RowId::LobbyChat`, `LevelEntry` map fields.

## Not done / limits

* Character preview is a drawn figure, not the model; dragon skin is a coloured chip.
* Map pictures are generated sketches unless the player supplies `previews/*.png`; no automatic disc capture.
* Results screen is not interactive (no "Next match" / "Back to lobby" buttons): the HUD gets no input; it shows the text
  the netplay layer gives (`resultsText`, optional `nextMatchSec`). The glue does not fill `countdownSec` or
  `nextMatchSec` (no countdown exists in the netcode yet) or `LanServer::mods` (not in the query reply).
* Kick / host controls beyond Start and Ready need netcode that does not exist.
* Gamepad text entry is still the old Up / Down / Right / Left character picker.

## Co-op HUD and character select (added the same day)

* `HudState` for `GameMode::Coop`: `partners` (`HudPartner`: name, character, colour, `hpFrac`, `down` + `respawnInSec`,
  marker data) draws a party panel top left (name, character, health bar, DOWN with the respawn countdown; up to 7
  partners, so 8 players with the local one), on-screen diamonds with name and distance, and edge arrows (`dirDeg`,
  `distance` in metres) for partners off screen. The co-op scoreboard has no ranks, teams or frag limit: columns Player /
  Creatures / Deaths / Ping / Time (`RosterRow::kills` is creatures slain), chips for party deaths and creatures slain;
  results show "Level Complete" or "Party Defeated" (`levelComplete`) and the standings. The glue does not fill `partners`,
  `levelComplete` or `characterName` yet (needs the co-op session data from the netplay / coop agents).
* Co-op lobby: `RowId::LobbyCharacter` selector under a large character panel (figure preview, name, model, description).
  The host is locked to Rynn (`CharacterEntry::hostOnly`), joiners cycle the other characters and the screen emits
  `MpActionKind::SetCharacter` (`MpAction::character` = class id). Roster rows show "as <character>"
  (`RosterRow::characterName`). The list comes from `MpState::characters` (to be filled from
  `drakan_coop_character::roster()`); until then `placeholderCharacters()` (Rynn, Wartok, Tarn, Sera, all labelled
  placeholder). `MpState::selfCharacter` is the session's own choice. The glue logs the action and does nothing else.

## Wiring to real data (second pass, same day)

Done (verified in game at 1920x1080, `scripts/harness/netplay_menu_two_instances.py --screens | --arena-index N | --coop | --kick`):

* **Arena through the menu**: the Host map list is `drakan_arena::maps()` (built-in, `mp_maps.ini`, mod packs; maps whose
  base level is not on the disc are hidden). Both instances load the arena (`--arena-index 1`: Skeleton Square). Recipes
  use `host_keys()`; the Map row is a list: Left / Right step it, Select enters it so Up / Down step through the maps
  (Select / Back leaves), elsewhere Up / Down move between rows.
* **Co-op**: the joiner's Character row lists `drakan_coop_character::roster()`; picking calls `setSelected` and the
  session sends it (new `SetCharacter` message, relayed to everybody in `Status`), so roster rows show "as Male Villager"
  in the lobby. A joiner whose character is still Rynn announces the first allowed one by itself.
* **Netcode (no version bump, same "older peers drop unknown types" rule as the arena messages)**: `Status` (S>C, 2 Hz:
  countdown kind + remaining ms, every player's ping and lobby character), `StatusEcho` (C>S, ping), `Kick` (C>S, honoured
  from the host of a listen server only, the target gets `Reject{Kicked}`), `SetCharacter`, `ServerInfo.mods` (trailing
  optional field), `ServerOptions::startCountdownMs` (menu hosts: Start Match counts down 3 s, `DRAKAN_MP_START_COUNTDOWN_MS`
  overrides). Next-match countdown = the Results timer (`DRAKAN_MP_RESULTS_SEC`). Tests: `net_lobby_test`, protocol test.
* **Menu / HUD data**: lobby "Match starts in N" card, Results "Next match in N", real ping for every player, Join list
  mods column (names of the loaded packs), host-only "Kick player" row (after Start Match), a rejected / kicked client
  sends `Leave` (the session thread used to linger and refuse the next join), FPS counter drops to the bottom corner while
  a multiplayer page / scoreboard / party panel is up, co-op edge arrows avoid the party panel.
* **Map pictures**: `previews/<name>.png`, `previews/mods/<name>.png` (packs' `previews/`; already copied by the mod layer)
  and `previews/captured/<name>.png`; a deathmatch on a map without a picture captures the window 9 s into the match with the
  HUD hidden (`DRAKAN_MP_CAPTURE=0` disables). It is a spawn-view screenshot, not a terrain render.

Still open: no top-down terrain render (the terrain streams around the player, no whole-level height field in memory);
character preview is still the drawn figure; Results screen has no buttons (Back to lobby needs the native pause
Quit-to-Title sequence driven from outside the pause screen); co-op party health / down / respawn / level complete are not
on the wire: `drakan_netplay::setCoopParty()` is the hook for `drakan_coop` to publish them (the panel hides the bar until it
does), partner markers need the camera projection (`markerValid` stays false); the Join page shows no kick reason text.
