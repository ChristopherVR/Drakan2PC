// SCES-500.06 native Controls screen typography. No guest calls or allocation.
#pragma once
#include "ps2_runtime.h"
#include <cstring>
namespace drakan_controls_menu {
inline PS2Runtime::RecompiledFunction original = nullptr, originalText = nullptr;
inline bool span(uint32_t p, uint32_t n) {
    return p >= 0x100000u && p < PS2_RAM_SIZE && n <= PS2_RAM_SIZE - p;
}
inline uint32_t word(const uint8_t* ram, uint32_t p) {
    uint32_t v;
    std::memcpy(&v, ram + p, 4);
    return v;
}
inline void layout(uint8_t* ram, uint32_t screen) {
    if (!span(screen, 0xa0) || word(ram, screen + 0x18) != 10)
        return;
    const auto gui = word(ram, 0x5565c8);
    if (!span(gui, 0x100))
        return;
    const auto width = word(ram, gui + 0xf8);
    if (width < 320 || width > 4096)
        return;
    auto widget = word(ram, screen + 0x7c);
    for (unsigned count = 0; widget && count < 32; ++count) {
        if (!span(widget, 0x58))
            break;
        uint16_t id;
        std::memcpy(&id, ram + widget + 0x14, 2);
        if (id == 0x2363 || id == 0x2364) {
            const int16_t x = static_cast<int16_t>(width * (id == 0x2363 ? 0.59f : 0.80f));
            const int16_t w = static_cast<int16_t>(width * 0.20f);
            std::memcpy(ram + widget + 0x1c, &x, 2);
            std::memcpy(ram + widget + 0x20, &w, 2);
        }
        widget = word(ram, widget + 4);
    }
}
inline void hook(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x441728u)
        layout(ram, getRegU32(c, 4));
    original(ram, c, rt);
}
inline void textHook(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const auto widget = getRegU32(c, 4);
    uint16_t id = 0;
    if (c->pc == 0x2c8728u && span(widget, 0x80))
        std::memcpy(&id, ram + widget + 0x14, 2);
    const auto parent = (id == 0x2363 || id == 0x2364 || id == 0x238c) ? word(ram, widget + 8) : 0u;
    const auto screen = span(parent, 0x1c) ? word(ram, parent + 0x18) : ~0u;
    const bool controls = screen == 10 && (id == 0x2363 || id == 0x2364);
    const bool messageOk = screen == 1 && id == 0x238c;
    if (!controls && !messageOk) {
        originalText(ram, c, rt);
        return;
    }
    // The retail focus effect rasterizes the name into a small intermediate
    // texture before adding fire. Draw these short radio labels directly from
    // the HD font instead; the checked box retains the selected-state cue.
    // The sole message-box OK button has the same low-resolution intermediate
    // and clips the right stroke of K. Its native hitbox and focus stay intact.
    const auto flags = word(ram, widget + 0x10);
    const auto normal = flags & ~0x40u;
    std::memcpy(ram + widget + 0x10, &normal, 4);
    originalText(ram, c, rt);
    const auto restored = word(ram, widget + 0x10) | (flags & 0x40u);
    std::memcpy(ram + widget + 0x10, &restored, 4);
}
inline void install(PS2Runtime& rt) {
    original = rt.lookupFunction(0x441728u);
    if (original)
        rt.replaceFunction(0x441728u, &hook);
    originalText = rt.lookupFunction(0x2c8728u);
    if (originalText)
        rt.replaceFunction(0x2c8728u, &textHook);
}
} // namespace drakan_controls_menu
