#pragma once
#include <cstdint>
#include <string>

namespace gsr {
// Evaluation alone is not evidence that the selected image was presented.
// Record the actual display circuit's epoch only after successful Present.
struct TemporalPresentation {
    uint64_t epoch = 0, processedPresents = 0, bypassedPresents = 0;
    bool seen = false, processed = false, world = false, succeeded = false;
    void reset() { *this = {}; }
    void present(bool nr, bool gameWorld, uint64_t imageEpoch, bool success) {
        seen = true;
        succeeded = success;
        world = gameWorld;
        processed = success && nr && gameWorld && imageEpoch != 0;
        epoch = imageEpoch;
        if (processed)
            ++processedPresents;
        else if (success && gameWorld)
            ++bypassedPresents;
    }
    std::string status(const std::string& detail) const {
        if (!seen)
            return "DLSS 5 enabled; no completed presentation yet. " + detail;
        if (!succeeded)
            return "DLSS 5 presentation failed. " + detail;
        if (processed)
            return "DLSS 5 active: processed gameplay image presented.";
        if (!world)
            return "DLSS 5 enabled; no new eligible gameplay world (menu/loading).";
        return "DLSS 5 enabled but BYPASSED in the last gameplay frame. " + detail;
    }
};
} // namespace gsr
