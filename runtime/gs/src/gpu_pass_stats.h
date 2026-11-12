// GPU time per pass of a presented frame, from timestamps taken at pass boundaries (backend neutral, unit tested).
// A backend records one mark per boundary ("from here on the GPU works for pass X") and the tick of the frame's
// end; this header turns the marks into milliseconds per pass and collects frames into a window that reports the
// median and the mean of each pass. GSR_GPU_PASS_TIMING turns the Direct3D 11 recorder on (d3d11_gpu_passes.h).
//
// What the numbers mean: a timestamp is taken when the GPU reaches that point of the command stream. Passes that are
// submitted in one go (the upscaler, the copies around it, the display and window blits) therefore measure GPU work.
// The world pass is fed draw by draw from the CPU, so its interval also contains the time the GPU waited for the
// next draw: on a CPU-bound frame it is an upper bound, not the raster cost.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gsr::gpupass {

enum class Pass : uint8_t {
    Frame,       // everything not named below (menus, target refreshes, the Present call itself)
    World,       // the engine's world phase: scene raster
    WorldEntry,  // upscaler world entry: depth and backdrop shrunk into the render rectangle
    Guides,      // colour, depth and camera motion prepared for the upscaler
    ModelMotion, // posed-model motion raster
    Evaluate,    // the upscaler itself
    Convert,     // output-sized guide reconstruction after the upscaler
    Compose,     // upscaled colour, alpha and depth written back to the world target
    AfterWorld,  // HUD and everything else the game draws after the world
    Display,     // display circuits composed into the display image
    PresentBlit, // display image scaled into the window
    Overlay,     // host overlay (settings, FPS counter)
    Count
};
constexpr size_t kPassCount = static_cast<size_t>(Pass::Count);

constexpr const char* name(Pass pass) {
    constexpr const char* names[kPassCount] = {"frame",   "world",   "worldEntry", "guides",  "modelMotion", "evaluate",
                                               "convert", "compose", "afterWorld", "display", "presentBlit", "overlay"};
    return static_cast<size_t>(pass) < kPassCount ? names[static_cast<size_t>(pass)] : "?";
}

struct Mark {
    Pass pass = Pass::Frame;
    uint64_t ticks = 0;
    bool worldBegin = false; // the engine opened a world here (worlds and presented frames are not one to one)
};

struct Frame {
    std::array<double, kPassCount> ms{};
    double total = 0;    // first mark to the frame's end: the frame interval on the GPU's clock
    uint32_t worlds = 0; // worlds begun in this frame
};

// The time from each mark to the next one (the last: to endTicks) goes to the mark's pass. False when the marks are
// unusable (none, a zero frequency, or ticks that run backwards: the caller drops the frame).
inline bool attribute(const Mark* marks, size_t count, uint64_t endTicks, uint64_t frequency, Frame& out) {
    out = {};
    if (!marks || !count || !frequency)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t next = i + 1 < count ? marks[i + 1].ticks : endTicks;
        if (next < marks[i].ticks || static_cast<size_t>(marks[i].pass) >= kPassCount)
            return false;
        out.ms[static_cast<size_t>(marks[i].pass)] +=
            1000.0 * static_cast<double>(next - marks[i].ticks) / static_cast<double>(frequency);
        out.worlds += marks[i].worldBegin;
    }
    out.total = 1000.0 * static_cast<double>(endTicks - marks[0].ticks) / static_cast<double>(frequency);
    return true;
}

inline double median(std::vector<double> values) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n % 2 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

// A run of presented frames. The game presents more often than it draws a world and a world's passes can fall into
// two presented frames, so the report gives each pass as milliseconds per world (the pass's sum over the run divided
// by the worlds begun in it), next to the frame interval (median and mean over the presented frames).
class Window {
public:
    void add(const Frame& frame) { m_frames.push_back(frame); }
    size_t size() const { return m_frames.size(); }
    void clear() { m_frames.clear(); }
    uint32_t worlds() const {
        uint32_t n = 0;
        for (const Frame& f : m_frames)
            n += f.worlds;
        return n;
    }
    double sum(Pass pass) const {
        double ms = 0;
        for (const Frame& f : m_frames)
            ms += pass == Pass::Count ? f.total : f.ms[static_cast<size_t>(pass)];
        return ms;
    }
    // Milliseconds per world of one pass, or of everything when pass == Count (0 without a world).
    double perWorld(Pass pass) const {
        const uint32_t n = worlds();
        return n ? sum(pass) / static_cast<double>(n) : 0.0;
    }
    std::string report() const {
        std::vector<double> lengths;
        for (const Frame& f : m_frames)
            lengths.push_back(f.total);
        char text[160];
        std::snprintf(
            text, sizeof text, "frames=%zu worlds=%u frameMedianMs=%.3f frameMeanMs=%.3f perWorld: total=%.3f",
            m_frames.size(), worlds(), median(lengths),
            m_frames.empty() ? 0.0 : sum(Pass::Count) / static_cast<double>(m_frames.size()), perWorld(Pass::Count));
        std::string line = text;
        for (size_t i = 0; i < kPassCount; ++i) {
            if (sum(static_cast<Pass>(i)) <= 0)
                continue;
            std::snprintf(text, sizeof text, " %s=%.3f", name(static_cast<Pass>(i)), perWorld(static_cast<Pass>(i)));
            line += text;
        }
        return line;
    }

private:
    std::vector<Frame> m_frames;
};

} // namespace gsr::gpupass
