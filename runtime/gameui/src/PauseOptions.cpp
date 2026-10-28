#include "gameui/PauseOptions.h"

#include <algorithm>

#include "gameui/Addresses.h"

namespace gameui {

PauseOptionRange pauseOptionRange(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
        case PauseOption::MusicVolume:
            return {addr::options::kVolumeMin, addr::options::kVolumeMax};
        case PauseOption::Scheme:
            return {0, addr::options::kSchemeCount - 1};
        default:
            return {0, 1};
    }
}

int32_t clampPauseOption(PauseOption o, int32_t v) {
    const PauseOptionRange r = pauseOptionRange(o);
    return std::clamp(v, r.lo, r.hi);
}

uint32_t pauseOptionAddress(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
            return addr::options::kMasterVolume;
        case PauseOption::MusicVolume:
            return addr::options::kMusicVolume;
        case PauseOption::Subtitles:
            return addr::options::kSubtitles;
        case PauseOption::Stereo:
            return addr::options::kStereo;
        case PauseOption::Scheme:
            return addr::options::kControllerScheme;
        case PauseOption::Vibration:
            return addr::options::kVibration;
        case PauseOption::ReversePitch:
            return addr::options::kReversePitch;
    }
    return 0;
}

const char* pauseOptionName(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
            return "MasterVolume";
        case PauseOption::MusicVolume:
            return "MusicVolume";
        case PauseOption::Subtitles:
            return "Subtitles";
        case PauseOption::Stereo:
            return "Stereo";
        case PauseOption::Scheme:
            return "Scheme";
        case PauseOption::Vibration:
            return "Vibration";
        case PauseOption::ReversePitch:
            return "ReversePitch";
    }
    return "?";
}

bool readPauseOptions(const IGuestMemory& m, PauseOptionsState& out) {
    bool ok = true;
    for (int i = 0; i < kPauseOptionCount; ++i) {
        const auto o = static_cast<PauseOption>(i);
        const int32_t raw = m.readS32(pauseOptionAddress(o));
        const PauseOptionRange r = pauseOptionRange(o);
        if (raw < r.lo || raw > r.hi)
            ok = false;
        out.set(o, raw);
    }
    out.valid = ok;
    return ok;
}

} // namespace gameui
