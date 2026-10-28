// GS local memory model: 4 MB of swizzled storage with per-block write stamps.
// The address tables (block/column tables) follow the layout used by PCSX2's GSLocalMemory/GSTables
// (GPL-3.0-or-later); they were cross-checked against the PS2 GS manual page layouts.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace gsr {

enum Psm : uint32_t {
    PSM_CT32 = 0x00,
    PSM_CT24 = 0x01,
    PSM_CT16 = 0x02,
    PSM_CT16S = 0x0A,
    PSM_T8 = 0x13,
    PSM_T4 = 0x14,
    PSM_T8H = 0x1B,
    PSM_T4HL = 0x24,
    PSM_T4HH = 0x2C,
    PSM_Z32 = 0x30,
    PSM_Z24 = 0x31,
    PSM_Z16 = 0x32,
    PSM_Z16S = 0x3A,
};

constexpr uint32_t kVramSize = 4u * 1024u * 1024u;
constexpr uint32_t kVramBlocks = kVramSize / 256u; // 16384 blocks of 64 words

namespace tables {
// block order inside a page, indexed [row][col] of the block grid
inline constexpr uint8_t block32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                                          {2, 3, 6, 7, 18, 19, 22, 23},
                                          {8, 9, 12, 13, 24, 25, 28, 29},
                                          {10, 11, 14, 15, 26, 27, 30, 31}};
inline constexpr uint8_t block32Z[4][8] = {{24, 25, 28, 29, 8, 9, 12, 13},
                                           {26, 27, 30, 31, 10, 11, 14, 15},
                                           {16, 17, 20, 21, 0, 1, 4, 5},
                                           {18, 19, 22, 23, 2, 3, 6, 7}};
inline constexpr uint8_t block16[8][4] = {{0, 2, 8, 10},    {1, 3, 9, 11},    {4, 6, 12, 14},   {5, 7, 13, 15},
                                          {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
inline constexpr uint8_t block16S[8][4] = {{0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26},  {9, 11, 25, 27},
                                           {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
inline constexpr uint8_t block16Z[8][4] = {{24, 26, 16, 18}, {25, 27, 17, 19}, {28, 30, 20, 22}, {29, 31, 21, 23},
                                           {8, 10, 0, 2},    {9, 11, 1, 3},    {12, 14, 4, 6},   {13, 15, 5, 7}};
inline constexpr uint8_t block16SZ[8][4] = {{24, 26, 8, 10},  {25, 27, 9, 11},  {16, 18, 0, 2}, {17, 19, 1, 3},
                                            {28, 30, 12, 14}, {29, 31, 13, 15}, {20, 22, 4, 6}, {21, 23, 5, 7}};
inline constexpr uint8_t block8[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                                         {2, 3, 6, 7, 18, 19, 22, 23},
                                         {8, 9, 12, 13, 24, 25, 28, 29},
                                         {10, 11, 14, 15, 26, 27, 30, 31}};
// 4-bit uses the same block grid as 16-bit (block16: 4 wide x 8 tall of 32x16 blocks)

inline constexpr uint8_t col32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13},       {2, 3, 6, 7, 10, 11, 14, 15},
                                        {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                                        {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
                                        {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
// 16-bit: two rows of 16 half-words per column (4 columns per block, column = (y>>1)&3)
inline constexpr uint8_t col16[2][16] = {{0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
                                         {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31}};
inline constexpr uint8_t col8[16][16] = {
    {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
    {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
    {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
    {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
    {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
    {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
    {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
    {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127},
    {128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182},
    {136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190},
    {161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151},
    {169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159},
    {224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214},
    {232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222},
    {193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247},
    {201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255}};
// 4-bit: column holds 16 rows x 32 nibbles = 512 nibbles; value = nibble index inside the 256-byte block
inline constexpr uint16_t col4[16][32] = {
    {0, 8,  32, 40, 64, 72, 96,  104, 2, 10, 34, 42, 66, 74, 98,  106,
     4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
     20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97,  105, 1, 9,  33, 41, 67, 75, 99,  107, 3, 11, 35, 43,
     69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
     85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
    {192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
     196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174},
    {208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
     212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190},
    {129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
     133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239},
    {145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
     149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255},
    {256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362,
     260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366},
    {272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378,
     276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382},
    {321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299,
     325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303},
    {337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315,
     341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319},
    {448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426,
     452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430},
    {464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442,
     468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446},
    {385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491,
     389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495},
    {401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507,
     405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511}};
} // namespace tables

// storage layout families
enum class Layout : uint8_t { L32, L32Z, L16, L16S, L16Z, L16SZ, L8, L4, Invalid };

inline Layout layoutOf(uint32_t psm) {
    switch (psm) {
        case PSM_CT32:
        case PSM_CT24:
        case PSM_T8H:
        case PSM_T4HL:
        case PSM_T4HH:
            return Layout::L32;
        case PSM_Z32:
        case PSM_Z24:
            return Layout::L32Z;
        case PSM_CT16:
            return Layout::L16;
        case PSM_CT16S:
            return Layout::L16S;
        case PSM_Z16:
            return Layout::L16Z;
        case PSM_Z16S:
            return Layout::L16SZ;
        case PSM_T8:
            return Layout::L8;
        case PSM_T4:
            return Layout::L4;
        default:
            return Layout::Invalid;
    }
}

struct PsmGeom {
    uint32_t pageW, pageH, blockW, blockH, bits; // bits = storage bits per pixel
};

inline PsmGeom geomOf(uint32_t psm) {
    switch (layoutOf(psm)) {
        case Layout::L32:
        case Layout::L32Z:
            return {64, 32, 8, 8, 32};
        case Layout::L16:
        case Layout::L16S:
        case Layout::L16Z:
        case Layout::L16SZ:
            return {64, 64, 16, 8, 16};
        case Layout::L8:
            return {128, 64, 16, 16, 8};
        case Layout::L4:
            return {128, 128, 32, 16, 4};
        default:
            return {64, 32, 8, 8, 32};
    }
}

// Pixel storage address. Returns the byte offset in local memory (not masked); for 4-bit layouts *nibble is the
// nibble within the byte (0 = low). bp is a block (64 word) number, bw the buffer width in 64-pixel units.
inline uint32_t pixelAddr(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t* nibble = nullptr) {
    using namespace tables;
    const uint32_t bpLow = bp & 31u;
    uint32_t page, blockId, inBlock;
    switch (layoutOf(psm)) {
        case Layout::L32:
        case Layout::L32Z: {
            const bool z = layoutOf(psm) == Layout::L32Z;
            page = (bp >> 5) + (y >> 5) * (bw ? bw : 1u) + (x >> 6);
            blockId = bpLow + (z ? block32Z[(y >> 3) & 3][(x >> 3) & 7] : block32[(y >> 3) & 3][(x >> 3) & 7]);
            inBlock = col32[y & 7][x & 7] * 4u;
            break;
        }
        case Layout::L16:
        case Layout::L16S:
        case Layout::L16Z:
        case Layout::L16SZ: {
            const Layout l = layoutOf(psm);
            page = (bp >> 5) + (y >> 6) * (bw ? bw : 1u) + (x >> 6);
            const uint8_t t = (l == Layout::L16)    ? block16[(y >> 3) & 7][(x >> 4) & 3]
                              : (l == Layout::L16S) ? block16S[(y >> 3) & 7][(x >> 4) & 3]
                              : (l == Layout::L16Z) ? block16Z[(y >> 3) & 7][(x >> 4) & 3]
                                                    : block16SZ[(y >> 3) & 7][(x >> 4) & 3];
            blockId = bpLow + t;
            inBlock = ((y >> 1) & 3u) * 64u + col16[y & 1][x & 15] * 2u;
            break;
        }
        case Layout::L8: {
            const uint32_t ppr = (bw >> 1) ? (bw >> 1) : 1u;
            page = (bp >> 5) + (y >> 6) * ppr + (x >> 7);
            blockId = bpLow + block8[(y >> 4) & 3][(x >> 4) & 7];
            inBlock = col8[y & 15][x & 15];
            break;
        }
        case Layout::L4: {
            const uint32_t ppr = (bw >> 1) ? (bw >> 1) : 1u;
            page = (bp >> 5) + (y >> 7) * ppr + (x >> 7);
            blockId = bpLow + block16[(y >> 4) & 7][(x >> 5) & 3];
            const uint32_t nib = col4[y & 15][x & 31];
            inBlock = nib >> 1;
            if (nibble)
                *nibble = nib & 1u;
            break;
        }
        default:
            return 0;
    }
    return ((page + (blockId >> 5)) << 13) + ((blockId & 31u) << 8) + inBlock;
}

// Block number (0..16383) containing pixel (for stamping)
inline uint32_t blockOf(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) {
    return (pixelAddr(psm, bp, bw, x, y) >> 8) & (kVramBlocks - 1);
}

inline bool isIndexed(uint32_t psm) {
    return psm == PSM_T8 || psm == PSM_T4 || psm == PSM_T8H || psm == PSM_T4HL || psm == PSM_T4HH;
}
inline bool isZ(uint32_t psm) {
    return (psm & 0x30u) == 0x30u;
}
inline bool isValidPsm(uint32_t psm) {
    return layoutOf(psm) != Layout::Invalid;
}

// bits of the *transfer stream* per pixel for host<->local transfers
inline uint32_t transferBits(uint32_t psm) {
    switch (psm) {
        case PSM_CT24:
        case PSM_Z24:
            return 24;
        case PSM_CT32:
        case PSM_Z32:
            return 32;
        case PSM_CT16:
        case PSM_CT16S:
        case PSM_Z16:
        case PSM_Z16S:
            return 16;
        case PSM_T8:
        case PSM_T8H:
            return 8;
        case PSM_T4:
        case PSM_T4HL:
        case PSM_T4HH:
            return 4;
        default:
            return 32;
    }
}

// min / max block number touched by a rectangle of pixels
inline void spanBlocks(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t& lo, uint32_t& hi) {
    lo = 0xFFFFFFFFu;
    hi = 0;
    if (!w || !h) {
        lo = 0;
        return;
    }
    const PsmGeom g = geomOf(psm);
    for (uint32_t by = y & ~(g.blockH - 1); by < y + h; by += g.blockH)
        for (uint32_t bx = x & ~(g.blockW - 1); bx < x + w; bx += g.blockW) {
            const uint32_t b = blockOf(psm, bp, bw, bx, by);
            if (b < lo)
                lo = b;
            if (b > hi)
                hi = b;
        }
}

class LocalMemory {
public:
    LocalMemory() : m_data(kVramSize, 0), m_stamp(kVramBlocks, 0) {}

    uint8_t* data() { return m_data.data(); }
    const uint8_t* data() const { return m_data.data(); }

    // monotonically increasing write clock; stamp[block] = clock value at last write
    uint64_t clock() const { return m_clock; }
    uint64_t blockStamp(uint32_t block) const { return m_stamp[block & (kVramBlocks - 1)]; }
    uint64_t maxStamp(uint32_t firstBlock, uint32_t count) const {
        uint64_t m = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t b = (firstBlock + i) & (kVramBlocks - 1);
            if (m_stamp[b] > m)
                m = m_stamp[b];
        }
        return m;
    }
    // Content hash of the 256-byte blocks first..last (inclusive, wrapping at the end of memory). Used to recognise
    // a texture the game re-uploaded with identical bytes.
    uint64_t hashBlocks(uint32_t first, uint32_t last) const {
        uint64_t h[4] = {0x9e3779b97f4a7c15ull, 0xc2b2ae3d27d4eb4full, 0x165667b19e3779f9ull, 0x27d4eb2f165667c5ull};
        const uint32_t count = ((last - first) & (kVramBlocks - 1)) + 1;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* block = m_data.data() + size_t((first + i) & (kVramBlocks - 1)) * 256u;
            for (uint32_t at = 0; at < 256; at += 32)
                for (unsigned lane = 0; lane < 4; ++lane) {
                    uint64_t v;
                    std::memcpy(&v, block + at + lane * 8, 8);
                    h[lane] = (h[lane] ^ v) * 0x100000001b3ull;
                    h[lane] ^= h[lane] >> 29;
                }
        }
        return (h[0] ^ (h[1] * 31) ^ (h[2] * 961) ^ (h[3] * 29791)) ^ count;
    }
    void touchBlocks(uint32_t firstBlock, uint32_t count) {
        ++m_clock;
        for (uint32_t i = 0; i < count; ++i)
            m_stamp[(firstBlock + i) & (kVramBlocks - 1)] = m_clock;
    }
    void touchRect(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        if (!w || !h)
            return;
        ++m_clock;
        const PsmGeom g = geomOf(psm);
        for (uint32_t by = y & ~(g.blockH - 1); by < y + h; by += g.blockH)
            for (uint32_t bx = x & ~(g.blockW - 1); bx < x + w; bx += g.blockW)
                m_stamp[blockOf(psm, bp, bw, bx, by)] = m_clock;
    }

    uint32_t read(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y) const {
        uint32_t nib = 0;
        const uint32_t a = pixelAddr(psm, bp, bw, x, y, &nib) & (kVramSize - 1);
        const uint8_t* p = m_data.data() + a;
        switch (psm) {
            case PSM_CT32:
            case PSM_Z32: {
                uint32_t v;
                std::memcpy(&v, p, 4);
                return v;
            }
            case PSM_CT24:
            case PSM_Z24: {
                uint32_t v;
                std::memcpy(&v, p, 4);
                return v & 0xFFFFFFu;
            }
            case PSM_CT16:
            case PSM_CT16S:
            case PSM_Z16:
            case PSM_Z16S: {
                uint16_t v;
                std::memcpy(&v, p, 2);
                return v;
            }
            case PSM_T8:
                return p[0];
            case PSM_T8H:
                return p[3];
            case PSM_T4:
                return (p[0] >> (nib * 4)) & 0xF;
            case PSM_T4HL:
                return p[3] & 0xF;
            case PSM_T4HH:
                return p[3] >> 4;
            default:
                return 0;
        }
    }

    // does not stamp
    void write(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t v) {
        uint32_t nib = 0;
        const uint32_t a = pixelAddr(psm, bp, bw, x, y, &nib) & (kVramSize - 1);
        uint8_t* p = m_data.data() + a;
        switch (psm) {
            case PSM_CT32:
            case PSM_Z32:
                std::memcpy(p, &v, 4);
                break;
            case PSM_CT24:
            case PSM_Z24:
                p[0] = (uint8_t)v;
                p[1] = (uint8_t)(v >> 8);
                p[2] = (uint8_t)(v >> 16);
                break;
            case PSM_CT16:
            case PSM_CT16S:
            case PSM_Z16:
            case PSM_Z16S: {
                uint16_t h = (uint16_t)v;
                std::memcpy(p, &h, 2);
                break;
            }
            case PSM_T8:
                p[0] = (uint8_t)v;
                break;
            case PSM_T8H:
                p[3] = (uint8_t)v;
                break;
            case PSM_T4:
                p[0] = (uint8_t)((p[0] & (nib ? 0x0F : 0xF0)) | ((v & 0xF) << (nib * 4)));
                break;
            case PSM_T4HL:
                p[3] = (uint8_t)((p[3] & 0xF0) | (v & 0xF));
                break;
            case PSM_T4HH:
                p[3] = (uint8_t)((p[3] & 0x0F) | ((v & 0xF) << 4));
                break;
            default:
                break;
        }
    }

private:
    std::vector<uint8_t> m_data;
    std::vector<uint64_t> m_stamp;
    uint64_t m_clock = 1;
};

} // namespace gsr
