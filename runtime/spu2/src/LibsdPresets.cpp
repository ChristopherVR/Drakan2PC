// Reads libsd's reverb presets out of the LIBSD.IRX on the player's own disc, so no Sony data ships with the port.
//
// Layout in the module's data: ten little-endian u32 work-area sizes (in 8-byte units, one per effect mode), then ten
// 68-byte records, each a zero u32 followed by the 32 u16 reverb register values. The table is recognised by an
// FNV-1a hash of those 720 bytes, taken from the Drakan PAL disc, so a different libsd build is rejected rather
// than misread.
#include "spu2/Spu2.h"

namespace spu2 {

namespace {
constexpr size_t kModes = 10;
constexpr size_t kSizesBytes = kModes * 4;
constexpr size_t kRecordBytes = 4 + 32 * 2;
constexpr size_t kTableBytes = kSizesBytes + kModes * kRecordBytes;
constexpr u64 kTableFnv1a = 0x896325285530ecfbull;

u32 readU32(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
u16 readU16(const u8* p) {
    return (u16)(p[0] | (p[1] << 8));
}
u64 fnv1a(const u8* p, size_t n) {
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
} // namespace

bool findLibsdEffectPresets(const u8* irx, size_t size, EffectPresets& out) {
    if (!irx || size < kTableBytes)
        return false;
    for (size_t at = 0; at + kTableBytes <= size; ++at) {
        // Mode OFF has the smallest possible work area (one 16-byte block): a cheap filter before hashing.
        if (readU32(irx + at) != 2 || fnv1a(irx + at, kTableBytes) != kTableFnv1a)
            continue;
        for (size_t m = 0; m < kModes; ++m) {
            out[m].workAreaBlocks = readU32(irx + at + m * 4) / 2;
            const u8* regs = irx + at + kSizesBytes + m * kRecordBytes + 4;
            for (size_t r = 0; r < out[m].regs.size(); ++r)
                out[m].regs[r] = readU16(regs + r * 2);
        }
        return true;
    }
    return false;
}

} // namespace spu2
