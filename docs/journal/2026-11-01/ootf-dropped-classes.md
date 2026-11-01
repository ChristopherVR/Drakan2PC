# OotF dropped PC objects: what was fixed, what is accepted (2026-11-01)

Starting point: `ootf-props-audit.md`, 778 objects of 16312 dropped ("unsupported" class rows, 798 by the per-row sum used below, which also
counts the rows of the 23 levels separately). Probes: `build/ootf-drops/` (`runall.sh <dir>` re-runs the audit for all 23 levels in
about 80 s; `cmp.py <dir>` compares with `build/ootf-props/after`). Kept objects 15534 -> 15698.

| category | objects before | after | decision |
|---|---:|---:|---|
| Water Droplet (144) | 108 | 0 | (a) PS2 numbers 144 as shields, but has **Water Drip, engine 146**, with the same Type / Period / Deviation fields (+ Max Particles). `PS2_SUBSTITUTES` 144 -> 146, forced (`FORCED_SUBSTITUTES`) because PS2 class 144 exists |
| Pressure Plate (29) | 42 (+9 not counted: 51 total in the audit text) | 0 | (a, partial) The PS2 plate class lives only in LEVEL4ON / LEVEL8OS, which the template does not list. The plate stays as a **static prop with the PC model** (Ground Object 17 clone, `MODEL_STANDINS`); it cannot be pressed. A detector + door link was designed (scenery + Detector 61 clone at the same place, links copied) but not built: every plate in the PC levels targets portcullises / doors, which are themselves dropped (notemplate), so nothing could be observed |
| Pirate Ship (130) | 3 | 0 | (a) static Ground Object with the ship model (the spline vehicle does not move) |
| Model-less engine 17 markers (Dragon Detector / Location Object / Seeking Class / Fly Object ...) | 11 | 0 | coverage floor 0 for model-less scenery clones: inert markers whatever the template fields are |
| Music triggers (129) | 20 | 0 | audit false positive: `resolverFor` + `applyMusicTriggers` already rewrite them to Triggered Music Loop; the audit now says `name-mapped` |
| MP pads / crystals (18, 127, 128, 86, 126) | 108 | 108, reason `arena-export` | (c) no PS2 engine class. Rynn pads were already exported as arena spawns by `ootfArenaFromPc`; dragon pads, red dragon pads, Soul Crystal, dragon invulnerability are now exported as arena **markers** (base / flag / zone) in `maps/<level>.ini`. The game does not read markers yet (netcode.md, MotD is engine work) |
| Map Object / Map Trigger (104) | 54 | 54 | (c) PC fields are atlas coordinates of the mission text bitmap (Mission Bitmap ID, Source X/Y, Width/Height); PS2 uses text ids (Journal Entry, Map Text). Only the map position and sound match. A clone would show a wrong or empty journal entry |
| Fog / Sky changers (143) | 31 | 31 | (c) the PS2 classes (7, all engine 143) have **no fields**: the fog colour belongs to the class record / hard coded, PS2 levels place them as scaled boxes with no params. The PC positive / negative fog colours cannot be expressed |
| Quickening Effect (80) | 4 | 4 | (c) PS2 Power FX shares 1 of 16 fields; a default PS2 particle would be a different effect |
| Class Messenger (137) | 1 | 1 | (c) Message Relay 224 exists (Incoming / Outgoing Message) but the PC object has no links and a `Receiver Class` that needs "all objects of class X" links built at convert time |
| Combination Lock (123) | 1 | 1 | (c) PS2 Combo Lock (223) lives in LEVEL4ON, not listed by the template |
| Ambient Sound (131) | 1 | 1 | (c) sound slot fields differ and the sound ids cannot be converted anyway |
| Fall01 / Black Box / Splash 03 | 8 | 8 | (c) PC models missing on the disc |
| Goblin 88, Succubus 100, Queen 101, Magic Crystal 91 | 0 | 0 | already stand-ins before this change (clones of Orc 49, flier 30, mana 171) |
| Doors, switches, ladders, ropes, flames, Kill Box (no model-bearing template) | 406 | 406 | not in this task's scope; needs the host-ODB model work (ootf-props-audit.md item 5) |

Result (class rows, objects): dropped 798 -> 614, of which 108 are exported as arena data and 406 wait for the door/switch hosting
work, leaving 100 accepted drops. Files: `ootfclass.ts` (substitutions, `MODEL_STANDINS`, `FORCED_SUBSTITUTES`, floor for model-less
scenery), `ootfpipeline.ts` (audit reasons), editor-core `ootf.ts` (markers).
