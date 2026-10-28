// PS-ADPCM (SPU "VAG") block decoder. Same arithmetic as PCSX2 SPU2 XA_decode_block.
#pragma once
#include <cstdint>

namespace spu2 {

// Decodes one 16-byte block (byte0: filter<<4 | shift, byte1: loop flags, bytes 2..15: 28 nibbles, low nibble
// first) into 28 samples. prev1/prev2 carry the filter history (most recent / one before).
void adpcmDecodeBlock(const uint8_t block[16], int32_t& prev1, int32_t& prev2, int16_t out[28]);

} // namespace spu2
