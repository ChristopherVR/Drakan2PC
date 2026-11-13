#pragma once
// The pipeline state a hardware backend last bound for a draw, so the next draw sets only what differs
// ([video] draw_state_cache, env GSR_DRAW_STATE_CACHE). GPU free: values are opaque words (object addresses, offsets,
// packed rectangles), the backend makes the calls.
//
// The rules that keep every draw's state exactly what a full bind would give:
//  * A slot is skipped only while it is known: set by this cache since the last forget(). The backend calls forget()
//    whenever anything other than its draw path may have used the device context (another pass, an upload, a copy,
//    the upscaler, present). Then every slot is set again by the next draw.
//  * The end of a draw normally releases its textures and targets. With the cache on, that release is held back
//    (hold()) and happens only when something other than the next draw uses the context (take() returns true: the
//    backend unbinds exactly what the release would have unbound, then forgets). A draw that samples a render target
//    never holds and is bound after a release, because a texture cannot be bound for reading while it is bound as a
//    target (the device would quietly unbind one of them).
// The draw count, the binds skipped and the forgets are counted for the [draw-state] log line.
#include <array>
#include <cstdint>
#include <cstring>

namespace gsr::draw_state {

enum class Slot : uint8_t {
    InputLayout,
    VertexBuffer0,
    VertexBuffer1,
    Topology,
    VertexShader,
    PixelShader,
    VertexConstants,
    PixelConstants,
    Textures, // slots 0 and 1 together (the texture and the destination copy)
    Samplers,
    Rasterizer,
    Viewport,
    Scissor,
    Targets, // colour target and depth buffer together
    Blend,
    Depth,
    Count
};

// Two words fold into one slot value (a buffer and its offset, a target and a depth buffer, a rectangle).
constexpr uint64_t pair(uint64_t a, uint64_t b) {
    return a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull + (a << 6) + (a >> 2));
}
inline uint64_t words(const void* data, size_t bytes) {
    uint64_t h = 0xCBF29CE484222325ull;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

struct Counters {
    uint64_t draws = 0;    // bindDraw calls with the cache on
    uint64_t sets = 0;     // slot binds made
    uint64_t skipped = 0;  // slot binds left out (the value was already bound)
    uint64_t forgets = 0;  // forget() calls that dropped known state
    uint64_t releases = 0; // held releases carried out (take() == true)
    uint64_t holds = 0;    // draws that ended without releasing
};

class Cache {
public:
    void enable(bool on) {
        enabled_ = on;
        forget();
        held_ = false;
    }
    bool enabled() const { return enabled_; }

    // True when the slot must be set to `value` (and records it as bound). With the cache off: always true.
    bool set(Slot slot, uint64_t value) {
        const auto i = static_cast<size_t>(slot);
        if (enabled_ && (known_ & (1u << i)) && value_[i] == value) {
            ++counters_.skipped;
            return false;
        }
        value_[i] = value;
        known_ |= 1u << i;
        ++counters_.sets;
        return true;
    }
    // Something other than the draw path may have changed the bound state.
    void forget() {
        if (known_)
            ++counters_.forgets;
        known_ = 0;
    }
    bool known(Slot slot) const { return (known_ >> static_cast<size_t>(slot)) & 1u; }
    uint64_t value(Slot slot) const { return value_[static_cast<size_t>(slot)]; }

    // The start of a draw's bindings (counts the draw).
    void beginDraw() {
        if (enabled_)
            ++counters_.draws;
    }
    // The end of a draw: true when the backend must release now (cache off, or the draw sampled a target); false
    // when the release is held for the next user of the context.
    bool endDraw(bool sampledTarget) {
        if (!enabled_ || sampledTarget) {
            held_ = false;
            return true;
        }
        held_ = true;
        ++counters_.holds;
        return false;
    }
    bool holding() const { return held_; }
    // Another user of the context is about to run. True when a held release must be carried out first; in every
    // case the known state is dropped.
    bool take() {
        const bool release = held_;
        held_ = false;
        if (release)
            ++counters_.releases;
        forget();
        return release;
    }
    // After a release the texture and target slots are known to be empty again (for the bind that follows at once).
    void released(uint64_t emptyTextures, uint64_t emptyTargets) {
        if (!enabled_)
            return;
        set(Slot::Textures, emptyTextures);
        set(Slot::Targets, emptyTargets);
    }
    const Counters& counters() const { return counters_; }

private:
    std::array<uint64_t, static_cast<size_t>(Slot::Count)> value_{};
    uint32_t known_ = 0;
    bool enabled_ = false;
    bool held_ = false;
    Counters counters_;
};

} // namespace gsr::draw_state
