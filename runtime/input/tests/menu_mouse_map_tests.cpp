// Standalone synthetic guest tests; no private data, game code or graphics.
#include "input/MenuMouse.h"
#include "input/FrameInput.h"
#include <cstdio>
#include <cstdlib>
#include <map>
using namespace drakan_input;
static unsigned checks;
static void check(bool b, const char* name) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
struct Guest final : IGuest {
    std::map<uint32_t, uint8_t> mem;
    unsigned calls = 0;
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
    uint32_t call(const GuestCall&) override {
        ++calls;
        return 0;
    }
};
struct Fixture {
    static constexpr uint32_t scr = 0x700000, section = 0x710000, sections = 0x720000;
    static constexpr uint32_t table = 0x730000, renderer = 0x740000, image = 0x750000, vertices = 0x760000;
    Guest g;
    MenuMouse mouse;
    MenuView view;
    int state = 0;
    bool complete = true;
    unsigned queries = 0;
    Fixture(unsigned count = 20) {
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
        g.write32(0x4fbb78, count);
        g.write32(image + 0x20, 1);
        g.write32(image + 0x18, 0x51cfe8);
        g.write32(image + 0x24, 16);
        g.write32(image + 0x28, 16);
        for (unsigned i = 0; i < count; ++i) {
            g.write32(table + i * 32 + 0x10, 100 + i);
            g.write32(table + i * 32 + 0x14, 7);
        }
        mouse.setMapStateQuery([this](IGuest&, uint32_t record, int& value) {
            ++queries;
            check(record >= table && record < table + g.read32(0x4fbb78) * 32, "bounded record query");
            value = state;
            return complete;
        });
        mouse.update(g, sample(false), view, 0);
    }
    static MenuMouseSample sample(bool click = true, float x = 110, float y = 210) {
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
    void begin() { mouse.beginInventoryMap(g, section); }
    void marker(unsigned index = 15, float x = 100, float y = 200) {
        g.writeF(vertices, 1792 + x);
        g.writeF(vertices + 4, 1792 + y);
        g.writeF(vertices + 32, 1792 + x + 20);
        g.writeF(vertices + 36, 1792 + y + 20);
        mouse.captureInventoryMapMarker(g, section, index, image, vertices, 2);
    }
    uint16_t update(MenuMouseSample s = sample()) { return mouse.update(g, s, view, 0).padTap; }
    void fresh(unsigned index = 15) {
        begin();
        marker(index);
    }
};
int main() {
    {
        Fixture f;
        f.fresh();
        check(f.update() == Pad::RIGHT, "marker click requests native arrow, never Cross");
        check(f.g.read32(Fixture::section + 0x6c) == 0 && f.g.calls == 0, "no direct selector write or guest action");
        f.g.write32(Fixture::section + 0x6c, 15);
        f.fresh();
        check(f.update(Fixture::sample(false)) == 0, "native selection acknowledges without activation");
    }
    {
        Fixture f;
        f.fresh();
        check(f.update(Fixture::sample(true, 50, 50)) == 0, "empty space ignores click");
        check(f.queries == 0, "empty space makes no native query");
    }
    {
        Fixture f;
        f.fresh();
        auto s = Fixture::sample();
        s.inside = false;
        check(f.update(s) == 0, "outside click ignored");
    }
    {
        Fixture f;
        f.begin();
        f.marker(15);
        f.marker(16);
        check(f.update() == 0, "overlapping markers ambiguous");
        check(f.queries == 0, "ambiguous hit makes no native query");
    }
    {
        Fixture f;
        f.fresh();
        f.marker(15);
        check(f.update() == 0, "duplicate primitive cannot enlarge click area");
    }
    {
        Fixture f;
        f.fresh();
        f.state = 1;
        check(f.update() == 0, "marker hidden after capture denied by current native state");
        check(f.queries == 1, "one state query per update");
    }
    {
        Fixture f;
        f.fresh();
        f.state = 2;
        check(f.update() == Pad::RIGHT, "native state2 selectable");
    }
    {
        Fixture f;
        f.fresh();
        f.complete = false;
        check(f.update() == 0, "incomplete native query fails closed");
    }
    {
        Fixture f;
        f.fresh();
        f.mouse.setMapStateQuery({});
        check(f.update() == 0, "missing native query fails closed");
    }
    {
        Fixture f;
        f.fresh();
        f.update(Fixture::sample(false, 50, 50));
        f.update(Fixture::sample(false, 50, 50));
        check(f.update() == 0, "draw older than two input updates is stale");
    }
    {
        Fixture f;
        f.fresh();
        f.g.write32(Fixture::table + 15 * 32 + 0x10, 999);
        check(f.update() == 0, "changed record identity denied");
    }
    {
        Fixture f;
        f.fresh();
        f.g.write32(Fixture::image + 0x24, 99);
        check(f.update() == 0, "reused image with changed dimensions denied");
    }
    {
        Fixture f;
        f.fresh();
        f.g.write32(Fixture::section + 0x70, 8);
        check(f.update() == 0, "changed region invalidates bounds");
    }
    {
        Fixture f;
        f.fresh();
        f.mouse.reset();
        check(f.update() == 0, "focus/reset invalidates captures");
    }
    {
        Fixture f;
        f.fresh();
        f.update();
        f.state = 1;
        f.fresh();
        check(f.update(Fixture::sample(false)) == 0 && f.mouse.debug().mapTarget == -1,
              "hidden pending target cancelled");
    }
    {
        Fixture f;
        f.fresh();
        f.update();
        f.fresh();
        check(f.update(Fixture::sample(false, 111, 210)) == 0 && f.mouse.debug().mapTarget == -1,
              "pointer movement cancels pending navigation");
    }
    {
        Fixture f;
        f.fresh();
        f.update();
        for (unsigned i = 0; i < 15; ++i) {
            f.fresh();
            f.update(Fixture::sample(false));
        }
        check(f.mouse.debug().mapTarget == -1, "stationary unacknowledged request times out");
    }
    {
        Fixture f;
        f.fresh();
        auto taps = f.update();
        unsigned steps = 0;
        for (unsigned tick = 0; tick < 65 && f.g.read32(Fixture::section + 0x6c) != 15; ++tick) {
            if (taps & Pad::RIGHT) {
                ++steps;
                f.g.write32(Fixture::section + 0x6c, f.g.read32(Fixture::section + 0x6c) + 1);
            }
            f.fresh();
            taps = f.update(Fixture::sample(false));
        }
        check(steps == 15 && f.g.read32(Fixture::section + 0x6c) == 15,
              "far target exceeds old eight-step limit with native acknowledgement");
    }
    {
        Fixture f;
        f.fresh();
        f.view = {80, 40, 1024, 1024, 0.5f, 1, true, .375f};
        auto s = Fixture::sample();
        s.clientW = 1280;
        s.clientH = 1200;
        s.cx = 80 + 110 * 2 * .375f;
        s.cy = 40 + 210 * 2;
        check(f.update(s) == Pad::RIGHT, "page transform and offset viewport applied once");
    }
    for (unsigned section : {1u, 2u, 4u}) {
        Fixture f;
        f.g.write32(Fixture::scr + 0x154, section);
        check(f.update() == 0, "uncertified category click never activates selected item");
        check(f.update(Fixture::sample(true, 450, 450)) == 0, "uncertified click does not leak relative navigation");
        auto s = Fixture::sample(false);
        s.wheel = 1;
        check(f.update(s) == Pad::L1, "wheel category travel retained");
        s.wheel = 0;
        s.pressed = MBTN_RIGHT;
        check(f.update(s) == Pad::SELECT, "right click close retained");
    }
    std::printf("Map mouse: %u synthetic CPU checks PASS\n", checks);
}
