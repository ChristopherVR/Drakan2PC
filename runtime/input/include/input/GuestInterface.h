// Abstract view of the recompiled PS2 guest, so the input module does not depend on PS2Recomp.
// The runtime owner implements this once on top of rdram + R5900Context (see README.md).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace drakan_input {

// End of the guest RAM the host maps (PS2_RAM_SIZE; the adapter static_asserts they match). Game objects can live
// anywhere below it, also above the retail 32 MB. A pointer bound uses this, never 0x2000000.
#if defined(__EMSCRIPTEN__)
inline constexpr uint32_t kGuestRamEnd = 0x04000000u;
#else
inline constexpr uint32_t kGuestRamEnd = 0x10000000u;
#endif

// Arguments for a guest call. Two conventions are supported by the module (see Config::turnCallAbi):
//  * integers go to $a0.. ($4..$11) in order,
//  * floats go to $f12.. in order (EABI style, counted separately from the integers).
// A module that wants "everything in GPRs" simply passes float bit patterns as integers.
struct GuestCall {
    uint32_t addr = 0;
    GuestCall() = default;
    explicit GuestCall(uint32_t a) : addr(a) {}
    GuestCall& arg(uint32_t v) {
        if (integerCount_ < integerRegisters_.size())
            integerRegisters_[integerCount_++] = v;
        else
            overflow_ = true;
        return *this;
    }
    GuestCall& argf(float v) {
        if (floatCount_ < floatRegisters_.size())
            floatRegisters_[floatCount_++] = v;
        else
            overflow_ = true;
        return *this;
    }
    bool valid() const { return addr != 0 && (addr & 3u) == 0 && !overflow_; }
    std::span<const uint32_t> integers() const { return {integerRegisters_.data(), integerCount_}; }
    std::span<const float> floats() const { return {floatRegisters_.data(), floatCount_}; }
    const std::array<uint32_t, 8>& integerRegisters() const { return integerRegisters_; }
    const std::array<float, 8>& floatRegisters() const { return floatRegisters_; }

private:
    std::array<uint32_t, 8> integerRegisters_{};
    std::array<float, 8> floatRegisters_{};
    std::size_t integerCount_ = 0;
    std::size_t floatCount_ = 0;
    bool overflow_ = false;
};

class IGuest {
public:
    virtual ~IGuest() = default;
    // Guest memory (EE virtual address, KSEG0-less 0 up to kGuestRamEnd). Little endian.
    virtual uint8_t read8(uint32_t addr) = 0;
    virtual uint32_t read32(uint32_t addr) = 0;
    virtual void write8(uint32_t addr, uint8_t v) = 0;
    virtual void write32(uint32_t addr, uint32_t v) = 0;
    // Run a known nonblocking guest routine on a copy of the current context. A completed call returns $v0,
    // including a legitimate zero. An invalid, unavailable, or incomplete call has no value.
    [[nodiscard]] std::optional<uint32_t> tryCall(const GuestCall& c) {
        return c.valid() ? callImmediate(c) : std::nullopt;
    }
    uint32_t call(const GuestCall& c) { return tryCall(c).value_or(0); }
    // Queue a guest call to run on its own scheduler-owned guest thread (like an RPC server thread) instead of nested
    // inside the hook. Use it for calls that can wait on a semaphore (sound resource loads): a wait inside call()
    // cannot be continued and would abandon the call with its locks held. Returns whether the request was accepted.
    // Only adapters with a guest scheduler can enqueue calls in the game.
    bool post(const GuestCall& c) { return c.valid() && enqueueCall(c); }
    // True while a post()ed call has not finished yet (always false for the synchronous default).
    virtual bool postBusy() const { return false; }

    // Helpers
    uint16_t read16(uint32_t a) { return static_cast<uint16_t>(read8(a) | (read8(a + 1) << 8)); }
    int16_t readS16(uint32_t a) { return static_cast<int16_t>(read16(a)); }
    float readF(uint32_t a) {
        uint32_t v = read32(a);
        float f;
        static_assert(sizeof f == 4);
        std::memcpy(&f, &v, 4);
        return f;
    }
    void writeF(uint32_t a, float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        write32(a, v);
    }

protected:
    virtual std::optional<uint32_t> callImmediate(const GuestCall& c) = 0;
    virtual bool enqueueCall(const GuestCall&) { return false; }
};

} // namespace drakan_input
