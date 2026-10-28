#include "../src/lib/drakan_widescreen_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>
#include <limits>

using drakan_ws::detail::WorldPolicy;
static int checks = 0;
static void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}

static void projectionChecks() {
    using namespace drakan_ws::detail;
    constexpr float wide = 16.0f / 9.0f;
    constexpr float focal = 76.27223205566406f;
    ProjectionScope scope;
    const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
    check(scope.effectiveAspect(7) == nativeAspect, "UI before any world is native");
    const float stock = -focal / 64.0f;
    const float native = projectionX(focal, 64, scope.effectiveAspect(7));
    check(std::memcmp(&native, &stock, 4) == 0, "native projection bit-identical to stock divide");
    scope.begin(7, 0x135f7c, wide);
    check(scope.world(7) && !scope.world(8), "world phase belongs only to its EE thread");
    check(scope.returnPc == 0x135f7c, "caller retained for early-return cleanup");
    const float sx = projectionX(focal, 64, scope.effectiveAspect(7));
    check(near(sx, -0.8938152194f), "world matrix uses measured wide scale");
    check(scope.effectiveAspect(8) == nativeAspect, "UI on another guest thread is native");
    check(!scope.end(8) && scope.world(7), "other thread cannot close suspended world");
    // Keeping this scope open models a yield/resume. A new host request is not
    // observed until the next world begins, so projection and culling agree.
    check(scope.effectiveAspect(7) == wide, "world aspect remains latched across yields");

    auto correct = [&](float& width, uint32_t pc = 0x199ff0, uint32_t ra = 0x133508, uint32_t output = 0x6c5e04,
                       uint32_t input = 0x1ff0000, int thread = 7, float depth = 76.27223205566406f,
                       float scale = -0.8938152194f) {
        return correctFrustumWidth(scope, thread, pc, ra, output, 0x6c5d80, input, 0x1ff0000, depth, scale, width);
    };
    float width = 64;
    check(correct(width) && near(width, 85.3333333f), "CPU frustum width follows actual wide matrix");
    check(near(width / focal, 1 / -sx), "CPU side slope equals VU clip slope");
    check(std::fabs(sx) < 1 && focal > 64 && focal < width,
          "x/depth=1 witness culled by old CPU planes is inside corrected planes and VU");
    float extended = width * 3;
    check(!correct(extended, 0x199ff0, 0x133668) && near(extended, 256),
          "extended frustum inherits width without a second widening");
    width = 64;
    check(!correct(width, 0x199ff8) && width == 64, "interior helper resume cannot apply correction twice");
    check(!correct(width, 0x199ff0, 0x123456), "unrelated frustum caller untouched");
    check(!correct(width, 0x199ff0, 0x133508, 0x6c5e18), "unrelated output planes untouched");
    check(!correct(width, 0x199ff0, 0x133508, 0x6c5e04, 0x1ff0010), "nonlocal input vector untouched");
    check(!correct(width, 0x199ff0, 0x133508, 0x6c5e04, 0x1ff0000, 8), "UI thread CPU planes untouched");
    check(!correct(width, 0x199ff0, 0x133508, 0x6c5e04, 0x1ff0000, 7, focal, 0), "zero projection rejected");
    check(!correct(width, 0x199ff0, 0x133508, 0x6c5e04, 0x1ff0000, 7, focal, 1), "reversed projection rejected");
    check(!correct(width, 0x199ff0, 0x133508, 0x6c5e04, 0x1ff0000, 7, std::numeric_limits<float>::quiet_NaN()),
          "invalid focal rejected");
    check(width == 64, "rejected corrections preserve input bits");

    check(scope.end(7), "EndWorld closes projection phase");
    check(!scope.world(7) && scope.effectiveAspect(7) == nativeAspect,
          "inventory draw and capture after EndWorld use native projection");
    check(near(projectionX(focal, 27.733118f, scope.effectiveAspect(7)), -2.750222f),
          "inventory viewport keeps its native scale");
    check(!correct(width) && width == 64, "UI CPU frustum remains native");
    check(!scope.end(7), "repeat EndWorld is harmless");
    scope.begin(7, 0x135f7c, nativeAspect);
    check(!correct(width) && width == 64, "native world CPU frustum untouched");
    check(projectionX(focal, 64, scope.effectiveAspect(7)) == stock, "switch to 4:3 overwrites a formerly wide scale");
    scope.end(7);
    scope.begin(7, 0x135f7c, wide);
    check(near(projectionX(focal, 64, scope.effectiveAspect(7)), sx),
          "next world reapplies widescreen after inventory");
    scope.end(7); // early-return cleanup uses the same restoration path
    check(scope.effectiveAspect(7) == nativeAspect, "early world return leaves UI native");
}

// Independent side-plane oracle: the EE plane uses width/focal, while the VU
// compares |projected X| with W=-viewZ. Test both signs away from the rounding
// boundary; this does not claim to replay the VU interpreter or any actor.
static void projectionSweepChecks() {
    using namespace drakan_ws::detail;
    unsigned cases = 0;
    for (float aspect : {nativeAspect, 16.0f / 9.0f, 21.0f / 9.0f, 32.0f / 9.0f})
        for (float degrees : {15.0f, 30.0f, 60.0f, 80.0f, 120.0f})
            for (float viewportWidth : {27.733118f, 32.0f, 64.0f}) {
                ProjectionScope scope;
                const float focal = 64.0f / std::tan(degrees * 0.008726646259971648f);
                scope.begin(7, 0x135f7c, aspect);
                const float sx = projectionX(focal, viewportWidth, scope.effectiveAspect(7));
                float width = viewportWidth;
                const bool changed = correctFrustumWidth(scope, 7, 0x199ff0, 0x133508, 0x6c5e04, 0x6c5d80, 0x1ff0000,
                                                         0x1ff0000, focal, sx, width);
                check(changed == (aspect != nativeAspect), "sweep: only wide world frustum is corrected");
                check(std::fabs((width / focal) * -sx - 1.0f) < 2e-6f,
                      "sweep: zoom and viewport preserve CPU/VU side slope");
                for (float depth : {32.0f, 64.0f, 4096.0f})
                    for (float edge : {-1.01f, -0.999f, 0.0f, 0.999f, 1.01f}) {
                        const float x = edge * depth * (width / focal);
                        const bool cpuInside = std::fabs(x) <= depth * (width / focal);
                        const bool vuInside = std::fabs(sx * x) <= depth;
                        check(cpuInside == vuInside, "sweep: both side-plane classifications agree");
                        check(vuInside == (std::fabs(edge) < 1.0f), "sweep: inside/outside witness remains meaningful");
                        ++cases;
                    }
                const float wideWidth = width;
                scope.end(7);
                check(scope.effectiveAspect(7) == nativeAspect, "sweep: UI after each zoom is native");
                const float nativeX = projectionX(focal, viewportWidth, scope.effectiveAspect(7));
                const float stock = -focal / viewportWidth;
                check(std::memcmp(&nativeX, &stock, sizeof(float)) == 0,
                      "sweep: UI projection is bit-identical after each wide world");
                check(!correctFrustumWidth(scope, 7, 0x199ff0, 0x133508, 0x6c5e04, 0x6c5d80, 0x1ff0000, 0x1ff0000,
                                           focal, sx, width) &&
                          width == wideWidth,
                      "sweep: UI cannot mutate world frustum inputs");
            }
    std::printf("projection sweep: %u side-plane witnesses (4 aspects, 5 FOVs, 3 viewport widths)\n", cases);
}

static void menuChecks() {
    using namespace drakan_ws::detail;
    for (float aspect : {16.0f / 9, 21.0f / 9, 32.0f / 9}) {
        WorldPolicy world;
        world.update(100, 0, 8, false);
        for (int screen : {8, 1, 2, 3, 4, 5, 9, 10, 13, 23, 27, 28}) {
            auto p = layoutPolicy(screen, world.wide, true, aspect, false);
            check(p.menu && p.presentWide, "title and child menus fill selected aspect");
            check(p.projection == nativeAspect, "title layout cannot widen world projection");
            check(std::fabs(p.foregroundScale * aspect - nativeAspect) < 1e-6f,
                  "menu text preserves physical geometry even with HUD correction disabled");
            for (float x : {20.0f, 128.0f, 256.0f, 384.0f, 492.0f}) {
                float rendered = 256 + (x - 256) * p.foregroundScale;
                float hit = 256 + (rendered - 256) / p.foregroundScale;
                check(std::fabs(hit - x) < 0.0001f, "centered menu mouse inverse matches foreground");
            }
        }
        world.update(200, 200, -1, false);
        world.update(1000, 200, 23, false);
        auto pause = layoutPolicy(23, world.wide, true, aspect, true);
        check(pause.menu && pause.presentWide && pause.projection == aspect,
              "long pause keeps wide world behind native menu");
        world.update(1006, 200, -1, false);
        auto resume = layoutPolicy(-1, world.wide, true, aspect, true);
        check(!resume.menu && resume.projection == aspect, "pause close restores gameplay layout with grace");
        check(gameplayHudSize(-1, true) && gameplayHudSize(23, true) && gameplayHudSize(21, true),
              "gameplay, pause and inventory keep the gameplay HUD size");
        check(!gameplayHudSize(23, false) && !gameplayHudSize(8, true) && !gameplayHudSize(9, true) &&
                  !gameplayHudSize(14, true) && !gameplayHudSize(1, true),
              "title, options, loading and message boxes lay out at size 1");
        world.update(1007, 1007, 8, false);
        auto title = layoutPolicy(8, world.wide, true, aspect, true);
        check(title.presentWide && title.projection == nativeAspect,
              "return to title preserves wide layout without world projection");
        auto loading = layoutPolicy(14, false, true, aspect, true);
        check(loading.menu && loading.presentWide && loading.projection == nativeAspect,
              "loading keeps selected presentation aspect without widening UI projection");
        check(std::fabs(loading.foregroundScale * aspect - nativeAspect) < 1e-6f,
              "loading text and progress retain native proportions");
        for (int screen : {-1, 19, 29, 30}) {
            auto movie = layoutPolicy(screen, false, true, aspect, true);
            check(!movie.menu && !movie.presentWide && movie.foregroundScale == 1,
                  "intro movie and unknown pages stay native");
        }
        {
            const auto credits = layoutPolicy(19, false, true, aspect, true);
            check(credits.stretchImage && !credits.menu && credits.projection == nativeAspect &&
                      credits.foregroundScale == 1,
                  "credits picture is stretched by the presenter only");
            check(!layoutPolicy(19, false, false, aspect, true).stretchImage, "native mode keeps the credits at 4:3");
            check(!layoutPolicy(8, false, true, aspect, true).stretchImage, "title uses its own backdrop policy");
        }
        check(!layoutPolicy(8, false, false, aspect, true).presentWide, "native mode stays native");
        check(!layoutPolicy(21, true, true, aspect, true).menu, "inventory retains separate policy");
    }
}

static void configurableFovChecks() {
    using namespace drakan_ws::detail;
    check(fieldOfViewScale(80) == 1, "original FOV is identity");
    check(fieldOfViewScale(100) < 1 && fieldOfViewScale(60) > 1, "larger FOV widens rather than zooms");
    check(fieldOfViewScale(-1) == fieldOfViewScale(60), "FOV lower bound");
    check(fieldOfViewScale(999) == fieldOfViewScale(120), "FOV upper bound");
    check(fieldOfViewScale(std::numeric_limits<float>::quiet_NaN()) == 1, "invalid FOV uses original");
    for (float fov : {60.f, 80.f, 100.f, 120.f}) {
        ProjectionScope scope;
        const float scale = fieldOfViewScale(fov);
        scope.begin(7, 0x135f7c, nativeAspect, scale);
        check(scope.effectiveFovScale(7) == scale && scope.effectiveFovScale(8) == 1,
              "FOV belongs only to world owner across yields");
        const float focal = 76.272232f, sx = projectionX(focal, 64, nativeAspect) * scale;
        float width = 64;
        const bool corrected = correctFrustumWidth(scope, 7, 0x199ff0, 0x133508, 0x6c5e04, 0x6c5d80, 0x1ff0000,
                                                   0x1ff0000, focal, sx, width);
        check(corrected == (fov != 80), "custom FOV corrects culling even at 4:3");
        // Independent angular oracle: edge ray of requested reference FOV.
        const float edge = std::tan(fov * .5f * .01745329252f);
        check(std::fabs(width / focal - edge) < 1e-5f, "CPU plane matches requested angular edge");
        scope.end(7);
        check(scope.effectiveFovScale(7) == 1, "UI FOV restored after EndWorld");
    }
}

int main() {
    configurableFovChecks();
    menuChecks();
    projectionChecks();
    projectionSweepChecks();
    WorldPolicy p;
    p.update(100, 0, 8, false);
    check(!p.wide, "cold title");
    for (int screen : {1, 2, 3, 4, 5, 9, 10, 13, 0x15, 0x17, 0x1b, 0x1c}) {
        p.update(200, 0, screen, false);
        check(!p.wide, "menus cannot establish world without camera");
    }
    p.update(300, 300, -1, false);
    check(p.wide, "camera establishes world");
    p.update(400, 300, 0x17, false);
    check(p.wide, "Escape preserves world even when first sampled after timeout");
    p.update(10000, 300, 0x17, false);
    check(p.wide, "long pause preserves world");
    p.update(20000, 300, 0x15, false);
    check(p.wide, "inventory preserves world");
    for (int screen : {1, 2, 3, 4, 5, 9, 10, 13, 0x1b, 0x1c}) {
        p.update(30000, 300, screen, false);
        check(p.wide, "world menu children preserve world");
    }
    {
        // Save: browser (5) -> save write (0x1a), camera ticks for the slot picture, then ~15 silent vblanks while the
        // card is written (build/harness/save-aspect: worldAge=15 on screen 26 pushed 4:3 for one tick).
        WorldPolicy save = p;
        save.update(30000, 30000, 0x1a, false);
        save.update(30015, 30000, 0x1a, false);
        check(save.wide, "save write keeps the world through the card write stall");
        check(!drakan_ws::detail::layoutPolicy(0x1a, true, true, 16.0f / 9, true).menu,
              "save write is drawn like gameplay");
        WorldPolicy cold;
        cold.update(100, 0, 0x1a, false);
        check(!cold.wide, "save write cannot establish a world without camera");
    }
    p.update(30006, 300, -1, false);
    check(p.wide, "closing menu retains six-vblank resume grace");
    p.update(30007, 30007, -1, false);
    check(p.wide, "resumed camera keeps world");
    p.update(30013, 30007, -1, false);
    check(p.wide, "camera recency inclusive at six");
    p.update(30020, 30007, 29, false);
    check(!p.wide, "unknown screen still expires after camera silence");
    for (int screen : {8, 0xe, 0x13}) {
        p.update(40000, 40000, -1, false);
        p.update(40001, 40000, screen, false);
        check(!p.wide, "frontend clears world even with recent camera");
        p.update(50000, 40000, 0x17, false);
        check(!p.wide, "pause cannot revive cleared frontend world");
    }
    p.update(60000, 60000, -1, false);
    p.update(70000, 60000, -1, true);
    check(p.wide, "F1 preserves held world");
    p.update(70006, 60000, -1, false);
    check(p.wide, "F1 close gets resume grace");
    p.update(70007, 60000, -1, false);
    check(p.wide, "gameplay retains aspect while the resumed camera is stalled");
    for (uint64_t spacing : {20ull, 60ull, 600ull}) {
        WorldPolicy slow;
        for (uint64_t tick = 100; tick < 100 + spacing * 3; ++tick) {
            const uint64_t camera = 100 + (tick - 100) / spacing * spacing;
            slow.update(tick, camera, -1, false);
            const auto layout = drakan_ws::detail::layoutPolicy(-1, slow.wide, true, 16.0f / 9, true);
            check(slow.wide && layout.presentWide && layout.projection == 16.0f / 9,
                  "slow gameplay and streaming stalls retain widescreen");
        }
        slow.update(100 + spacing * 3, 100, 8, false);
        check(!slow.wide, "title clears held stalled gameplay");
        slow.update(100 + spacing * 4, 100, 30, false);
        check(!slow.wide, "movie does not revive held gameplay");
    }
    {
        WorldPolicy movie;
        for (uint64_t tick = 1000; tick < 1100; ++tick) // logo / intro: camera ticks every vblank, IPU every other
            movie.update(tick, tick, 30, false, tick & ~1ull);
        check(!movie.wide, "movie frames keep the presentation 4:3 despite camera updates");
        movie.update(1099 + WorldPolicy::kMovieGrace, 1099 + WorldPolicy::kMovieGrace, -1, false, 1099);
        check(!movie.wide, "movie grace covers the end of the movie");
        movie.update(1100 + WorldPolicy::kMovieGrace, 1100 + WorldPolicy::kMovieGrace, -1, false, 1099);
        check(movie.wide, "gameplay camera after the movie establishes the world");
    }
    for (int screen : {0x1d, 0x1e}) {
        WorldPolicy stall; // camera ticking on a movie screen with no IPU frame for longer than the grace
        for (uint64_t tick = 2000; tick < 2100; ++tick)
            stall.update(tick, tick, screen, false, 2000);
        check(!stall.wide, "movie screens stay 4:3 through IPU stalls and after the last frame");
    }
    p.update(79999, 0, 8, false);
    p.update(80000, 0, -1, true);
    check(!p.wide, "F1 cannot establish world");
    std::printf("widescreen policy: %d checks passed\n", checks);
}
