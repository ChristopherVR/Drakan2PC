#include "world/TerrainChunks.h"

#include <cstdio>

namespace tc = world::terrain_chunks;

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };
    check(tc::kNarrowStride == 0x4cu, "disc record is 0x4c bytes");
    check(tc::kNarrowDescriptor == 0x3cu, "disc descriptor at +0x3c");
    check(tc::kWideStride == 0xa4u && tc::kWideDescriptor == 0x94u, "wide record layout");
    check(tc::maxTextures(false) == 9u, "disc engine: 9 textures (drawer flags)");
    check(tc::maxTextures(true) == 32u, "wide variant: 32 textures (record refs)");
    check(tc::kWideDrawerFlags * 4u == 0x100u, "drawer flag array of the variant is 0x100 bytes");
    if (failures == 0)
        std::printf("world_terrain_chunks_test: ok\n");
    return failures == 0 ? 0 : 1;
}
