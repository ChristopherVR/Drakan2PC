// Read-only game flow shared by disc profiles. All pointer chains are bounded.
#pragma once
#include "drakan_region.h"
#include <cstring>
#include <span>

namespace drakan_input_flow {
inline bool inMask(int id, uint32_t mask) {
    return id >= 0 && id < 32 && (mask & (1u << id)) != 0;
}
constexpr bool autostartReady(unsigned lookCalls, unsigned playFrames) {
    return (lookCalls >= 100 && playFrames >= 80) || playFrames >= 240;
}
class Reader {
public:
    explicit Reader(std::span<const uint8_t> memory) : memory_(memory) {}
    uint32_t word(uint32_t address, uint32_t offset = 0) const {
        address &= 0x1fffffffu;
        if (address < 0x100000u || address >= memory_.size() || uint64_t(offset) + 4 > memory_.size() - address)
            return 0;
        uint32_t value;
        std::memcpy(&value, memory_.data() + address + offset, 4);
        return value;
    }
    int screen() const {
        const auto& layout = drakan_region::build().inputFlow;
        const uint32_t p = word(drakan_region::guestAddress(0x4fb60cu)) & 0x1fffffffu;
        if (p < 0x100000u || uint64_t(p) + layout.screenId + 4 > memory_.size())
            return -1;
        return static_cast<int32_t>(word(p, layout.screenId));
    }
    bool scripted() const { return word(drakan_region::guestAddress(0x4fbafcu)) != 0; }
    bool gameplay() const {
        const auto& layout = drakan_region::build().inputFlow;
        const uint32_t context = word(drakan_region::guestAddress(0x5565d4u));
        const uint32_t handle = word(context, layout.contextCamera);
        const uint32_t inner = word(handle, layout.handleInner);
        const uint32_t camera = word(inner, layout.innerData);
        const uint32_t p = camera & 0x1fffffffu;
        if (p < 0x100000u || uint64_t(p) + layout.cameraMode + 4 > memory_.size())
            return false;
        return inMask(static_cast<int>(word(camera, layout.cameraMode)), layout.gameplayModes) && !scripted() &&
               screen() < 0;
    }

private:
    std::span<const uint8_t> memory_;
};
} // namespace drakan_input_flow
