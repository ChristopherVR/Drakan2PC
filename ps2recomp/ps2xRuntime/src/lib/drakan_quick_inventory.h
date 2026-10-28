// EE-thread-only bridge for the blocking Quick Inventory command (SCES_500.06).
// Native pad dispatch may yield; RecompGuest's synchronous nested calls cannot.
#pragma once
#include <bitset>
#include <cstdint>

namespace drakan_input {
class QuickInventoryBridge {
public:
    template <class Guest, class Call, class Log>
    bool intercept(Guest& g, const Call& call, bool enabled, Log log) {
        if (call.addr != 0x131fa8u || call.ni < 5 || call.i[1] != 0x403au)
            return false;
        const unsigned source = call.i[2] & 0x1ffu;
        if (!enabled) {
            reset();
            return true;
        }
        if (!call.i[3]) {
            held_.reset(source);
            if (!held_.any())
                mask_ = 0;
            return true;
        }
        // The live keymap accounts for the user's controller scheme. The pad
        // dispatch table holds 17 {action, button mask, ...} records.
        for (unsigned n = 0; n < 17; ++n) {
            const uint32_t action = g.read32(0x4e9800u + n * 12);
            const uint32_t mask = g.read32(0x4e9804u + n * 12);
            if (action >= 512 || !mask || mask > 0xffffu || (mask & (mask - 1)))
                continue;
            if (g.read16(call.i[0] + 8 + action * 2) != 0x403au)
                continue;
            device_ = call.i[0];
            action_ = action;
            mask_ = static_cast<uint16_t>(mask);
            held_.set(source);
            return true;
        }
        // Never fall back to the unsafe synchronous path when this scheme has
        // no native Quick Inventory binding. A later fresh press can retry.
        reset();
        log("[quick-inventory] command suppressed: no native pad binding for 0x403a\n");
        return true;
    }

    template <class Guest>
    uint16_t buttons(Guest& g, bool enabled) {
        if (!enabled || (mask_ && g.read16(device_ + 8 + action_ * 2) != 0x403au))
            reset();
        return held_.any() ? mask_ : 0;
    }
    void reset() {
        held_.reset();
        mask_ = 0;
    }

private:
    std::bitset<512> held_;
    uint32_t device_ = 0, action_ = 0;
    uint16_t mask_ = 0;
};
} // namespace drakan_input
