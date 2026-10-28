#include "drakan_controlled_actor.h"
#include "input/DrakanInput.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <string>
using namespace drakan_input;
struct Guest : IGuest {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x2000000);
    uint8_t read8(uint32_t a) override { return bytes.at(a & 0x1ffffff); }
    uint32_t read32(uint32_t a) override {
        return read8(a) | (uint32_t(read8(a + 1)) << 8) | (uint32_t(read8(a + 2)) << 16) |
               (uint32_t(read8(a + 3)) << 24);
    }
    void write8(uint32_t a, uint8_t v) override { bytes.at(a & 0x1ffffff) = v; }
    void write32(uint32_t a, uint32_t v) override {
        for (int i = 0; i < 4; ++i)
            write8(a + i, uint8_t(v >> (8 * i)));
    }
    uint32_t call(const GuestCall&) override { return 0; }
};
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok)
        ++failures;
}
Guest fixture() {
    Guest g;
    g.write32(0x5565d4, 0x600000);
    g.write32(0x6004b0, 0x610000);
    g.write32(0x600320, 0x620000);
    g.write32(0x620008, 0x630000);
    g.write32(0x620010, 3);
    g.write8(0x610010, 3);
    g.write32(0x630000 + 3 * 36 + 8, 0x1001c);
    g.write32(0x6100fc, 0x640000);
    g.write32(0x640010, 0x650000);
    g.write32(0x6004b4, 0x660000);
    g.write32(0x6600fc, 0x670000);
    g.write32(0x670010, 0x680000);
    g.write32(0x68014c, 1);
    g.write32(0x4fb730, 1);
    return g;
}
int main(int argc, char** argv) {
    auto g = fixture();
    auto role = drakan_controlled_actor::read(g);
    check(role.dragon && role.dragonGameplay,
          "native inclusive resource limit identifies controlled dragon without camera cache");
    g.write32(0x630000 + 3 * 36 + 8, 0x10009);
    g.write32(0x6504d8, 1);
    role = drakan_controlled_actor::read(g);
    check(!role.dragon && !role.dragonGameplay, "on-foot associated dragon is not controlled dragon");
    g = fixture();
    g.write32(0x4fbafc, 1);
    check(!drakan_controlled_actor::read(g).dragonGameplay, "scripted flow does not gain fallback gameplay");
    g = fixture();
    g.write32(0x4fb730, 0);
    check(!drakan_controlled_actor::read(g).dragonGameplay, "disabled native player control blocks fallback");
    g = fixture();
    g.write32(0x68014c, 7);
    check(!drakan_controlled_actor::read(g).dragonGameplay, "non-gameplay camera mode blocks fallback");
    g = fixture();
    g.write32(0x68014c, 6);
    check(drakan_controlled_actor::read(g).dragonGameplay, "native dragon look camera also permits gameplay");
    g = fixture();
    g.write8(0x610010, 4);
    check(!drakan_controlled_actor::read(g).dragon, "resource index above native limit rejected");
    g = fixture();
    g.write8(0x610010, 255);
    g.write8(0x610011, 255);
    check(!drakan_controlled_actor::read(g).dragon, "native invalid resource sentinel rejected");
    g = fixture();
    g.write32(0x620008, 0x1fffff0);
    check(!drakan_controlled_actor::read(g).dragon, "resource table overrun rejected");
    g = fixture();
    g.write32(0x5565d4, 0);
    check(!drakan_controlled_actor::read(g).dragonGameplay, "unloaded context clears mode");
    g = fixture();
    g.write32(0x6004b0, 0);
    check(!drakan_controlled_actor::read(g).dragon, "missing controlled actor clears mode");
    g = fixture();
    g.write32(0x6004b0, 0x80610000);
    check(drakan_controlled_actor::read(g).dragon, "native cached RAM alias accepted");
    g = fixture();
    Config cfg = Config::defaults();
    InputModule module;
    module.initHeadless(cfg);
    module.setMenuActive(false);
    g.write32(cfg.addrStickMap, 0);
    g.writeF(cfg.addrStickMap + 8, 1.f);
    FrameInput f;
    f.focused = true;
    f.captured = true;
    f.keyDown[0x39] = true;
    module.beginFrame(f);
    module.beforePadMgrUpdate(g, false);
    check(g.read32(cfg.addrStickMap) == 3, "on-foot existing strafe mapping remains active");
    role = drakan_controlled_actor::read(g);
    module.setMenuActive(!role.dragonGameplay);
    module.beforePadMgrUpdate(g, role.dragon);
    module.beginFrame(f);
    uint8_t pad[32];
    module.fillPadSample(0, 0, pad);
    check(g.read32(cfg.addrStickMap) == 0, "live dragon role restores native axis instead of on-foot strafe");
    check((unpackPadSample(pad).buttons & Pad::TRIANGLE) != 0 && !(unpackPadSample(pad).buttons & Pad::CROSS),
          "stale-look dragon uses gameplay Space/Triangle not menu Cross");
    module.setMenuActive(true);
    module.beginFrame(f);
    module.fillPadSample(0, 0, pad);
    check((unpackPadSample(pad).buttons & Pad::CROSS) != 0, "interactive menu retains existing Cross mapping");
    module.setMenuActive(false);
    module.setSuspended(true);
    module.beginFrame(f);
    module.fillPadSample(0, 0, pad);
    check(unpackPadSample(pad).buttons == 0, "settings overlay suspension neutralizes dragon gameplay input");
    module.setSuspended(false);
    f.focused = false;
    module.beginFrame(f);
    module.fillPadSample(0, 0, pad);
    check(unpackPadSample(pad).buttons == 0, "focus loss neutralizes dragon gameplay input");
    for (int i = 1; i < argc; ++i) {
        Guest actual;
        std::ifstream in(argv[i], std::ios::binary);
        in.read(reinterpret_cast<char*>(actual.bytes.data()), actual.bytes.size());
        check(in.gcount() == 0x2000000, "actual RAM file fully read");
        auto a = drakan_controlled_actor::read(actual);
        const bool mounted = std::string(argv[i]).find("ram_dragon_start") == std::string::npos;
        std::printf("RAM %s dragon=%d gameplay=%d\n", argv[i], a.dragon, a.dragonGameplay);
        check(a.dragon == mounted && a.dragonGameplay == mounted,
              "actual fixture control swap resolves current actor, independent of old camera argument");
    }
    return failures ? 1 : 0;
}
