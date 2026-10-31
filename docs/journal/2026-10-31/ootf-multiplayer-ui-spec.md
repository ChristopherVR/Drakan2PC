# Order of the Flame multiplayer UI: what is known (2026-10-31, updated with the retail data)

Purpose: give the port a screen flow and option set to aim at. The first version of this note (web sources only) could not
find any menu description. This revision adds the **data files of the user's own OotF install** (string tables, option value
tables, the retail README, level data); details and method are in `ootf-data-study.md`. No OotF screenshots, images or
manual pages were copied. The ASCII wireframes are our own drawings.

**Important limit:** the PC menus are laid out in code, not in data. `Dragon.rrc` has no layout or coordinate records, so no
pixel rects exist to extract. Wireframes below are built from the real **control inventory, labels, order and option lists**;
relative placement is only stated where the README says it (marked [data-text]); everything else is [P].

Tags:
- **[data]** read from OotF data files (`Dragon.rrc` strings and tables, `.odb`, `.lvl`, `.lfl`) in this session.
- **[data-text]** stated in the retail `ReadMe.txt` or the 4.4.5 `Readme_Patch.txt` shipped in the packages.
- **[S]** web page fetched and read (URL in section 9). **[S-snip]** search snippet or 403 page, second-hand.
- **[D]** from the OpenDrakan notes in `ootf-reuse-study.md` section 3 (network capture).
- **[U]** unknown or not stored in data. **[P]** our proposal for the port (not OotF).

## 0. Summary of findings

- Modes: three network modes, each a different **level group**: Air Only Match (dragons), Ground Match (Rynn only), Master of the
  Dragon (Rynn plus one claimable dragon). A fourth label "Air and Ground" exists but no list ships. [data] [S PC Watch]
- Players: the host screen offers **2 to 12** [data]. README says "1 to 20 and No Limit" and advises 8 or fewer on the internet
  [data-text]. Earlier sources: 8 (capture, PC Watch). The port should default to 8 and allow the data's 2-12.
- Options (host): max players, kills limit, time limit, number of teams (none, 2, 3, 4), friendly fire, master-server
  registration; plus server name, password, level group, starting level. [data]
- Player setup: name, colour (hue/saturation box and brightness box), human skin (4 stock skins), dragon skin (7 stock skins,
  Air Only only), connection speed (5 presets). [data] [data-text]
- Teams: default names Fire, Water, Earth, Air; names and RGB colours are server config; team select after level load;
  4.4.5 adds team scores, team colour by the name, friendly fire and per-map overrides. [data-text]
- Scoreboard columns Player, Kills, Deaths, Ping, Time; chat key; no results screen (banner then next level). [data]
- Custom maps: any folder under `Multiplayer/` with a `.lvl`; level groups are `.lfl` text files. [data] [data-text]
- Online play is effectively gone; LAN is the practical route. [S]

## 1. Screen flow (OotF, from data) and our mapping

```
Main menu (7 gems: Multiplayer / Load / New / Save / Options / Credits / Exit) [data]
  -> Multiplayer gem: Player setup screen (name, colour, skins, speed) with Host Game / Join Game / mplayer.com [data]
       -> Host Game: Server name, password, level group, starting level, game options, Begin Game [data]
       -> Join Game: server table, Add Server, Remove, Server Info, Refresh, Stop, Connect [data]
            -> Add Server dialog (address, query port) [data]
            -> Server Info (IP, level, players with kills/ping, rule table) [data]
  -> level loads -> (team games) Team select -> play [data-text]
  -> in play: scoreboard key, chat key, change-teams key [data]
  -> kill limit or time limit: banner, next level of the group loads [data]
```

Our port: **LAN + direct IP + (later) relay** instead of master servers and mplayer.com [P]. Keep the same screen order so the
original README stays valid as documentation.

## 2. Screens

### 2.1 Main menu entry [data for inventory, U for positions]

- A ring with seven gem buttons plus a hint line; the multiplayer gem is the README's entry point. The gem to action
  mapping and gem positions are code-defined [U]. Hint texts (one per action) and a resume hint exist.

```
        (  ring  )
   [gem top]           hint line: one text per gem
 [gem L] [gem mid] [gem R]       <- positions [P]; 7 gems: top, left, middle, right, lower-left, lower-right, bottom [data]
   [gem LL]       [gem LR]
        [gem bottom]
```

### 2.2 Player setup (the "Multiplayer" screen) [data]

Control order in the string table: Player Name, Host Game, Join Game, mplayer.com, Player Color, Human Skin, Dragon Skin,
Connection Speed. Hints exist for host, join, mplayer.com, the colour box, the brightness box and the dragon look.

| Control | Detail |
| --- | --- |
| Player Name | text field, any characters, shown above the character in play [data-text] |
| Player Color | two boxes: hue/saturation, brightness; the Rynn portrait to the right updates at once; colour also shown beside the name in the stats box [data-text] |
| Human Skin | rotating portrait cycling through armour/clothing styles in the chosen colour; stock skin model array has 4 entries [data] |
| Dragon Skin | slider under a rotating dragon portrait; 7 stock dragon skins; no effect in Master of the Dragon [data] [data-text] |
| Connection Speed | list: Modem 28.8k, Modem 33.6k, Modem 56k, ISDN 112k, LAN/T1 1M [data] |
| Host Game / Join Game | buttons [data] |
| mplayer.com | button; free internet play community [data] |

```
+--------------------------------------------------------------+
| Player Name: [____________]                                  |
| Player Color: [hue/sat box] [brightness box]     [Rynn view] |
| Human Skin:   [rotating portrait]                            |
| Dragon Skin:  [rotating dragon]  <--slider-->                |
| Connection Speed: [ LAN/T1 - 1M v ]                          |
| [ Host Game ]  [ Join Game ]  [ mplayer.com ]                |
+--------------------------------------------------------------+
Arrangement [P]; the controls, labels and order are [data].
```

### 2.3 Host Game [data]

Top to bottom (README 4.3 and the string order): Server Name, Password (blank = open), Level Group, Starting Level, Game Options
box, Begin Game. The default server name pattern is "<player>'s Server". Level Group list: "All Levels" first, then every `.lfl`
in `Multiplayer/`.

| Option | Values (from the tables in `Dragon.rrc`) |
| --- | --- |
| Maximum Number of Players | 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 |
| Kills Limit | No Limit, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200 |
| Time Limit | No Limit, 5, 10, 15, 20, 30, 45 minutes, 1 hour |
| Number of Teams | No Teams, 2, 3, 4 |
| Friendly Fire? | No, Yes (only matters in team games) |
| Register With Master Servers? | No, Yes |

Rules: both limits "No Limit" means the starting level runs until the host quits [data-text]. Errors: enter a player name,
enter a server name, select a starting level, session creation failed, no levels in the folder, unreadable level list file
(falls back to all levels).

```
+--------------------------------------------------------------+
| Server Name: [______________]    Password: [__________]      |
| Level Group: [ All Levels v ]    Starting Level: [ list    ] |
| Game Options                                                 |
|   Maximum Number of Players  [ 8  v ]                        |
|   Kills Limit                [ 25 v ]                        |
|   Time Limit                 [ 15 min v ]                    |
|   Number of Teams            [ No Teams v ]                  |
|   Friendly Fire?             [ No v ]                        |
|   Register With Master Servers? [ No v ]                     |
| [ Begin Game ]                                               |
+--------------------------------------------------------------+
Arrangement [P]; fields, order and value lists [data].
```

### 2.4 Join Game, Add Server, Server Info [data]

- Table columns: Server Name, Current Level, Players, Ping; sorted by ping, lowest first [data-text].
- Row colour: red incompatible version, white internet or custom ("+" prefix on custom), blue LAN, green selected. Password
  servers are marked beside the name [data-text].
- Buttons: Add Server, Remove (custom entries), Server Info, Refresh, Stop (while scanning), Connect (lower right, [data-text]).
  Double-clicking a name also joins.
- Status strings: contacting master server, master server contacted, contacting server, a wait message, "Available Servers: %d",
  "Valid Servers: %d".
- Add Server: Server Address, Query Port (default offered), OK, Cancel. Errors: already in list, enter a server.domain or IP.
- Server Info: IP Address, Current Level, players (name, kills, ping), table Server Rule / Rule Value (sorted by name), Connect,
  Cancel. Standard rules: Admin_Name, Admin_Email, Admin_Web, URL_Levels, URL_MOD; also Number of Teams and Network Speed
  (4.4.2) [data-text].

```
+----------------------------------------------------------------+
| Available Servers: n   Valid Servers: m    status text         |
| Server Name          | Current Level    | Players | Ping       |
| ----------------------------------------------------------------|
| host (blue = LAN)    | Relic (ground)   | 3/8     | 12         |
| +custom (white)      | ...                                     |
| [Add Server][Remove][Server Info][Refresh][Stop]    [ Connect ]|
+----------------------------------------------------------------+
Row/column set and colours [data]; arrangement [P] except Connect lower right [data-text].
```

Discovery protocol [D]: UDP `\status\` key/value (see section 8); the browser's data fields match these keys.

### 2.5 Team select [data-text]

Shown to every joining player of a team game after the level has loaded and before play; click once on a team in the list. The
scoreboard is shown with it so team sizes are visible (4.4.2). The "Change Teams" command (bindable) reopens it. Default team
names Fire, Water, Earth, Air; the server edits `TeamName1..4` and `TeamColor1..4` (RGB 0-255) in its config. In air levels the
dragon skin is not set automatically in the retail build (recommended: red, green serpent, tan lizard, grey); 4.4.2 fixed it
to give a whole team the same dragon skin. All non-dragon team members share a skin colour.

```
+--------------------------+
| Choose a team            |
|  [ Fire  ]  n players    |
|  [ Water ]  n players    |
|  [ Earth ]  n players    |   rows = Number of Teams (2..4) [data]; layout [P]
|  [ Air   ]  n players    |
+--------------------------+
```

### 2.6 Scoreboard (key "Map/Scoreboard", TAB by default) [data]

Columns: **Player, Kills, Deaths, Ping, Time**. Colour swatch beside the name. In team games team scores are separate from
player scores and a leaver's contribution stays (4.4.5). The dedicated-server STATUS list adds Kills/Hour and Time Online.
In single player the same key opens the quest map (not part of multiplayer).

```
+--------------------------------------------------+
| Player            Kills  Deaths  Ping   Time     |
| [c] name             12      5     34   08:12    |
| ...                                              |
| (team games: team totals, team colour [c])       |
+--------------------------------------------------+
Columns [data]; arrangement [P].
```

### 2.7 Chat and messages [data]

- Key "Network Chat". Backslash toggles global and private text; team messages are yellow (4.4.5). Flood protection: message
  "cannot chat for 15 seconds". Commands SAY / TEAMSAY may be bound; server commands LEVEL, KICK, STATUS, TEAMS, GIB, ...
- Event messages (short): "%s has joined the game", "%s has left the game", "%s joined %s" (team join), kicked messages,
  server terminated, session full, wrong password, level files mismatch, connection lost, level changed by server.
- Four sound events: time limit, kill limit, chat (public, private), player died, new dragon owner.

```
chat line (bottom of screen, text entry on key)      "name: message"  (team = yellow)
event line (centre top)                              "<player> has joined the game"
```

Placement [P].

### 2.8 Results [data]

No results screen exists. At the limit the game shows a banner (kill limit or time limit reached; the next level loads in a few
seconds) and loads the next map of the level group. Scores can be logged to a file by the server (4.4.5). For the port a simple
end-of-round summary is a design addition [P].

```
+------------------------------------------+
| Game KILL limit reached! Next level...   |   banner only [data]
+------------------------------------------+
```

## 3. Mode rules in data (short; full table in `ootf-data-study.md` section 3)

| Mode | Level Type | Spawns and special objects |
| --- | --- | --- |
| Air Only | 0 | dragon launch pads, dragon health and invulnerability pickups; 2 of 4 maps also have Rynn pads and a Dragon |
| Ground | 1 | 12-18 Rynn launch pads, weapon pads, no dragon |
| Master of the Dragon | 3 | Rynn pads, one Dragon, Fire Dragon Soul Crystal, red launch pad, DragonStrike pickups, dragon energy recharge x0.25 |

- Score: kills; end at kills limit (team total in team games) or time limit; deaths shown. [data] [data-text]
- Respawn: items carry a per-class interval (10-150 s); dragon launch pad respawn 3 s; arena levels (4.4.5) have loser 5 s /
  winner 2 s. [data]
- Team colour: names and RGB set by server config; per-map overrides via 4.4.5 level properties. Default RGB values are not
  in the data. [data-text]

## 4. Level and map list (strings 36876-36885) [data]

Air: Alwarren of Death, Arena of Death, Grotto of Death, Nemesis. Ground: Hill of Fire, Relic, Sanctuary. Master of the Dragon:
Island of Death, Rise to Power, Volcano of Death. Each name is shown with a type suffix in the list ("(air)", "(ground)",
"(MD)").

## 5. Custom maps, weapons, models [data]

See `ootf-data-study.md` section 4: folder per map under `Multiplayer/`, `.lfl` level groups, identical files required on
server and client, data-only classes for weapons and pickups, skins as model arrays.

## 6. Patches and the multiplayer state

- Retail used DirectPlay; 4.4.2 replaced it with Winsock (firewall friendly, modem friendlier, default client port 27910);
  4.4.5 added team scoring and per-map team overrides, joystick axis binding and a "nightmare" difficulty. [data-text]
- AiO patch: NAT fix, QTracker master list, dedicated-server launcher; "Arokh.ini" is that patch's config (not content). [S] [data]
- Multiplayer is largely defunct online; LAN works. [S]

## 7. Open questions

1. Real pixel layout of every screen (needs a playthrough capture; not in data).
2. Gem order on the main menu; where "mplayer.com" sits.
3. Default team RGB values (config), and the meaning of the four colour fields of "Level Properties".
4. Whether the list-file "Air and Ground" group is built-in (string exists, no list ships).

## 8. Server query reply keys [D]

`gamename drakan`, `gamever 375/445`, `hostname`, `hostport 27045`, `mapname`, `gametype` (e.g. "Nur Boden" = ground only),
`numplayers`, `maxplayers 8`, `gamemode openplaying`, `Dedicated Server?`, `score limit`, `time limit`, `Network Speed`,
`Num Teams`, friendly fire, `password`. README ports: game 27045, query 27046. These match the data's Server Info rules.

## 9. Port implications (our design, not OotF)

- Use sections 1-2 as the skeleton and label every screen with our own names and art; do not use OotF images or layouts.
- Build `MatchSettings` from the host options (max players default 8, kills limit, time limit, teams 0/2/3/4, friendly fire,
  password, level group) and let the browser rows show Name, Level, Players, Ping and a lock.
- Ship Deathmatch (Ground), Team Deathmatch, then Air Only; Master of the Dragon needs soul-crystal ownership transfer and
  dragon respawn rules.
- Honour the original hint that dragon skin is cosmetic in MD and that human and dragon health are separate there.

## 10. Sources

Retail data (this session): `Dragon.rrc` strings and tables, `ReadMe.txt` (disc), `Readme_Patch.txt` (4.4.5), `Multiplayer/*.lfl`,
`Common/Multiplayer`, `Common/Skins`, `Common/Interface` databases, the 23 level files.

Web [S] (fetched and read):
1. PC Watch, 2000-03-15: https://pc.watch.impress.co.jp/docs/article/20000315/game.htm
2. WineHQ bug 53643: https://list.winehq.org/hyperkitty/list/wine-bugs@list.winehq.org/thread/NKSNKC44YCIYO2LHYDM2NU4AABZ3YHJV/
3. LAN Gaming listing: https://lan-gaming.codeberg.page/games/drakan-order-of-the-flame/
4. GamePressure AiO v2.170: https://www.gamepressure.com/download/drakan-order-of-the-flame-drakan-aio-unofficial-patch-v2170-fina/zdf22c
5. oldgames.sk review: https://www.oldgames.sk/en/game/drakan/download/4424
6. GamesFirst 1999: https://gamesfirst.com/articles/1999/drakan-order-of-the-flame/
7. The Video Game Library (Prima guide): https://www.thevideogamelibrary.org/book/drakan-order-of-the-flame-prima-official-strategy-guide
8. itch.io Level Editor+ post: https://itch.io/post/14506380
9. Wikipedia: https://en.wikipedia.org/wiki/Drakan:_Order_of_the_Flame

[S-snip] (search snippet or 403): FilePlanet v4.4.4 and v4.4.5 notes (fileplanet.com/archive/p-15163, p-12125), MobyGames press
text, Blues News 7834, Vogons p=1341290, retail listings. Local: `ootf-reuse-study.md` section 3 [D].
