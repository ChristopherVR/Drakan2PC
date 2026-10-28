#include "ps2_vu1_aot.h"

#include <cstdlib>
#include <cstring>

#include "ps2_vu1_image.inc"

namespace Vu1Aot {
const bool g_blockStats = [] {
    const char* e = std::getenv("DRAKAN_VU_STATS");
    return e && e[0] && e[0] != '0';
}();
const uint32_t kPairs = kVu1ImagePairs;
Fn g_table[kVu1ImagePairs];
Fn g_dispatch[kVu1ImagePairs];
Fn g_blockDispatch[kVu1ImagePairs];

void registerSlice0();
void registerSlice1();
void registerSlice2();
void registerSlice3();
void registerBlocks0();
void registerBlocks1();
void registerBlocks2();
void registerBlocks3();
void registerNativeKernels();

bool enabled() {
    static const bool on = []() {
        const char* e = std::getenv("DRAKAN_VU_AOT");
        return !(e && e[0] == '0');
    }();
    return on;
}

bool blocksEnabled() {
    static const bool on = []() {
        const char* e = std::getenv("DRAKAN_VU_BLOCKS");
        return !(e && e[0] == '0');
    }();
    return on;
}

bool recoveryEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_RECOVERY");
        return !(e && e[0] == '0');
    }();
    return on;
}

bool imageMatches(const uint8_t* code, uint32_t size) {
    if (size < kVu1ImagePairs * 8u || std::memcmp(code, kVu1Image, kVu1ImagePairs * 8u) != 0)
        return false;
    static const bool registered = []() {
        registerSlice0();
        registerSlice1();
        registerSlice2();
        registerSlice3();
        for (uint32_t i = 0; i < kVu1ImagePairs; ++i)
            g_dispatch[i] = g_table[i];
        if (blocksEnabled()) {
            registerBlocks0();
            registerBlocks1();
            registerBlocks2();
            registerBlocks3();
        }
        for (uint32_t i = 0; i < kVu1ImagePairs; ++i)
            g_blockDispatch[i] = g_dispatch[i];
        if (blocksEnabled())
            registerNativeKernels();
        return true;
    }();
    (void)registered;
    return true;
}
} // namespace Vu1Aot
