// Standalone MenuMouse regression: compile with src/MenuMouse.cpp only.
// Synthetic guest metadata, no game code, native command calls, or GPU.
#include "input/MenuMouse.h"
#include "input/NativeInventoryFeedback.h"
#include "input/FrameInput.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <map>
#include <vector>

using namespace drakan_input;
static unsigned checks = 0, failures = 0;
static void check(bool value, const char* name) {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", name);
    }
}
struct Guest : IGuest {
    std::map<uint32_t, uint8_t> bytes;
    std::vector<uint8_t> snapshot;
    unsigned calls = 0;
    uint8_t read8(uint32_t p) override {
        auto i = bytes.find(p);
        return i != bytes.end() ? i->second : (p < snapshot.size() ? snapshot[p] : 0);
    }
    uint32_t read32(uint32_t p) override {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i)
            v |= uint32_t(read8(p + i)) << (i * 8);
        return v;
    }
    void write8(uint32_t p, uint8_t v) override { bytes[p] = v; }
    void write32(uint32_t p, uint32_t v) override {
        for (unsigned i = 0; i < 4; ++i)
            write8(p + i, uint8_t(v >> (i * 8)));
    }
    uint32_t call(const GuestCall&) override {
        ++calls;
        return 0;
    }
};
struct Fixture {
    static constexpr uint32_t screen = 0x700000, sc = 0x710000, ctx = 0x720000;
    static constexpr uint32_t cameraObject = 0x730000, camera = cameraObject + 0x14;
    static constexpr uint32_t section = 0x740000, controller = 0x750000, transform = 0x760000;
    Guest g;
    MenuMouse mouse;
    MenuView view;
    Fixture() {
        g.write32(0x4fb60c, screen);
        g.write32(0x5565c8, sc);
        g.write32(0x5565d4, ctx);
        g.write32(screen + 0x18, 21);
        g.write32(sc + 0xf8, 512);
        g.write32(sc + 0xfc, 512);
        g.write32(ctx + 0x4b4, cameraObject);
        g.writeF(sc + 0x40, -256);
        g.writeF(sc + 0x44, -256);
        g.writeF(sc + 0x48, -1);
        g.writeF(sc + 0x4c, -1);
        g.writeF(sc + 0x100, 2048);
        g.writeF(sc + 0x104, 2048);
        for (unsigned i = 0; i < 3; ++i) {
            g.writeF(camera + 0xc + i * 16, 1);
            g.writeF(camera + 0x30 + i * 4, 1);
            g.writeF(transform + 0xc + i * 16, 1);
        }
        g.writeF(transform + 8, -100);
        g.write32(section + 0x10, screen);
        g.write32(section + 0x70, controller);
        g.write32(section + 0xc, transform);
        g.writeF(section + 0x60, 10);
        g.writeF(section + 0x64, 10);
        g.writeF(section + 0x68, -40);
        g.writeF(section + 0x6c, 20);
        g.write32(controller + 0x38, 0x900000);
        g.write32(controller + 0x48, 0x900100);
        // Establish active screen before the draw callback, as in normal frames.
        mouse.update(g, sample(false), view, 0);
    }
    static MenuMouseSample sample(bool click = true) {
        MenuMouseSample s;
        s.clientW = s.clientH = 512;
        s.cx = 179;
        s.cy = 205;
        s.inside = true;
        if (click) {
            s.pressed = MBTN_LEFT;
            s.down = MBTN_LEFT;
        }
        return s;
    }
    void capture() { mouse.captureInventoryGrid(g, section); }
    uint16_t click() { return mouse.update(g, sample(), view, 0).padTap; }
    static constexpr uint32_t player = 0x770000, icons = 0x780000, nodes = 0x790000;
    void initCategories() {
        g.write32(screen + 0x14c, player);
        g.write32(screen + 0x19c, icons);
        g.write32(screen + 0x1a4, 5);
        g.writeF(screen + 0x158, 1);
        for (unsigned i = 0; i < 5; ++i) {
            const uint32_t r = icons + i * 0x24, n = nodes + i * 0x120;
            g.write32(r + 0x10, screen);
            g.write32(r + 0x1c, n);
            g.write32(n, 0x51b8b0);
            g.write32(n + 0x104, 0x2000);
            g.writeF(n + 0xb8, -80 + float(i) * 30);
            g.writeF(n + 0xbc, 70);
            g.writeF(n + 0xc0, -100);
            g.writeF(n + 0xc4, 20);
            g.writeF(n + 0xd4, 10);
        }
    }
    void captureCategories() {
        for (unsigned i = 0; i < 5; ++i)
            mouse.captureInventoryCategory(g, icons + i * 0x24);
    }
    static MenuMouseSample categorySample(unsigned i, bool click = true) {
        auto s = sample(click);
        s.cx = 256 + (-70 + float(i) * 30) * 2.56f;
        s.cy = 64;
        return s;
    }
};

static void categoryChecks() {
    {
        Fixture f;
        f.initCategories();
        f.g.write32(Fixture::screen + 0x154, 4);
        f.captureCategories();
        check(f.mouse.update(f.g, Fixture::categorySample(4, false), f.view, 0).padTap == 0,
              "moving into native category row cannot navigate Journal contents");
        auto gap = Fixture::categorySample(0, false);
        gap.cx = 115;
        check(f.mouse.update(f.g, gap, f.view, 0).padTap == 0,
              "moving to outside-icon gap cannot emit relative content navigation");
        gap.pressed = gap.down = MBTN_LEFT;
        check(f.mouse.update(f.g, gap, f.view, 0).padTap == 0,
              "outside-icon gap click remains neutral after pointer travel");
        gap.pressed = gap.down = 0;
        gap.wheel = -1;
        check(f.mouse.update(f.g, gap, f.view, 0).padTap == Pad::R1,
              "native wheel paging remains available in category row");
        gap.wheel = 0;
        gap.pressed = MBTN_RIGHT;
        check(f.mouse.update(f.g, gap, f.view, 0).padTap == Pad::SELECT,
              "native close remains available in category row");
        auto contents = Fixture::sample(false);
        contents.cx = 300;
        contents.cy = 205;
        check(f.mouse.update(f.g, contents, f.view, 0).padTap == 0,
              "pointer travel over uncaptured section contents does not navigate");
    }
    {
        Fixture f;
        f.initCategories();
        f.g.write32(Fixture::controller + 0x20, 1);
        f.capture();
        for (unsigned frame = 0; frame < 6; ++frame) {
            f.captureCategories();
            f.mouse.update(f.g, Fixture::categorySample(0, false), f.view, 0);
        }
        check(f.click() == 0 && !f.mouse.debug().clickAbsoluteGrid,
              "category-row early return does not freeze stale Equipment grid age");
    }
    // Exercise every source/destination with actual native wrap acknowledgement.
    for (unsigned count : {3u, 5u})
        for (unsigned from = 0; from < count; ++from)
            for (unsigned to = 0; to < count; ++to) {
                Fixture f;
                f.initCategories();
                f.g.write32(Fixture::player + 0x590, count == 3 ? 1u : 0u);
                f.g.write32(Fixture::screen + 0x154, from);
                f.captureCategories();
                check(f.mouse.debug().categoryCaptures == count, "only native available categories capture");
                unsigned current = from, pulses = 0;
                bool pressing = true;
                for (unsigned frame = 0; frame < 9; ++frame) {
                    f.captureCategories();
                    const auto before = f.g.bytes;
                    const auto r = f.mouse.update(f.g, Fixture::categorySample(to, pressing), f.view, 0);
                    pressing = false;
                    check(f.g.bytes == before, "category path never writes guest state");
                    check((r.padTap & ~(Pad::L1 | Pad::R1)) == 0, "category never emits activation or item selection");
                    if (r.padTap) {
                        ++pulses;
                        current = r.padTap == Pad::R1 ? (current + 1) % count : (current + count - 1) % count;
                        f.g.write32(Fixture::screen + 0x154, current); // fake native handler acknowledgement
                    }
                    if (current == to && f.mouse.debug().categoryTarget == -1)
                        break;
                }
                const unsigned forward = (to + count - from) % count;
                check(current == to, "queued native navigation reaches clicked category");
                check(pulses == std::min(forward, count - forward), "shortest native wrap path only");
                check(f.g.calls == 0, "category capture/navigation has no synchronous guest calls");
            }
    {
        Fixture f;
        f.initCategories();
        f.captureCategories();
        f.g.write32(Fixture::screen + 0x154, 1);
        auto gap = Fixture::categorySample(0);
        gap.cx = 115;
        check(f.mouse.update(f.g, gap, f.view, 0).padTap == 0,
              "gap between icon boxes never activates current Spells item");
        for (unsigned i = 0; i < 6; ++i)
            f.mouse.update(f.g, Fixture::categorySample(0, false), f.view, 0);
        check(f.mouse.update(f.g, Fixture::categorySample(0), f.view, 0).padTap == 0,
              "stale category bounds cannot fall through to Cross");
    }
    {
        Fixture f;
        f.initCategories();
        f.captureCategories();
        auto s = Fixture::categorySample(1);
        s.cx = s.cx * .75f;
        f.view.valid = true;
        f.view.w = f.view.h = 512;
        f.view.inventoryPageScaleX = .75f;
        f.view.hudScaleX = .4f;
        check(f.mouse.update(f.g, s, f.view, 0).padTap == Pad::R1,
              "category mapping uses whole-page scale independently of HUD");
        f.mouse.reset();
        f.captureCategories();
        f.view.x = 400;
        f.view.y = 100;
        f.view.w = 3000;
        f.view.h = 2000;
        s.cx = 400 + (Fixture::categorySample(1).cx / 512 * .75f) * 3000;
        s.cy = 100 + 64.0f / 512 * 2000;
        s.clientW = 3840;
        s.clientH = 2160;
        check(f.mouse.update(f.g, s, f.view, 0).padTap == Pad::R1, "category mapping works in offset 4K viewport");
    }
    {
        Fixture f;
        f.initCategories();
        f.g.write32(Fixture::player + 0x590, 1);
        f.captureCategories();
        check(f.mouse.debug().categoryCaptures == 3, "shop mode omits native hidden Map and Journal tabs");
        check(f.mouse.update(f.g, Fixture::categorySample(4), f.view, 0).padTap == 0,
              "hidden category cannot navigate");
    }
    for (unsigned bad = 0; bad < 6; ++bad) {
        Fixture f;
        f.initCategories();
        for (unsigned i = 0; i < 5; ++i) {
            const uint32_t n = Fixture::nodes + i * 0x120;
            if (bad == 0)
                f.g.write32(n + 0x104, 0);
            if (bad == 1)
                f.g.writeF(n + 0xc0, -20);
            if (bad == 2)
                f.g.writeF(n + 0xc8, .1f);
            if (bad == 3)
                f.g.writeF(n + 0xc4, 0);
        }
        if (bad == 4)
            f.g.writeF(Fixture::camera, 10);
        if (bad == 5)
            f.g.writeF(Fixture::screen + 0x15c, .5f);
        f.captureCategories();
        check(f.mouse.debug().categoryCaptures == 0,
              "uncached/near/rotated/degenerate/world-camera/animated bounds reject");
        check(f.mouse.update(f.g, Fixture::categorySample(1), f.view, 0).padTap == 0,
              "unproved category geometry never navigates");
    }
    {
        Fixture f;
        f.initCategories();
        f.captureCategories();
        check(f.mouse.update(f.g, Fixture::categorySample(2), f.view, 0).padTap == Pad::R1,
              "queue first step is native R1");
        f.mouse.reset();
        f.captureCategories();
        check(f.mouse.update(f.g, Fixture::categorySample(2, false), f.view, 0).padTap == 0,
              "reset cancels category queue");
        f.mouse.update(f.g, Fixture::categorySample(2), f.view, 0);
        auto back = Fixture::categorySample(2, false);
        back.pressed = MBTN_RIGHT;
        check(f.mouse.update(f.g, back, f.view, 0).padTap == Pad::SELECT,
              "back cancels category queue and retains native close");
    }
    {
        Fixture f;
        f.initCategories();
        f.g.writeF(Fixture::sc + 0x100, 1963);
        f.captureCategories();
        auto s = Fixture::categorySample(1);
        s.cx -= 85;
        check(f.mouse.update(f.g, s, f.view, 0).padTap == Pad::R1,
              "native GS page offset is included before hit testing");
    }
    {
        Fixture f;
        f.initCategories();
        unsigned taps = 0;
        for (unsigned i = 0; i < 27; ++i) {
            f.captureCategories();
            const auto r = f.mouse.update(f.g, Fixture::categorySample(2, i == 0), f.view, 0);
            taps += r.padTap != 0; // Native handler deliberately never acknowledges.
            check((r.padTap & ~(Pad::L1 | Pad::R1)) == 0, "native veto never turns into activation");
        }
        check(taps == 8 && f.mouse.debug().categoryTarget == -1,
              "unacknowledged category navigation has a bounded timeout");
    }
    {
        Fixture f;
        f.initCategories();
        f.captureCategories();
        f.mouse.update(f.g, Fixture::categorySample(4), f.view, 0);
        f.g.write32(Fixture::player + 0x590, 1);
        f.captureCategories();
        check(f.mouse.update(f.g, Fixture::categorySample(4, false), f.view, 0).padTap == 0 &&
                  f.mouse.debug().categoryTarget == -1,
              "native available-tab change cancels invalid queued target");
        auto wheel = Fixture::categorySample(1, false);
        wheel.wheel = -1;
        check(f.mouse.update(f.g, wheel, f.view, 0).padTap == Pad::R1,
              "wheel paging stays native after queue cancellation");
    }
}
static void savedCategoryChecks(const char* path) {
    Guest g;
    g.snapshot.resize(32u * 1024u * 1024u);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(g.snapshot.data()), std::streamsize(g.snapshot.size()));
    check(input.gcount() == std::streamsize(g.snapshot.size()), "saved inventory RAM read completely");
    if (input.gcount() != std::streamsize(g.snapshot.size()))
        return;
    const uint32_t screen = g.read32(0x4fb60c), sc = g.read32(0x5565c8), ctx = g.read32(0x5565d4);
    check(g.read32(screen + 0x18) == 21 && g.read32(screen + 0x1a4) == 5,
          "saved native inventory contains five category records");
    const uint32_t base = g.read32(screen + 0x19c), camera = g.read32(ctx + 0x4b4) + 0x14;
    MenuMouse m;
    MenuView view;
    m.update(g, Fixture::sample(false), view, 0);
    // Replay source-proven 430e90 identity + 42fd18 UI FOV40/132f60 placement
    // in a PRIVATE host overlay. The snapshot itself contains the later world
    // camera; this is an offline geometry check, not a live camera capture.
    for (unsigned i = 0; i < 3; ++i) {
        g.writeF(camera + i * 4, 0);
        g.writeF(camera + 0x30 + i * 4, 1);
        for (unsigned k = 0; k < 3; ++k)
            g.writeF(camera + 0xc + i * 4 + k * 12, i == k ? 1.0f : 0.0f);
    }
    const float focal = 1.0f / std::tan(40.0f * 0.008726646f);
    const uint32_t w = g.read32(sc + 0xf8), h = g.read32(sc + 0xfc);
    g.writeF(sc + 0x40, -float(w) * .5f);
    g.writeF(sc + 0x44, -float(h) * .5f);
    g.writeF(sc + 0x48, -focal);
    g.writeF(sc + 0x4c, -focal / .7f); // 1d8520 constant pixel aspect
    g.writeF(sc + 0x100, float(2048 + int(float(w >> 1) * -.33332998f)));
    g.writeF(sc + 0x104, 2048);
    float previousRight = -1e6f;
    for (unsigned i = 0; i < 5; ++i) {
        m.captureInventoryCategory(g, base + i * 0x24);
        check(m.debug().capturedCategory == int(i) && m.debug().categoryCaptures == i + 1,
              "actual cached category background certifies in native UI state");
        const auto& d = m.debug();
        check(d.categoryLeft > previousRight && d.categoryTop >= 0 && d.categoryBottom < float(h),
              "saved native category boxes are separate and inside framebuffer");
        previousRight = d.categoryRight;
        std::printf("saved category %u native box %.2f,%.2f -> %.2f,%.2f (offline UI-state replay)\n", i,
                    d.categoryLeft, d.categoryTop, d.categoryRight, d.categoryBottom);
    }
    check(g.calls == 0, "saved category audit executes no guest function");
}
// Section contents (Spells, Skills, shop list, Journal). The fixture camera maps a plane point (x, y) of the section
// frame to GUI (256 + 2.56x, 256 - 2.56y).
static MenuMouseSample at(float x, float y, uint8_t pressed = 0, int wheel = 0) {
    auto s = Fixture::sample(false);
    s.cx = x;
    s.cy = y;
    s.pressed = s.down = pressed;
    s.wheel = wheel;
    return s;
}
struct ContentFixture : Fixture {
    static constexpr uint32_t sections = 0x7c0000, page = 0x7d0000;
    void sectionAt(unsigned index, uint32_t vtable) {
        g.write32(screen + 0x14c, player);
        g.write32(screen + 0x154, index);
        g.writeF(screen + 0x158, 1);
        g.write32(screen + 0x1a8, sections);
        g.write32(screen + 0x1b0, 5);
        g.write32(sections + index * 4, page);
        g.write32(page + 0x10, screen);
        g.write32(page + 0x18, vtable);
        g.write32(page + 0xc, transform);
        g.write32(page + 0x70, player + 0x584);
        g.writeF(page + 0x60, 10);
        g.writeF(page + 0x64, 10);
        g.writeF(page + 0x68, -40);
        g.writeF(page + 0x6c, 20);
    }
    uint16_t tap(const MenuMouseSample& s) { return mouse.update(g, s, view, 0).padTap; }
};

static void contentChecks() {
    const uint32_t ctl = Fixture::player + 0x584;
    {
        ContentFixture f;
        f.sectionAt(1, 0x54f3a8); // Spells
        f.g.write32(ctl + 0x24, 1);
        f.g.write32(ctl + 0x4a8, 1);
        f.g.write32(Fixture::player + 0x1030, 3);
        f.g.write32(Fixture::player + 0x1028, 0x7e0000);
        f.g.write32(0x7e0000, 1);        // spell 0 known
        f.g.write32(0x7e0000 + 0x28, 1); // spell 2 known, spell 1 not
        f.mouse.captureInventorySpells(f.g, ContentFixture::page);
        check(f.mouse.debug().contentItems == 2, "spells capture only known spells");
        // Spell 2's cell centre (-20, 20).
        check(f.tap(at(204.8f, 204.8f)) == Pad::RIGHT, "hovering a spell steers native navigation");
        check(f.mouse.takeSpellSelection(f.g, ctl, 0) == 2, "spell selector receives the hovered spell");
        check(f.mouse.takeSpellSelection(f.g, ctl, 1) == -1, "the Square commit is never redirected");
        f.g.write32(ctl + 0x4b4, 2);
        f.mouse.captureInventorySpells(f.g, ContentFixture::page);
        check(f.tap(at(205, 205)) == 0, "hover selection ends at the native acknowledgement");
        f.mouse.captureInventorySpells(f.g, ContentFixture::page);
        check(f.tap(at(205, 205, MBTN_LEFT)) == Pad::SQUARE, "click on the selected spell readies it (Square)");
        f.mouse.captureInventorySpells(f.g, ContentFixture::page);
        check(f.tap(at(179.2f, 204.8f, MBTN_LEFT)) == 0, "unknown spell cell is not clickable");
        f.g.write32(ctl, 1);
        f.mouse.captureInventorySpells(f.g, ContentFixture::page);
        check(f.tap(at(205, 205, MBTN_LEFT)) == 0, "a busy controller delays the spell action");
        f.g.write32(ctl, 0);
        f.mouse.captureInventorySpells(f.g, ContentFixture::page);
        check(f.tap(at(205, 205)) == Pad::SQUARE, "the delayed action follows once the controller is idle");
        check(f.g.calls == 0, "spell path makes no guest calls");
    }
    {
        ContentFixture f;
        f.sectionAt(2, 0x54f1f0); // Skills
        f.g.write32(ContentFixture::page + 0x100, 3);
        f.g.write32(0x4fbad0, 3);
        f.g.write32(0x4fbaf4, 0x200); // rotr8(0x200) = 2 unspent points
        f.g.write32(ContentFixture::page + 0xfc, 0x7e0000);
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t row = 0x7e0000 + i * 0x1b4;
            f.g.write32(row + 0x14, Fixture::transform);
            f.g.writeF(row + 0x28, -50);
            f.g.writeF(row + 0x2c, 60 - float(i) * 30);
            f.g.writeF(row + 0x30, 10);
            f.g.writeF(row + 0x34, 40 - float(i) * 30);
            f.g.writeF(row + 0x38, 20);
        }
        f.mouse.captureInventorySkills(f.g, ContentFixture::page);
        check(f.mouse.debug().contentItems == 3, "skills capture three rows with unspent points");
        // Row 1 spans y 10..30 (GUI 179..230), x -50..60 (level number included).
        check(f.tap(at(395, 205)) == Pad::DOWN, "hovering the next skill row steps down");
        f.g.write32(ContentFixture::page + 0x58, 1);
        f.mouse.captureInventorySkills(f.g, ContentFixture::page);
        check(f.tap(at(390, 205, MBTN_LEFT)) == Pad::RIGHT, "click on a skill row spends a point");
        f.mouse.captureInventorySkills(f.g, ContentFixture::page);
        check(f.tap(at(390, 205, 0, -1)) == Pad::LEFT, "wheel down over a row takes a point back");
        f.mouse.captureInventorySkills(f.g, ContentFixture::page);
        check(f.tap(at(390, 150)) == Pad::UP, "hovering the row above steps up");
        f.g.write32(0x4fbaf4, 0);
        f.mouse.captureInventorySkills(f.g, ContentFixture::page);
        check(f.mouse.debug().contentItems == 0, "no selectable rows without points");
        check(f.tap(at(390, 205, MBTN_LEFT)) == 0, "click without points does nothing");
        f.mouse.captureInventorySkills(f.g, ContentFixture::page);
        check(f.tap(at(390, 205, 0, -1)) == Pad::R1, "wheel without points pages sections");
    }
    {
        ContentFixture f;
        f.sectionAt(2, 0x54f488); // shop list
        f.g.write32(ctl + 0xc, 1);
        f.g.write32(ctl + 0x24, 2);
        f.g.write32(ctl + 0x18, 0x7e0000);
        f.g.write32(0x7e0000 + 0x1c, 2);
        auto draw = [&] {
            f.mouse.beginInventoryShop(f.g, ContentFixture::page);
            f.mouse.captureShopItem(f.g, ContentFixture::page, 0, 1, 1, 0, 0);
            f.mouse.captureShopItem(f.g, ContentFixture::page, 1, 2, 1, 1, 0);
        };
        draw();
        check(f.mouse.debug().contentItems == 2, "shop items are captured as drawn");
        // Item 1 spans x -35..-15 (GUI 166..218).
        check(f.tap(at(212, 205)) == Pad::RIGHT, "hovering a fully drawn stock item steers navigation");
        check(f.mouse.takeShopSelection(f.g, ctl, 0) == 1, "shop selector receives the hovered item");
        check(f.mouse.takeShopSelection(f.g, ctl, 1) == -1, "a native buy is never redirected");
        f.g.write32(ctl + 0x1c, 1);
        draw();
        check(f.tap(at(212, 205, MBTN_LEFT)) == Pad::SQUARE, "click on the selected stock item buys (Square)");
        f.g.write32(0x7e0000 + 0x1c, 5); // a scrolling strip: three items are not drawn
        f.g.write32(ctl + 0x1c, 0);
        draw();
        check(f.tap(at(210, 200)) == 0, "a scrolling strip does not select on hover");
        draw();
        check(f.tap(at(210, 200, MBTN_LEFT)) == Pad::RIGHT, "a click still selects in a scrolling strip");
        f.g.write32(ctl + 0x24, 0);
        draw();
        check(f.mouse.debug().contentItems == 0 || f.tap(at(210, 200, MBTN_LEFT)) == 0,
              "shop list is inert while another pane has the focus");
    }
    {
        ContentFixture f;
        f.sectionAt(4, 0x54e288); // Journal
        f.g.writeF(Fixture::sc + 0x50, 2048 - 256);
        f.g.writeF(Fixture::sc + 0x58, 2048 - 256);
        const uint32_t rect = 0x7e0000;
        f.g.write32(rect, 100);
        f.g.write32(rect + 4, 100);
        f.g.write32(rect + 8, 300);
        f.g.write32(rect + 12, 300);
        auto draw = [&] {
            f.mouse.beginInventoryJournal(f.g, ContentFixture::page);
            f.mouse.captureJournalPage(f.g, rect);
            f.mouse.captureJournalGlyph(f.g, 1, 320, 368);
        };
        draw();
        check(f.tap(at(330, 375, MBTN_LEFT)) == Pad::RIGHT, "click on the next-region arrow");
        draw();
        check(f.tap(at(200, 200, 0, 1)) == Pad::UP, "wheel over the page scrolls");
        draw();
        check(f.tap(at(400, 450, 0, 1)) == Pad::L1, "wheel beside the page pages sections");
        draw();
        check(f.tap(at(200, 200, MBTN_LEFT)) == 0, "click on journal text does nothing");
    }
    {
        Fixture f; // Equipment inside a shop: the click sells (Circle)
        f.g.write32(Fixture::controller + 0x20, 1);
        f.g.write32(Fixture::controller + 0xc, 1);
        f.capture();
        check(f.click() == Pad::CIRCLE, "equipment click in a shop sells the item");
        f.g.write32(Fixture::controller, 1); // busy with a selection change
        f.capture();
        check(f.click() == 0, "a busy controller delays the equipment action");
        f.g.write32(Fixture::controller, 0);
        f.capture();
        check(f.mouse.update(f.g, Fixture::sample(false), f.view, 0).padTap == Pad::CIRCLE,
              "the delayed equipment action follows once the controller is idle");
    }
    {
        Fixture f;
        const struct {
            uint32_t id;
            uint16_t tap;
        } pages[] = {
            {0x10, Pad::START}, {0x1d, Pad::START}, {0x1e, Pad::START}, {0x13, Pad::CROSS}, {0x1c, Pad::CROSS}};
        for (const auto& page : pages) {
            f.g.write32(Fixture::screen + 0x18, page.id);
            check(f.click() == page.tap, "a click continues a page without widgets");
            check(f.mouse.update(f.g, Fixture::sample(false), f.view, 0).padTap == 0, "only the click continues");
        }
    }
}
int main(int argc, char** argv) {
    contentChecks();
    {
        Fixture f;
        f.g.write32(Fixture::controller + 0x20, 1);
        f.capture();
        check(f.click() == Pad::SQUARE, "fresh projected occupied cell activates observed selection");
        check(f.mouse.debug().clickAbsoluteGrid, "fresh geometry is eligible");
        f.mouse.reset();
        check(f.click() == 0, "reset must reject old geometry before next native draw");
        check(!f.mouse.debug().clickAbsoluteGrid, "reset invalidates absolute hit bounds");
        check(f.mouse.takeInventorySelection(f.g, Fixture::controller) == -1,
              "reset cannot leave native selection request");
        f.capture();
        check(f.click() == Pad::SQUARE, "new draw restores absolute clicks after reset");
        check(f.g.calls == 0, "grid selection path never invokes synchronous guest commands");
    }
    {
        Fixture f;
        f.g.write32(Fixture::controller + 0x20, 1);
        f.capture();
        auto unfocused = Fixture::sample(false);
        unfocused.focused = false;
        check(f.mouse.update(f.g, unfocused, f.view, 0).padTap == 0, "focus loss is neutral");
        check(f.click() == 0, "focus regain cannot reuse pre-focus-loss projection");
        f.capture();
        check(f.click() == Pad::SQUARE, "fresh post-focus draw re-enables click");
    }
    {
        Fixture f;
        f.g.write32(Fixture::controller + 0x20, 1);
        f.capture();
        f.g.write32(0x4fb60c, 0);
        f.mouse.update(f.g, Fixture::sample(false), f.view, 0);
        f.g.write32(0x4fb60c, Fixture::screen);
        check(f.click() == 0, "reopening same screen address requires new native draw");
        check(!f.mouse.debug().clickAbsoluteGrid, "same-address reopen rejects stale bounds");
        f.capture();
        check(f.click() == Pad::SQUARE, "same-address reopen accepts new draw");
    }
    {
        Fixture f;
        f.capture();
        check(f.click() == Pad::RIGHT, "unselected occupied cell queues ordinary navigation");
        check(f.mouse.takeInventorySelection(f.g, Fixture::controller) == 1, "fresh request reaches matching selector");
        f.mouse.reset();
        check(f.mouse.takeInventorySelection(f.g, Fixture::controller) == -1, "reset drops pending destination");
        check(f.click() == 0, "reset drops pending activation and capture together");
    }
    {
        Fixture f;
        f.g.write32(Fixture::controller + 0x20, 1);
        f.capture();
        for (unsigned i = 0; i < 5; ++i)
            f.mouse.update(f.g, Fixture::sample(false), f.view, 0);
        check(f.click() == 0, "existing four-update age bound remains enforced");
        f.capture();
        auto outside = Fixture::sample();
        outside.cx = 510;
        outside.cy = 500;
        check(f.mouse.update(f.g, outside, f.view, 0).padTap == 0, "outside grid never activates selected item");
    }
    categoryChecks();
    {
        Guest g;
        const uint32_t p = 0x790000;
        for (unsigned i = 0; i < 4; ++i)
            g.write32(p + 4 * i, ((0x40 + i) << 24) | 0x00338822);
        check(!native_feedback::selectionBorder(g, 0, p, 2), "unrelated native rect caller unchanged");
        check(!native_feedback::selectionBorder(g, 0x435f9c, p, 0x12),
              "special selected border retains native meaning");
        check(native_feedback::selectionBorder(g, 0x435f9c, p, 2), "audited ordinary selection border refined");
        for (unsigned i = 0; i < 4; ++i)
            check(g.read32(p + 4 * i) == (((0x40 + i) << 24) | 0x0070d6f4),
                  "all border vertices retain independent native alpha");
        for (unsigned i = 0; i < 9; ++i)
            g.writeF(p + 4 * i, i < 6 ? float(i) : .8f);
        check(!native_feedback::categoryTint(g, 0x42b4a0, p, false), "nonhovered category unchanged");
        check(!native_feedback::categoryTint(g, 0, p, true), "unrelated material caller unchanged");
        check(native_feedback::categoryTint(g, 0x42b4a0, p, true), "audited hovered category tinted");
        for (unsigned i = 0; i < 6; ++i)
            check(g.readF(p + 4 * i) == float(i), "hover preserves other native material fields");
        check(!native_feedback::categoryTint(g, 0x42b4a0, p, true), "unexpected native material values fail closed");
    }
    {
        Fixture f;
        f.initCategories();
        f.captureCategories();
        auto hover = Fixture::categorySample(1, false);
        check(f.mouse.update(f.g, hover, f.view, 0).padTap == 0, "category hover never changes section");
        const uint32_t record = f.g.read32(Fixture::screen + 0x19c) + 0x24;
        check(f.mouse.inventoryCategoryHovered(f.g, record), "fresh category hover authorizes native tint");
        f.g.write32(Fixture::screen + 0x154, 1);
        check(!f.mouse.inventoryCategoryHovered(f.g, record), "selected category keeps native highlight");
        f.g.write32(Fixture::screen + 0x154, 0);
        f.mouse.reset();
        check(!f.mouse.inventoryCategoryHovered(f.g, record), "focus/reset cancels category tint");
    }
    if (argc == 2)
        savedCategoryChecks(argv[1]);
    std::printf("MenuMouse inventory capture/categories: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
