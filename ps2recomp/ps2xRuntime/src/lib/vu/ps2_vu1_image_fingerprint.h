#pragma once
#include <cstddef>
#include <cstdint>

// Compile-time fingerprints of selected words of the VU1 microcode image. Native replacements of microprogram
// fragments assert that the locally generated image (aot/ps2_vu1_image.inc, built from the player's disc and never
// committed) still holds the code they were written for, without repeating the game's instruction words here.
// FNV-1a over the little-endian bytes of each 64-bit instruction pair, in the order given.
namespace Vu1Fingerprint {
constexpr uint64_t kOffset = 0xcbf29ce484222325ull;
constexpr uint64_t mix(uint64_t h, uint64_t word) {
    for (int i = 0; i < 8; ++i)
        h = (h ^ ((word >> (8 * i)) & 0xffu)) * 0x100000001b3ull;
    return h;
}
template <size_t N>
constexpr uint64_t ofPcs(const uint64_t* image, const uint32_t (&pcs)[N]) {
    uint64_t h = kOffset;
    for (size_t i = 0; i < N; ++i)
        h = mix(h, image[pcs[i]]);
    return h;
}
constexpr uint64_t ofRange(const uint64_t* image, uint32_t first, uint32_t count) {
    uint64_t h = kOffset;
    for (uint32_t i = 0; i < count; ++i)
        h = mix(h, image[first + i]);
    return h;
}
} // namespace Vu1Fingerprint
