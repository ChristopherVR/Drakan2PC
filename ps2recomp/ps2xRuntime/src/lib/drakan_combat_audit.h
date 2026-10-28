// Optional SCES_500.06 combat input audit. No guest writes or forced actions.
// DRAKAN_COMBAT_AUDIT=N: log input edges, plus an idle sample every N frames.
#pragma once
#include <cstdlib>
#include <cstdint>

namespace drakan_combat_audit {
inline bool ram(uint32_t address, uint32_t bytes) {
    const uint32_t physical = address & 0x1fffffffu;
    return physical >= 0x100000u && physical < 0x2000000u && bytes <= 0x2000000u - physical;
}

template <class Guest, class Module, class Log>
void sample(Guest& g, Module& module, Log log, int frame, int screen, bool dragon) {
    static const int every = [] {
        const char* e = std::getenv("DRAKAN_COMBAT_AUDIT");
        return e ? std::atoi(e) : 0;
    }();
    if (every <= 0)
        return;
    static uint16_t lastButtons = 0;
    static bool lastFocused = true, lastSuspended = false;
    const auto& f = module.frame();
    uint8_t bytes[32] = {};
    if (!module.fillPadSample(0, 0, bytes))
        return;
    const uint16_t buttons = static_cast<uint16_t>(~((bytes[2] << 8) | bytes[3]));
    const bool edge = buttons != lastButtons || f.keyPressed.any() || f.keyReleased.any() || f.mousePressed ||
                      f.mouseReleased || f.wheel || lastFocused != f.focused || lastSuspended != module.suspended();
    lastButtons = buttons;
    lastFocused = f.focused;
    lastSuspended = module.suspended();
    if (!edge && frame % every != 0)
        return;
    const uint32_t ctx = g.read32(0x5565d4);
    const uint32_t handle = ram(ctx, 0x4b4) ? g.read32(ctx + 0x4b0) : 0;
    const uint32_t inner = ram(handle, 0x100) ? g.read32(handle + 0xfc) : 0;
    const uint32_t player = ram(inner, 0x14) ? g.read32(inner + 0x10) : 0;
    const bool valid = ram(player, 0x1044);
    auto rd = [&](uint32_t offset) { return valid ? g.read32(player + offset) : 0u; };
    const uint32_t vt = rd(0x57c);
    const uint32_t attackFn = ram(vt, 0x168) ? g.read32(vt + 0x164) : 0;
    // These fields are observations, not proof of hits, a parry, or animation completion.
    log("[combat-audit] f=%d screen=%d dragon=%d focus=%d capture=%d suspended=%d mouse=%02x press=%02x release=%02x "
        "wheel=%d pad=%04x sticks=%u,%u,%u,%u player=%08x valid=%d flags90=%08x flags94=%08x move=%08x,%08x "
        "attackFn=%08x itemEAC=%04x itemED0=%04x state1040=%u\n",
        frame, screen, dragon ? 1 : 0, f.focused ? 1 : 0, module.captured() ? 1 : 0, module.suspended() ? 1 : 0,
        f.mouseDown, f.mousePressed, f.mouseReleased, f.wheel, buttons, bytes[4], bytes[5], bytes[6], bytes[7], player,
        valid ? 1 : 0, rd(0x90), rd(0x94), rd(0x504), rd(0x510), attackFn, valid ? g.read16(player + 0xeac) : 0,
        valid ? g.read16(player + 0xed0) : 0, rd(0x1040));
}
} // namespace drakan_combat_audit
