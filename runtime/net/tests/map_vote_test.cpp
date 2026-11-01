// Map vote on the results screen (MapVote / MapVoteState): candidates, tallies, the winner on the host's buttons and on
// the results timer, withdrawn votes and leaving voters. docs/reference/netcode.md "Map vote".
#include "TestUtil.h"

using namespace drakan::net;
using namespace drakan::net::test;

namespace {

ArenaDef arena(const std::string& name, bool dm = true) {
    ArenaDef a;
    a.id = name;
    a.name = name;
    a.level = "Level 1A - Surdana";
    a.deathmatch = dm;
    a.teamDeathmatch = true;
    a.maxPlayers = 8;
    a.startHealth = 100;
    a.spawns.resize(2);
    return a;
}

ServerOptions options(size_t maps) {
    ServerOptions o;
    o.config.level = "alpha";
    o.config.fragLimit = 0;
    o.config.timeLimitSec = 0;
    o.config.maxPlayers = 6;
    o.respawnProtectionMs = 0;
    o.loadTimeoutMs = 2000;
    o.resultsDurationMs = 5000;
    o.startCountdownMs = 0;
    const char* names[] = {"alpha", "beta", "gamma", "delta", "epsilon"};
    for (size_t i = 0; i < maps; ++i)
        o.rotation.push_back(arena(names[i]));
    o.hasArena = true;
    o.arena = o.rotation[0];
    return o;
}

struct Results {
    Rig rig;
    MatchClient *a, *b, *c;
    explicit Results(ServerOptions o) : rig(std::move(o)) {
        a = &rig.addClient("A");
        b = &rig.addClient("B");
        c = &rig.addClient("C");
        rig.run(200);
        CHECK(rig.server->startMatch(rig.now));
        rig.run(60);
        for (MatchClient* p : {a, b, c})
            p->sendLoadDone();
        rig.run(100);
        CHECK(rig.server->phase() == MatchPhase::Playing);
        rig.server->endMatch(rig.now);
        rig.run(100);
        CHECK(a->phase() == MatchPhase::Results);
    }
};

void wire() {
    MapVoteState s;
    s.candidates = {"Surdana Meadow", "Skeleton Square"};
    s.tally = {2, 1};
    s.winner = 1;
    MapVoteState back;
    CHECK(decode(encode(s), back) && back.candidates == s.candidates && back.tally == s.tally && back.winner == 1);
    s.winner = 2; // out of range
    CHECK(!decode(encode(s), back));
    MapVote v;
    v.choice = 2;
    MapVote vb;
    CHECK(decode(encode(v), vb) && vb.choice == 2);
    v.choice = 3; // only three candidates
    CHECK(!decode(encode(v), vb));
    v.choice = kNoVote;
    CHECK(decode(encode(v), vb) && vb.choice == kNoVote);
}

void candidatesAndTally() {
    Results r(options(5));
    const MapVoteState& st = r.a->mapVote();
    // Three candidates: the maps after the current one, in rotation order.
    CHECK(st.candidates.size() == 3 && st.candidates[0] == "beta" && st.candidates[1] == "gamma" &&
          st.candidates[2] == "delta");
    CHECK(r.b->mapVote().candidates == st.candidates);
    CHECK(st.winner == kNoVote && st.tally == std::vector<uint8_t>({0, 0, 0}));
    r.a->sendMapVote(2);
    r.b->sendMapVote(1);
    r.c->sendMapVote(2);
    r.rig.run(100);
    CHECK(r.b->mapVote().tally == std::vector<uint8_t>({0, 1, 2}));
    CHECK(r.a->myMapVote() == 2 && r.b->myMapVote() == 1);
    // A vote can be changed and withdrawn; a bad index is not sent.
    r.c->sendMapVote(0);
    r.b->sendMapVote(kNoVote);
    r.rig.run(100);
    CHECK(r.a->mapVote().tally == std::vector<uint8_t>({1, 0, 1}));
    CHECK(!r.a->sendMapVote(3));
    // Leaving takes the vote with it.
    r.rig.clients[2]->disconnect();
    r.rig.run(300);
    CHECK(r.a->mapVote().tally == std::vector<uint8_t>({0, 0, 1}));
    // The results timer applies the leading map when the lobby comes back.
    r.rig.run(5200);
    CHECK(r.rig.server->phase() == MatchPhase::Lobby);
    CHECK(r.rig.server->config().level == "delta" && r.a->config().level == "delta");
    CHECK(r.a->mapVote().candidates.empty());
}

void tiesAndNoVotes() {
    Results r(options(4));
    r.a->sendMapVote(1);
    r.b->sendMapVote(0);
    r.rig.run(100);
    r.rig.run(5200); // 1:1, the lowest index wins
    CHECK(r.rig.server->config().level == "beta");
    // No votes: the level stays.
    Results q(options(4));
    q.rig.run(5200);
    CHECK(q.rig.server->config().level == "alpha");
}

void hostNextMapUsesVote() {
    Results r(options(4));
    r.b->sendMapVote(1); // gamma
    r.rig.run(100);
    r.a->sendResultsAction(ResultsChoice::NextMap); // a is the host
    r.rig.run(100);
    CHECK(r.rig.server->phase() == MatchPhase::Loading && r.rig.server->config().level == "gamma");
    CHECK(r.b->config().level == "gamma");
    // Without a vote the button keeps its old meaning (the next map of the rotation).
    Results q(options(4));
    q.a->sendResultsAction(ResultsChoice::NextMap);
    q.rig.run(100);
    CHECK(q.rig.server->config().level == "beta");
}

void fewMaps() {
    Results two(options(2)); // one other map: the current one fills the list
    CHECK(two.a->mapVote().candidates.size() == 2 && two.a->mapVote().candidates[0] == "beta" &&
          two.a->mapVote().candidates[1] == "alpha");
    Results one(options(1)); // nothing to choose from
    CHECK(one.a->mapVote().candidates.empty());
    one.a->sendMapVote(0);
    one.rig.run(100);
    CHECK(one.rig.server->voteCandidates().empty());
    // Co-op never votes.
    ServerOptions o = options(3);
    o.config.mode = MatchMode::Coop;
    Results co(o);
    CHECK(co.a->mapVote().candidates.empty());
}

} // namespace

int main() {
    wire();
    candidatesAndTally();
    tiesAndNoVotes();
    hostNextMapUsesVote();
    fewMaps();
    std::puts("net_map_vote_test: ok");
    return 0;
}
