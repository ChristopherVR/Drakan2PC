# Co-op party HUD: partner health on the wire (2026-11-01)

- Added `Vitals` (message 29, no version bump, see netcode.md "Vitals"). Own health goes C>S, the server relays all entries with
  Status. Tests: `net_lobby_test` (round trip, no forging), `net_protocol_test` (type range).
- Party panel (`runtime/gameui/src/Multiplayer.cpp`): rows are now 560x88 design units, 30 px names, 26 px health bar with
  "hp / max" numbers, DOWN + respawn seconds in large type, optional dragon bar.
- Arokh's health: found and sent since the follow-up (float at dragon data + 0x6b4), see [coop-dragons-health-mount](coop-dragons-health-mount.md).
