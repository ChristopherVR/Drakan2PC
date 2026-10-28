#include "drakan_quick_inventory.h"
#include <cstdio>
#include <map>
struct Guest {
    std::map<unsigned, unsigned> mem;
    unsigned read32(unsigned a) { return mem[a]; }
    unsigned short read16(unsigned a) { return static_cast<unsigned short>(mem[a]); }
};
struct Call {
    unsigned addr = 0x131fa8, i[8] = {0x4e90c0, 0x403a, 14, 1, 100};
    int ni = 5;
};
int failed = 0, checks = 0, warnings = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok)
        ++failed;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
}
int main() {
    const auto log = [](const char*) { ++warnings; };
    Guest g;
    Call c;
    drakan_input::QuickInventoryBridge bridge;
    g.mem[0x4e9800] = 0x128;
    g.mem[0x4e9804] = 8;
    g.mem[c.i[0] + 8 + 2 * 0x128] = 0x403a;
    c.i[1] = 0x4006;
    check(!bridge.intercept(g, c, true, log), "ordinary attack callback is not intercepted");
    c.i[1] = 0x403a;
    check(bridge.intercept(g, c, true, log) && bridge.buttons(g, true) == 8, "Quick Inventory down becomes native R1");
    c.i[3] = 0;
    bridge.intercept(g, c, true, log);
    check(bridge.buttons(g, true) == 0, "release removes native overlay");
    c.i[3] = 1;
    bridge.intercept(g, c, true, log);
    c.i[2] = 15;
    bridge.intercept(g, c, true, log);
    c.i[2] = 14;
    c.i[3] = 0;
    bridge.intercept(g, c, true, log);
    check(bridge.buttons(g, true) == 8, "second action source retains hold");
    c.i[2] = 15;
    bridge.intercept(g, c, true, log);
    check(bridge.buttons(g, true) == 0, "last source release clears hold");
    c.i[3] = 1;
    bridge.intercept(g, c, true, log);
    check(bridge.buttons(g, false) == 0 && bridge.buttons(g, true) == 0, "lost input ownership clears without replay");
    bridge.intercept(g, c, false, log);
    check(bridge.buttons(g, true) == 0, "disabled press cannot leave a deferred attack");
    bridge.intercept(g, c, true, log);
    g.mem[c.i[0] + 8 + 2 * 0x128] = 0x4006;
    check(bridge.buttons(g, true) == 0, "scheme change cannot turn held Quick Inventory into attack");
    check(bridge.intercept(g, c, true, log) && bridge.buttons(g, true) == 0 && warnings == 1,
          "missing native binding fails closed");
    g.mem[0x4e9800] = 0x126;
    g.mem[0x4e9804] = 4;
    g.mem[c.i[0] + 8 + 2 * 0x126] = 0x403a;
    check(bridge.intercept(g, c, true, log) && bridge.buttons(g, true) == 4, "live alternative scheme resolves L1");
    std::printf("checks=%d failures=%d\n", checks, failed);
    return failed ? 1 : 0;
}
