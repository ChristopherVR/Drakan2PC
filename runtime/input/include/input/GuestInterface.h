// Abstract view of the recompiled PS2 guest, so the input module does not depend on PS2Recomp.
// The runtime owner implements this once on top of rdram + R5900Context (see README.md).
#pragma once
#include <cstdint>
#include <cstring>

namespace drakan_input {

// Arguments for a guest call. Two conventions are supported by the module (see Config::turnCallAbi):
//  * integers go to $a0.. ($4..$11) in order,
//  * floats go to $f12.. in order (EABI style, counted separately from the integers).
// A module that wants "everything in GPRs" simply passes float bit patterns as integers.
struct GuestCall {
    uint32_t addr = 0;
    uint32_t i[8] = {};
    int ni = 0;
    float f[8] = {};
    int nf = 0;
    GuestCall() = default;
    explicit GuestCall(uint32_t a) : addr(a) {}
    GuestCall& arg(uint32_t v) {
        i[ni++] = v;
        return *this;
    }
    GuestCall& argf(float v) {
        f[nf++] = v;
        return *this;
    }
};

class IGuest {
public:
    virtual ~IGuest() = default;
    // Guest memory (EE virtual address, KSEG0-less 0x00000000-0x01ffffff). Little endian.
    virtual uint8_t read8(uint32_t addr) = 0;
    virtual uint32_t read32(uint32_t addr) = 0;
    virtual void write8(uint32_t addr, uint8_t v) = 0;
    virtual void write32(uint32_t addr, uint32_t v) = 0;
    // Call a (recompiled) guest function to completion and return $v0 (low 32 bits).
    // Must be safe to call from inside a replaced-function hook (use a copy of the current context).
    virtual uint32_t call(const GuestCall& c) = 0;
    // Queue a guest call to run on its own scheduler-owned guest thread (like an RPC server thread) instead of nested
    // inside the hook. Use it for calls that can wait on a semaphore (sound resource loads): a wait inside call()
    // cannot be continued and would abandon the call with its locks held. Fire and forget; the default runs it
    // synchronously.
    virtual void post(const GuestCall& c) { call(c); }
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
};

} // namespace drakan_input
