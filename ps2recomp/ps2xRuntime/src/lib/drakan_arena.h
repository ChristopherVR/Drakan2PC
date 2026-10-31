// Drakan: multiplayer arena world mode (docs/reference/netcode.md "Multiplayer maps", journal 2026-10-31/mp-arenas.md).
//
// A deathmatch / team deathmatch match is played the way Drakan: Order of the Flame did it: a dedicated map, not the
// campaign. The map is a campaign (or mod pack) level's terrain; when the match level is up this module
//   * removes everything that belongs to the story: creatures and NPCs (every handle with creature hit points), the
//     cut scene player (0x10033), conversation handlers (0x100b4), the movie player (0x10016), level exits
//     (0x10037), quest items (0x10021), treasure (0x1006d), chests (0x100c5) and, in the first seconds, every loose
//     weapon / item that is not one of the map's pickups;
//   * moves the player to the map's spawn point (the server picks it: farthest from the other players);
//   * turns the map's pickups into host-authoritative respawning pickups (claim / state messages of runtime/net);
//   * keeps the player inside the map's bounds, gives the loadout, vetoes every save write, empties the journal.
// It is active while the session is a non-co-op match in the Loading / Playing / Results phase (drakan_netplay calls
// setSession). The map list (maps()) is what the multiplayer menu shows. Compiled only with the GS renderer.
#pragma once
#include "net/ArenaMaps.h"

#include <cstdint>
#include <string>
#include <vector>

class PS2Runtime;
namespace drakan_input {
class IGuest;
}

namespace drakan_arena {

// One row of the map list for the menu (stable order: built-in maps first, then the player's file, then mod packs).
struct MapEntry {
    std::string id;    // "surdana-meadow"
    std::string name;  // display name, the value to put in HostConfig.level / MatchConfig.level
    std::string level; // base level title ("Level 1A - Surdana")
    std::string description;
    std::string source; // "built-in", a file, "mod <id>"
    bool deathmatch = true, teamDeathmatch = true;
    int maxPlayers = 8;
    int spawns = 0, pickups = 0;
    bool hasBounds = false;
    float bounds[4] = {0, 0, 0, 0}; // minX, minZ, maxX, maxZ (world units), for a map preview
    bool installed = true;          // the base level exists on this disc / in the loaded mod packs
};

// The catalog is read once at start-up (install): configs/mp_maps.ini compiled in, `mp_maps.ini` next to the exe (and
// the file named by DRAKAN_MP_MAPS), then the `maps/*.ini` of every loaded mod pack (addPackMaps). Thread safe.
std::vector<MapEntry> maps();
const drakan::net::ArenaDef* findMap(const std::string& idOrName); // pointer stays valid until the next add
// Mod packs: parse `text` (the mp_maps.ini format) as the maps of `source` ("mod <id>"). Problems are logged and the
// offending arena dropped. Returns the number of maps added.
size_t addMaps(const std::string& text, const std::string& source);
// Which base levels exist (disc levels plus mod pack levels): maps whose level is not installed are listed with
// installed = false. Called once by drakan_overrides.
void setInstalledLevels(std::vector<std::string> levels);

void install(PS2Runtime& runtime);

// ---- session (drakan_netplay, any thread) ----
// The arena world mode on / off. `def` (may be null: a plain level match, the world is still stripped of the campaign)
// is copied. Cleared when the session leaves the Loading / Playing / Results phases or turns into a co-op match.
void setSession(bool active, const drakan::net::ArenaDef* def);
bool active();
bool hasSpawns(); // the map has spawn points the server chooses from
// The local player's player id (0 = unknown) for claim bookkeeping.
void setLocalPlayer(uint8_t id);

// ---- pickups (drakan_netplay forwards; any thread) ----
void onPickupState(uint16_t index, bool taken, uint8_t player); // net::PickupState
bool takePickupClaim(uint16_t& index);                          // net::PickupClaim to send; false when none

// ---- the PvP glue (drakan_pvp) ----
// EE thread. Move the controlled actor to spawn point `index` (the server's Respawn.spawnIndex); false when the map has
// none or the index is out of range (the caller falls back to its own choice). Also files the actor in its new
// collision cell and arms the first-spawn loadout.
bool placeAtSpawn(drakan_input::IGuest& guest, uint8_t* rdram, int index);

// EE thread, every PadMgr_Update (after drakan_pvp::beforePadMgr).
void beforePadMgr(drakan_input::IGuest& guest, uint8_t* rdram);

// Test aid (DRAKAN_ARENA_TEST_GOTO): true when the input glue should press Use (L1) once; the player stands on a
// pickup.
bool takeUseTap();

// Journal / quest entries must stay empty while this is true (the tab and pause screens clear them).
bool journalHidden();

} // namespace drakan_arena
