#pragma once
#include <cstdint>

namespace Vu1Flags {
// No observer runs inside flag retirement. Keep the FIFO cursor and resulting
// registers in locals, then publish the retired prefix once, in the same order.
template <class Entry>
inline void retire(const Entry* entries, uint32_t mask, uint64_t cycle, uint32_t& head, uint32_t& count, uint32_t& mac,
                   uint32_t& status, uint32_t& clip) {
    if (!count || entries[head].readyCycle > cycle)
        return;
    uint32_t at = head, left = count;
    uint32_t nextMac = mac, nextStatus = status, nextClip = clip;
    do {
        const Entry& entry = entries[at];
        if (entry.readyCycle > cycle)
            break;
        if (entry.writesMac)
            nextMac = entry.mac;
        if (entry.writesStatus) {
            const uint32_t current = entry.status & 0xFu;
            nextStatus = (nextStatus & 0xFF0u) | current | ((current | entry.extraSticky) << 6);
        }
        if (entry.writesSticky)
            nextStatus = (nextStatus & 0x03Fu) | (entry.status & 0xFC0u);
        if (entry.writesClip)
            nextClip = entry.clip;
        at = (at + 1u) & mask;
        --left;
    } while (left);
    head = at;
    count = left;
    mac = nextMac;
    status = nextStatus;
    clip = nextClip;
}
} // namespace Vu1Flags
