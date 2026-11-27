// Reading and writing guest (PS2) RAM from host code: the one set of accessors the game glue (src/lib/drakan_*.cpp)
// uses instead of its own rd32 / wr32 / ramPtr copies.
//
// Model: `ram` is the host base of guest RAM (the `rdram` every recompiled function and hook receives). A guest
// address is what the game stores in a pointer field or register: a KUSEG address (0x00100000...), or the same bytes
// through KSEG0/KSEG1 or the uncached mirror (0x80..., 0xA0..., 0x20...).
//
// Rules, the same for every accessor here:
// * Address decode: the KSEG / mirror bits are stripped and the address wraps at PS2_RAM_SIZE (ramOffset), the way
//   the recompiled code and the runtime decode guest addresses (ps2_memory.h, ps2ResolveGuestPointer). The 16 KB
//   scratchpad (0x70000000) is not RAM and is not handled here; use getMemPtr for it.
// * Out of range: a value that would run past the end of guest RAM reads as 0 and a write there is dropped. With the
//   wrap above that only happens for the last bytes of RAM, so the plain accessors never fault.
// * Little endian, unaligned access allowed (memcpy, which compiles to one load / store).
// * They do not validate that an address is a real game object. Check a pointer read from guest memory with
//   isHeapPtr first when it may be null, a flag word or garbage.
//
// The readHeap / writeHeap variants are stricter: they read 0 (and drop the write) unless the whole value lies in the
// heap window [kHeapPtrMin, PS2_RAM_SIZE) after stripping the KSEG bits. Use them where a null or small pointer must
// read 0 instead of low RAM (pointer chains through objects that may not exist yet).
//
// Everything is inline and allocation free: safe in per-frame and per-vertex paths.
#pragma once

#include <cstdint>
#include <cstring>

#include "runtime/ps2_memory.h"

namespace guest {

// Lowest address of a game object: below 1 MB live the EE kernel and the loader, never a heap / data pointer.
inline constexpr uint32_t kHeapPtrMin = 0x00100000u;
// Strips the KSEG0 / KSEG1 / uncached mirror bits of a guest virtual address.
inline constexpr uint32_t kPhysMask = 0x1fffffffu;

// Guest virtual address -> physical address (KSEG bits stripped, not wrapped).
constexpr uint32_t phys(uint32_t address) {
    return address & kPhysMask;
}

// Guest address -> byte offset into `ram`: KSEG bits stripped and wrapped at PS2_RAM_SIZE. Always < PS2_RAM_SIZE.
constexpr uint32_t ramOffset(uint32_t address) {
    return address & PS2_RAM_MASK;
}

// True when `pointer` (the raw value of a guest pointer field, KSEG bits NOT stripped) points at `bytes` bytes inside
// the guest heap / data window [kHeapPtrMin, PS2_RAM_SIZE). Use isHeapPtr(phys(p), n) to accept KSEG pointers too.
constexpr bool isHeapPtr(uint32_t pointer, uint32_t bytes = 4) {
    return pointer >= kHeapPtrMin && bytes <= PS2_RAM_SIZE && pointer <= PS2_RAM_SIZE - bytes;
}

// ---- plain accessors (wrap, 0 past the end of RAM)

template <typename T>
inline T read(const uint8_t* ram, uint32_t address) {
    const uint32_t offset = ramOffset(address);
    T value{};
    if (offset <= PS2_RAM_SIZE - sizeof(T))
        std::memcpy(&value, ram + offset, sizeof(T));
    return value;
}

template <typename T>
inline void write(uint8_t* ram, uint32_t address, T value) {
    const uint32_t offset = ramOffset(address);
    if (offset <= PS2_RAM_SIZE - sizeof(T))
        std::memcpy(ram + offset, &value, sizeof(T));
}

inline uint8_t read8(const uint8_t* ram, uint32_t address) {
    return ram[ramOffset(address)];
}
inline uint16_t read16(const uint8_t* ram, uint32_t address) {
    return read<uint16_t>(ram, address);
}
inline uint32_t read32(const uint8_t* ram, uint32_t address) {
    return read<uint32_t>(ram, address);
}
inline float readF(const uint8_t* ram, uint32_t address) {
    return read<float>(ram, address);
}

inline void write8(uint8_t* ram, uint32_t address, uint8_t value) {
    ram[ramOffset(address)] = value;
}
inline void write16(uint8_t* ram, uint32_t address, uint16_t value) {
    write<uint16_t>(ram, address, value);
}
inline void write32(uint8_t* ram, uint32_t address, uint32_t value) {
    write<uint32_t>(ram, address, value);
}
inline void writeF(uint8_t* ram, uint32_t address, float value) {
    write<float>(ram, address, value);
}

// ---- strict accessors (0 / dropped unless inside the heap window, KSEG bits stripped)

inline uint32_t readHeap32(const uint8_t* ram, uint32_t address) {
    return isHeapPtr(phys(address), 4) ? read32(ram, address) : 0u;
}
inline uint16_t readHeap16(const uint8_t* ram, uint32_t address) {
    return isHeapPtr(phys(address), 2) ? read16(ram, address) : uint16_t{0};
}
inline void writeHeap32(uint8_t* ram, uint32_t address, uint32_t value) {
    if (isHeapPtr(phys(address), 4))
        write32(ram, address, value);
}

// Read-only view of guest RAM behind one of the runtime libraries' memory interfaces (world::IGuestMemory,
// gameui::IGuestMemory: both declare `read8` and `read32` const), with the plain accessor rules above.
template <typename Interface>
class RamView final : public Interface {
public:
    explicit RamView(const uint8_t* ram) : ram_(ram) {}
    uint8_t read8(uint32_t address) const override { return guest::read8(ram_, address); }
    uint32_t read32(uint32_t address) const override { return guest::read32(ram_, address); }

private:
    const uint8_t* ram_;
};

} // namespace guest
