#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace drakan_coop {

// The guest AI reads the controlled actor's position and rotation. Borrow just those fields for one
// handler, restoring the enclosing target even if the EE dispatcher unwinds the native call stack.
class ScopedAiTarget {
public:
    static constexpr unsigned kTransformBytes = 12 * sizeof(uint32_t);
    ScopedAiTarget(uint8_t* local, const uint8_t* target, std::atomic<bool>& active)
        : local_(local), active_(active), wasActive_(active.exchange(true)) {
        std::memcpy(saved_.data(), local_, saved_.size());
        std::memmove(local_, target, saved_.size());
    }
    ~ScopedAiTarget() {
        std::memcpy(local_, saved_.data(), saved_.size());
        active_.store(wasActive_);
    }
    ScopedAiTarget(const ScopedAiTarget&) = delete;
    ScopedAiTarget& operator=(const ScopedAiTarget&) = delete;

private:
    uint8_t* local_;
    std::atomic<bool>& active_;
    bool wasActive_;
    std::array<uint8_t, kTransformBytes> saved_;
};

} // namespace drakan_coop
