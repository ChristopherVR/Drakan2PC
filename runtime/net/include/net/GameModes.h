#pragma once
// Custom game modes: a small data-driven descriptor, `[mode <id>]` sections in the same INI text as the arenas
// (configs/mp_maps.ini, a player's mp_maps.ini, mod packs' maps/*.ini). Format and what the runtime honours today:
// docs/reference/netcode.md "Custom game modes". Std only.
//
//   [mode instagib-lite]
//   name = Instagib Lite          ; shown in menus
//   description = ...
//   base = dm                     ; dm | tdm: the engine rule set the mode builds on (default dm)
//   teams = 0                     ; 0 free for all, or 2..4 (needs base = tdm)
//   score_kill = 2                ; score per kill (default 1), per death (default 0), per objective (default 0)
//   score_death = 0
//   score_objective = 0
//   frag_limit = 20               ; 0 none, <= 1000
//   time_limit = 600              ; seconds, 0 none, <= 7200
//   respawn_delay = 1             ; seconds 0..60 (default 3)
//   friendly_fire = off
//   start_health = 0              ; 0 = the map's
//   loadout = 0x20abc             ; resource ids added to the map's loadout
//   weapons = 0x20abc             ; allowed weapon resource ids, empty = all
//   objective = capture points=1  ; repeatable: capture | hold [points=N] [seconds=N] [radius=N]
#include "net/ArenaMaps.h"
#include "net/Protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace drakan::net {

struct ModeObjective {
    enum class Kind : uint8_t { Capture, Hold };
    Kind kind = Kind::Capture;
    uint32_t points = 1;
    uint32_t seconds = 0; // hold: time inside a zone for one completion
    float radius = 0;     // 0 = the marker's own radius
};

struct ModeDef {
    std::string id;
    std::string name;
    std::string description;
    std::string source;
    bool teamBase = false; // base = tdm
    uint8_t teams = 0;
    int32_t scoreKill = 1, scoreDeath = 0, scoreObjective = 0;
    uint16_t fragLimit = 20;
    uint16_t timeLimitSec = 600;
    uint8_t respawnDelaySec = 3;
    bool friendlyFire = false;
    uint16_t startHealth = 0; // 0 = the map's
    std::vector<uint32_t> loadout;
    std::vector<uint32_t> weapons;
    std::vector<ModeObjective> objectives;
    // Teams the mode plays with: 0 for a free for all (base = dm), else teams (2 when base = tdm and none was given).
    uint8_t teamCount() const { return !teamBase ? 0 : teams >= 2 ? teams : 2; }
};

class ModeCatalog {
public:
    // Parses the [mode <id>] sections of `text` (other sections are skipped); an existing id is replaced. Errors are
    // "<source>:<line>: message"; an invalid mode is dropped as a whole. Returns the number added or replaced.
    size_t add(const std::string& text, const std::string& source, std::vector<std::string>* errors = nullptr);
    const std::vector<ModeDef>& modes() const { return modes_; }
    const ModeDef* find(const std::string& idOrName) const; // id or display name, case-insensitive
    void clear() { modes_.clear(); }

private:
    std::vector<ModeDef> modes_;
};

// The match options of a mode: mode (Deathmatch / TeamDeathmatch by base), fragLimit (the limit on the mode's score,
// see netcode.md), timeLimitSec, respawnDelayMs, startHealth (when non-zero) and modeId. The rest of a ModeDef
// (scoring, teams, friendly fire, objectives) the server reads from ServerOptions::modes by that id.
void applyModeToConfig(const ModeDef& mode, MatchConfigData& config);

// Whether `mode` can be played on `arena`: the map lists the mode id (`modes = ..., <id>`) or supports the mode's base
// (dm / tdm); a capture objective needs a flag and a base marker, a hold objective a zone marker; 3..4 teams need the
// map's player limit to allow that many players. `why` (optional) gets the reason when it does not fit.
bool modeFitsArena(const ModeDef& mode, const ArenaDef& arena, std::string* why = nullptr);

} // namespace drakan::net
