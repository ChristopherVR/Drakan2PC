// Read-only view of guest (EE) memory for the modern Tab screen reader. std only.
// The game glue implements it over rdram (or adapts drakan_input::IGuest); tests use FlatGuestMemory over a byte
// buffer or a RAM dump.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gameui {

class IGuestMemory {
public:
    virtual ~IGuestMemory() = default;
    // EE address (KSEG bits stripped, 0 up to the mapped guest RAM size the adapter knows), little endian.
    // Out-of-range reads return 0.
    virtual uint8_t read8(uint32_t addr) const = 0;
    virtual uint32_t read32(uint32_t addr) const;

    uint16_t read16(uint32_t addr) const { return static_cast<uint16_t>(read8(addr) | (read8(addr + 1) << 8)); }
    int32_t readS32(uint32_t addr) const { return static_cast<int32_t>(read32(addr)); }
    // NUL-terminated string of at most `maxBytes` bytes. The game's text is Latin-1 (PAL disc); the result is UTF-8.
    std::string readString(uint32_t addr, size_t maxBytes) const;
};

// Flat little-endian RAM image (an rdram copy of any size, or a test buffer). Addresses lose their KSEG / uncached
// bits (`a & kAddressMask`); anything past `size` reads as 0. Does not own the bytes.
class FlatGuestMemory final : public IGuestMemory {
public:
    static constexpr uint32_t kAddressMask = 0x0fffffffu; // largest mapped guest RAM (256 MB)
    FlatGuestMemory(const uint8_t* bytes, size_t size) : bytes_(bytes), size_(size) {}
    uint8_t read8(uint32_t addr) const override;
    uint32_t read32(uint32_t addr) const override;

private:
    const uint8_t* bytes_;
    size_t size_;
};

// Latin-1 -> UTF-8 (used by readString; exposed for tests).
std::string latin1ToUtf8(const char* text, size_t n);
// UTF-8 -> Latin-1, for text the game font draws (its glyphs are Windows-1252). A character outside Latin-1, or a
// malformed sequence, becomes '?'.
std::string utf8ToLatin1(std::string_view text);

} // namespace gameui
