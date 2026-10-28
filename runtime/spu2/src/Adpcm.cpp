// Port of PCSX2 SPU2 XA_decode_block (GPL-3.0-or-later, PCSX2 Dev Team).
#include "spu2/Adpcm.h"
#include <algorithm>

namespace spu2 {

static const int32_t kPred[16][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};

void adpcmDecodeBlock(const uint8_t block[16], int32_t& prev1, int32_t& prev2, int16_t out[28]) {
    const int32_t header = block[0];
    const int32_t shift = (header & 0xF) + 16;
    const int id = (header >> 4) & 0xF;
    const int32_t pred1 = kPred[id][0]; // ids > 4 decode as "no prediction" (table zero-filled)
    const int32_t pred2 = kPred[id][1];
    for (int i = 0; i < 14; ++i) {
        const uint8_t b = block[2 + i];
        int32_t data = (int32_t)((uint32_t)b << 28); // low nibble -> top
        int32_t pcm = (data >> shift) + (((pred1 * prev1) + (pred2 * prev2) + 32) >> 6);
        pcm = std::clamp<int32_t>(pcm, -0x8000, 0x7fff);
        out[i * 2] = (int16_t)pcm;
        data = (int32_t)((uint32_t)b << 24) & (int32_t)0xF0000000; // high nibble -> top
        int32_t pcm2 = (data >> shift) + (((pred1 * pcm) + (pred2 * prev1) + 32) >> 6);
        pcm2 = std::clamp<int32_t>(pcm2, -0x8000, 0x7fff);
        out[i * 2 + 1] = (int16_t)pcm2;
        prev2 = pcm;
        prev1 = pcm2;
    }
}

} // namespace spu2
