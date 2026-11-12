// Which page rows of a render target a use refreshes from memory (rt_sync_rows.h), with the target shapes measured in
// docs/journal/2026-11-12/perf-gpu.md: 128 pixel wide scratch targets of 512 rows (16 page rows of 32) whose first 128
// rows are drawn and sampled while the game uploads textures into the rows below.
#include "rt_sync_rows.h"

#include <cstdio>

static int g_fail = 0;
static void expectRows(gsr::rt_sync::Rows got, uint32_t first, uint32_t end, const char* what) {
    const bool ok = got.first == first && got.end == end;
    std::printf("  [%s] %s (rows %u..%u)\n", ok ? "ok" : "FAIL", what, got.first, got.end);
    if (!ok)
        ++g_fail;
}

int main() {
    using namespace gsr::rt_sync;
    expectRows(pageRows(0, 128, 32, 16), 0, 4, "128 pixel rows are four page rows");
    expectRows(pageRows(0, 129, 32, 16), 0, 5, "a started page row counts");
    expectRows(pageRows(40, 41, 32, 16), 1, 2, "one pixel row");
    expectRows(pageRows(500, 900, 32, 16), 15, 16, "clamped to the target");
    expectRows(pageRows(600, 900, 32, 16), 16, 16, "past the target: nothing");
    expectRows(pageRows(10, 10, 32, 16), 0, 0, "empty range");
    expectRows(pageRows(0, 128, 64, 8), 0, 2, "16 bit targets have 64 row pages");

    // scissor rows are inclusive
    expectRows(drawRows(0, 127, false, 0, 0, 32, 16), 0, 4, "draw into a clean target: its scissor rows");
    expectRows(drawRows(0, 127, false, 300, 400, 32, 16), 0, 4, "a stale dirty range of a clean target is ignored");
    expectRows(drawRows(0, 63, true, 0, 128, 32, 16), 0, 4, "draw inside the dirty range: the dirty range");
    expectRows(drawRows(448, 479, true, 0, 24, 32, 16), 0, 15, "draw far below the dirty range: the whole hull");
    expectRows(drawRows(0, 31, true, 448, 480, 32, 16), 0, 15, "draw far above the dirty range: the whole hull");
    expectRows(drawRows(0, 2047, false, 0, 0, 32, 16), 0, 16, "open scissor: every row");
    expectRows(drawRows(0, 127, true, 5, 5, 32, 16), 0, 4, "empty dirty range adds nothing");

    expectRows(sampleRows(0, 128, 32, 16), 0, 5, "texture at the base: its rows and the one below");
    expectRows(sampleRows(128, 128, 32, 16), 3, 9, "texture inside: one page row on each side");
    expectRows(sampleRows(384, 128, 32, 16), 11, 16, "texture ending at the last row");
    expectRows(sampleRows(0, 1024, 32, 16), 0, 16, "texture larger than the target");
    expectRows(sampleRows(600, 64, 32, 16), 16, 16, "texture past the target: nothing");
    std::printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
