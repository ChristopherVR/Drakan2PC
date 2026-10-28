#include "../src/lib/drakan_scene_camera_policy.h"
#include "../src/lib/drakan_widescreen_policy.h"
#include <cstdio>
#include <cstdlib>
using namespace drakan_scene_camera::policy;
static unsigned checks = 0;
static void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}
int main() {
    drakan_ws::detail::ProjectionScope scope;
    check(!worldPacket(scope.world(7), false, true, true, true),
          "matching UI camera before world cannot establish World");
    scope.begin(7, 0x135f7c, 16.0f / 9.0f);
    check(worldPacket(scope.world(7), false, true, true, true),
          "valid background world remains eligible with inventory GUI21 allowed");
    check(!worldPacket(scope.world(8), false, true, true, true),
          "matching UI camera on other guest thread cannot establish World");
    check(!worldPacket(scope.world(7), true, true, true, true), "preview remains nonworld inside engine world phase");
    check(!worldPacket(scope.world(7), false, false, true, true),
          "object-local matrix still rejected inside world phase");
    check(!worldPacket(scope.world(7), false, true, false, true), "frontend GUI remains rejected inside world phase");
    check(!worldPacket(scope.world(7), false, true, true, false), "world without viewport remains rejected");
    scope.end(7);
    check(!worldPacket(scope.world(7), false, true, true, true),
          "matching inventory camera after EndWorld and GUI-begin cannot reopen World");
    check(!worldPacket(scope.world(7), false, true, true, true), "repeated UI viewport setup cannot reopen World");
    scope.begin(7, 0x135f7c, drakan_ws::detail::nativeAspect);
    check(worldPacket(scope.world(7), false, true, true, true), "native4:3 engine world still emits World");
    scope.end(7);
    check(!worldPacket(scope.world(7), false, true, true, true), "native4:3 UI still remains nonworld");
    View old{1714, -1, 512, 512, -0.893815f, -1.70251f}, now = old;
    now.tick = 1719;
    bool hadWorld = true;
    int gui = -1;
    for (unsigned i = 0; i < 5; ++i)
        updateAnchorContext(false, true, -1, hadWorld, gui);
    check(hadWorld && gui == -1, "five native HUD previews preserve world anchor");
    check(!reset(reasons(true, hadWorld, true, now, old, 0, 1)),
          "measured delta5 world after previews no longer spuriously resets");
    check(reset(reasons(false, hadWorld, true, now, old, 0, 1)),
          "preview/non-world packet itself remains a reset boundary");
    updateAnchorContext(false, true, 14, hadWorld, gui);
    check(!hadWorld && gui == 14, "known loading GUI preview invalidates anchor");
    check((reasons(true, hadWorld, true, now, old, 0, 1) & FirstWorld) != 0,
          "return from actual GUI still resets as first world");
    hadWorld = true;
    gui = 8;
    updateAnchorContext(false, true, -1, hadWorld, gui);
    check(!hadWorld && gui == -1, "unknown preview cannot preserve anchor across known GUI transition");
    hadWorld = true;
    gui = -1;
    updateAnchorContext(false, false, -1, hadWorld, gui);
    check(!hadWorld, "non-preview non-world invalidation is preserved");
    hadWorld = false;
    gui = -1;
    updateAnchorContext(false, true, -1, hadWorld, gui);
    check(!hadWorld, "HUD preview never creates a missing world anchor");
    updateAnchorContext(true, false, -1, hadWorld, gui);
    check(hadWorld && gui == -1, "real world establishes anchor");
    check(reset(reasons(true, false, true, now, old, 0, 1)), "mode/reset invalidation still forces first-world reset");
    now.tick = old.tick + 6;
    check(!reset(reasons(true, true, true, now, old, 0, 1)), "exact six-tick threshold unchanged");
    now.tick = old.tick + 7;
    check((reasons(true, true, true, now, old, 0, 1) & TickGap) != 0, "seven-tick gap still resets");
    now.tick = old.tick - 1;
    check((reasons(true, true, true, now, old, 0, 1) & (TickGap | BackwardsTick)) == (TickGap | BackwardsTick),
          "backwards unsigned-tick behavior preserved");
    now = old;
    check(!reset(reasons(true, true, true, now, old, 4096.0f * 4096.0f, 0.5f)),
          "exact movement/turn thresholds unchanged");
    check((reasons(true, true, true, now, old, 4096.0f * 4096.0f + 16, 1) & Jump) != 0,
          "real camera jump still resets after HUD previews");
    check((reasons(true, true, true, now, old, 0, 0.49f) & Turn) != 0, "real camera turn still resets");
    now.width = 1024;
    check((reasons(true, true, true, now, old, 0, 1) & SizeChange) != 0, "viewport size change still resets");
    now = old;
    now.projectionX += 0.001f;
    check((reasons(true, true, true, now, old, 0, 1) & ProjectionChange) != 0, "projection change still resets");
    now = old;
    now.gui = 8;
    check((reasons(true, true, true, now, old, 0, 1) & GuiChange) != 0, "actual GUI ID transition still resets");
    std::printf("PASS: %u scene-camera world-scope, HUD anchor and unchanged reset-policy checks\n", checks);
}
