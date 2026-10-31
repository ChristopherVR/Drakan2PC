# Order of the Flame multiplayer UI: what is known (2026-10-31)

Purpose: give the port a screen flow and option set to aim at. The honest finding is that **no source describes
the OotF multiplayer menus screen by screen**. The flow below is therefore a mix of (a) what sources say exists,
(b) what the OpenDrakan notes say about the server, and (c) a proposed layout for our port, clearly marked as ours.
No OotF screenshots, images or manual pages were copied or reproduced. All wireframes here are our own placeholders.

Tags used:
- **[S]** sourced, page fetched and read directly (URL in section 4).
- **[S-snip]** sourced from a web search snippet or a page that returned 403 on fetch. Treat as second-hand.
- **[D]** from the OpenDrakan notes in `docs/journal/2026-10-31/ootf-reuse-study.md` (section 3), network capture, not web.
- **[U]** uncertain or not found. Nothing was guessed silently.
- **[P]** our proposal for the port (not OotF).

Conflicts between sources are called out where they matter.

## 0. Summary of findings

- Modes: three network modes are documented [S, PC Watch]: Deathmatch, Dragon's Duel, Dragon Master.
- Players: "up to 8" is the most repeated figure [S]. Two listings say 12 and 1-16 [S, conflicting, see 3.1].
- Connections: LAN, internet (including the mplayer.com service), and two-player modem-to-modem [S].
- Options: time limit (optional), team division (optional), team play / friendly fire / team colour / team names
  (v4.4.5 patch notes) [S-snip], plus server query keys (frag/score limit, time limit, friendly fire, teams, password,
  dedicated flag, network speed, gametype) [D].
- Screens: none of the in-game menu layouts, labels or positions are documented in anything I could read. [U]
- Customisation (name, model, skin, colour): not documented, except that a team colour setting exists [S-snip]. [U]
- Custom maps, custom weapons, custom models for multiplayer: not documented. The level editor and editing tools
  exist, but no multiplayer map folder or naming rule was found. [U]
- HUD (scoreboard key, chat key, kill feed): not documented. [U]
- Online play is effectively gone: the official master-server path is dead and fan groups ran servers for years [S].
  LAN is the practical route today [S, Wine report].

## 1. Proposed screen flow (ours, with the known facts placed in it)

Known-to-exist elements are marked. Everything about layout is [U] unless the wireframe says [P].

```
Main menu [U layout]
  -> Multiplayer [U label; a Multiplayer entry is implied by the game's multiplayer support]
       -> Connection type  [S: LAN / Internet / Modem]
            LAN ----------> Host game [U] | Join game [S-snip: "join-game screen" exists, v4.4.4 notes]
            Internet -----> Server list via master server [S-snip v4.4.4; S PC Watch: mplayer.com]
            Modem --------> 2 players, modem-to-modem [S press release, via MobyGames snippet]
       -> Player setup [U]           (name, model/skin, team colour?)
       -> Game setup (host only) [U] (mode, map, players, limits, teams, dragons, password)
       -> Lobby / briefing [U]       (player list, ready state?)
       -> In-game                    (scoreboard, chat, kill messages [U])
       -> Results [U]
```

Error text known to occur [S, Wine report]: when DirectPlay (dplay) is missing, creating a session shows
"check your connection settings". This is a message in the network path, not a screen. The fix on modern Windows
is the DirectPlay component, which Wine users installed via winetricks.

## 2. Screens

Each screen gives: controls, layout, options, and sources. The wireframes are [P] (our proposal) unless marked otherwise.
No OotF layout is known to us, so no wireframe represents OotF.

### 2.1 Main menu -> Multiplayer entry [U]

- Controls: a "Multiplayer" entry on the main menu. Label not confirmed. [U]
- Sources: the game is listed with "Single-player, multiplayer" modes [S, Wikipedia infobox].

```
+------------------------------------------+
|              DRAKAN (logo)               |
|                                          |
|   [ Single Player ]                      |
|   [ Multiplayer   ]   <- label [U]       |
|   [ Options       ]   [U]                |
|   [ Quit          ]   [U]                |
+------------------------------------------+
```

### 2.2 Connection type [S for the three choices, U for the layout]

- Controls: three choices: LAN, Internet, Modem. [S: press release snippet, PC Watch]
- Modem is two players only. [S-snip]
- Labels as shown in the game: [U]

```
+----------------------------------------+
|  MULTIPLAYER                           |
|  Connection:                           |
|   ( ) LAN                              |
|   ( ) Internet                         |
|   ( ) Modem (2 players)                |
|                                        |
|  [ Back ]                    [ Next ]  |
+----------------------------------------+
```

### 2.3 Host or join [S-snip for join, U for the rest]

- A "join game" screen exists and a patch fixed a long delay on it [S-snip, v4.4.4 notes].
- The join screen looks up the Drakan master server address by DNS (fixed in 4.4.4 so the system no longer hangs) [S-snip].
- Host vs join split: [U]. The LAN server creation path is documented: creating a session needs DirectPlay [S, Wine].

```
+----------------------------------------+
|  LAN                                   |
|  [ Host game ]     [U]                 |
|  [ Join game ]     [S-snip exists]     |
|                                        |
|  [ Back ]                              |
+----------------------------------------+
```

### 2.4 Server list [U]

- A server list (browse) exists by implication of the join screen [S-snip].
- Columns, sort order, refresh button and ping display: [U].
- The discovery protocol is a UDP broadcast `\status\` with backslash key/value replies [D]. The reply keys are listed in
  section 3. The list is therefore built from those keys (hostname, map, players, max players, gametype).

```
+--------------------------------------------------------------+
|  SERVERS                              [ Refresh ] [U]        |
|  Name            Map        Players    Ping   Mode     Lock  |
|  ----------------------------------------------------------   |
|  <hostname>      <map>      n / 8      ...    <mode>   pw?   |
|  <hostname>      ...                                         |
|                                                              |
|  [ Back ]                              [ Join ]              |
+--------------------------------------------------------------+
```

Column names above are [P]. Server-query fields are [D]: `hostname`, `mapname`, `numplayers`, `maxplayers`, `gametype`,
`gamemode`, `password`, `Dedicated Server?`.

### 2.5 Player setup [U]

- Name: not documented. [U]
- Model or skin: not documented. [U]
- Team colour: a setting exists in v4.4.5 notes ("team color", "the names of the teams") [S-snip].
- Character choice: the reviewer says the dragon is the better pick than Rynn in multiplayer because Arokh moves faster [S, gamesfirst]. Whether Rynn and Arokh are both selectable at the setup screen is [U].

```
+----------------------------------------+
|  PLAYER SETUP                          |
|  Name:     [__________] [U: max len]   |
|  Character: < Rynn | Arokh > [U]       |
|  Team:     < none | A | B > [S-snip]   |
|  Colour:   [swatch row] [U]            |
|                                        |
|  [ Back ]                 [ Continue ] |
+----------------------------------------+
```

Japanese naming: the PC Watch article calls the player character "Lin" (リン) [S]. This is the Japanese release name for
Rynn, not a second character. Our port should use the English name.

### 2.6 Game setup (host) [partly S-snip / D; layout U]

Options. Each row says where the option is known from.

| Option | Known values / range | Source |
|---|---|---|
| Game mode | Deathmatch, Dragon's Duel, Dragon Master | [S, PC Watch] |
| Gametype (ground vs air) | "ground only" seen in a German build (`gametype` = "Nur Boden") | [D] |
| Max players | Up to 8 for every mode; `maxplayers` = 8 in capture | [S PC Watch, press release S-snip; D] |
| Map | Several battlefields; list not known | [S PC Watch: "several"; count U] |
| Time limit | Optional; value range U | [S PC Watch: "optional time limits"] |
| Frag / score limit | Field `score limit` seen; range and UI U | [D] |
| Teams | Optional team division; "Num Teams" field | [S PC Watch; D] |
| Friendly fire | On/off, per the capture; team-play patch notes | [D; S-snip v4.4.5] |
| Dragons | Dragon Duel and Dragon Master are dragon modes. An on/off toggle is not documented. | [S PC Watch; toggle U] |
| Weapons | Lists for single and multiplayer modes (Prima guide). Per-weapon toggles U. | [S, Prima listing] |
| Respawn | Not found | [U] |
| Password | Field `password` exists | [D] |
| Server name | Field `hostname` | [D] |
| Network speed | Preset field `Network Speed` | [D] |
| Dedicated server | Field `Dedicated Server?` | [D] |

Conflicts to resolve in the port (not in OotF):
- Max players: "8" [S, PC Watch, press release] vs "12" [S, lan-gaming listing] vs "1-16" [S-snip, a retail listing]. Eight is
  the only number that is in a developer-era text and in the capture. Use 8.

```
+----------------------------------------------+
|  GAME SETUP (host)                           |
|  Server name:  [______________]   [D field]  |
|  Password:     [______] (optional) [D]       |
|  Mode:         < Deathmatch | Dragon's Duel | Dragon Master > [S] |
|  Map:          < Battlefield ... > [U list]  |
|  Max players:  < 2 .. 8 >        [S: 8]      |
|  Time limit:   < off | 5 | 10 | 15 min > [U]|
|  Score limit:  < ... > [U range]             |
|  Teams:        [ ] on   Friendly fire [ ] [D]|
|  Dragons:      [ ] (mode-dependent) [U]      |
|                                              |
|  [ Back ]                       [ Start ]    |
+----------------------------------------------+
```

Bracketed ranges in the wireframe are [P] unless a source is given.

### 2.7 Lobby / briefing [U]

- Not documented. Whether a lobby exists separate from setup: [U].
- A "ready" state, countdown, or chat in the lobby: [U].

```
+----------------------------------------+
|  LOBBY  <server name> [D]              |
|  Players                 Team  Ready   |
|  ------------------------------------  |
|  1  <host>                    yes [U]  |
|  2  <client>                  no  [U]  |
|  Chat: [______________] [U]            |
|                                        |
|  [ Leave ]              [ Start ] host |
+----------------------------------------+
```

### 2.8 In-game HUD: scoreboard, chat, kill messages [U]

- Scoreboard key: [U].
- Chat key: [U]. The protocol carries chat as a wrapped packet [D].
- Kill messages: [U].
- Dragon HUD in dragon modes: the modes exist [S PC Watch]; HUD details [U].

```
+----------------------------------------------------------+
|  [score / time]                         [weapon]          |
|                                                           |
|  kill feed (top right) [U]                                |
|                                                           |
|  chat (bottom left, when open) [U]                        |
+----------------------------------------------------------+
```

### 2.9 Results [U]

- A score or results screen after a round is likely (frag limit, time limit exist [D, S]); its layout is not documented. [U]

```
+----------------------------------------+
|  ROUND OVER [U]                        |
|  Rank  Player          Frags  Deaths  |
|  ...                                   |
|  [ Next round ] [U]     [ Leave ]      |
+----------------------------------------+
```

## 3. Server query reply keys [D]

From the OpenDrakan capture notes (not fetched by me; see the journal note, section 3). These are the strongest
evidence for what the browser and host screens read and write:
`gamename drakan`, `gamever 375/445`, `hostname`, `hostport 27045`, `mapname`, `gametype` (value seen "Nur Boden"),
`numplayers`, `maxplayers 8`, `gamemode openplaying`, `Dedicated Server?`, `score limit`, `time limit`,
`Network Speed` (preset), `Num Teams`, friendly fire, `password`.

Note: the capture is from a specific build. Values are [D], not verified on the UI.

### 3.1 Player count conflict

- "Up to 8": PC Watch [S], MobyGames press text [S-snip], multiple retailers [S-snip], capture `maxplayers 8` [D].
- "12": lan-gaming listing, LAN and online fields [S]. Unverified; the listing is a compatibility database.
- "1-16": one retail listing [S-snip].

## 4. Custom maps, weapons, and models

- Custom maps for multiplayer: no folder, file naming rule, or install step was found. [U]
- Level editor: a Vogons post says a user spent hours in the Level Editor fixing visibility data on stock levels after
  hacking the game to double draw distance [S-snip, vogons p=1341290 snippet; the page returned 403].
- Level sharing via codes: a fan mod ("Level Editor+") adds "Load from Clipboard" and "Playtest Level" to the editor.
  The mod is for building levels, not content expansion; playing others' levels does not need it. [S, itch.io post]
- Editing tools: the official tools allow custom 3D models and textures [S-snip, Blues News; page 403].
- AiO unofficial patch (UCyborg) bundles the editing tools and earlier fixes [S, gamepressure].
  Its text says the aim is "making the multiplayer component fully functional on modern operating systems and over modern internet" [S].
- Custom weapons or models in multiplayer: not documented. [U]

## 5. Patches and the multiplayer state

- Demo lacks multiplayer. A repack was needed to test it [S, Wine report].
- v4.4.4: join-game screen delay (caused by DNS settings) fixed; the game no longer hangs on the master server lookup [S-snip].
- v4.4.5: team play, friendly fire, team colour, team names, Team Arena settings [S-snip]. Exact variable names [U].
- AiO v2.170 requires the official 445 patch first [S, gamepressure].
- Multiplayer "pretty much defunct" with a few fan-run servers [S, oldgames.sk review].

## 6. Mode descriptions (from PC Watch, translated) [S]

- Deathmatch: play as Lin (Rynn) and defeat rivals.
- Dragon's Duel: ride Arokh and fight rivals.
- Dragon Master: one dragon is on the map. Only the player who finds it can ride it. Riders have a large advantage.
- All modes: up to 8 players, optional time limit, optional teams.
- Connections: LAN, internet, and mplayer.com for strangers.
- Several battlefields.

Translated text is a paraphrase; the Japanese quote is in the PC Watch article.

## 7. Open questions (to test, not to guess)

1. Real screen names and button labels: needs the manual (not found online) or a playthrough video with the menus on screen.
2. Whether "Dragon Master" uses a per-map single dragon or the same rule across maps.
3. Exact values and steps for time limit, score limit, and respawn.
4. Map list and whether any map is dragon-only.
5. Custom map folder rules, if the port wants them.
6. Scoreboard, chat, and kill feed keys.
7. Whether dragons can be toggled off in the game setup, or only chosen by mode.

Suggested checks: the original PDF manual (not found on archive.org or replacementdocs via search), the GameSpot review
by Greg Kasavin (2 Aug 1999 per Wikipedia citation; page not retrieved), the IGN review (3 Sep 1999; not retrieved),
and any YouTube playthrough showing the multiplayer menus. Only the wording of the sources should be reused; no
screenshots.

## 8. Port implications (our design, not OotF)

- Use the screen flow in section 1 as the skeleton. Label every screen with our own names and art.
- Build the option set from section 2.6 and the server keys in section 3. Limit to 8 players. Start with
  Deathmatch and Team Deathmatch, then a dragon mode.
- Keep the server browser on the same key/value list as the query, so the list and the setup share one `MatchSettings`.
- Do not use OotF images, screenshots, or layouts. Build new art for each screen.

## 9. Sources

Fetched and read directly [S]:
1. PC Watch, March 15 2000 (Japanese), mode and network description. https://pc.watch.impress.co.jp/docs/article/20000315/game.htm
2. WineHQ bug 53643, "creating server doesn't work". https://list.winehq.org/hyperkitty/list/wine-bugs@list.winehq.org/thread/NKSNKC44YCIYO2LHYDM2NU4AABZ3YHJV/
3. LAN Gaming compatibility listing (LAN 12, Online 12). https://lan-gaming.codeberg.page/games/drakan-order-of-the-flame/
4. GamePressure, Drakan AiO Unofficial Patch v2.170. https://www.gamepressure.com/download/drakan-order-of-the-flame-drakan-aio-unofficial-patch-v2170-fina/zdf22c
5. oldgames.sk review (fan review, "multiplayer mode is pretty much defunct"). https://www.oldgames.sk/en/game/drakan/download/4424
6. GamesFirst 1999 article (dragon-on-dragon, MPlayer). https://gamesfirst.com/articles/1999/drakan-order-of-the-flame/
7. The Video Game Library, Prima official strategy guide listing (weapon lists, multiplayer strategies). https://www.thevideogamelibrary.org/book/drakan-order-of-the-flame-prima-official-strategy-guide
8. itch.io post, Level Editor+ mod (level codes, Load from Clipboard, Playtest Level). https://itch.io/post/14506380
9. Wikipedia, Drakan: Order of the Flame (modes; review citations). https://en.wikipedia.org/wiki/Drakan:_Order_of_the_Flame

Search snippet or 403 on fetch [S-snip]:
10. FilePlanet, Drakan v4.4.4 notes (join-game screen, master server). https://www.fileplanet.com/archive/p-15163/Drakan-v444
11. FilePlanet, Drakan v4.4.5 notes (team play, friendly fire, team colour/names, Team Arena). https://www.fileplanet.com/archive/p-12125/Drakan-v445
12. MobyGames, press-release text (8 players LAN/internet, 2 modem). https://www.mobygames.com/game/337/-/adblurbs/ (also https://mobygames.com/game/337; 403 on fetch)
13. Blues News, new Drakan editing tools (custom 3D models and textures). https://www.bluesnews.com/s/7834/new-drakan-editing-tools
14. Vogons forum, level editor visibility work. https://www.vogons.org/viewtopic.php?p=1341290
15. Retail listing (1-16 players, "Number of Players"). https://www.ebay.com/p/7087 (listing copy, weak)
16. German retail ad (50 weapons incl. magic and dragon attacks; "up to 8 players"). Via search snippet of https://www.ebay.de/p/7087 (listing copy, weak)

From the project journal [D]:
17. `docs/journal/2026-10-31/ootf-reuse-study.md`, section 3 (OpenDrakan network capture notes, server query keys). Local file, not web.

Not retrieved (403 or redirected away): GameSpot review by Kasavin, IGN review by Lopez, PCGames.de retro special,
gry-online.pl review pages, mobygames.com direct, fileplanet v4.4.5 direct, bluesnews direct, Vogons p=234497 direct.
The Wikipedia redirect for the-spoiler.com points to a walkthrough of Drakan: The Ancients' Gates, not OotF multiplayer; not used.
