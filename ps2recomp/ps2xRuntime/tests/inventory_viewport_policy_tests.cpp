#include "../src/lib/drakan_inventory_viewport_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}
static bool near(float a, float b) {
    return std::fabs(a - b) < .0002f;
}
static bool apply(float& scale, float hud = .571428571f, uint32_t pc = 0x132f60, uint32_t ra = 0x2c4444,
                  int screen = 21, uint32_t obj = 0x6c5d80, uint32_t renderer = 0x6c5d80, uint32_t frame = 512,
                  uint32_t viewport = 512, int offset = 170) {
    return drakan_ws::detail::inventoryWorldCrop(pc, ra, screen, obj, renderer, frame, viewport, offset, hud, scale);
}
int main() {
    // Captured first-world crop edge 315.0625, centre426. This uses the
    // submitted edge to reconstruct an arithmetic fixture, not live RAM proof.
    constexpr float oldScale = (426.0f - 315.0625f) / 256.0f;
    float scale = oldScale;
    check(apply(scale), "captured inventory crop is eligible");
    check(near(426 - 256 * scale, 315.0625f * (4.0f / 7.0f)), "new left edge follows GUI compression");
    check(426 - 256 * scale < 211, "expanded crop covers observed stale211..315 gap");
    check(scale > oldScale, "viewport expands before clipping");
    for (float aspect : {16.f / 9, 21.f / 9, 32.f / 9})
        for (float original : {.05f, .25f, oldScale, .66667f, .95f})
            for (int offset : {0, 85, 170}) {
                float s = original;
                const float hud = (4.f / 3) / aspect;
                check(apply(s, hud, 0x132f60, 0x2c4444, 21, 0x6c5d80, 0x6c5d80, 512, 512, offset),
                      "animation crop and supported aspect accepted");
                const float centre = 256.f + offset;
                const float oldLeft = centre - 256 * original, newLeft = centre - 256 * s;
                check(near(newLeft, oldLeft * hud), "expanded native edge aligns to mapped edge");
                check(near((newLeft + centre + 256 * s) / 2, centre), "character centre unchanged");
                for (float focal : {32.f, 76.27223f, 128.f}) {
                    const float oldExtent = -256 * original, newExtent = -256 * s;
                    const float oldProjection = -focal / (64 * original) * (4.f / 3) / aspect;
                    const float newProjection = -focal / (64 * s) * (4.f / 3) / aspect;
                    check(near(oldExtent * oldProjection, newExtent * newProjection),
                          "raster scale times projection preserves actor size at each FOV");
                    const float witness = (64 * original + 64 * s) / 2 / focal;
                    check(witness > 64 * original / focal && witness < 64 * s / focal,
                          "newly exposed side-plane witness survives expanded CPU crop");
                }
                // Original native setter must precede this crop. No second
                // correction on already cropped +118 or a yielded resume.
                float repeat = s;
                check(!apply(repeat, hud, 0x132f60, 0x2c4444, 21, 0x6c5d80, 0x6c5d80, 512, uint32_t(512 * s), offset) &&
                          repeat == s,
                      "cropped-width input does not compound correction");
            }
    for (float hud :
         {0.f, 1.f, 1.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        float s = oldScale;
        check(!apply(s, hud) && s == oldScale, "native/disabled/invalid HUD factor untouched");
    }
    for (int screen : {-1, 8, 9, 19, 23, 27}) {
        float s = oldScale;
        check(!apply(s, .5714f, 0x132f60, 0x2c4444, screen) && s == oldScale,
              "other GUI and gameplay screens untouched");
    }
    for (int box : {1, 2, 3, 0x1b})
        check(drakan_ws::detail::inventoryCropScreen(box, 21) == 21, "message box over the inventory keeps its crop");
    check(drakan_ws::detail::inventoryCropScreen(21, -1) == 21, "inventory itself");
    for (int parent : {-1, 8, 9, 0x17})
        check(drakan_ws::detail::inventoryCropScreen(1, parent) == 1, "message box over another screen");
    check(drakan_ws::detail::inventoryCropScreen(0x17, 21) == 0x17, "non-box screen over the inventory");
    for (uint32_t ra : {0x430230u, 0x430884u, 0x134034u, 0u}) {
        float s = oldScale;
        check(!apply(s, .5714f, 0x132f60, ra) && s == oldScale, "grid/preview/FOV/unrelated caller untouched");
    }
    float s = oldScale;
    check(!apply(s, .5714f, 0x132f64), "resumed instruction cannot expand again");
    check(!apply(s, .5714f, 0x132f60, 0x2c4444, 21, 0x123456), "other renderer untouched");
    check(!apply(s, .5714f, 0x132f60, 0x2c4444, 21, 0, 0), "null renderer rejected");
    for (int offset : {-85, 513})
        check(!apply(s, .5714f, 0x132f60, 0x2c4444, 21, 0x6c5d80, 0x6c5d80, 512, 512, offset),
              "leftward UI placement or invalid offset rejected");
    for (float original :
         {0.f, -1.f, 1.f, 2.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        float value = original;
        check(!apply(value), "invalid or noncrop scale rejected");
    }
    std::printf("inventory viewport policy: %u CPU checks PASS\n", checks);
}
