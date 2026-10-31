// The engine's developer debug menu (drakan_debug_menu.h, docs/reference/debug-menu.md). Addresses are PAL canonical,
// translated through the disc profile (feature DebugMenu).
#include "drakan_debug_menu.h"
#include "drakan_config.h"
#include "drakan_region.h"
#include "input/Config.h"
#include "input/DrakanInput.h"
#include "input/GuestInterface.h"
#include "input/PadSynth.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "settings/Hud.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace drakan_debug_menu {
namespace {

using drakan_region::guestAddress;
namespace Pad = drakan_input::Pad;

// The menu object (static, built by 0x1ccd68 at start-up). +0 open flag (Open 0x1d5d88 / Close 0x1d5d98 have no
// callers left), +8 current page, page p at +0xc + 16 p: {cursor, u32* option ids, ?, count}; +0xc0 char* names[126]
// indexed by option id.
constexpr uint32_t kMenuPal = 0x4e8d48u;
constexpr uint32_t kPageCount = 11;
// (menu, option id) -> char* value text (a static buffer, or a constant string).
constexpr uint32_t kFormatValuePal = 0x1d49f0u;
// The menu's key callback (command, action id, pressed, time ms) -> the handler 0x1d4e88. Never installed by retail.
constexpr uint32_t kKeyCallbackPal = 0x131ff0u;
// PadMgr (InputDev): command callback slot at +0x410; Game_OnInputCommand while the menu is closed.
constexpr uint32_t kPadMgrPal = 0x4e90c0u, kCallbackOffset = 0x410u;
constexpr uint32_t kGameCallbackPal = 0x23ce18u;
// Pad action ids (docs/reference/input.md section 3.1) and the menu handler's DirectInput Enter.
constexpr uint32_t kActStart = 0x120u, kActCross = 0x123u, kDikEnter = 0x1cu;

uint32_t menu() {
    return guestAddress(kMenuPal);
}
uint32_t callbackSlot() {
    return guestAddress(kPadMgrPal) + kCallbackOffset;
}

// Host keys while the menu is open (DirectInput scancodes) -> the pad buttons the native handler reads. Up / Down
// option, Left / Right value +-1 (Shift +-10, Ctrl +-100), Enter / Space run (Cross, remapped to Enter by the callback
// hook), PgUp / PgDn page (Square / Circle).
struct KeyMap {
    int dik;
    uint16_t pad, shift, ctrl;
    bool repeat;
};
constexpr std::array<KeyMap, 9> kKeys{{
    {0xc8, Pad::UP, Pad::UP, Pad::UP, true},
    {0xd0, Pad::DOWN, Pad::DOWN, Pad::DOWN, true},
    {0xcb, Pad::LEFT, Pad::L1, Pad::L2, true},
    {0xcd, Pad::RIGHT, Pad::R1, Pad::R2, true},
    {0x1c, Pad::CROSS, Pad::CROSS, Pad::CROSS, false},
    {0x9c, Pad::CROSS, Pad::CROSS, Pad::CROSS, false},
    {0x39, Pad::CROSS, Pad::CROSS, Pad::CROSS, false},
    {0xc9, Pad::SQUARE, Pad::SQUARE, Pad::SQUARE, true},
    {0xd1, Pad::CIRCLE, Pad::CIRCLE, Pad::CIRCLE, true},
}};
constexpr int kDikEsc = 0x01, kDikLShift = 0x2a, kDikRShift = 0x36, kDikLCtrl = 0x1d, kDikRCtrl = 0x9d;
// A held key repeats after this delay, at this interval (a tap needs a sample without the button in between).
constexpr auto kRepeatDelay = std::chrono::milliseconds(350), kRepeatEvery = std::chrono::milliseconds(70);
// Controller buttons handed to the menu as they are (Start closes; Select and the stick clicks do nothing there).
constexpr uint16_t kPadPassThrough = Pad::UP | Pad::DOWN | Pad::LEFT | Pad::RIGHT | Pad::CROSS | Pad::SQUARE |
                                     Pad::CIRCLE | Pad::L1 | Pad::R1 | Pad::L2 | Pad::R2;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_open{false};
std::atomic<bool> g_closeRequest{false};
// EE thread
uint32_t g_savedCallback = 0;
uint16_t g_padPrev = 0;
std::chrono::steady_clock::time_point g_comboSince{};
bool g_comboHeld = false, g_comboFired = false;
std::array<std::chrono::steady_clock::time_point, kKeys.size()> g_heldSince{}, g_lastRepeat{};
PS2Runtime::RecompiledFunction g_origKey = nullptr;

std::mutex g_pageM;
Page g_page;
std::string g_toggleName = "F3", g_comboName = "L3+R3";

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
    return g.read32(menu() + 0xcu + page * 16u + off);
}

void setOpen(drakan_input::IGuest& g, drakan_input::InputModule& input, bool open) {
    const uint32_t slot = callbackSlot(), key = guestAddress(kKeyCallbackPal), game = guestAddress(kGameCallbackPal);
    if (open) {
        const uint32_t cb = g.read32(slot);
        g_savedCallback = cb == key ? game : cb;
        g.write32(slot, key);
        g.write32(menu(), 1u);
        g_padPrev = input.frame().gamepad.buttons; // held buttons (the combo) are not menu presses
    } else {
        if (g.read32(slot) == key)
            g.write32(slot, g_savedCallback ? g_savedCallback : game);
        g.write32(menu(), 0u);
        std::lock_guard<std::mutex> l(g_pageM);
        g_page = Page{};
    }
    // The game gets neutral input while the menu is open: WASD and the mouse do not move Rynn. The menu's keys reach
    // the native handler as pad taps.
    input.setGuestMuted(open);
    g_open.store(open);
    std::fprintf(stderr, "[debug-menu] %s (callback 0x%x)\n", open ? "open" : "closed", g_savedCallback);
}

// The DEBUG_MENU binding (a key or mouse button, on its press) or the controller combination ([gamepad]
// debug_menu_combo, once per hold of kDebugMenuHoldMs).
bool togglePressed(const drakan_input::InputModule& input) {
    const drakan_input::FrameInput& f = input.guestMuted() ? input.hostFrame() : input.frame();
    const uint16_t combo = input.config().debugMenuCombo;
    const bool held = combo && (f.gamepad.buttons & combo) == combo;
    const auto now = std::chrono::steady_clock::now();
    if (held && !g_comboHeld)
        g_comboSince = now;
    if (!held)
        g_comboFired = false;
    g_comboHeld = held;
    if (held && !g_comboFired && now - g_comboSince >= std::chrono::milliseconds(drakan_input::kDebugMenuHoldMs)) {
        g_comboFired = true;
        return true;
    }
    drakan_input::FrameInput edge = f;
    edge.keyDown = edge.keyPressed;
    edge.mouseDown = edge.mousePressed;
    for (const auto& b : input.config().game)
        if (b.act == drakan_input::Binding::DebugMenu && drakan_input::sourceLevel(b, edge, true))
            return true;
    return false;
}

void updateToggleName(const drakan_input::InputModule& input) {
    std::string name;
    for (const auto& b : input.config().game)
        if (b.act == drakan_input::Binding::DebugMenu && b.src == drakan_input::Binding::Key) {
            if (const char* n = drakan_input::nameOfKey(b.code))
                name = n;
            break;
        }
    const uint16_t combo = input.config().debugMenuCombo;
    std::lock_guard<std::mutex> l(g_pageM);
    g_toggleName = name;
    g_comboName = combo ? drakan_input::padComboToText(combo) : std::string();
}

// Keyboard and controller of this frame -> pad taps for the native handler; true = close the menu.
bool feedMenu(drakan_input::InputModule& input) {
    const drakan_input::FrameInput& f = input.hostFrame();
    const auto now = std::chrono::steady_clock::now();
    if (f.keyPressed[kDikEsc])
        return true;
    const bool shift = f.keyDown[kDikLShift] || f.keyDown[kDikRShift];
    const bool ctrl = f.keyDown[kDikLCtrl] || f.keyDown[kDikRCtrl];
    uint16_t taps = 0;
    for (size_t i = 0; i < kKeys.size(); ++i) {
        const KeyMap& k = kKeys[i];
        const uint16_t bit = ctrl ? k.ctrl : shift ? k.shift : k.pad;
        if (f.keyPressed[static_cast<size_t>(k.dik)]) {
            taps |= bit;
            g_heldSince[i] = g_lastRepeat[i] = now;
        } else if (k.repeat && f.keyDown[static_cast<size_t>(k.dik)] && now - g_heldSince[i] >= kRepeatDelay &&
                   now - g_lastRepeat[i] >= kRepeatEvery) {
            taps |= bit;
            g_lastRepeat[i] = now;
        }
    }
    const uint16_t pad = f.gamepad.buttons, pressed = static_cast<uint16_t>(pad & ~g_padPrev);
    g_padPrev = pad;
    if (pressed & Pad::START)
        return true;
    taps |= pressed & kPadPassThrough;
    if (taps)
        input.injectPadTap(taps, 1);
    return false;
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
    const uint32_t key = guestAddress(kKeyCallbackPal);
    g_origKey = runtime.lookupFunction(key);
    runtime.replaceFunction(key, &hookKeyCallback);
    installHudLayer();
}

void beforePadMgr(drakan_input::IGuest& g, drakan_input::InputModule& input, bool settingsOpen) {
    bool open = g_open.load();
    if (g_closeRequest.exchange(false) && open) {
        setOpen(g, input, false);
        open = false;
    }
    if (settingsOpen || !g_enabled.load()) {
        if (open)
            setOpen(g, input, false);
        return;
    }
    if (togglePressed(input)) {
        open = !open;
        if (open)
            updateToggleName(input);
        setOpen(g, input, open);
        return; // the key that opened the menu is not a menu key
    }
    if (!open)
        return;
    // The game re-registers its callbacks on some transitions: keep the menu's in the slot.
    const uint32_t slot = callbackSlot(), key = guestAddress(kKeyCallbackPal);
    if (const uint32_t cb = g.read32(slot); cb != key) {
        g_savedCallback = cb;
        g.write32(slot, key);
    }
    if (feedMenu(input))
        setOpen(g, input, false);
}

void afterPadMgr(drakan_input::IGuest& g) {
    if (!g_open.load())
        return;
    Page p;
    const uint32_t m = menu();
    const uint32_t current = std::min(g.read32(m + 8u), kPageCount - 1);
    for (uint32_t i = 0; i < kPageCount; ++i) {
        if (pageField(g, i, 0xc) == 0)
            continue;
        ++p.pages;
        if (i <= current)
            p.page = p.pages;
    }
    const uint32_t count = std::min(pageField(g, current, 0xc), 64u);
    const uint32_t ids = pageField(g, current, 4);
    const uint32_t names = g.read32(m + 0xc0u);
    const uint32_t format = guestAddress(kFormatValuePal);
    p.cursor = static_cast<int>(pageField(g, current, 0));
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t id = g.read32(ids + 4u * i);
        Row r;
        r.name = id < 126u ? guestString(g, g.read32(names + 4u * id)) : std::string();
        r.value = guestString(g, g.call(drakan_input::GuestCall(format).arg(m).arg(id)));
        p.rows.push_back(std::move(r));
    }
    // DRAKAN_DEBUG_MENU_LOG=1: one line per page / cursor move and per shown value that changed (test harness).
    static const bool log = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_MENU_LOG");
        return e && *e == '1';
    }();
    std::lock_guard<std::mutex> l(g_pageM);
    if (log) {
        if (p.page != g_page.page || p.cursor != g_page.cursor)
            std::fprintf(stderr, "[debug-menu] at page %d row %d: %s\n", p.page, p.cursor,
                         p.cursor >= 0 && p.cursor < static_cast<int>(p.rows.size())
                             ? p.rows[static_cast<size_t>(p.cursor)].name.c_str()
                             : "?");
        if (p.page == g_page.page && p.rows.size() == g_page.rows.size())
            for (size_t i = 0; i < p.rows.size(); ++i)
                if (p.rows[i].value != g_page.rows[i].value)
                    std::fprintf(stderr, "[debug-menu] value page %d row %zu %s: %s -> %s\n", p.page, i,
                                 p.rows[i].name.c_str(), g_page.rows[i].value.c_str(), p.rows[i].value.c_str());
    }
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
        Page p;
        std::string toggle;
        {
            std::lock_guard<std::mutex> l(g_pageM);
            p = g_page;
            toggle = g_toggleName;
            if (!g_comboName.empty())
                toggle += (toggle.empty() ? "hold " : " / hold ") + g_comboName;
        }
        if (p.rows.empty())
            return;
        std::vector<dsettings::DebugMenuRow> rows;
        rows.reserve(p.rows.size());
        for (const auto& r : p.rows)
            rows.push_back({r.name, r.value});
        char title[64];
        std::snprintf(title, sizeof title, "Debug menu  %d / %d", p.page, p.pages);
        const std::string help = "Up/Down option   Left/Right value (Shift x10, Ctrl x100)   Enter run   PgUp/PgDn page"
                                 "   " +
                                 (toggle.empty() ? std::string() : toggle + " / ") + "Esc / Start close";
        dsettings::drawDebugMenuPanel(title, rows, p.cursor, help, view.w, view.h, view.uiScale);
    };
    dsettings::hudLayers().set(dsettings::HudSlot::DebugMenu, std::move(layer));
}

} // namespace drakan_debug_menu
