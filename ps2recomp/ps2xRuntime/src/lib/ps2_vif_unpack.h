#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>

namespace vif_unpack {
// The source bytes of one vector in the low lanes, zero above; never reads past them (the packet may end there).
template <unsigned Bytes>
inline __m128i loadBytes(const uint8_t* source) {
    if constexpr (Bytes == 4) {
        int32_t word;
        std::memcpy(&word, source, 4);
        return _mm_cvtsi32_si128(word);
    } else if constexpr (Bytes == 8) {
        return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(source));
    } else if constexpr (Bytes == 12) {
        int32_t word;
        std::memcpy(&word, source + 8, 4);
        return _mm_unpacklo_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(source)), _mm_cvtsi32_si128(word));
    } else {
        __m128i value = _mm_setzero_si128();
        std::memcpy(&value, source, Bytes);
        return value;
    }
}

// Unmasked, non-fill uploads have no ROW/COL dependencies. Dispatch the format
// once, then copy/expand whole runs between STCYCL gaps and VU memory wraps.
template <unsigned Components, unsigned Bits, bool Unsigned>
inline void runs(uint8_t* memory, const uint8_t* source, uint32_t address, uint32_t count, uint32_t cl, uint32_t wl,
                 uint32_t& cameraFirst, uint32_t& cameraEnd) {
    uint32_t cycle = 0;
    while (count) {
        const uint32_t n = std::min({count, wl - cycle, 1024u - address});
        if (address < 7u) {
            cameraFirst = std::min(cameraFirst, address);
            cameraEnd = std::max(cameraEnd, std::min(7u, address + n));
        }
        uint8_t* dest = memory + address * 16u;
        if constexpr (Components == 4 && Bits == 32) {
            std::memcpy(dest, source, n * 16u);
            source += n * 16u;
        } else {
            for (uint32_t i = 0; i < n; ++i, dest += 16u, source += Components * Bits / 8u) {
                // Copy only the source bytes: V2/V3 must not read beyond the packet.
                __m128i value = loadBytes<Components * Bits / 8u>(source);
                if constexpr (Bits == 8) {
                    value = _mm_unpacklo_epi8(value, _mm_setzero_si128());
                    value = _mm_unpacklo_epi16(value, _mm_setzero_si128());
                    if constexpr (!Unsigned)
                        value = _mm_srai_epi32(_mm_slli_epi32(value, 24), 24);
                } else if constexpr (Bits == 16) {
                    value = _mm_unpacklo_epi16(value, _mm_setzero_si128());
                    if constexpr (!Unsigned)
                        value = _mm_srai_epi32(_mm_slli_epi32(value, 16), 16);
                }
                if constexpr (Components == 1)
                    value = _mm_shuffle_epi32(value, 0);
                else if constexpr (Components < 4) {
                    const __m128i keep = _mm_set_epi32(-1, Components == 2 ? -1 : 0, 0, 0);
                    const __m128i old = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dest));
                    value = _mm_or_si128(_mm_and_si128(old, keep), _mm_andnot_si128(keep, value));
                }
                _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), value);
            }
        }
        count -= n;
        address = (address + n) & 1023u;
        cycle += n;
        if (cycle == wl) {
            address = (address + cl - wl) & 1023u;
            cycle = 0;
        }
    }
}

// Masked (STMASK) and/or offset/difference mode (STMOD 1/2) uploads without fill cycles, one vector at a time. Matches
// the scalar loop of PS2Memory::processVIF1Data lane by lane: per write cycle (capped at 3) every field takes the
// decompressed value (plus ROW in modes 1 and 2; mode 2 also stores the sum to ROW), ROW, COL[cycle] or keeps the old
// lane. Fields a V1/V2/V3 format does not supply start from the old lane, as in the scalar loop (V1 broadcasts).
template <unsigned Components, unsigned Bits, bool Unsigned>
inline void maskedRuns(uint8_t* memory, const uint8_t* source, uint32_t address, uint32_t count, uint32_t cl,
                       uint32_t wl, bool maskEnable, uint32_t mask, uint32_t mode, uint32_t* row, const uint32_t* col,
                       uint32_t& cameraFirst, uint32_t& cameraEnd) {
    // Lane selects per write cycle position (0..3): decompressed, row, col, keep.
    __m128i selData[4], selRow[4], selCol[4], selKeep[4], colValue[4];
    for (uint32_t c = 0; c < 4; ++c) {
        alignas(16) uint32_t d[4], r[4], k[4], o[4];
        for (uint32_t f = 0; f < 4; ++f) {
            const uint32_t spec = maskEnable ? (mask >> ((c * 4u + f) * 2u)) & 3u : 0u;
            d[f] = spec == 0u ? ~0u : 0u;
            r[f] = spec == 1u ? ~0u : 0u;
            o[f] = spec == 2u ? ~0u : 0u;
            k[f] = spec == 3u ? ~0u : 0u;
        }
        selData[c] = _mm_load_si128(reinterpret_cast<const __m128i*>(d));
        selRow[c] = _mm_load_si128(reinterpret_cast<const __m128i*>(r));
        selCol[c] = _mm_load_si128(reinterpret_cast<const __m128i*>(o));
        selKeep[c] = _mm_load_si128(reinterpret_cast<const __m128i*>(k));
        colValue[c] = _mm_set1_epi32(static_cast<int>(col[c]));
    }
    __m128i rowValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row));
    const bool add = mode == 1u || mode == 2u;
    const bool difference = mode == 2u;
    uint32_t cycle = 0;
    for (uint32_t i = 0; i < count; ++i, source += Components * Bits / 8u) {
        if (address < 7u) {
            cameraFirst = std::min(cameraFirst, address);
            cameraEnd = std::max(cameraEnd, address + 1u);
        }
        uint8_t* dest = memory + address * 16u;
        const __m128i old = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dest));
        __m128i value;
        constexpr unsigned kBytes = Components * Bits / 8u;
        if constexpr (Components == 4 && Bits == 32) {
            value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
        } else {
            value = loadBytes<kBytes>(source);
            if constexpr (Bits == 8) {
                value = _mm_unpacklo_epi8(value, _mm_setzero_si128());
                value = _mm_unpacklo_epi16(value, _mm_setzero_si128());
                if constexpr (!Unsigned)
                    value = _mm_srai_epi32(_mm_slli_epi32(value, 24), 24);
            } else if constexpr (Bits == 16) {
                value = _mm_unpacklo_epi16(value, _mm_setzero_si128());
                if constexpr (!Unsigned)
                    value = _mm_srai_epi32(_mm_slli_epi32(value, 16), 16);
            }
            if constexpr (Components == 1)
                value = _mm_shuffle_epi32(value, 0);
            else if constexpr (Components < 4) {
                const __m128i keep = _mm_set_epi32(-1, Components == 2 ? -1 : 0, 0, 0);
                value = _mm_or_si128(_mm_and_si128(old, keep), _mm_andnot_si128(keep, value));
            }
        }
        const uint32_t c = cycle > 3u ? 3u : cycle;
        if (add) {
            value = _mm_add_epi32(value, rowValue);
            if (difference) // ROW follows the written sum only in the fields that took the data
                rowValue = _mm_or_si128(_mm_and_si128(selData[c], value), _mm_andnot_si128(selData[c], rowValue));
        }
        __m128i out = _mm_and_si128(selData[c], value);
        out = _mm_or_si128(out, _mm_and_si128(selRow[c], rowValue));
        out = _mm_or_si128(out, _mm_and_si128(selCol[c], colValue[c]));
        out = _mm_or_si128(out, _mm_and_si128(selKeep[c], old));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), out);
        address = (address + 1u) & 1023u;
        if (++cycle == wl) {
            address = (address + cl - wl) & 1023u;
            cycle = 0;
        }
    }
    if (difference)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(row), rowValue);
}

inline bool fast(uint8_t* memory, const uint8_t* source, uint32_t address, uint32_t count, uint32_t cl, uint32_t wl,
                 uint8_t vn, uint8_t vl, bool zeroExtend, bool masked, uint32_t mode, uint32_t& cameraFirst,
                 uint32_t& cameraEnd, uint32_t mask = 0u, uint32_t* row = nullptr, const uint32_t* col = nullptr) {
    mode &= 3u;
    if (cl < wl || vl > 2u)
        return false;
    const bool plain = !masked && (mode == 0u || mode == 3u);
    if (!plain && (!row || !col))
        return false;
#define VIF_RUN(C, B)                                                                                                  \
    do {                                                                                                               \
        if (!plain) {                                                                                                  \
            if (zeroExtend)                                                                                            \
                maskedRuns<C, B, true>(memory, source, address, count, cl, wl, masked, mask, mode, row, col,           \
                                       cameraFirst, cameraEnd);                                                        \
            else                                                                                                       \
                maskedRuns<C, B, false>(memory, source, address, count, cl, wl, masked, mask, mode, row, col,          \
                                        cameraFirst, cameraEnd);                                                       \
        } else if (zeroExtend)                                                                                         \
            runs<C, B, true>(memory, source, address, count, cl, wl, cameraFirst, cameraEnd);                          \
        else                                                                                                           \
            runs<C, B, false>(memory, source, address, count, cl, wl, cameraFirst, cameraEnd);                         \
        return true;                                                                                                   \
    } while (0)
#define VIF_FORMAT(C)                                                                                                  \
    case C - 1:                                                                                                        \
        switch (vl) {                                                                                                  \
            case 0:                                                                                                    \
                VIF_RUN(C, 32);                                                                                        \
            case 1:                                                                                                    \
                VIF_RUN(C, 16);                                                                                        \
            case 2:                                                                                                    \
                VIF_RUN(C, 8);                                                                                         \
        }                                                                                                              \
        break
    switch (vn) {
        VIF_FORMAT(1);
        VIF_FORMAT(2);
        VIF_FORMAT(3);
        VIF_FORMAT(4);
    }
#undef VIF_FORMAT
#undef VIF_RUN
    return false;
}
} // namespace vif_unpack
#else
namespace vif_unpack {
inline bool fast(uint8_t*, const uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t, uint8_t, bool, bool,
                 uint32_t, uint32_t&, uint32_t&) {
    return false;
}
} // namespace vif_unpack
#endif
