#pragma once
#include <cstdint>
namespace gsr {
// Sharpening is a final RGB-only operation, independent of temporal history.
// Reapplying unrelated video settings must not recreate NGX/NR resources.
constexpr bool upscaleNeedsReset(uint32_t previousMode, uint32_t previousPreset, uint32_t mode, uint32_t preset,
                                 bool fault) {
    return fault || previousMode != mode || (mode != 0 && previousPreset != preset);
}
} // namespace gsr
