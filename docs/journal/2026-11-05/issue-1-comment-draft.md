Thanks for testing v0.1.2 and for putting the examples in one place. Here’s where things stand on the development build. A strikethrough means we addressed that item and checked the behavior described below. The remaining lines would benefit from another test.

**Issues Encountered**

1. ~~Audio: Almost constant high-pitched beeping, even in the main menu.~~ We found an idle sound sample producing an 11 kHz tone. It disappeared from both the game output and our system recording after the fix. A retest on your AE-7 would be welcome.
2. ~~When raytracing is enabled, the game renders two shadows for entities, as well as a spherical shadow in front of the player that moves with the camera.~~ We verified both fixes on DX11 and WebGPU. On a Surdana cliff we could bring the curved patch back by letting a camera-following sky model cast shadows. Excluding it removed the patch. We used a reconstructed viewpoint, so a check at your exact spot is still welcome.
3. ~~Language Selection: German is selected in both F1 settings and drakan.ini, but the game still loads English text and voices.~~ We switched from English to Deutsch through the F1 settings path and restarted with the same INI and card. The menus used German and the recorded intro narration matched the German soundtrack on the original PAL disc. The English control matched the English soundtrack.

**Editor/Drakan 1**

- Exporting Drakan 1 levels with the editor works and Drakan2PC detects the mod, but the levels won't load. ~~I also don't get the option to choose between D1 and D2 when starting the game.~~ For a manually exported zip, put it in the game’s mods folder, enable the pack in the Mods menu and restart with `game = ask`. Newly discovered packs start disabled. We verified the campaign choice, the owl, logo and landscape during Intro, and the handoff to playable Ruined Village. We used English D1 files with the port set to German. Your German D1 installation and the updated portable editor are the remaining combination to test.
- ~~When importing Drakan 1 levels, the “Template Level” dropdown appears behind the editor window.~~ Mouse and keyboard selection passed in Chromium. A quick check in the portable editor would be welcome.

**Language**

- Replacing the English files with the German ones inside the PS2 ISO causes extreme stuttering. German now works directly from the original PAL ISO. We compared German and English on that disc and saw no German-specific stutter in the scene tested. We haven’t reproduced the modified-image case.

**Controls**

- ~~The directional attacks that originally used the D-pad on PS2 only work with the arrow keys, not WASD.~~ All four WASD directions now play the same attacks as the corresponding arrow keys.
- ~~When casting spells, W and S work, but A and D don't.~~ Both key sets completed the same Fireball gesture and cast it in-game.

**Multiplayer**

- ~~Picking up items freezes the client completely. For example, City of the Damned loads fine but freezes as soon as you pick something up.~~ We fixed pickup registration and inventory cleanup, and extended the arena boundary to include Full Mana. Two-player tests collected Lastat’s Blade and all three Mana pickups at their stock coordinates. The server accepted each pickup, the other player saw it disappear, and both players kept moving and using inventory. We started the players near the items for these checks.
- ~~Surdana Meadow gets stuck in a cutscene right after starting the level.~~ The arena now filters campaign actors before they can start the cutscene. We entered the match and moved freely.
- ~~In Inquisitor's Lair, W and S only rotate the character instead of moving normally.~~ We fixed startup with incomplete controller settings. Valid saved settings are kept and missing fields receive defaults. Host and client both moved forward and backward without turning in our Inquisitor match.
- If you start singleplayer, return to the main menu and then try to start multiplayer, the client freezes when loading the level. We haven't reproduced a hang inside the multiplayer level loader. After both players continued a Surdana save and returned to title, they could join and move in City of the Damned on D3D11 and Surdana Meadow on WebGPU. Twice a client starting from New Game reached Inquisitor's Lair and then lost control at spawn 2. On the repeat we traced it to landing damage immediately after placement. A separate run with HD settings exited with a memory allocation error while still in singleplayer. We're leaving this open. If it happens again, the map and the last lines of the client's log would help distinguish these cases.
- ~~When the host leaves a multiplayer game, the other player doesn't get disconnected and can still move around in the level.~~ Quitting as host returned the client to title with a connection-loss message, and the client could host a new game.
- Other players are very laggy and sometimes seem to randomly teleport to different positions or ~~spawn points~~. We fixed two reproducible jumps. Creating a map thumbnail used to move the live player back to a spawn. Losing several movement updates could also make a partner who was running normally appear to teleport. With an 850 ms packet outage, the largest visible step fell from 2,349 to 178 game units on the same route. We’re keeping general lag open for a real-network retest. We also caught the host temporarily using the partner’s position while a creature chose its target. Twice the game interrupted that operation. The fix restored the host’s position both times and neither player saw a jump. We’re keeping the earlier sending-player teleport open until we can reproduce it with the old behavior.
- ~~Other players’ character models are always displayed in low poly, even when they're close to you.~~ We reproduced this in versus: the host saw a nearby Rynn with only 208 vertices. The same view now uses her full 1,628-vertex mesh. In co-op, a nearby Northmen Archer used all 1,020 vertices and correctly switched to 352 farther away.

**Small suggestion**

- ~~Add an option to set CPU affinity when compiling, for example using a hex mask like 0xFFF.~~ The Windows builder now accepts `-CpuAffinity 0xFFF`. We verified that the compiler subprocesses inherit the mask.
