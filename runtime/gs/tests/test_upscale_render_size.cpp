// Render rectangle under a temporal upscaler (upscale_render_size.h): the plain rule must give the sizes the renderer
// always used, the view rule the upscaler's own input size for the picture that reaches the screen.
#include "upscale_render_size.h"

#include <cmath>
#include <cstdio>
#include <utility>

static int g_fail = 0;
static void expectSize(gsr::upscale::Size got, uint32_t w, uint32_t h, const char* what) {
    const bool ok = got.width == w && got.height == h;
    std::printf("  [%s] %s: %ux%u\n", ok ? "ok" : "FAIL", what, got.width, got.height);
    if (!ok) {
        ++g_fail;
        std::printf("         want %ux%u\n", w, h);
    }
}

static void expectPlan(const gsr::upscale::Plan& p, uint32_t tw, uint32_t th, uint32_t ow, uint32_t oh, uint32_t rw,
                       uint32_t rh, bool windowSized, const char* what) {
    const bool ok = p.target.width == tw && p.target.height == th && p.resolve.width == ow && p.resolve.height == oh &&
                    p.render.width == rw && p.render.height == rh && p.windowSized == windowSized;
    std::printf("  [%s] %s: target %ux%u, upscaler %ux%u -> %ux%u, window sized %d\n", ok ? "ok" : "FAIL", what,
                p.target.width, p.target.height, p.render.width, p.render.height, p.resolve.width, p.resolve.height,
                (int)p.windowSized);
    if (!ok) {
        ++g_fail;
        std::printf("         want target %ux%u, upscaler %ux%u -> %ux%u, window sized %d\n", tw, th, rw, rh, ow, oh,
                    (int)windowSized);
    }
}
static void expectTrue(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    g_fail += !ok;
}

// upscale::plan: what the upscaler writes and reads, and the LOD bias that goes with it.
static void planTests() {
    using namespace gsr::upscale;
    const float quality = 1.0f / 1.5f, balanced = 1.0f / 1.724f, performance = 0.5f;
    const View w4k{512, 512, 3840, 2160}, w1440{512, 512, 2560, 1440};
    // the owner's case: 512 x 512 frame, scale 8, 3840 x 2160 window
    expectPlan(plan(512, 512, 8, quality, false, w4k), 4096, 4096, 3840, 2160, 2560, 1440, true, "4K quality");
    expectPlan(plan(512, 512, 8, balanced, false, w4k), 4096, 4096, 3840, 2160, 2226, 1252, true, "4K balanced");
    expectPlan(plan(512, 512, 8, performance, false, w4k), 4096, 4096, 3840, 2160, 1920, 1080, true, "4K performance");
    expectPlan(plan(512, 512, 8, quality, true, w4k), 4096, 4096, 4096, 4096, 4096, 4096, false,
               "DLAA keeps the whole target");
    expectPlan(plan(512, 512, 5, quality, false, w1440), 2560, 2560, 2560, 1440, 1706, 960, true,
               "1440p quality: one axis already equals the target");
    // no view, or one the presenter cannot use: the target, as before
    expectPlan(plan(512, 512, 8, quality, false), 4096, 4096, 4096, 4096, 2730, 2730, false, "unknown view");
    expectPlan(plan(512, 512, 8, quality, false, {512, 512, 3840, 0}), 4096, 4096, 4096, 4096, 2730, 2730, false,
               "half a view is no view");
    // a window that needs more pixels than the target has on an axis: the target
    expectPlan(plan(512, 512, 4, quality, false, w4k), 2048, 2048, 2048, 2048, 1364, 1364, false,
               "scale 4 at 4K: the window is larger than the target");
    expectPlan(plan(512, 512, 5, quality, false, w4k), 2560, 2560, 2560, 2560, 1706, 1706, false,
               "scale 5 at 4K: wider than the target, although not taller");
    // a window exactly the target gains nothing and stays the plain case
    expectPlan(plan(512, 512, 2, quality, false, {512, 512, 1024, 1024}), 1024, 1024, 1024, 1024, 682, 682, false,
               "window equal to the target");
    // a mapping that is not a whole number of window pixels cannot be shown unscaled
    expectPlan(plan(512, 512, 8, quality, false, {448, 448, 3840, 2160}), 4096, 4096, 4096, 4096, 2730, 2730, false,
               "fractional window pixels per frame");
    // a display that shows 448 of the frame's 512 lines: the frame's own share of the window, whole numbers
    expectPlan(plan(512, 512, 8, quality, false, {512, 448, 3584, 1792}), 4096, 4096, 3584, 2048, 2388, 1364, true,
               "the display shows part of the frame");
    // pillarboxed 4:3 picture on a 4K screen, scale 6
    expectPlan(plan(512, 512, 6, quality, false, {512, 512, 2880, 2160}), 3072, 3072, 2880, 2160, 1920, 1440, true,
               "pillarboxed 4:3");
    // LOD bias: log2(render / resolve); the owner's case keeps the standard quality bias on both axes
    expectTrue(std::fabs(lodBias(plan(512, 512, 8, quality, false, w4k)) - std::log2(2.0f / 3.0f)) < 1e-3f,
               "4K quality bias is log2(2/3)");
    expectTrue(std::fabs(lodBias(plan(512, 512, 8, quality, false)) - std::log2(2730.0f / 4096.0f)) < 1e-6f,
               "plain rule bias is what the renderer always used");
    expectTrue(lodBias(plan(512, 512, 8, quality, true, w4k)) == 0.0f, "DLAA has no bias");
    expectTrue(std::fabs(lodBias(plan(512, 512, 8, performance, false, w4k)) + 1.0f) < 1e-6f, "performance bias -1");
    expectTrue(lodBias(Plan{}) == 0.0f, "an empty plan has no bias");
    // the unfiltered write-back: every resolve pixel is shown by at least one target pixel, in order, and the
    // target pixel under a window pixel's centre shows that window pixel
    for (const auto& [target, resolve] :
         {std::pair{4096u, 3840u}, {4096u, 2160u}, {2560u, 1440u}, {3072u, 2880u}, {512u, 512u}, {17u, 16u}}) {
        bool monotone = true, onto = resolveIndex(0, target, resolve) == 0, centre = true;
        uint32_t previous = 0;
        for (uint32_t t = 0; t < target; ++t) {
            const uint32_t r = resolveIndex(t, target, resolve);
            monotone = monotone && r < resolve && (r == previous || r == previous + 1);
            previous = r;
        }
        onto = onto && previous == resolve - 1;
        for (uint32_t x = 0; x < resolve; ++x) {
            const uint32_t t = static_cast<uint32_t>((uint64_t(x) * 2 + 1) * target / (uint64_t(resolve) * 2));
            centre = centre && resolveIndex(t, target, resolve) == x;
        }
        std::printf("  [%s] write-back %u <- %u: in order, every pixel shown, centre pixel exact\n",
                    monotone && onto && centre ? "ok" : "FAIL", target, resolve);
        g_fail += !(monotone && onto && centre);
    }
    expectTrue(resolveIndex(5, 0, 16) == 0 && resolveIndex(5, 16, 0) == 0, "degenerate sizes do not divide by zero");
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
    planTests();
    std::printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
