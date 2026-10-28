// The game options the modern pause menu's Audio and Controller pages show (phase 2 of
// docs/journal/2026-10-25/modern-pause-menu.md): seven s32 fields of the native options object, read from guest memory.
// Read only; the pause screen changes them through PauseActions the glue carries out on the native widgets.
#pragma once
#include <cstdint>

#include "gameui/GuestMemory.h"

namespace gameui {

enum class PauseOption : uint8_t {
    MasterVolume, // 0..100
    MusicVolume,  // 0..100
    Subtitles,    // 0 / 1
    Stereo,       // 0 / 1
    Scheme,       // controller scheme 0..2 = A / B / C
    Vibration,    // 0 / 1
    ReversePitch, // 0 / 1 (the native "Reverse Pitch", invert Y)
};
inline constexpr int kPauseOptionCount = 7;

struct PauseOptionRange {
    int32_t lo = 0, hi = 1;
};
PauseOptionRange pauseOptionRange(PauseOption o);
int32_t clampPauseOption(PauseOption o, int32_t v);
uint32_t pauseOptionAddress(PauseOption o); // addr::options::k...
const char* pauseOptionName(PauseOption o);

struct PauseOptionsState {
    // All seven fields read and in range. When false the pause screen greys the Audio / Controller rows out.
    bool valid = false;
    int32_t values[kPauseOptionCount] = {100, 100, 1, 1, 0, 1, 0};

    int32_t get(PauseOption o) const { return values[static_cast<int>(o)]; }
    void set(PauseOption o, int32_t v) { values[static_cast<int>(o)] = clampPauseOption(o, v); }
    bool operator==(const PauseOptionsState&) const = default;
};

// Reads the seven fields. A value out of its range (no options object yet, foreign memory) makes the state invalid;
// the values are stored clamped either way. Returns `out.valid`.
bool readPauseOptions(const IGuestMemory& m, PauseOptionsState& out);

} // namespace gameui
