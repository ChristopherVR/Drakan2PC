# Order of the Flame multiplayer parity audit

## Baseline before the fix

Compared the retail install's ten multiplayer LVLs, their object class records, the three retail `.lfl` lists and `ReadMe.txt`
with the arena definitions and converted LVLs in `dist/DrakanPC/mods/ootf-levels.zip`. Ran the installed `drakan.exe` with the
same pack in isolated one-player arena sessions on Relic and Grotto of Death. The two runs covered level entry and initial item
population. The file comparison covered all ten maps, including those two. No network match or visual survey of all ten maps was run.

## Per-map results

The retail starts column counts the dragon launch records on Air Only maps and the Rynn launch records on the other maps.
Retail power-ups counts placed multiplayer power-up, invulnerability and health actors. It excludes loose weapon objects,
decorative weapon pads and the Soul Crystal. Port pickups counts arena-managed entries in `maps/*.ini` before startup. The two
pickup columns count different kinds of records. An item present in a converted LVL does not establish that it appears in a match.

| Retail group | Map | Retail starts | Port starts | Retail power-ups | Port pickups |
| --- | --- | ---: | ---: | ---: | ---: |
| Air Only | Alwarren of Death | 5 dragon | 5 dragon-pad positions | 8 | 0 |
| Air Only | Arena of Death | 5 dragon | 5 dragon-pad positions | 1 | 0 |
| Air Only | Grotto of Death | 8 dragon | 8 Rynn-pad positions | 7 | 1 |
| Air Only | Nemesis | 4 dragon | 4 Rynn-pad positions | 1 | 0 |
| Ground Only | Hill of Fire | 12 | 12 | 23 | 4 |
| Ground Only | Relic | 18 | 9 | 28 | 8 |
| Ground Only | Sanctuary | 15 | 8 | 13 | 3 |
| Master of the Dragon | Island of Death | 6 | 6 | 33 | 5 |
| Master of the Dragon | Rise to Power | 9 | 9 | 40 | 6 |
| Master of the Dragon | Volcano of Death | 5 | 5 | 25 | 5 |

## Game rules

- The retail `.lfl` files define Air Only, Ground Only and Master of the Dragon as map groups with their own ordered lists.
  The retail README describes cycling levels in the chosen group after a match and choosing two to four teams or no teams.
  The port gives all ten maps the same `dm, tdm` flags. The host menu's group selector filters by built-in or custom map, and
  standard Team Deathmatch supports two teams with friendly fire disabled. The dragon mode cards are unavailable.
  See [the multiplayer menu](../../../runtime/gameui/src/Multiplayer.cpp) and [match rules](../../../runtime/net/src/MatchServer.cpp).
- The importer uses the exported pad count as the map's player limit, capped at 12. The host applies that limit to the match,
  so Nemesis caps at four players, Volcano of Death at five, and Sanctuary at eight. The retail README presents the player
  limit as a host setting. See [the arena importer](../../../editor/packages/editor-core/src/ootf.ts) and
  [host configuration](../../../ps2recomp/ps2xRuntime/src/lib/drakan_netplay.cpp).
- The retail Master of the Dragon starts players on foot with a single unclaimed dragon. The Soul Crystal gives control of it.
  A kill transfers ownership to the killer. If the rider dies without a killer, the dragon becomes unclaimed and the crystal
  returns. In the port, Island of Death, Rise to Power and Volcano of Death register the Soul Crystal and red launch pad as
  generic markers and ship no rules that use them. See [the arena importer](../../../editor/packages/editor-core/src/ootf.ts)
  and [the original data study](../2026-10-31/ootf-data-study.md#3-modes-and-rules-as-defined-in-data).

## Items and respawns

- The port defines 32 arena pickups total, all Great Swords, Iron Clubs or Steel Clubs at the source Weapon Pad locations,
  with a uniform 30-second respawn. It also defines a club-and-axe starting loadout on every map. At the same spots the retail
  game can place a named weapon, armor, a random power-up or a dragon item. Sanctuary has loose axe, cutlass and hammer
  power-ups with 10-second respawns. Rise to Power has 33 Greater Life Crystal actors with 20-second respawns and two
  DragonStrike actors with 60-second respawns. Grotto of Death places a 90-second dragon invulnerability pickup at the pad
  where the converted arena spawns its Great Sword. All three retail invulnerability pickups are marker-only in the port.
- Many source item placements survive in the converted LVL with the same object ID and horizontal coordinates. Engine class
  120, the PC multiplayer power-up, is retargeted to PS2 engine class 10. Placement records alone cannot provide that class's
  original multiplayer effects or clocks. Retail Relic has eight Rynn health boosts, six life crystals, and four named special
  weapon power-ups. The converted level still has those records, but the Relic runtime snapshots show no health or potion
  actors before or after arena initialization. The arena grants the club and axe and spawns its own weapon pickups.
- Relic pickup 6 is a separate runtime failure. The factory creates its Great Sword at `30387 202120 34483`. The loose-item
  sweep removes that same actor before the pickup binds, and the pickup is then disabled with `never registered`. Seven other
  pickup entries reached the present state in that run. See [the arena sweep and pickup binding](../../../ps2recomp/ps2xRuntime/src/lib/drakan_arena.cpp).

## Starts and geometry

- The importer keeps pad X/Z for the chosen pad type, but changes height using the highest floor and exports yaw `0` for
  every start. Alwarren of Death moves one dragon start up by about 21,202 world units. Rise to Power moves one Rynn start
  up by 16,101 units. Sanctuary loses seven co-located pads with distinct headings. Relic's nine co-located pairs match in
  height and heading. The eight dragon starts in Grotto and four in Nemesis have different horizontal positions from the Rynn
  pads the port uses. On Grotto, the converted Dragon object also prevents the mounted-start fallback from running. The
  installed run started on foot and armed Rynn with the club. See [the arena importer](../../../editor/packages/editor-core/src/ootf.ts)
  and [the mounted-start fallback](../../../editor/packages/formats/src/ootfpipeline.ts).
- Every map keeps the original number of terrain layers. Compared layers retain their diagonals. In Alwarren of Death, two
  water layers are cropped and four other water layers have adjusted vertex heights. The conversion turns empty water
  layers into empty floor records on Rise to Power, Volcano of Death and Island of Death. These format changes establish
  that the terrain data differs. They do not by themselves show a route or collision regression.

The importer test suite passed all six existing cases. Its weapon test checks the generic Weapon Pad substitution. The
test does not exercise retail pickup parity, Air Only starts or Master of the Dragon rules.

## Implementation and verification

The regenerated pack carries 87 retail spawn transforms and imports all ten map positions in the original group rotation.
The data comparison matched each exported launch pad's position and facing and 181 of 182 authored pickup records. For the
181 imported records, the positions and respawn delays match the retail level and class data. Hill of Fire has 22 of its
23 records because Fire Summon references a PC effect without a converted equivalent.

Two game clients joined all ten maps. All four Air Only maps started the clients mounted. Ground Only and Master of the
Dragon started both clients on foot. In Arena of Death the host recorded consecutive dragon-health claims 5.03 to 5.09
seconds apart, including the next client update after the five-second timer. In Sanctuary the original Axe was collected,
removed on the peer, and collected again 10.18 seconds later after the authored ten-second respawn. In Island of Death the
Soul Crystal granted native control of the dragon to the collector, the peer rendered that dragon, and both clients
continued running through the end of the timed check. The inventory sweep now checks the mounted rider's inventory before
removing items, which resolved the crash observed just after first mounting. A controlled hit claim from the second client
killed the rider and transferred the dragon to that client. The new rider's first settled pose matched the former rider's
last mounted position when the test kept the outgoing dragon ghost clear of the handoff. With the test ghost pinned near
the rider, the native collision system moved the new dragon away from the handoff point. Volcano of Death had one load
crash and passed the subsequent two-client retry. The crash location also appears in earlier level-loading investigations.

The match-server test exercises exclusive ownership, transfer of the dragon and its last mounted position on a kill,
release after an environmental death report, and health and respawn rules. Further parity work includes neutral dragon
aggression and the automatic activation and replication of the timed potion effects. Health and
protection rewards are collected by proximity. Some native items also fall back to proximity and inventory grant when the
actor factory cannot register their visible world model. Source terrain differences from the original audit remain.

## Pickup displays, automatic effects and the hostile dragon

The next conversion matched all 182 retail pickup records across the ten maps. Every record has a display resource
made from the supplied game data. The display is a separate scenery actor, so the server's taken and respawn state can
show or hide it independently of the native item instance granted to the winner. The Fire Summon display passed through
the native draw hook, was suppressed when collected and drew again after the 30-second timer.

Hill of Fire's `FIRE!` item has `Discharge On Contact` set. Its effect uses a 2.8-second fire ring with an initial
velocity of 11,000 world units per second and damage of 90,000. The converter resolved the supplied effect as native
engine class 90. Both game clients created the ring at the cast site. A peer on the nearby Rynn Health platform received
the lethal hit from the server as the ring expanded.

On Relic, two separate Random Power pads were restricted in the private test definition to exercise the two original
outcomes deterministically. Both players activated their effects on collection. The invisible player disappeared on the
peer and reappeared after 30 seconds. The invulnerable player rejected hits during the 20-second effect and took them
again afterward. The shipped map still selects from the full original weighted list.

Hill of Fire's QuickSpeed pickup automatically started its five-second server timer and dispatched native potion
type 2 on collection. Both clients remained in the match across its twenty-second respawn. The movement probe did not
establish a usable running baseline, so these logs verify activation and timing. They do not measure the speed increase.

Island of Death's unclaimed dragon flew toward nearby players and hit both during a two-client run. A separate run
showed the attacks stopping when the Soul Crystal granted native control. The server drives this behavior and sends
its pose and breath events. Its pursuit and firing behavior approximate the retail AI.
