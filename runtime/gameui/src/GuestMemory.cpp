#include "gameui/GuestMemory.h"

namespace gameui {

uint32_t IGuestMemory::read32(uint32_t addr) const {
    return uint32_t(read8(addr)) | (uint32_t(read8(addr + 1)) << 8) | (uint32_t(read8(addr + 2)) << 16) |
           (uint32_t(read8(addr + 3)) << 24);
}

std::string latin1ToUtf8(const char* text, size_t n) {
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}

std::string IGuestMemory::readString(uint32_t addr, size_t maxBytes) const {
    std::string raw;
    for (size_t i = 0; i < maxBytes; ++i) {
        const uint8_t c = read8(addr + static_cast<uint32_t>(i));
        if (c == 0)
            break;
        raw.push_back(static_cast<char>(c));
    }
    return latin1ToUtf8(raw.data(), raw.size());
}

uint8_t FlatGuestMemory::read8(uint32_t addr) const {
    const size_t a = addr & 0x1ffffffu;
    return a < size_ ? bytes_[a] : 0;
}

uint32_t FlatGuestMemory::read32(uint32_t addr) const {
    const size_t a = addr & 0x1ffffffu;
    if (a + 4 > size_)
        return IGuestMemory::read32(addr);
    return uint32_t(bytes_[a]) | (uint32_t(bytes_[a + 1]) << 8) | (uint32_t(bytes_[a + 2]) << 16) |
           (uint32_t(bytes_[a + 3]) << 24);
}

} // namespace gameui
