#pragma once
#include <cstdint>

// For FMAC results already due at the end of a block with no internal flag
// observer/writer. Incoming flag entries are older than every block result
// (latency four, at least one cycle since the preceding issue).
struct Vu1FlagBatch {
    uint32_t mac = 0, current = 0, sticky = 0;
    void add(uint32_t nextMac, uint32_t status, uint32_t extraSticky) {
        mac = nextMac;
        current = status & 15u;
        sticky |= ((current | extraSticky) & 15u) << 6;
    }
    void apply(uint32_t& targetMac, uint32_t& status) const {
        targetMac = mac;
        status = (status & 0xff0u) | current | sticky;
    }
};
