// CPU-only checks of the production MenuMouse path. No game, saves, or GPU.
#include "input/DrakanInput.h"
#include <filesystem>
#include <fstream>
#include "input/FrameInput.h"
#include "../runtime/gs/src/hud_page.h" // read-only production forward map
#include <cstdio>
#include <vector>
using namespace drakan_input;
struct Guest : IGuest {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x2000000);
    int calls = 0, writes = 0;
    uint8_t read8(uint32_t a) override { return mem.at(a); }
    uint32_t read32(uint32_t a) override {
        return read8(a) | (uint32_t(read8(a + 1)) << 8) | (uint32_t(read8(a + 2)) << 16) |
               (uint32_t(read8(a + 3)) << 24);
    }
    void write8(uint32_t a, uint8_t v) override {
        mem.at(a) = v;
        ++writes;
    }
    void write32(uint32_t a, uint32_t v) override {
        for (int i = 0; i < 4; ++i)
            write8(a + i, uint8_t(v >> (i * 8)));
    }
    uint32_t call(const GuestCall&) override {
        ++calls;
        return 0;
    }
};
int failed = 0, count = 0;
void check(bool v, const char* name) {
    ++count;
    std::printf("%s %s\n", v ? "PASS" : "FAIL", name);
    failed += !v;
}
int main() {
    Guest g;
    MenuMouse m;
    const uint32_t scr = 0x700000, sec = 0x710000, ctl = 0x720000, tr = 0x730000, sc = 0x740000, ctx = 0x750000,
                   cam = 0x760000;
    g.write32(0x4fb60c, scr);
    g.write32(scr + 0x18, 21);
    g.write32(scr + 0x154, 0);
    g.write32(sec + 0x10, scr);
    g.write32(sec + 0x70, ctl);
    g.write32(sec + 0xc, tr);
    g.write32(0x5565c8, sc);
    g.write32(0x5565d4, ctx);
    g.write32(ctx + 0x4b4, cam - 0x14);
    g.write32(sc + 0xf8, 512);
    g.write32(sc + 0xfc, 512);
    g.write32(sc + 0x118, 512);
    g.writeF(sc + 0x100, 2048);
    g.writeF(sc + 0x104, 2048);
    g.writeF(sc + 0x40, 256);
    g.writeF(sc + 0x48, 2);
    g.writeF(sc + 0x44, 256);
    g.writeF(sc + 0x4c, 2);
    g.writeF(tr + 8, -1024);
    for (int k = 0; k < 3; ++k) {
        g.writeF(tr + 0xc + k * 16, 1);
        g.writeF(cam + 0xc + k * 16, 1);
        g.writeF(cam + 0x30 + k * 4, 1);
    }
    g.writeF(sec + 0x60, 80);
    g.writeF(sec + 0x64, 80);
    g.writeF(sec + 0x68, -350);
    g.writeF(sec + 0x6c, 200);
    g.write32(ctl + 0x38, 0x800000);
    g.write32(ctl + 0x48, 0x810000);
    g.write32(ctl + 0x38 + 9 * 16, 1);
    MenuView v;
    v.valid = true;
    v.w = 640;
    v.h = 512;
    v.hudScaleX = .6f;
    auto sample = [](float x, float y, uint8_t press = 0) {
        MenuMouseSample s;
        s.inside = true;
        s.cx = x;
        s.cy = y;
        s.clientW = 640;
        s.clientH = 512;
        s.pressed = press;
        s.down = press;
        return s;
    };
    auto run = [&](MenuMouseSample s) {
        m.captureInventoryGrid(g, sec);
        return m.update(g, s, v, 5000);
    };
    // Native PadMgr observes the new screen before its draw publishes geometry.
    // Screen-change reset intentionally rejects any geometry captured before it.
    auto first = run(sample(151.25f, 156, MBTN_LEFT));
    check(first.padTap == 0 && m.takeInventorySelection(g, ctl) == -1,
          "screen-change tick rejects pre-reset draw geometry; next draw is required");
    // 42ab78/42ac68 identity camera: grid [61,136]..[381,256], cell1 centre121,156.
    auto r = run(sample(151.25f, 156, MBTN_LEFT));
    check(m.debug().gridLeft == 61 && m.debug().gridTop == 136 && m.debug().gridRight == 381 &&
              m.debug().gridBottom == 256,
          "native camera projection reproduces 8x3 cell corners");
    check(m.debug().inventoryCell == 1 && r.padTap == Pad::RIGHT,
          "640-client click selects cell1 and queues navigation, not equip");
    check(m.takeInventorySelection(g, ctl) == 1, "native selector receives pending target");
    check(m.takeInventorySelection(g, ctl + 4) == -1, "unrelated controller cannot consume request");
    // Simulate native selector completion; this is not live gameplay evidence.
    g.write32(ctl + 0x20, 1);
    r = run(sample(151.25f, 156));
    check(r.padTap == Pad::SQUARE, "equip waits for observed native selection even after click release");
    r = run(sample(151.25f, 156));
    check(r.padTap == 0, "resting pointer cannot repeat equip");
    r = run(sample(201.25f, 156, MBTN_LEFT));
    check(r.padTap == 0 && m.takeInventorySelection(g, ctl) == -1, "empty grid click never equips previous item");
    r = run(sample(151.25f, 196, MBTN_LEFT));
    check(m.debug().inventoryCell == 1 && r.padTap == Pad::SQUARE,
          "multi-cell continuation resolves to its item origin");
    r = run(sample(101.25f, 156, MBTN_LEFT));
    check(m.takeInventorySelection(g, ctl) == 0, "second item request resolves independently of pointer travel");
    auto unfocused = sample(101.25f, 156);
    unfocused.focused = false;
    run(unfocused);
    check(m.takeInventorySelection(g, ctl) == -1, "focus loss cancels pending activation");
    run(sample(101.25f, 156, MBTN_LEFT));
    m.reset();
    check(m.takeInventorySelection(g, ctl) == -1, "overlay reset cancels pending activation");
    run(sample(101.25f, 156, MBTN_LEFT));
    g.write32(ctl + 0x38, 0x820000);
    check(m.takeInventorySelection(g, ctl) == -1, "item replacement invalidates queued request");
    r = run(sample(101.25f, 156));
    check(r.padTap == 0, "item replacement cancels activation");
    run(sample(101.25f, 156, MBTN_LEFT));
    g.write32(scr + 0x154, 1);
    check(m.takeInventorySelection(g, ctl) == -1, "page change rejects stale selection");
    g.write32(scr + 0x154, 0);
    m.reset();
    run(sample(101.25f, 156, MBTN_LEFT));
    g.write32(0x4fb60c, 0);
    check(m.takeInventorySelection(g, ctl) == -1, "screen change rejects stale selection");
    g.write32(0x4fb60c, scr);
    m.reset();
    run(sample(101.25f, 156, MBTN_LEFT));
    for (int i = 0; i < 5; ++i)
        m.update(g, sample(101.25f, 156), v, 5000);
    check(m.takeInventorySelection(g, ctl) == -1, "stale draw geometry rejects delayed selection");
    r = run(sample(151.25f, 156, MBTN_RIGHT));
    check(r.padTap == Pad::SELECT, "right click preserves native close");
    auto wheel = sample(151.25f, 156);
    wheel.wheel = -1;
    r = run(wheel);
    check(r.padTap == Pad::R1, "wheel preserves native section switching");
    int x = 0, y = 0;
    auto outside = sample(-1, 200);
    check(!MenuMouse::mapToGui(outside, v, 512, 512, &x, &y), "outside viewport cannot clamp into an edge hit");
    auto margin = sample(10, 200);
    check(!MenuMouse::mapToGui(margin, v, 512, 512, &x, &y), "outside compressed HUD cannot clamp into an edge hit");
    MenuView pillar;
    pillar.valid = true;
    pillar.x = 240;
    pillar.y = 0;
    pillar.w = 1440;
    pillar.h = 1080;
    auto large = sample(240 + 121.f / 512 * 1440, 156.f / 512 * 1080);
    large.clientW = 1920;
    large.clientH = 1080;
    m.captureInventoryGrid(g, sec);
    r = m.update(g, large, pillar, 5000);
    check(m.debug().inventoryCell == 1, "pillarboxed 1920 client maps same physical cell");
    check(!MenuMouse::mapToGui(sample(239, 200), pillar, 512, 512, &x, &y), "pillarbox margin rejected");
    MenuMouse fresh;
    r = fresh.update(g, sample(151.25f, 156, MBTN_LEFT), v, 5000);
    check(r.padTap == 0, "missing native capture fails closed without equipping old selection");
    run(sample(101.25f, 156, MBTN_LEFT));
    MenuMouseOptions disabled;
    disabled.inventoryMouse = false;
    m.setOptions(disabled);
    check(m.takeInventorySelection(g, ctl) == -1, "disabling inventory mouse rejects pending native request");
    r = run(sample(101.25f, 156, MBTN_LEFT));
    check(r.padTap == 0, "disabled inventory mouse emits no activation");
    m.setOptions(MenuMouseOptions{});
    m.reset();
    v.inventoryPageScaleX = .75f;
    // Forward renderer transform, then inverse MenuMouse transform. HUD stays independently .6.
    auto pageClientX = [](float nativeX, const MenuView& view) {
        return view.x + gsr::hud::inventoryMap(512, view.inventoryPageScaleX).x(nativeX) / 512 * view.w;
    };
    const float scaledCell1 = pageClientX(121, v);
    r = run(sample(scaledCell1, 156, MBTN_LEFT));
    check(m.debug().inventoryCell == 1, "actual .75 page scale is undone independently of HUD scale");
    const float scaledCell0 = pageClientX(81, v);
    r = run(sample(scaledCell0, 156, MBTN_LEFT));
    check(m.debug().inventoryCell == 0 && m.takeInventorySelection(g, ctl) == 0,
          "scaled page click requests correct native cell");
    r = run(sample(481, 156, MBTN_LEFT));
    check(r.padTap == 0 && m.debug().gx == -1, "outside left-anchored .75 page rejected rather than clamped");
    pillar.inventoryPageScaleX = .75f;
    large.cx = pageClientX(121, pillar);
    m.captureInventoryGrid(g, sec);
    r = m.update(g, large, pillar, 5000);
    check(m.debug().inventoryCell == 1, "page scale is inverted about viewport left edge with pillarboxing");
    pillar.x = 480;
    pillar.y = 120;
    pillar.w = 2880;
    pillar.h = 1920;
    large.clientW = 3840;
    large.clientH = 2160;
    large.cx = pageClientX(121, pillar);
    large.cy = pillar.y + 156.f / 512 * pillar.h;
    m.captureInventoryGrid(g, sec);
    r = m.update(g, large, pillar, 5000);
    check(m.debug().inventoryCell == 1, "4K client and offset viewport retain scaled page hit");
    m.reset();
    v.inventoryPageScaleX = 1;
    r = run(sample(151.25f, 156, MBTN_LEFT));
    check(m.debug().inventoryCell == 1, "identity page metadata preserves unscaled 3D grid despite HUD squeeze");
    m.reset();
    v.inventoryPageScaleX = .75f;
    // Actual native page caller sets 132f60(scale=1, offset=int(256*-.33332998)) = -85.
    g.writeF(sc + 0x100, 1963);
    m.captureInventoryGrid(g, sec);
    check(m.debug().gridLeft == -24 && m.debug().gridRight == 296,
          "native -85 inventory placement included in absolute GS grid");
    r = run(sample(pageClientX(36, v), 156, MBTN_LEFT));
    check(m.debug().inventoryCell == 1, "native viewport placement and backend page scale compose correctly");
    g.writeF(sc + 0x104, 2060);
    m.captureInventoryGrid(g, sec);
    check(m.debug().gridTop == 148 && m.debug().gridBottom == 268,
          "absolute GS Y offset applied independently of viewport height");
    // 132f60 keeps floating half-size while +118 truncates to221: don't substitute its integer half for GS centre.
    g.write32(sc + 0x118, 221);
    g.writeF(sc + 0x100, 2218);
    m.captureInventoryGrid(g, sec);
    check(m.debug().gridLeft == 231 && m.debug().gridRight == 551,
          "odd preview width uses uploaded GS centre without local-origin truncation");
    g.writeF(sc + 0x100, 2048);
    g.writeF(sc + 0x104, 2048);
    g.write32(sc + 0x118, 512);
    v.inventoryPageScaleX = 1;
    m.reset();
    g.calls = 0;
    const int writes = g.writes;
    run(sample(101.25f, 156, MBTN_LEFT));
    m.takeInventorySelection(g, ctl);
    check(g.calls == 0 && g.writes == writes,
          "mouse projection/request neither calls guest code nor writes selection memory");
    // Exercise the actual scripted initHeadless->beginFrame->afterPadMgr path,
    // not only MenuMouse samples already in the correct client coordinate space.
    const auto scriptPath = std::filesystem::temp_directory_path() /
                            ("drakan-inventory-headless-" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    {
        std::ofstream file(scriptPath);
        file << "1 cursor 56 126\n2 button down 1\n3 button up 1\n";
    }
    InputModule headless;
    headless.initHeadless(Config::defaults());
    std::string error;
    check(headless.loadScript(scriptPath.string(), &error), "headless module loads bounded real input script");
    headless.setMenuActive(true);
    headless.setMenuContext(true);
    headless.setScriptClientSize(3818, 2032);
    MenuView physical;
    physical.valid = true;
    physical.x = 103;
    physical.w = 3612;
    physical.h = 2032;
    physical.hudScaleX = .75f;
    physical.inventoryPageScaleX = .75f;
    headless.setMenuView(physical);
    g.writeF(sec + 0x68, -422);
    g.writeF(sec + 0x6c, 244);
    g.write32(ctl + 0x20, 1);
    headless.beginFrame();
    check(headless.frame().clientW == 3818 && headless.frame().clientH == 2032 && headless.frame().cursorX == 334 &&
              headless.frame().cursorY == 500,
          "initHeadless script scales virtual56,126 to actual334,500 without HostInput");
    headless.menuMouseRw().captureInventoryGrid(g, sec);
    headless.afterPadMgrUpdate(g);
    headless.beginFrame();
    headless.menuMouseRw().captureInventoryGrid(g, sec);
    headless.afterPadMgrUpdate(g);
    const auto& cd = headless.menuMouse().debug();
    check(cd.inventoryClickSerial == 1 && cd.gx == 43 && cd.gy == 125 && cd.clickRawCell == 0 && cd.clickItem == 0 &&
              cd.clickGridAge <= 4,
          "headless click diagnostics confirm physical viewport inverse and current native cell");
    check(headless.menuMouseRw().takeInventorySelection(g, ctl) == 0,
          "headless click reaches native selection request instead of missing viewport");
    headless.setScriptClientSize(3840, 2160);
    headless.beginFrame();
    check(headless.frame().cursorX == 336 && headless.frame().cursorY == 531 && headless.frame().clientW == 3840 &&
              headless.frame().mouseReleased == MBTN_LEFT,
          "script resize rescales original virtual coordinates once and preserves release");
    headless.setScriptClientSize(0, 0);
    headless.beginFrame();
    check(headless.frame().clientW == 640 && headless.frame().cursorX == 56,
          "CPU-only headless script keeps virtual client without a real window");
    check(!windowClientSize(nullptr, nullptr, nullptr),
          "null client-size query does not require Raw Input or a window");
    std::filesystem::remove(scriptPath);
    std::printf("%d checks, %d failures\n", count, failed);
    return failed ? 1 : 0;
}
