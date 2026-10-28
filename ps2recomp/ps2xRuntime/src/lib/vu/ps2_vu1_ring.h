#pragma once
#include <cstdint>
#include <cstring>

namespace Vu1Ring {
// Power-of-two VU ring, nonzero size. Copy only already-due packet payload;
// the caller stops at each GIFtag boundary and retains its transfer credit.
inline void copyPayload(uint8_t* dest, const uint8_t* source, uint32_t size, uint32_t offset, uint32_t bytes) {
    offset &= size - 1u;
    while (bytes) {
        const uint32_t contiguous = size - offset;
        const uint32_t n = bytes < contiguous ? bytes : contiguous;
        std::memcpy(dest, source + offset, n);
        dest += n;
        bytes -= n;
        offset = 0;
    }
}
// VU memory is normally a power-of-two ring. Keep general-size behavior for
// diagnostic callers without integer divisions on every PATH1 qword/store.
inline uint32_t wrap(uint32_t offset, uint32_t size) {
    return (size & (size - 1u)) == 0u ? offset & (size - 1u) : offset % size;
}

inline uint32_t distance(uint32_t at, uint32_t start, uint32_t size) {
    if ((size & (size - 1u)) == 0u)
        return (at - start) & (size - 1u);
    return ((at % size) + size - (start % size)) % size;
}
} // namespace Vu1Ring
