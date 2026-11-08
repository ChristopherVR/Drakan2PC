#include "../src/lib/drakan_netplay_policy.h"
#include "../src/lib/drakan_coop_target.h"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace drakan_netplay::policy;
    assert(leaveAfterTitle(1, 8, false));                // confirmed manual quit
    assert(!leaveAfterTitle(1, 2, false));               // No returns to pause
    assert(!leaveAfterTitle(-1, 2, false));              // opening pause
    assert(!leaveAfterTitle(2, 1, false));               // opening confirmation
    assert(!leaveAfterTitle(8, 8, false));               // title already visible
    assert(!leaveAfterTitle(1, 8, true));                // automatic results return retains lobby/error
    assert(resumeLobby(true, true, false, false));       // loss after Playing needs error/Leave handling
    assert(resumeLobby(true, true, false, true));        // lobby rejection also needs acknowledgment
    assert(resumeLobby(true, false, true, true));        // connected results/lobby preserves next match
    assert(!resumeLobby(true, false, true, false));      // shortcut title under running match stays hidden
    assert(!resumeLobby(true, false, false, false));     // still connecting
    assert(!resumeLobby(false, true, false, true));      // no abandoned session to clean
    assert(arenaWorldWanted(true, true, false, true));   // deathmatch loading / playing / results
    assert(!arenaWorldWanted(true, true, true, true));   // co-op keeps the campaign
    assert(!arenaWorldWanted(true, true, false, false)); // lobby: the next title New Game is ordinary
    assert(!arenaWorldWanted(true, false, false, true)); // connection lost
    assert(!arenaWorldWanted(false, true, false, true)); // session torn down (Quit to Title): singleplayer again
    assert(std::string(disconnectNotice(0)) == "The host ended the match"); // heard a moment ago: the host left
    assert(std::string(disconnectNotice(3999)) == "The host ended the match");
    assert(std::string(disconnectNotice(4000)) == "Connection to the server lost"); // silent first: a timeout
    assert(std::string(disconnectNotice(10000)) == "Connection to the server lost");
    using Target = drakan_coop::ScopedAiTarget;
    std::array<uint8_t, Target::kTransformBytes> local{}, partner{}, door{};
    local.fill(1);
    partner.fill(2);
    door.fill(3);
    const auto initial = local;
    std::atomic<bool> redirected{false};
    struct Transfer {};
    try {
        Target outer(local.data(), partner.data(), redirected);
        {
            Target nested(local.data(), door.data(), redirected);
            assert(redirected && local == door);
        }
        assert(redirected && local == partner);
        throw Transfer{};
    } catch (const Transfer&) {
        assert(!redirected && local == initial);
    }
    std::puts("netplay_policy_test ok");
}
