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
    // Custom game modes score points (kills, deaths and objectives each worth something). Without `hasScore` the points
    // are the kills, the standard deathmatch.
    bool hasScore = false;
    int32_t score = 0;
    // A mode with lives: what is left (kLivesUnlimited = no limit) and whether the player is out of the match.
    uint8_t lives = kLivesUnlimited;
    bool eliminated = false;
    int32_t points() const { return hasScore ? score : static_cast<int32_t>(kills); }
};

// Players still in the match before eliminated ones (a mode with lives), then most points (kills in a standard match),
// then most kills, then fewest deaths, then lowest player id (stable and identical on every machine).
inline void sortScoreboard(std::vector<ScoreRow>& rows) {
    std::sort(rows.begin(), rows.end(), [](const ScoreRow& a, const ScoreRow& b) {
        if (a.eliminated != b.eliminated)
            return !a.eliminated;
        if (a.points() != b.points())
            return a.points() > b.points();
        if (a.kills != b.kills)
            return a.kills > b.kills;
        if (a.deaths != b.deaths)
            return a.deaths < b.deaths;
        return a.player < b.player;
    });
}

// LastStanding: a mode with lives, one player (team) is left with lives.
enum class EndReason : uint8_t { None, FragLimit, TimeLimit, Ended, LastStanding };

struct MatchOutcome {
    EndReason reason = EndReason::None;
    PlayerId winner = kNoPlayer;   // deathmatch: the top player; kNoPlayer on a tie
    uint8_t winningTeam = kNoTeam; // team deathmatch: 0 / 1; kNoTeam on a tie
    bool tie = false;
};

// Winner of a match from the final scores. Last standing: the player (team) left with lives. Deathmatch: strictly more
// points (kills in a standard match), or equal points and kills and strictly fewer deaths, than every other player;
// anything else is a tie. Team deathmatch: the team with the most points (2..kMaxTeams teams; `teamScore` has kMaxTeams
// entries, unused teams are 0 and never win).
inline MatchOutcome decideOutcome(std::vector<ScoreRow> rows, MatchMode mode, const int32_t teamScore[kMaxTeams],
                                  EndReason reason, size_t teamCount = 2) {
    MatchOutcome o;
    o.reason = reason;
    if (rows.empty()) {
        o.tie = true;
        return o;
    }
    if (mode == MatchMode::TeamDeathmatch && reason == EndReason::LastStanding) {
        // The team that still has a player in the match.
        int left = -1;
        bool several = false;
        for (const ScoreRow& r : rows)
            if (!r.eliminated && r.team < kMaxTeams) {
                several = several || (left >= 0 && left != r.team);
                left = r.team;
            }
        if (left >= 0 && !several) {
            o.winningTeam = static_cast<uint8_t>(left);
            return o;
        }
    }
    if (mode == MatchMode::TeamDeathmatch) {
        size_t best = 0;
        bool tie = false;
        for (size_t t = 1; t < teamCount && t < kMaxTeams; ++t) {
            if (teamScore[t] > teamScore[best]) {
                best = t;
                tie = false;
            } else if (teamScore[t] == teamScore[best]) {
                tie = true;
            }
        }
        if (tie)
            o.tie = true;
        else
            o.winningTeam = static_cast<uint8_t>(best);
        return o;
    }
    sortScoreboard(rows);
    if (rows.size() > 1 && rows[0].eliminated == rows[1].eliminated && rows[0].points() == rows[1].points() &&
        rows[0].kills == rows[1].kills && rows[0].deaths == rows[1].deaths)
        o.tie = true;
    else
        o.winner = rows[0].player;
    return o;
}
// The standard two-team form (team kills).
inline MatchOutcome decideOutcome(std::vector<ScoreRow> rows, MatchMode mode, const uint16_t teamKills[2],
                                  EndReason reason) {
    const int32_t scores[kMaxTeams] = {teamKills[0], teamKills[1], 0, 0};
    return decideOutcome(std::move(rows), mode, scores, reason, 2);
}

} // namespace drakan::net
