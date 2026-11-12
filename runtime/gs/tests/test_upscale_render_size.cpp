// Render rectangle under a temporal upscaler (upscale_render_size.h): the plain rule must give the sizes the renderer
// always used, the view rule the upscaler's own input size for the picture that reaches the screen.
#include "upscale_render_size.h"

#include <cstdio>

static int g_fail = 0;
static void expectSize(gsr::upscale::Size got, uint32_t w, uint32_t h, const char* what) {
    const bool ok = got.width == w && got.height == h;
    std::printf("  [%s] %s: %ux%u\n", ok ? "ok" : "FAIL", what, got.width, got.height);
    if (!ok) {
        ++g_fail;
        std::printf("         want %ux%u\n", w, h);
    }
}

int main() {
    using gsr::upscale::renderSize;
    const float quality = 1.0f / 1.5f, balanced = 1.0f / 1.724f, performance = 0.5f;
    // plain rule: 512 x 512 frame at scale 8 (3840 x 2160 screen), quality
    expectSize(renderSize(4096, 4096, quality, false), 2730, 2730, "plain, scale 8, quality");
    expectSize(renderSize(4096, 4096, performance, false), 2048, 2048, "plain, scale 8, performance");
    expectSize(renderSize(1536, 1344, quality, false), 1024, 896, "plain, 512 x 448 frame at scale 3");
    expectSize(renderSize(4096, 4096, quality, true), 4096, 4096, "DLAA renders the whole output");
    expectSize(renderSize(4096, 4096, quality, true, 3840, 2160), 4096, 4096, "DLAA ignores the view");
    expectSize(renderSize(16, 16, quality, false), 16, 16, "never under 16 and never over the output");
    // view rule: the upscaler's standard input for the picture on screen
    expectSize(renderSize(4096, 4096, quality, false, 3840, 2160), 2560, 1440, "view 3840 x 2160, quality");
    expectSize(renderSize(4096, 4096, balanced, false, 3840, 2160), 2226, 1252, "view 3840 x 2160, balanced");
    expectSize(renderSize(4096, 4096, performance, false, 3840, 2160), 1920, 1080, "view 3840 x 2160, performance");
    expectSize(renderSize(2560, 2560, quality, false, 2560, 1440), 1706, 960, "view 2560 x 1440 at scale 5");
    // 4:3 picture on a 3840 x 2160 screen: 2880 x 2160 view, scale 6 (3072)
    expectSize(renderSize(3072, 3072, quality, false, 2880, 2160), 1920, 1440, "pillarboxed 4:3 view");
    // a view larger than the output on an axis never raises the render size above the plain rule
    expectSize(renderSize(1024, 1024, quality, false, 3840, 2160), 682, 682, "low scale: the output limits both axes");
    expectSize(renderSize(2048, 2048, quality, false, 3840, 1080), 1364, 720, "one axis limited by each");
    expectSize(renderSize(4096, 4096, quality, false, 0, 2160), 2730, 2730, "an unknown view is the plain rule");
    std::printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
