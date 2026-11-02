# Multiplayer

The PC version adds multiplayer that the PS2 game never had: **deathmatch** and **team deathmatch** on arena maps, and **co-op**, where friends play the campaign together. It runs over your local network or the internet with no account or online service. Every player needs their own copy of the PC version, the same release, built from the same disc region.

Multiplayer is new and still growing. Some menu rows are placeholders that are greyed out, and dragons (Arokh) do not take part yet.

## Play against each other (deathmatch)

### Host a game

1. On the title screen choose **Multiplayer**.
2. Set your **Player Name** and **Player Color**, then choose **Host Game**.
3. Pick the **Starting Level**: a disc level or a map from an installed mod pack (marked "(custom)"). Set the **Maximum Number of Players** (2 to 12), the **Kills Limit** and the **Time Limit**. **Number of Teams: 2** makes it team deathmatch. A **Password** keeps strangers out.
4. Choose **Begin Game**. You are now in the lobby. When everyone has joined and pressed **Ready**, press **Start Match**.

### Join a game

1. Choose **Multiplayer > Join Game**. Games on your local network appear by themselves (blue rows), sorted by ping.
2. For a game over the internet, choose **Add Server** and type the host's address and port (for example `203.0.113.5:47800`).
3. Select the game and choose **Connect** (enter the password if it has one). In the lobby, press **Ready**.

### During a match

| Key | Action |
| --- | --- |
| F4 (hold) | Scoreboard: kills, deaths and ping per player |
| F6 | Chat |
| Esc | Pause menu (Quit to Title leaves the match) |

When the match ends, the results screen shows the standings. The host chooses **Back to lobby**, **Next map** or **Rematch**; everyone can vote for the next map by clicking its card.

## Play the campaign together (co-op)

1. On the title screen choose **New Game > Co-op**.
2. The host chooses **Host Game**, picks a campaign level and chooses **Begin Game**. Friends choose **Join Game** and connect as above.
3. Everyone plays the same level together. Players cannot hurt each other; creatures are shared, and when the host takes an exit everyone follows into the next level.

If the host quits, the game ends for everyone.

## Hosting over the internet

The host's game listens on UDP port **47800** (or the next free port up to 47809). For players outside your home network, forward that UDP port on your router to the host PC and allow `drakan.exe` through the Windows firewall. Joiners use **Add Server** with the host's public address. On a local network nothing needs to be set up.

## Make your own arena

Any level can become a multiplayer map in the editor: place spawn points and pickups, choose what stays from the campaign, and export it as a mod pack. Follow [Make a multiplayer map](../reference/editor-guide.md#multiplayer) in the editor task guides, then install the pack on every player's PC ([Install and share mods](mods.md)). All players need the same map pack; a player without it sees "Level files mismatch" in the lobby.

## Problems

- **The game is not in the Join list:** both PCs must be on the same network for automatic discovery; otherwise use Add Server. Check the firewall on the host.
- **"Rejected" or a version message:** all players need the same release of the PC version, built from the same disc region.
- **Level files mismatch:** install the same map pack as the host and restart the game.

Technical details (protocol, dedicated server, server options): [Netcode reference](../reference/netcode.md).
