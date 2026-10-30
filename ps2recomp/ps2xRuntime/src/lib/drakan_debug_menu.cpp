// The engine's developer debug menu (drakan_debug_menu.h, docs/reference/debug-menu.md). PAL addresses: the feature is
// enabled for the discs whose profile lists DebugMenu.
#include "drakan_debug_menu.h"
#include "drakan_config.h"
#include "input/DrakanInput.h"
#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "settings/Hud.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>

namespace drakan_debug_menu {
namespace {

// The menu object (static, built by 0x1ccd68 at start-up). +0 open flag (Open 0x1d5d88 / Close 0x1d5d98 have no
// callers left), +8 current page, page p at +0xc + 16 p: {cursor, u32* option ids, ?, count}; +0xc0 char* names[126]
// indexed by option id.
constexpr uint32_t kMenu = 0x4e8d48u;
constexpr uint32_t kPageCount = 11;
constexpr uint32_t kNames = kMenu + 0xc0u;
// (menu, option id) -> char* value text (a static buffer, or a constant string).
constexpr uint32_t kFormatValue = 0x1d49f0u;
// The menu's key callback (command, action id, pressed, time ms) -> the handler 0x1d4e88. Never installed by retail.
constexpr uint32_t kKeyCallback = 0x131ff0u;
// PadMgr (InputDev) command callback slot; Game_OnInputCommand while the menu is closed.
constexpr uint32_t kCallbackSlot = 0x4e90c0u + 0x410u;
constexpr uint32_t kGameCallback = 0x23ce18u;
// Pad action ids (docs/reference/input.md section 3.1) and the menu handler's DirectInput Enter.
constexpr uint32_t kActStart = 0x120u, kActCross = 0x123u, kDikEnter = 0x1cu;
// DirectInput scancodes of the host keys (FrameInput::keyPressed).
constexpr int kDikF3 = 0x3d, kDikPageUp = 0xc9, kDikPageDown = 0xd1;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_open{false};
std::atomic<bool> g_closeRequest{false};
uint32_t g_savedCallback = 0; // EE thread
PS2Runtime::RecompiledFunction g_origKey = nullptr;

std::mutex g_pageM;
Page g_page;

std::string guestString(drakan_input::IGuest& g, uint32_t addr, size_t max = 96) {
    std::string s;
    if (addr == 0 || addr >= PS2_RAM_SIZE)
        return s;
    for (size_t i = 0; i < max; ++i) {
        const char ch = static_cast<char>(g.read8(addr + static_cast<uint32_t>(i)));
        if (!ch)
            break;
        s.push_back(ch);
    }
    // Names are padded with spaces to a column ("Misc        ").
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

uint32_t pageField(drakan_input::IGuest& g, uint32_t page, uint32_t off) {
    return g.read32(kMenu + 0xcu + page * 16u + off);
}

void setOpen(drakan_input::IGuest& g, bool open) {
    if (open) {
        const uint32_t cb = g.read32(kCallbackSlot);
        g_savedCallback = cb == kKeyCallback ? kGameCallback : cb;
        g.write32(kCallbackSlot, kKeyCallback);
        g.write32(kMenu, 1u);
    } else {
        if (g.read32(kCallbackSlot) == kKeyCallback)
            g.write32(kCallbackSlot, g_savedCallback ? g_savedCallback : kGameCallback);
        g.write32(kMenu, 0u);
        std::lock_guard<std::mutex> l(g_pageM);
        g_page = Page{};
    }
    g_open.store(open);
    std::fprintf(stderr, "[debug-menu] %s (callback 0x%x)\n", open ? "open" : "closed", g_savedCallback);
}

// Cross is the confirm button of the port's bindings (Enter = CROSS); the handler only knows DirectInput Enter for
// "run this option". Start (Esc) closes the menu.
void hookKeyCallback(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t action = getRegU32(c, 5), pressed = getRegU32(c, 6);
    if (action == kActStart) {
        if (pressed)
            g_closeRequest.store(true);
        SET_GPR_U32(c, 2, 1u);
        c->pc = getRegU32(c, 31);
        return;
    }
    if (action == kActCross)
        SET_GPR_U32(c, 5, kDikEnter);
    g_origKey(rdram, c, rt);
}

} // namespace

void configure() {
    g_enabled.store(drakan_cfg::getBool("general", "debug_menu", "DRAKAN_DEBUG_MENU", false));
}
void setEnabled(bool on) {
    g_enabled.store(on);
    if (!on)
        g_closeRequest.store(true);
}
bool enabled() {
    return g_enabled.load();
}
bool isOpen() {
    return g_open.load();
}

void install(PS2Runtime& runtime) {
    configure();
    g_origKey = runtime.lookupFunction(kKeyCallback);
    runtime.replaceFunction(kKeyCallback, &hookKeyCallback);
    installHudLayer();
}

void beforePadMgr(drakan_input::IGuest& g, drakan_input::InputModule& input, bool settingsOpen) {
    const auto& keys = input.frame().keyPressed;
    bool open = g_open.load();
    if (g_closeRequest.exchange(false) && open) {
        setOpen(g, false);
        open = false;
    }
    if (settingsOpen || !g_enabled.load()) {
        if (open)
            setOpen(g, false);
        return;
    }
    if (keys[kDikF3]) {
        setOpen(g, !open);
        open = !open;
    }
    if (!open)
        return;
    // The game re-registers its callbacks on some transitions: keep the menu's in the slot.
    if (const uint32_t cb = g.read32(kCallbackSlot); cb != kKeyCallback) {
        g_savedCallback = cb;
        g.write32(kCallbackSlot, kKeyCallback);
    }
    // Pages: Square / Circle on the controller (the handler's 0x124 / 0x125); PgUp / PgDn on the keyboard.
    if (keys[kDikPageUp])
        input.injectPadTap(drakan_input::Pad::SQUARE);
    if (keys[kDikPageDown])
        input.injectPadTap(drakan_input::Pad::CIRCLE);
}

void afterPadMgr(drakan_input::IGuest& g) {
    if (!g_open.load())
        return;
    Page p;
    const uint32_t current = std::min(g.read32(kMenu + 8u), kPageCount - 1);
    for (uint32_t i = 0; i < kPageCount; ++i) {
        if (pageField(g, i, 0xc) == 0)
            continue;
        ++p.pages;
        if (i <= current)
            p.page = p.pages;
    }
    const uint32_t count = std::min(pageField(g, current, 0xc), 64u);
    const uint32_t ids = pageField(g, current, 4);
    const uint32_t names = g.read32(kNames);
    p.cursor = static_cast<int>(pageField(g, current, 0));
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t id = g.read32(ids + 4u * i);
        Row r;
        r.name = id < 126u ? guestString(g, g.read32(names + 4u * id)) : std::string();
        r.value = guestString(g, g.call(drakan_input::GuestCall(kFormatValue).arg(kMenu).arg(id)));
        p.rows.push_back(std::move(r));
    }
    std::lock_guard<std::mutex> l(g_pageM);
    g_page = std::move(p);
}

Page snapshot() {
    std::lock_guard<std::mutex> l(g_pageM);
    return g_page;
}

void installHudLayer() {
    dsettings::HudLayer layer;
    layer.active = [] { return g_open.load(); };
    layer.draw = [](const dsettings::HudView& view) {
        const Page p = snapshot();
        if (p.rows.empty())
            return;
        std::vector<dsettings::DebugMenuRow> rows;
        rows.reserve(p.rows.size());
        for (const auto& r : p.rows)
            rows.push_back({r.name, r.value});
        char title[64];
        std::snprintf(title, sizeof title, "Debug menu  %d / %d", p.page, p.pages);
        dsettings::drawDebugMenuPanel(title, rows, p.cursor,
                                      "Up/Down option   Left/Right value   Enter run   PgUp/PgDn page   F3 close",
                                      view.w, view.h, view.uiScale);
    };
    dsettings::hudLayers().set(dsettings::HudSlot::DebugMenu, std::move(layer));
}

} // namespace drakan_debug_menu
