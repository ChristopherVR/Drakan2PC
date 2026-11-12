// Policy of sampling a texture straight from a render target (rt_texture_source.h), with the cases measured for
// issue 6 (docs/journal/2026-11-10/issue6-view-dependent-textures.md).
#include "rt_texture_source.h"

#include <cstdio>

static int g_fail = 0;
static void expect(bool got, bool want, const char* what) {
    std::printf("  [%s] %s\n", got == want ? "ok" : "FAIL", what);
    if (got != want)
        ++g_fail;
}

int main() {
    using gsr::rt_source::targetOwnsTexture;
    // {page rows of the texture inside the target, rows that only mirror memory}, offset, py, h, target rows
    expect(targetOwnsTexture({4, 0}, false, 0, 128, 512), true, "target drawn over the whole texture");
    expect(targetOwnsTexture({4, 0}, true, 128, 128, 512), true, "drawn atlas cell below the target's base");
    expect(targetOwnsTexture({4, 0}, false, 0, 128, 128), true, "read back target that memory did not replace");
    // The HUD atlas at row 480 of an old 128 x 512 target: only one of its four page rows exists (empty health orb).
    expect(targetOwnsTexture({1, 1}, true, 480, 128, 512), false, "texture runs past the last target row");
    expect(targetOwnsTexture({1, 0}, true, 480, 128, 512), false, "the same even when that row was drawn");
    expect(targetOwnsTexture({4, 4}, true, 128, 128, 512), false, "uploaded texture on never drawn target rows");
    expect(targetOwnsTexture({4, 4}, false, 0, 128, 512), false, "uploaded texture at an old target's base");
    // The HUD atlas with the orb liquid drawn into one of its four page rows.
    expect(targetOwnsTexture({4, 3}, false, 0, 128, 256), false, "mostly uploaded atlas with one drawn row band");
    // A frame buffer with a few patches stamped into one page row, sampled with a power of two size.
    expect(targetOwnsTexture({14, 1}, false, 0, 512, 448), true, "frame buffer larger than its target");
    expect(targetOwnsTexture({16, 8}, false, 0, 512, 512), true, "half drawn target stays a target");
    expect(targetOwnsTexture({16, 9}, false, 0, 512, 512), false, "more than half from memory");
    expect(targetOwnsTexture({0, 0}, false, 0, 128, 512), false, "no rows inside the target");
    using gsr::rt_source::rowInRange;
    expect(rowInRange(0, 32, 0, 32), true, "page row 0 inside rows 0..32");
    expect(rowInRange(1, 32, 0, 32), false, "page row 1 outside rows 0..32");
    expect(rowInRange(1, 32, 63, 64), true, "page row 1 touched by its last pixel row");
    expect(rowInRange(2, 32, 0, 64), false, "range ends at the row's first pixel row");
    expect(rowInRange(0, 32, 5, 5), false, "empty range");
    std::printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
