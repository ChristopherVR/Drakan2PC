// Order-of-the-Flame style inventory views: bindings, ini migration and the menu mouse in a centred view.
// Standalone synthetic guest; no private data, game code or graphics.
#include "input/Config.h"
#include "input/FrameInput.h"
#include "input/MenuMouse.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
using namespace drakan_input;
static unsigned checks;
static void check(bool b, const char* name) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
static int viewOf(const Config& c, int dik) {
    int view = 0;
    for (const auto& b : c.game)
        if (b.src == Binding::Key && b.code == dik && b.act == Binding::View)
            view = int(b.value);
    return view;
}
static unsigned bindingsOf(const Config& c, int dik) {
    return unsigned(std::count_if(c.game.begin(), c.game.end(),
                                  [&](const Binding& b) { return b.src == Binding::Key && b.code == dik; }));
}
static Config parsed(const std::string& text) {
    Config c = Config::defaults();
    std::string warn;
    Config::parse(text, c, &warn);
    check(warn.empty(), "test ini parses without warnings");
    return c;
}

struct Guest final : IGuest {
    std::map<uint32_t, uint8_t> mem;
    uint8_t read8(uint32_t p) override {
        auto i = mem.find(p);
        return i == mem.end() ? 0 : i->second;
    }
    uint32_t read32(uint32_t p) override {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i)
            v |= uint32_t(read8(p + i)) << (i * 8);
        return v;
    }
    void write8(uint32_t p, uint8_t v) override { mem[p] = v; }
    void write32(uint32_t p, uint32_t v) override {
        for (unsigned i = 0; i < 4; ++i)
            write8(p + i, uint8_t(v >> (i * 8)));
    }
    uint32_t call(const GuestCall&) override { return 0; }
};

// Map section with one marker drawn at native x = markerX, 20 x 20 pixels.
struct MapFixture {
    static constexpr uint32_t scr = 0x700000, section = 0x710000, sections = 0x720000;
    static constexpr uint32_t table = 0x730000, renderer = 0x740000, image = 0x750000, vertices = 0x760000;
    Guest g;
    MenuMouse mouse;
    MenuView view;
    explicit MapFixture(int inventoryView) {
        g.write32(0x4fb60c, scr);
        g.write32(0x5565c8, renderer);
        g.write32(renderer + 0xf8, 512);
        g.write32(renderer + 0xfc, 512);
        g.write32(scr + 0x18, 21);
        g.write32(scr + 0x154, 3);
        g.write32(scr + 0x1a8, sections);
        g.write32(scr + 0x1b0, 5);
        g.writeF(scr + 0x158, 1);
        g.write32(sections + 12, section);
        g.write32(section + 0x10, scr);
        g.write32(section + 0x70, 7);
        g.write32(0x4fbb70, table);
        g.write32(0x4fbb78, 4);
        g.write32(image + 0x20, 1);
        g.write32(image + 0x18, 0x51cfe8);
        g.write32(image + 0x24, 16);
        g.write32(image + 0x28, 16);
        for (unsigned i = 0; i < 4; ++i) {
            g.write32(table + i * 32 + 0x10, 100 + i);
            g.write32(table + i * 32 + 0x14, 7);
        }
        mouse.setMapStateQuery([](IGuest&, uint32_t, int& value) {
            value = 0;
            return true;
        });
        // 16:9 output: the renderer maps the page with x' = 0.75 x from the left edge.
        view.valid = true;
        view.w = 512;
        view.h = 512;
        view.inventoryPageScaleX = 0.75f;
        mouse.setInventoryView(inventoryView);
        mouse.update(g, sample(false, 0, 0), view, 0);
    }
    static MenuMouseSample sample(bool click, float x, float y) {
        MenuMouseSample s;
        s.inside = true;
        s.clientW = s.clientH = 512;
        s.cx = x;
        s.cy = y;
        if (click) {
            s.pressed = MBTN_LEFT;
            s.down = MBTN_LEFT;
        }
        return s;
    }
    void draw(float markerX, float markerY) {
        mouse.beginInventoryMap(g, section);
        g.writeF(vertices, 1792 + markerX);
        g.writeF(vertices + 4, 1792 + markerY);
        g.writeF(vertices + 32, 1792 + markerX + 20);
        g.writeF(vertices + 36, 1792 + markerY + 20);
        mouse.captureInventoryMapMarker(g, section, 2, image, vertices, 2);
    }
    uint16_t click(float clientX, float clientY) {
        return mouse.update(g, sample(true, clientX, clientY), view, 0).padTap;
    }
};

int main() {
    {
        const auto saves = [](const Config& c, int dik, uint32_t action) {
            return std::count_if(c.game.begin(), c.game.end(), [&](const Binding& b) {
                return b.src == Binding::Key && b.code == dik && b.act == Binding::Save && b.value == action;
            });
        };
        check(saves(Config::defaults(), 0x3f, kQuickSave) == 1, "default quicksave F5");
        check(saves(parsed("[bindings]\nW = LSTICK_UP\n"), 0x42, kQuickLoad) == 1,
              "older configuration gains quickload F8");
        const auto custom = parsed("[bindings]\nQ = QUICKSAVE\nL = QUICKLOAD\nF5 = NONE\nF8 = CROSS\n");
        check(saves(custom, 0x10, kQuickSave) == 1 && saves(custom, 0x26, kQuickLoad) == 1,
              "Order of the Flame shortcuts can be configured");
        check(bindingsOf(custom, 0x3f) == 0 && bindingsOf(custom, 0x42) == 1,
              "quick save migration preserves NONE and custom keys");
    }
    constexpr int kM = 0x32, kI = 0x17;
    {
        const Config d = Config::defaults();
        check(viewOf(d, kM) == int(kViewMap) && bindingsOf(d, kM) == 1, "default M = VIEW_MAP");
        check(d.mapView, "map view on by default");
        bool tab = false;
        for (const auto& b : d.game)
            tab =
                tab || (b.src == Binding::Key && b.code == 0x0F && b.act == Binding::PadBtn && b.value == Pad::SELECT);
        check(tab, "Tab stays SELECT (the normal inventory screen)");
        check(viewOf(d, kI) == int(kViewStrip) && bindingsOf(d, kI) == 1, "default I = VIEW_STRIP");
        check(d.inventoryStrip, "strip view on by default");
    }
    {
        // A file written before the view: its [bindings] replace the defaults and have no M.
        const Config c = parsed("[bindings]\nW = LSTICK_UP\nI = ACT:0x17\n");
        check(viewOf(c, kM) == int(kViewMap) && bindingsOf(c, kM) == 1, "older file gets M = VIEW_MAP");
        check(bindingsOf(c, kI) == 1 && viewOf(c, kI) == int(kViewStrip), "old default I = ACT:0x17 becomes the strip");
    }
    {
        const Config c = parsed("[bindings]\nI = CROSS\n");
        check(viewOf(c, kI) == 0 && bindingsOf(c, kI) == 1, "a user's own I binding is kept");
    }
    {
        const Config c = parsed("[bindings]\nI = ACT:0x17 | CROSS\n");
        check(viewOf(c, kI) == 0 && bindingsOf(c, kI) == 2, "a combined I binding is the user's and stays");
    }
    {
        const Config c = parsed("[bindings]\nI = ACT:0x17\nF5 = VIEW_STRIP\n");
        check(viewOf(c, kI) == 0, "VIEW_STRIP on another key: I keeps the keymap action");
    }
    {
        const Config c = parsed("[menu]\ninventory_strip = 0\n");
        check(!c.inventoryStrip && viewOf(c, kI) == int(kViewStrip), "inventory_strip = 0 keeps the key");
    }
    {
        const Config c = parsed("[bindings]\nM = NONE\n");
        check(bindingsOf(c, kM) == 0, "M = NONE stays unbound");
    }
    {
        const Config c = parsed("[bindings]\nM = CROSS\n");
        check(bindingsOf(c, kM) == 1 && viewOf(c, kM) == 0, "a user's own M binding is kept");
    }
    {
        const Config c = parsed("[bindings]\nF4 = VIEW_MAP\n");
        check(bindingsOf(c, kM) == 0, "VIEW_MAP on another key: M is not added");
    }
    {
        const Config c = parsed("[menu]\nmap_view = 0\n");
        check(!c.mapView && viewOf(c, kM) == int(kViewMap), "map_view = 0 keeps the key, drops the layout");
    }
    {
        // Native page: left-anchored, a marker at native 100..120 is at client 0.75 x.
        MapFixture f(0);
        f.draw(100, 200);
        check(f.click(110 * 0.75f, 205) == Pad::RIGHT, "no view: left-anchored page");
    }
    {
        // Map view: the renderer centres the page, x' = 256 + (x - 256) * 0.75.
        MapFixture f(1);
        f.draw(100, 200);
        check(f.click(110 * 0.75f, 205) == 0, "map view: the left-anchored position is empty");
        f.draw(100, 200);
        check(f.click(256 + (110 - 256) * 0.75f, 205) == Pad::RIGHT, "map view: marker hit on the centred page");
        f.draw(400, 200);
        check(f.click(256 + (410 - 256) * 0.75f, 205) == Pad::RIGHT, "map view: right half reachable");
        auto s = MapFixture::sample(false, 300, 300);
        s.wheel = 1;
        check(f.mouse.update(f.g, s, f.view, 0).padTap == 0, "map view: the wheel does not switch sections");
        auto r = MapFixture::sample(false, 300, 300);
        r.pressed = MBTN_RIGHT;
        check(f.mouse.update(f.g, r, f.view, 0).padTap == Pad::SELECT, "map view: right click closes (SELECT)");
    }
    std::printf("inventory views: %u checks passed\n", checks);
    return 0;
}
