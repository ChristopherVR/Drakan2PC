// Host-only parts of drakan_clip_subtitles (no guest or runtime dependency; unit tested).
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <settings/Subtitles.h>

namespace drakan_clip_subtitles::detail {

// The sound instances whose pages were already started, by play handle. A handle is unique per play and the gain
// update that reports it runs every tick, so a small ring of the newest handles is enough.
class StartedHandles {
public:
    bool seen(uint32_t handle) const {
        for (const uint32_t h : ring_)
            if (h == handle)
                return true;
        return false;
    }
    void add(uint32_t handle) {
        ring_[next_] = handle;
        next_ = (next_ + 1) % ring_.size();
    }

private:
    std::array<uint32_t, 64> ring_{}; // handles are never 0 (bit 31 is set)
    size_t next_ = 0;
};

// cp1252 / Latin-1 descriptor text -> UTF-8 for the overlay font (0x80..0x9f are not used by subtitle text).
inline std::string latin1ToUtf8(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (const unsigned char c : in) {
        if (c < 0x80)
            out += static_cast<char>(c);
        else {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// Pages of a descriptor text; `clipSeconds` (the descriptor's clip length) ends a last page that has no end mark.
inline std::vector<dsettings::SubCue> clipCues(const std::string& descriptorText, float clipSeconds) {
    const int endMs = std::isfinite(clipSeconds) && clipSeconds > 0.0f && clipSeconds < 3600.0f
                          ? static_cast<int>(clipSeconds * 1000.0f)
                          : 0;
    return dsettings::parseClipCues(latin1ToUtf8(descriptorText), endMs);
}

} // namespace drakan_clip_subtitles::detail
