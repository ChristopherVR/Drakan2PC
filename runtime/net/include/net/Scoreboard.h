#pragma once
// Scoreboard rows and the outcome of a finished match, shared by MatchServer and MatchClient (and so by whatever UI
// draws the scoreboard). std only. See docs/reference/netcode.md "PvP".
#include "net/Protocol.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace drakan::net {

struct ScoreRow {
    PlayerId player = kNoPlayer;
    std::string name;
    uint8_t team = kNoTeam;
    uint16_t kills = 0;
    uint16_t deaths = 0;
    int32_t health = 0;
    bool alive = false;
};

// Most kills first, then fewest deaths, then lowest player id (stable and identical on every machine).
inline void sortScoreboard(std::vector<ScoreRow>& rows) {
    std::sort(rows.begin(), rows.end(), [](const ScoreRow& a, const ScoreRow& b) {
        if (a.kills != b.kills)
            return a.kills > b.kills;
        if (a.deaths != b.deaths)
            return a.deaths < b.deaths;
        return a.player < b.player;
    });
}

enum class EndReason : uint8_t { None, FragLimit, TimeLimit, Ended };

struct MatchOutcome {
    EndReason reason = EndReason::None;
    PlayerId winner = kNoPlayer;   // deathmatch: the top player; kNoPlayer on a tie
    uint8_t winningTeam = kNoTeam; // team deathmatch: 0 / 1; kNoTeam on a tie
    bool tie = false;
};

// Winner of a match from the final scores. Deathmatch: strictly more kills, or equal kills and strictly fewer deaths,
// than every other player; anything else is a tie. Team deathmatch: the team with more kills.
inline MatchOutcome decideOutcome(std::vector<ScoreRow> rows, MatchMode mode, const uint16_t teamKills[2],
                                  EndReason reason) {
    MatchOutcome o;
    o.reason = reason;
    if (rows.empty()) {
        o.tie = true;
        return o;
    }
    if (mode == MatchMode::TeamDeathmatch) {
        if (teamKills[0] == teamKills[1])
            o.tie = true;
        else
            o.winningTeam = teamKills[0] > teamKills[1] ? 0 : 1;
        return o;
    }
    sortScoreboard(rows);
    if (rows.size() > 1 && rows[0].kills == rows[1].kills && rows[0].deaths == rows[1].deaths)
        o.tie = true;
    else
        o.winner = rows[0].player;
    return o;
}

} // namespace drakan::net
