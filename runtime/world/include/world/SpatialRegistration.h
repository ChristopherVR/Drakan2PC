#pragma once

#include <cstdint>

namespace world {

// Dynamic actors created before their terrain cell is resident need another native
// registration attempt later. Limit native work across actors and space failed retries.
class SpatialRegistrationRetry {
public:
    template <typename Register>
    bool service(uint64_t tick, bool liveAtHomeAndUnclaimed, int32_t cell, bool& attemptedThisBoundary,
                 Register registerActor) {
        if (!liveAtHomeAndUnclaimed || cell != -1 || attemptedThisBoundary || tick < nextAttempt_)
            return false;
        attemptedThisBoundary = true;
        // More than 64 pickups x 3 boundaries between scans: later entries also get a turn.
        nextAttempt_ = tick + 240;
        return registerActor() >= 0;
    }

private:
    uint64_t nextAttempt_ = 0;
};

} // namespace world
