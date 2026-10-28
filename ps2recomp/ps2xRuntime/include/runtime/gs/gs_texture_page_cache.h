#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace GSMem {
class TexturePageCache {
public:
    static constexpr uint32_t kPageSize = 8192u;

    void Invalidate() noexcept { m_pageBase = UINT32_MAX; }

    // Parallel readers may clone the cache only when its retained page is
    // coherent with VRAM. Otherwise preserve serial stale-page behavior.
    bool MatchesVram(const uint8_t* vram) const noexcept {
        return m_pageBase == UINT32_MAX || std::memcmp(m_bytes.data(), vram + m_pageBase, kPageSize) == 0;
    }

    // The caller must prove that all sampled pages remain immutable and that
    // the retained page is coherent before entering this mode. Preserve the
    // final hardware-cache observation with one copy when the scope ends.
    void BeginImmutableRead() noexcept {
        m_immutableRead = true;
        m_immutableTouched = false;
    }
    void EndImmutableRead(const uint8_t* vram) noexcept {
        if (m_immutableTouched)
            std::memcpy(m_bytes.data(), vram + m_pageBase, kPageSize);
        m_immutableRead = false;
        m_immutableTouched = false;
    }

    // byteAddress is the wrapped, swizzled VRAM address. The returned
    // pointer is valid only until the next miss or invalidation.
    const uint8_t* Resolve(const uint8_t* vram, uint32_t byteAddress) noexcept {
        const uint32_t pageBase = byteAddress & ~(kPageSize - 1u);
        if (m_immutableRead) {
            m_pageBase = pageBase;
            m_immutableTouched = true;
            return vram + byteAddress;
        }
        if (m_pageBase != pageBase) {
            std::memcpy(m_bytes.data(), vram + pageBase, kPageSize);
            m_pageBase = pageBase;
        }
        return m_bytes.data() + (byteAddress & (kPageSize - 1u));
    }

private:
    alignas(64) std::array<uint8_t, kPageSize> m_bytes{};
    uint32_t m_pageBase = UINT32_MAX;
    bool m_immutableRead = false, m_immutableTouched = false;
};
} // namespace GSMem
