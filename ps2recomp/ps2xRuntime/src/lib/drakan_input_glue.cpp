// Drakan: mouse + keyboard (+XInput) input. Glue between the standalone module decomp/runtime/input (namespace
// drakan_input) and the recompiled game:
//   * libpad (scePadRead & co, linked into the game) is replaced: the game never touches padman, the 32-byte DualShock
//     sample is synthesised from the host input,
//   * after-hooks on PadMgr_Update (0x1e3970) and Camera_UpdateLook (0x44e4a8) deliver direct mouse look and the
//     ACT:/CMD: command bindings / menu cursor.
//
// Environment:
//   DRAKAN_INPUT=0            keep the stock PS2Recomp scePad stubs (keyboard Z/X/C/V ... + raylib gamepad)
//   DRAKAN_INPUT_INI=<path>   configuration file (default: drakan_input.ini next to the exe, created if missing)
//   DRAKAN_INPUT_SCRIPT=<f>   test mode: host input comes from a script (see runtime/input/include/input/Script.h)
//   DRAKAN_INPUT_MENU=0|1     force gameplay (0) or menu (1) mode instead of the automatic detection
//   DRAKAN_INPUT_LOG=<n>      log the player/camera state every n frames (and the first hook calls)
//   DRAKAN_AUTOPRESS=<ms>, DRAKAN_AUTOPRESS_UNTIL=<ms>   tap Start+Cross for 150 ms every period (headless menu
//   navigation)
#if defined(PS2X_HAVE_DRAKAN_INPUT) || defined(PS2X_HAVE_PORTABLE_UI)
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include "input/DrakanInput.h"
#include "drakan_settings_host.h"
#include "drakan_dual_wield.h"
#include "drakan_menus.h"
#include "drakan_pause.h"
#include "drakan_config.h"
#include "drakan_window.h"
#if defined(__EMSCRIPTEN__) && defined(PS2X_HAVE_DRAKAN_INPUT)
#include "drakan_input_web.h"
#elif !defined(_WIN32) && defined(PS2X_HAVE_DRAKAN_INPUT)
#include "drakan_input_glfw.h"
#endif
#include "drakan_combat_audit.h"
#include "drakan_parry_audit.h"
#include "drakan_quick_inventory.h"
#include "drakan_inventory_views.h"
#include "drakan_inventory_views_policy.h"
#include "drakan_tab_screen.h"
#include "drakan_tab_preview_policy.h"
#include "drakan_save_previews.h"
#include "drakan_region.h"
#include "drakan_input_flow.h"
#include "drakan_widescreen.h"
#include "gameui/Addresses.h"
#include "gameui/Reader.h"
#include "input/NativeInventoryFeedback.h"
#include "input/QuickSlots.h"
#include "drakan_present_clock.h"
#include "drakan_scene_camera.h"
#include <gsr/gsr.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

// raylib (declared here to keep <windows.h> and raylib.h apart)
extern "C" void SetExitKey(int key);
extern "C" void SetWindowPosition(int x, int y);
extern "C" void* GetWindowHandle(void);
// The D3D11 backend runs in a native window (drakan_window); raylib's window only exists for the software GS reference
// path.
static void* hostHwnd() {
#if defined(_WIN32)
    return drakan_win::handle() ? drakan_win::handle() : GetWindowHandle();
#elif defined(PS2X_HAVE_DRAKAN_INPUT)
    return drakan_win::handle();
#else
    return nullptr;
#endif
}
void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

extern std::atomic<bool> g_drakanMovieSeen; // drakan_overrides.cpp

namespace {
using namespace drakan_input;

constexpr uint32_t kPadInit = 0x4b0948u, kPadPortOpen = 0x4b0ba8u, kPadRead = 0x4b0ef8u, kPadGetState = 0x4b0f78u,
                   kPadSetMainMode = 0x4b1490u, kPadSetActDirect = 0x4b1548u, kPadMgrUpdate = 0x1e3970u,
                   kCameraUpdateLook = 0x44e4a8u, kCameraFollowTick = 0x41f848u, kBowLaunch = 0x20e6c8u;

PS2Runtime::RecompiledFunction g_origPadMgrUpdate = nullptr, g_origCameraLook = nullptr;
PS2Runtime::RecompiledFunction g_origCameraFollow = nullptr, g_origBowLaunch = nullptr;
PS2Runtime::RecompiledFunction g_parryOriginal[4] = {};
using ParryReduction = drakan_parry_audit::PendingReduction;
ParryReduction g_parryPending[64] = {};
template <int N>
void hookParry(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    using namespace drakan_parry_audit;
    const Ram r{rdram};
    const auto p = getRegU32(c, 4), arg = getRegU32(c, 5), attacker = getRegU32(c, 6);
    const auto ra = getRegU32(c, 31), stack = getRegU32(c, 29), id = ++serial;
    const int thread = rt->eeScheduler().currentThreadId();
    ParryReduction* pending = thread >= 0 && thread < 64 ? &g_parryPending[thread] : nullptr;
    if constexpr (N == 3) {
        // 0x314ab0 may yield and resume through many aliases. Correlate at
        // its leaf HP setter instead of treating a C++ return as completion.
        // Native 0x314ec8: stats=a0, newHP=a1, s1=damage, s3=Rynn, s5=&damage.
        const ParryReduction saved = pending ? *pending : ParryReduction{};
        const int hpBefore = r.hp();
        const bool match = hpUpdateMatches(r, saved, ra, p, int32_t(arg), getRegU32(c, 19), stack,
                                           int32_t(getRegU32(c, 17)), getRegU32(c, 21));
        if (pending)
            *pending = {};
        if (match)
            state(r, "hp-enter", saved.player, saved.id, saved.id, saved.attacker, saved.incoming, saved.result,
                  hpBefore);
        g_parryOriginal[N](rdram, c, rt);
        if (match)
            state(r, "hp-exit", saved.player, saved.id, saved.id, saved.attacker, saved.incoming, saved.result,
                  hpBefore, c->pc == ra);
        return;
    }
    const int hpBefore = r.hp();
    const int incoming = int32_t(arg);
    if constexpr (N == 2) {
        if (pending)
            *pending = {};
        state(r, "reduce-enter", p, id, id, attacker, incoming, 0, hpBefore);
    }
    g_parryOriginal[N](rdram, c, rt);
    const bool complete = c->pc == ra;
    if constexpr (N == 2) {
        const int result = int32_t(getRegU32(c, 2));
        state(r, "reduce-exit", p, id, id, attacker, incoming, result, hpBefore, complete);
        if (pending && complete && r.resource(r.u(p + 0x80)) == 0x10009)
            *pending = {id, p, attacker, stack, incoming, result};
    } else
        state(r, N == 0 ? "begin" : "tick", p, id, 0, 0, 0, 0, hpBefore, complete);
}
void installParryAudit(PS2Runtime& rt) {
    const char* env = std::getenv("DRAKAN_PARRY_AUDIT");
    if (!env || std::strcmp(env, "1") != 0)
        return;
    constexpr uint32_t addresses[] = {0x3016e8u, 0x303ff8u, 0x314578u, 0x3d5710u};
    PS2Runtime::RecompiledFunction hooks[] = {hookParry<0>, hookParry<1>, hookParry<2>, hookParry<3>};
    for (int i = 0; i < 4; ++i) {
        g_parryOriginal[i] = rt.lookupFunction(addresses[i]);
        if (!g_parryOriginal[i])
            return;
    }
    drakan_parry_audit::enabled = true;
    for (int i = 0; i < 4; ++i)
        rt.replaceFunction(addresses[i], hooks[i]);
    std::fprintf(stderr, "[parry] {\"kind\":\"installed\",\"version\":2,\"max_rows\":10000}\n");
}
std::atomic<uint32_t> g_player{0};
std::atomic<uint32_t> g_limits{0};
std::atomic<int> g_lookFrame{-100}; // frame number of the last Camera_UpdateLook call
std::atomic<int> g_lookCalls{0};
int g_menuForce = -1; // -1 auto, 0 gameplay, 1 menu
int g_logEvery = 0;
int g_hookLog = 0;
long g_autoPressMs = 0, g_autoPressUntil = 0;
std::chrono::steady_clock::time_point g_t0;
std::FILE* g_lf = nullptr; // DRAKAN_INPUT_LOGFILE=<path>: our log lines go there (flushed) instead of stderr
int g_lastScreen = -2;
bool g_noScreenLayer = false; // DRAKAN_INPUT_SCREEN_LAYER=0: do not switch to the menu layer when a GUI screen is open
// GUI screen manager (0x4fb600, docs/reference/settings-menu.md / docs/journal/2026-10-17/crash-listener.md): active
// screen object at +0xc, its screen id at +0x18 (1,2,3,0x1b = message / yes-no boxes, 8 title, 0x17 pause menu, 9
// options ...). -1 = no active screen.
int activeScreenId(const uint8_t* rdram) {
    return drakan_input_flow::Reader({rdram, PS2_RAM_SIZE}).screen();
}
bool isMenuScreen(int id) {
    return drakan_input_flow::inMask(id, drakan_region::build().inputFlow.cursorScreens);
}
std::atomic<bool> g_dragon{false}; // live context actor, refreshed at every PadMgr update
void ilog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_lf ? g_lf : stderr, fmt, ap);
    va_end(ap);
    if (g_lf)
        std::fflush(g_lf);
}
bool g_debugKeys = false;
int g_skipBits = 0;
bool g_autoStart = false; // DRAKAN_INPUT_AUTOSTART=1: test aid, taps Cross/Start until the player is under control
int g_tapPad = 0;         // pad bits (active high, DualShock mask) tapped for the current frame
int g_playRun = 0;
bool g_steady = false;
bool g_skipMemcard = true;
bool g_wasMenu = true;
QuickInventoryBridge g_quickInventory;
QuickSlotDriver g_quickSlots; // QUICK1..QUICK9 keys through the native Quick Inventory bar (input/QuickSlots.h)
double nowS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
}

std::atomic<int> g_postsPending{0}; // RecompGuest::post() calls that have not returned yet
constexpr int kPauseScreenId = static_cast<int>(drakan_region::build().inputFlow.pauseScreen);

// Guest access on top of rdram; call() runs a recompiled function to completion on a copy of the current context.
class RecompGuest final : public IGuest {
public:
    RecompGuest(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) : m_rdram(rdram), m_ctx(ctx), m_rt(rt) {}
    void requestQuit() {
#if defined(PS2X_HAVE_DRAKAN_INPUT)
        drakan_win::requestClose();
#else
        m_rt->requestStop();
#endif
    }
    uint8_t read8(uint32_t a) override { return m_rdram[a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)]; }
    uint32_t read32(uint32_t a) override {
        uint32_t v;
        std::memcpy(&v, m_rdram + (a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)), 4);
        return v;
    }
    void write8(uint32_t a, uint8_t v) override { m_rdram[a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)] = v; }
    void write32(uint32_t a, uint32_t v) override {
        std::memcpy(m_rdram + (a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)), &v, 4);
    }
    uint32_t call(const GuestCall& c) override {
        const auto& input = module();
        if (drakan_region::supports(drakan_region::Feature::AdvancedInput) &&
            g_quickInventory.intercept(*this, c, !input.menuActive() && input.frame().focused && !input.suspended(),
                                       ilog))
            return 1;
        if (c.addr == 0 || !m_rt->hasFunction(c.addr))
            return 0;
        const bool trace = c.addr == 0x131fa8u && std::getenv("DRAKAN_COMBAT_CALL_TRACE");
        if (trace)
            ilog("[combat-call] enter target=%x cmd=%x action=%x pressed=%u sp=%x\n", c.addr, c.i[1], c.i[2], c.i[3],
                 getRegU32(m_ctx, 29));
        // R5900Context is large: keep it off the stack. The recompiled function expects pc == its own entry (otherwise
        // it treats pc as a resume point) and returns by storing $ra into pc; $ra = 0 is the "finished" marker.
        auto tmp = std::make_unique<R5900Context>(*m_ctx);
        for (int i = 0; i < c.ni; ++i)
            SET_GPR_U32(tmp.get(), 4 + i, c.i[i]);
        for (int i = 0; i < c.nf; ++i)
            tmp->f[12 + i] = c.f[i];
        SET_GPR_U32(tmp.get(), 31, 0);
        tmp->pc = c.addr;
        tmp->in_delay_slot = false;
        ps2xNoPreemptEnter(); // no scheduler preemption inside the nested call (it would unwind with a resume pc)
        for (int guard = 0; guard < 64 && tmp->pc != 0u; ++guard) {
            if (!m_rt->hasFunction(tmp->pc))
                break;
            PS2Runtime::RecompiledFunction fn = m_rt->lookupFunction(tmp->pc);
            if (trace)
                ilog("[combat-call] step=%d pc=%x ra=%x sp=%x\n", guard, tmp->pc, getRegU32(tmp.get(), 31),
                     getRegU32(tmp.get(), 29));
            fn(m_rdram, tmp.get(), m_rt);
            if (trace)
                ilog("[combat-call] step=%d returned pc=%x ra=%x sp=%x\n", guard, tmp->pc, getRegU32(tmp.get(), 31),
                     getRegU32(tmp.get(), 29));
        }
        ps2xNoPreemptLeave();
        if (trace)
            ilog("[combat-call] leave pc=%x result=%u sp=%x\n", tmp->pc, getRegU32(tmp.get(), 2),
                 getRegU32(tmp.get(), 29));
        return getRegU32(tmp.get(), 2);
    }

    void post(const GuestCall& c) override {
        if (c.addr == 0 || !m_rt->hasFunction(c.addr))
            return;
        // Runs on a scheduler-owned dispatcher thread at the priority of the game's RPC servers: it may block on a
        // semaphore (sound resource loads) without abandoning a nested hook call.
        constexpr int kPostPriority = 6;
        ++g_postsPending;
        m_rt->eeScheduler().postGuestCall(
            c.addr, c.i[0], c.i[1], c.i[2], c.i[3], [](uint32_t) { --g_postsPending; }, kPostPriority);
    }
    bool postBusy() const override { return g_postsPending.load() > 0; }

    bool mapMarkerState(uint32_t record, int& state) {
        if (!m_rt->hasFunction(0x3937c8u))
            return false;
        auto tmp = std::make_unique<R5900Context>(*m_ctx);
        SET_GPR_U32(tmp.get(), 4, record);
        SET_GPR_U32(tmp.get(), 31, 0);
        tmp->pc = 0x3937c8u;
        tmp->in_delay_slot = false;
        ps2xNoPreemptEnter();
        for (unsigned guard = 0; guard < 16 && tmp->pc; ++guard) {
            if (!m_rt->hasFunction(tmp->pc))
                break;
            m_rt->lookupFunction(tmp->pc)(m_rdram, tmp.get(), m_rt);
        }
        ps2xNoPreemptLeave();
        if (tmp->pc)
            return false;
        state = static_cast<int32_t>(getRegU32(tmp.get(), 2));
        return state == 0 || state == 2;
    }

private:
    uint8_t* m_rdram;
    R5900Context* m_ctx;
    PS2Runtime* m_rt;
};

// Modern Tab screen (drakan_tab_screen.inl), used by the view hooks and the native selector hooks.
void tabBegin(const uint8_t* ram, uint32_t screen, drakan_tab::HostKind kind);
void tabEnd(const char* why);
void tabNoteMarker(int quest, float x, float y);
void tabPublishMarkers();
void tabTick(RecompGuest& g);
void tabFrame(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t screen);
float tabWorldCrop(RecompGuest& g, R5900Context* c);
void tabPlaceGrid(RecompGuest& g, uint32_t section);
void tabRestoreGrid(RecompGuest& g, uint32_t section = 0);
void tabCaptureGrid(RecompGuest& g, uint32_t section);
int tabSteerCell(RecompGuest& g, uint32_t ctl);
int tabSteerSpell();
bool tabMapDrawing();
drakan_views::MapLayout tabMapLayout(float x, float y, float w, float h, unsigned frameW, unsigned frameH);
// The modern screen's placement of the native grid for this Equipment draw (drakan_tab_screen.inl tabPlaceGrid).
struct TabGridPlace {
    uint32_t section = 0;
    float cw = 0, ch = 0, x0 = 0, y0 = 0;
    bool moved = false;
    bool hidden = false; // no target (another host page): the grid is not drawn
    drakan_views::Rect target;
};
TabGridPlace g_tabGrid;

#include "drakan_map_mouse_hooks.inl"
#include "drakan_inventory_content_hooks.inl"
#include "drakan_inventory_views.inl"
#include "drakan_tab_screen.inl"
int g_quickOperation = 0; // 1 save, 2 load; EE thread, native card transaction in progress
#include "drakan_pause_screen.inl"
#include "drakan_menu_screens.inl"
#include "drakan_quick_save.inl"

inline void ret(R5900Context* c, int32_t v) {
    SET_GPR_S32(c, 2, v);
    c->pc = getRegU32(c, 31);
}

// Capture the inventory plane while native draw has installed its UI camera.
// Selection runs through the game's ordinary selector, including its detail/preview updates.
PS2Runtime::RecompiledFunction g_origInventoryGrid = nullptr, g_origInventorySelect = nullptr;
PS2Runtime::RecompiledFunction g_origInventoryCategory = nullptr;
PS2Runtime::RecompiledFunction g_origInventoryBorder = nullptr, g_origInventoryMaterial = nullptr;
bool inventoryFeedbackAllowed(uint8_t* ram) {
    return activeScreenId(ram) == 0x15 && module().frame().focused && !module().suspended();
}
void hookInventoryBorder(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x2c8ff8 && getRegU32(c, 31) == 0x435f9c && inventoryFeedbackAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        native_feedback::selectionBorder(g, getRegU32(c, 31), getRegU32(c, 8), getRegU32(c, 19));
    }
    g_origInventoryBorder(ram, c, rt);
}
// 42a4b0(section, object, matrix, 0, 0) draws one inventory mesh. Inside the item draw 434e80 it also draws the
// quick-slot marker (the object at section+0x80, a white-blue corner quad), which ended up inside the host views' icon
// crops. While a host view draws the grid that one call is skipped: nothing is written to guest memory, so nothing can
// outlive an interrupted draw (the earlier pointer swap could, commit 8715191). The host draws its own quick-slot mark.
bool quickSlotMarkerDraw(uint8_t* ram, R5900Context* c) {
    const uint32_t ra = getRegU32(c, 31), section = getRegU32(c, 4) & 0x1fffffffu, obj = getRegU32(c, 5);
    if (ra < 0x434e80u || ra >= 0x435b00u || !obj || section < 0x100000u || section >= PS2_RAM_SIZE - 0x100u)
        return false;
    uint32_t marker = 0;
    std::memcpy(&marker, ram + section + 0x80, 4);
    return marker == obj && invHostDrawing();
}
void hookInventoryMaterial(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x42a4b0 && quickSlotMarkerDraw(ram, c)) {
        static unsigned skips = 0;
        if (skips++ < 3)
            ilog("[tab-screen] quick-slot marker draw skipped (host view, ra %x)\n", getRegU32(c, 31));
        c->pc = getRegU32(c, 31);
        return;
    }
    if (c->pc == 0x42a4b0 && getRegU32(c, 31) == 0x42b4a0 && inventoryFeedbackAllowed(ram)) {
        RecompGuest g(ram, c, rt);
        native_feedback::categoryTint(g, getRegU32(c, 31), getRegU32(c, 6),
                                      module().menuMouse().inventoryCategoryHovered(g, getRegU32(c, 4)));
    }
    g_origInventoryMaterial(ram, c, rt);
}
void hookInventoryCategory(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x42b308u) {
        RecompGuest g(rdram, c, rt);
        auto& mouse = module().menuMouseRw();
        const auto before = mouse.debug().categoryCaptures;
        mouse.captureInventoryCategory(g, getRegU32(c, 4));
        const auto& d = mouse.debug();
        if (d.categoryCaptures != before && (d.categoryCaptures <= 5 || d.categoryCaptures % 150 == 0))
            ilog("[in] inventory category=%d box=%.2f,%.2f..%.2f,%.2f capture=%u frame=%d\n", d.capturedCategory,
                 d.categoryLeft, d.categoryTop, d.categoryRight, d.categoryBottom, d.categoryCaptures,
                 module().frameNo());
    }
    g_origInventoryCategory(rdram, c, rt);
}
void hookInventoryGrid(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc != 0x437170u) {
        g_origInventoryGrid(rdram, c, rt);
        return;
    }
    RecompGuest g(rdram, c, rt);
    auto& mouse = module().menuMouseRw();
    mouse.captureInventoryGrid(g, getRegU32(c, 4));
    tabCaptureGrid(g, getRegU32(c, 4));
    const auto& d = mouse.debug();
    if (d.inventoryCaptures > 0 && (d.inventoryCaptures <= 3 || d.inventoryCaptures % 30 == 0))
        ilog("[in] inventory projected grid %.2f,%.2f -> %.2f,%.2f (GUI units, capture=%u frame=%d screen=%d)\n",
             d.gridLeft, d.gridTop, d.gridRight, d.gridBottom, d.inventoryCaptures, module().frameNo(),
             activeScreenId(rdram));
    g_origInventoryGrid(rdram, c, rt);
}
void hookInventorySelect(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    RecompGuest g(rdram, c, rt);
    auto& m = module();
    if (c->pc == 0x33eaf8u) {
        const int target = tabSteerCell(g, getRegU32(c, 4));
        if (target >= 0) {
            ilog("[tab-screen] native selection old=%u target=%d\n", g.read32(getRegU32(c, 4) + 0x20), target);
            SET_GPR_U32(c, 5, static_cast<uint32_t>(target));
            g_origInventorySelect(rdram, c, rt);
            return;
        }
    }
    if (c->pc == 0x33eaf8u && m.menuActive() && m.frame().focused && !m.suspended()) {
        const int target = m.menuMouseRw().takeInventorySelection(g, getRegU32(c, 4));
        if (target >= 0) {
            ilog("[in] inventory mouse native selection controller=%x old=%u target=%d\n", getRegU32(c, 4),
                 g.read32(getRegU32(c, 4) + 0x20), target);
            SET_GPR_U32(c, 5, static_cast<uint32_t>(target));
        }
    }
    g_origInventorySelect(rdram, c, rt);
}

void hookPadInit(uint8_t*, R5900Context* c, PS2Runtime*) {
    ret(c, 1);
}
void hookPadPortOpen(uint8_t*, R5900Context* c, PS2Runtime*) {
    ret(c, 1);
}
void hookPadGetState(uint8_t*, R5900Context* c, PS2Runtime*) {
    ret(c, 6);
}
void hookPadSetMainMode(uint8_t*, R5900Context* c, PS2Runtime*) {
    ret(c, 1);
}
void hookPadSetActDirect(uint8_t*, R5900Context* c, PS2Runtime*) {
    ret(c, 1);
}

void hookPadRead(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t port = getRegU32(c, 4), slot = getRegU32(c, 5), buf = getRegU32(c, 6);
    uint8_t s[32];
    if (!fillPadSample(static_cast<int>(port), static_cast<int>(slot), s)) {
        ret(c, 0);
        return;
    }
    if (port == 0 && g_skipMemcard && !g_drakanMovieSeen.load()) {
        // memory card warning screen ("Press X to continue"): tap Cross until the game starts the logo movie
        const double t = nowS();
        if (t > 2.0 && t < 120.0 && std::fmod(t, 0.4) < 0.12)
            s[3] &= static_cast<uint8_t>(~(0x40u));
        static bool said = false;
        if (!said && t > 2.0) {
            said = true;
            ilog("[input] skipping the memory card warning: tapping Cross until the game starts the logo movie\n");
        }
    }
    if (port == 0 && g_tapPad) {
        s[2] &= static_cast<uint8_t>(~((g_tapPad >> 8) & 0xff));
        s[3] &= static_cast<uint8_t>(~(g_tapPad & 0xff));
    }
    if (port == 0) {
        RecompGuest guest(rdram, c, rt);
        const auto& input = module();
        const uint16_t bits =
            g_quickInventory.buttons(guest, !input.menuActive() && input.frame().focused && !input.suspended());
        s[2] &= static_cast<uint8_t>(~(bits >> 8));
        s[3] &= static_cast<uint8_t>(~bits);
        // Quick-slot keys: the scheme's Quick Inventory / Attack buttons, first released then pressed (active low).
        const QuickSlotDriver::Overlay quick = g_quickSlots.overlay();
        s[2] |= static_cast<uint8_t>(quick.up >> 8);
        s[3] |= static_cast<uint8_t>(quick.up);
        s[2] &= static_cast<uint8_t>(~(quick.down >> 8));
        s[3] &= static_cast<uint8_t>(~quick.down);
        static uint16_t last = 0;
        if (bits != last && std::getenv("DRAKAN_COMBAT_CALL_TRACE"))
            ilog("[quick-inventory] frame=%d native pad overlay %04x -> %04x\n", input.frameNo(), last, bits);
        last = bits;
    }
    // With DRAKAN_INPUT_AUTOSTART the taps only serve menus and cut scenes: in free gameplay Start would open the pause
    // menu (and Cross jump), which keeps resetting the "under control" count in levels that run below ~60 FPS.
    if (port == 0 && g_autoPressMs > 0 && !(g_autoStart && (g_steady || g_playRun > 0))) {
        const long t = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_t0).count());
        if (t > 3000 && (g_autoPressUntil <= 0 || t < g_autoPressUntil) && (t % g_autoPressMs) < 150) {
            if (!std::getenv("DRAKAN_AUTOPRESS_NOSTART"))
                s[2] &= static_cast<uint8_t>(~(0x08u)); // START (0x800 active low -> bit 3 of the high byte)
            s[3] &= static_cast<uint8_t>(~(0x40u));     // CROSS 0x40
        }
    }
    std::memcpy(rdram + (buf & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)), s, 32);
    ret(c, 32);
}

void dumpState(RecompGuest& g, const char* label) {
    const uint32_t player = g_player.load();
    ilog("[in-dump %s] frame=%d player=0x%x\n", label, module().frameNo(), player);
    if (!player)
        return;
    const uint32_t heading = g.read32(player + 0x80);
    ilog("  player+0x00:");
    for (int i = 0; i < 64; ++i) {
        if (i % 8 == 0 && i)
            ilog("\n  player+0x%02x:", i * 4);
        ilog(" %08x", g.read32(player + i * 4));
    }
    ilog("\n  heading=0x%x", heading);
    if (heading)
        for (int i = 0; i < 48; ++i) {
            if (i % 8 == 0)
                ilog("\n  h+0x%02x:", i * 4);
            ilog(" %8.4f", g.readF(heading + i * 4));
        }
    {
        const uint32_t o = 0x4fb6d0; // options object (docs/reference/input.md 3.2)
        ilog("  options: invertY(+0x64)=%u look-sens(+0x78)=%.3f scheme(+0x2e8)=%u stickCfg(+0x7c..)= %d %d %d %d\n",
             g.read32(o + 0x64), g.readF(o + 0x78), g.read32(o + 0x2e8), static_cast<int>(g.read32(o + 0x7c)),
             static_cast<int>(g.read32(o + 0x80)), static_cast<int>(g.read32(o + 0x84)),
             static_cast<int>(g.read32(o + 0x88)));
    }
    const uint32_t ctx = g.read32(0x5565d4);
    const uint32_t camH = ctx ? g.read32(ctx + 0x4b4) : 0;
    const uint32_t inner = camH ? g.read32(camH + 0xfc) : 0;
    const uint32_t cam = inner ? g.read32(inner + 0x10) : 0;
    ilog("\n  cam=0x%x", cam);
    if (cam)
        ilog(" pitch84=%.4f yaw90=%.4f 94=%.4f 98=%.4f rate9c=%.4f a0=%.4f a4=%.4f flags48=%08x", g.readF(cam + 0x84),
             g.readF(cam + 0x90), g.readF(cam + 0x94), g.readF(cam + 0x98), g.readF(cam + 0x9c), g.readF(cam + 0xa0),
             g.readF(cam + 0xa4), g.read32(cam + 0x48));
    ilog("\n  turnUpVec(0x4e8c48)= %08x %08x %08x %08x\n", g.read32(0x4e8c48), g.read32(0x4e8c4c), g.read32(0x4e8c50),
         g.read32(0x4e8c54));
}

// QUICK1..QUICK9 (default keys 1..5): the Nth item of the native quick inventory, used / equipped through the bar
// (input/QuickSlots.h). EE thread, before the game samples the pad; the overlay is applied in hookPadRead.
void quickSlotsTick(RecompGuest& g, int scr, bool gameplay) {
    InputModule& m = module();
    const FrameInput& f = m.frame();
    int slot = 0;
    for (const Binding& b : m.config().game) {
        if (b.act != Binding::Quick)
            continue;
        if ((b.src == Binding::Key && f.keyPressed[size_t(b.code) & 255]) ||
            (b.src == Binding::Mouse && (f.mousePressed & b.code)))
            slot = int(b.value);
    }
    const bool allowed = gameplay && scr < 0 && !m.suspended() && f.focused && !g_dragon.load() && !invHostView() &&
                         g_invViewRequest == drakan_views::View::None;
    std::string log;
    g_quickSlots.tick(g, slot, allowed, m.config().addrPadMgr, &log);
    if (slot && !allowed)
        log += "[quick-slot] slot " + std::to_string(slot) + ": not in free gameplay\n";
    if (!log.empty())
        ilog("[in] frame %d %s", m.frameNo(), log.c_str());
}

bool inputGameplay(RecompGuest& guest, uint8_t* ram, int upcoming) {
    const drakan_input_flow::Reader flow({ram, PS2_RAM_SIZE});
    // A mode-1/6 camera also exists during the opening scripted walk. On foot,
    // native look callbacks prove control; mounted controllers need not call it.
    return flow.gameplay() &&
           ((g_lookFrame.load() > 0 && upcoming - g_lookFrame.load() <= 3) || module().controlledDragon(guest));
}

void hookPadMgrUpdate(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    // Settings overlay open: hold the game at this frame boundary (nothing is mid-frame). The host thread keeps
    // presenting the last image plus the overlay; the frame that was about to start runs normally after the menu
    // closes.
    if (drakan_pause::holdRequested()) {
        while (drakan_pause::holdRequested() && !rt->isStopRequested())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // A feedback sound of the host-drawn title / pause / Options / Save / Load screens: played as a guest invocation of
    // this thread (tabPlaySound); the thread then re-enters this hook from its entry, with the request taken.
    if (const uint32_t sound = drakan_menu_screens::shared().uiSound.exchange(0))
        tabPlaySound(rdram, c, rt, sound); // returns only when nothing was queued
    if (g_skipBits & 4) {
        static int n = 0;
        if (++n % 100 == 0)
            ilog("[in-bare] t=%.2f frames=%d\n", nowS(), n);
        g_origPadMgrUpdate(rdram, c, rt);
        return;
    }
    InputModule& m = module();
    const int upcoming = m.frameNo() + 1;
    RecompGuest actorGuest(rdram, c, rt);
    g_dragon = m.controlledDragon(actorGuest);
    // Normal dragon control does not call Camera_UpdateLook every frame:
    // controllers A/B only call it in camera mode 6 (on-foot C calls it
    // regularly). Do not let a stale on-foot look switch flight to menus.
    bool gameplay = inputGameplay(actorGuest, rdram, upcoming);
    const int scr = activeScreenId(rdram);
    drakan_menus::tick(rdram, c, rt);
    if (scr != g_lastScreen) {
        ilog("[in] t=%.2f frame %d: gui screen %d -> %d (gameplay=%d)\n", nowS(), upcoming, g_lastScreen, scr,
             gameplay ? 1 : 0);
        // The pause menu's Quit asks for confirmation in box screen 1; "Yes" returns to the title menu (8), "No" back
        // to the pause menu. The PC port closes the game instead (drakan.ini [general] quit_exits = 0 keeps the
        // original "back to the title").
        // The modern pause menu's "Quit to Title" answers the same box Yes and wants the title: suppressed once.
        const bool toTitle = g_lastScreen == 1 && g_pauseQuitToTitle.exchange(false) &&
                             std::chrono::steady_clock::now() - g_pauseQuitToTitleAt < std::chrono::seconds(30);
        if (g_lastScreen == 1 && scr == 8 && toTitle) {
            ilog("[in] Quit to Title (modern pause menu): the title menu stays\n");
        } else if (g_lastScreen == 1 && scr == 8 &&
                   drakan_cfg::getBool("general", "quit_exits", "DRAKAN_QUIT_EXITS", true)) {
            ilog("[in] Quit confirmed: closing the game\n");
            rt->requestStop();
        }
        g_lastScreen = scr;
    }
    // A GUI screen is open (message / info box with OK, pause menu, inventory ...): even while the camera still runs,
    // the keyboard and mouse must drive the menu layer (Enter / click = Cross) and the cursor is released.
    if (scr >= 0 && g_menuForce < 0 && !g_noScreenLayer)
        gameplay = false;
    if (g_menuForce >= 0)
        gameplay = g_menuForce == 0;
    g_playRun = gameplay ? g_playRun + 1 : 0;
    m.setPlayRun((g_autoStart && !g_steady) ? 0 : g_playRun);
    m.setActiveScreen(scr);
    uint32_t scripted = 0;
    std::memcpy(&scripted, rdram + drakan_region::guestAddress(0x4fbafcu), 4);
    drakan_pause::tickInput(m, scr, gameplay, scripted != 0);
    // On foot the camera-look hook proves control; mounted on Arokh (Ravenshold starts in flight) it is not called,
    // so a long uninterrupted gameplay run counts as well.
    if (g_autoStart && !g_steady && drakan_input_flow::autostartReady(g_lookCalls.load(), g_playRun)) {
        g_steady = true;
        ilog("[in] autostart: player under control at frame %d\n", upcoming);
    }
    m.setMenuActive(!gameplay);
    m.setMenuContext(isMenuScreen(scr));
    if (g_hookLog > 0 && gameplay == g_wasMenu) {
        ilog("\n[in] t=%.2f frame %d: %s mode\n", nowS(), upcoming, gameplay ? "gameplay" : "menu");
        --g_hookLog;
    }
    g_wasMenu = !gameplay;
    if (m.scripted()) {
        int width = 0, height = 0;
        windowClientSize(hostHwnd(), &width, &height);
        m.setScriptClientSize(width, height); // headless scripts have no HostInput::poll
    }
    // Mouse motion is windowed on the refresh grid the engine tick is snapped to (drakan_frame_rate.h).
    m.setSampleRefresh(drakan_present_clock::lockedRefreshSeconds());
    if (!(g_skipBits & 16))
        beginFrame();
    RecompGuest g(rdram, c, rt);
    inventoryViewsTick(g, scr, gameplay);
    quickSlotsTick(g, scr, gameplay);
    pauseTick(rdram, g);
    menuScreensTick(rdram, c, rt, g); // the modern title / Save / Load screens (after the pause: input mode)
    quickSaveTick(rdram, c, rt, g, scr, gameplay);
    if (!(g_skipBits & 8)) {
        beforePadMgrUpdate(g, g_dragon.load());
        updateBowAim(g);
    }
    drakan_parry_audit::frame = upcoming;
    g_origPadMgrUpdate(rdram, c, rt);
    if (!(g_skipBits & 8))
        afterPadMgrUpdate(g);
    drakan_combat_audit::sample(g, m, ilog, upcoming, scr, g_dragon.load());
    { // inventory / map mouse: log section changes and the adapter's D-pad / click taps (verification aid)
        static int lastSec = -1, lastTaps = 0;
        const int sec = m.menuMouse().inventorySection();
        static unsigned clickLogs = 0;
        static int clickFrame = -1;
        if (sec >= 0 && (m.frame().mousePressed & MBTN_LEFT) && clickLogs < 32 && clickFrame != m.frameNo()) {
            ++clickLogs;
            clickFrame = m.frameNo();
            const auto& d = m.menuMouse().debug();
            const auto& v = d.clickView;
            ilog("[in] inventory click #%u frame=%d serial=%u cursor=%d,%d client=%d,%d view=%.2f,%.2f,%.2f,%.2f "
                 "valid=%d hud=%.6f page=%.6f GUI=%d,%d/%d,%d inside=%d inGui=%d hit=%d item=%d age=%u absolute=%d "
                 "selected=%d target=%d expectedGrid=%.2f,%.2f->%.2f,%.2f\n",
                 clickLogs, m.frameNo(), d.inventoryClickSerial, m.frame().cursorX, m.frame().cursorY,
                 m.frame().clientW, m.frame().clientH, v.x, v.y, v.w, v.h, v.valid ? 1 : 0, v.hudScaleX,
                 v.inventoryPageScaleX, d.gx, d.gy, d.clickW, d.clickH, d.clickSample.inside ? 1 : 0,
                 d.clickInGui ? 1 : 0, d.clickRawCell, d.clickItem, d.clickGridAge, d.clickAbsoluteGrid ? 1 : 0,
                 d.clickSelected, d.inventoryTarget, d.gridLeft, d.gridTop, d.gridRight, d.gridBottom);
        }
        if (sec != lastSec) {
            ilog("[in] inventory section %d -> %d\n", lastSec, sec);
            lastSec = sec;
        }
        const int taps = m.menuMouse().debug().taps + m.menuMouse().debug().presses;
        if (taps != lastTaps) {
            if (sec >= 0)
                ilog("[in] inventory mouse input (section %d, taps+presses=%d)\n", sec, taps);
            lastTaps = taps;
        }
    }
    if (g_debugKeys &&
        m.frame().keyPressed[0x43]) { // F9 (DRAKAN_INPUT_DEBUGKEYS=1): whole guest RAM -> <DRAKAN_SHOT_DIR>/ram_<n>.ram
                                      // (reverse engineering: diff before / after a UI action)
        static int n = 0;
        const char* dir = std::getenv("DRAKAN_SHOT_DIR");
        const std::string path = std::string(dir ? dir : ".") + "/ram_" + std::to_string(n++) + ".ram";
        if (std::FILE* rf = std::fopen(path.c_str(), "wb")) {
            std::fwrite(rdram, 1, PS2_RAM_SIZE, rf);
            std::fclose(rf);
            ilog("[in] ram dump %s\n", path.c_str());
        }
    }
    if (m.scripted()) {
        const ScriptEvents& ev = m.scriptEvents();
        if (ev.dualWield >= 0) {
            drakan_dual_wield::setEnabled(ev.dualWield != 0);
            ilog("[in] script dual_wield=%d\n", ev.dualWield);
        }
        if (ev.menu != -2)
            g_menuForce = ev.menu;
        if (ev.logOn >= 0)
            g_logEvery = ev.logOn;
        for (const std::string& sh : ev.shots) {
            const char* dir = std::getenv("DRAKAN_SHOT_DIR");
            const std::string base = std::string(dir ? dir : ".") + "/";
            // The renderer's copy of the next presented image stays valid when the window is hidden or covered;
            // the desktop BMP is kept for older tools.
#if defined(PS2X_HAVE_GSR)
            gsr::requestWindowShot(base + sh + ".png");
#endif
            // The desktop BMP copies the screen where the window is: with DRAKAN_NO_ACTIVATE (window kept in the
            // background) that would be whatever else is on screen, so it is not taken.
            static const bool background = [] {
                const char* e = std::getenv("DRAKAN_NO_ACTIVATE");
                return e && *e == '1';
            }();
            const std::string path = base + "input_ingame_" + sh + ".bmp";
            ilog("[in] screenshot %s -> %s (t=%.3f frame=%d)\n", sh.c_str(),
                 background || screenshotWindowBmp(hostHwnd(), path.c_str()) ? "ok" : "FAILED", nowS(), m.frameNo());
        }
        for (const std::string& d : ev.dumps) {
            if (d.rfind("ram", 0) == 0) { // `dump ram_<label>`: whole guest RAM to <DRAKAN_SHOT_DIR>/<label>.ram
                                          // (diffing inventory / map state)
                const char* dir = std::getenv("DRAKAN_SHOT_DIR");
                const std::string path = std::string(dir ? dir : ".") + "/" + d + ".ram";
                if (std::FILE* rf = std::fopen(path.c_str(), "wb")) {
                    std::fwrite(rdram, 1, PS2_RAM_SIZE, rf);
                    std::fclose(rf);
                    ilog("[in] ram dump %s\n", path.c_str());
                    ilog("[in-ram-time] label=%s seconds=%.6f frame=%d\n", d.c_str(), nowS(), m.frameNo());
                }
            } else
                dumpState(g, d.c_str());
        }
        if (ev.quit) {
            ilog("[in] script quit at frame %d\n", m.frameNo());
            rt->requestStop();
        }
    }
    if (g_logEvery > 0 && (m.frameNo() % g_logEvery) == 0) {
        const uint32_t p = g_player.load();
        ilog("\n[in-log] t=%.2f frame=%d %s player=0x%x lookCalls=%d dx=%d dy=%d cap=%d foc=%d\n", nowS(), m.frameNo(),
             gameplay ? "play" : "menu", p, g_lookCalls.load(), m.frame().dx, m.frame().dy, m.captured() ? 1 : 0,
             m.frame().focused ? 1 : 0);
        if (p) {
            const uint32_t h = g.read32(p + 0x80);
            const uint32_t cx = g.read32(0x5565d4);
            const uint32_t ch = cx ? g.read32(cx + 0x4b4) : 0;
            const uint32_t ci = ch ? g.read32(ch + 0xfc) : 0;
            const uint32_t cam = ci ? g.read32(ci + 0x10) : 0;
            const uint32_t camObj = cam ? g.read32(cam + 0x148) : 0;
            auto camPos = [&](uint32_t i) { return camObj ? g.readF(camObj + 0x14 + 4 * i) : 0.f; };
            if (h)
                ilog("[in-pos] f=%d pos=(%.1f %.1f %.1f) row0=(%.4f %.4f %.4f) yawA=%.4f camPitch=%.4f camYaw=%.4f "
                     "st=%08x y_d4=%.1f spd=%.1f mv=%x/%x/%x jt=%d camPos=(%.1f %.1f %.1f) dt=%.5f\n",
                     m.frameNo(), g.readF(h + 0x14), g.readF(h + 0x18), g.readF(h + 0x1c), g.readF(h + 0x20),
                     g.readF(h + 0x24), g.readF(h + 0x28), std::atan2(g.readF(h + 0x28), g.readF(h + 0x20)),
                     cam ? g.readF(cam + 0x84) : 0.f, cam ? g.readF(cam + 0x90) : 0.f, g.read32(p + 0xc4),
                     g.readF(p + 0xd4), g.readF(p + 0x8), g.read32(p + 0x504), g.read32(p + 0x508), g.read32(p + 0x50c),
                     static_cast<int>(g.read32(p + 0xaf0)), camPos(0), camPos(1), camPos(2),
                     cx ? g.readF(cx + 0x24) : 0.f);
            // Rynn's animation records (0x303ff8: model = *(player+0x80)+0x50, record of layer n = *(model+0xe8) +
            // s16 *(model+0xdc)[n] * 0x80; +0 flags, +0x50 id, +0x54 speed, +0x58 time): layer player+0xaec (the
            // attack layer while ab0 bit 2 is set) and the base layer 0, with the action / request flags around them.
            const uint32_t mdl = h ? g.read32(h + 0x50) : 0;
            const uint32_t idxTab = mdl ? g.read32(mdl + 0xdc) : 0, recs = mdl ? g.read32(mdl + 0xe8) : 0;
            const auto layerRec = [&](uint32_t layer) {
                return idxTab && recs ? recs + uint32_t(int16_t(g.read16(idxTab + 2 * layer))) * 0x80u : 0u;
            };
            const uint32_t rec = layerRec(g.read32(p + 0xaec)), rec0 = layerRec(0);
            if (rec && rec0)
                ilog("[in-anim] f=%d anim=%x spd=%.3f t=%.3f rf=%x aec=%d base=%x/%.3f/%.3f/%x f90=%x f94=%x ab0=%x "
                     "ab4=%x b28=%x cc0=%x dt=%.4f\n",
                     m.frameNo(), g.read32(rec + 0x50), g.readF(rec + 0x54), g.readF(rec + 0x58), g.read32(rec),
                     int(g.read32(p + 0xaec)), g.read32(rec0 + 0x50), g.readF(rec0 + 0x54), g.readF(rec0 + 0x58),
                     g.read32(rec0), g.read32(p + 0x90), g.read32(p + 0x94), g.read32(p + 0xab0), g.read32(p + 0xab4),
                     g.read32(p + 0xb28), g.read32(p + 0xcc0), cx ? g.readF(cx + 0x24) : 0.f);
        }
        if (p && std::getenv("DRAKAN_INPUT_LOGDUMP"))
            dumpState(g, "log");
    }
    // Test aid (DRAKAN_DEBUG_OPEN_SHOP=<n>[,<n>...]): once Rynn has been under control for a while, open the level's
    // n-th shop (class 0x100ac objects in the entity table) through the game's own entry 33be50(any, shop); each
    // further number opens after the previous shop was closed and control returned. The screen factory behind it must
    // run on the real context, so it is tail-called like the title Quit box (docs/reference/save-slots.md).
    // Test aid (DRAKAN_DEBUG_SKILL_POINTS=<n>): unspent skill points for the Skills page, stored as the game keeps them
    // (rotr8(*0x4fbaf4 ^ *0x4fe86c)), once Rynn is under control.
    static int skillPoints =
        std::getenv("DRAKAN_DEBUG_SKILL_POINTS") ? std::atoi(std::getenv("DRAKAN_DEBUG_SKILL_POINTS")) : 0;
    if (skillPoints > 0 && gameplay && scr < 0 && g_playRun > 150) {
        const uint32_t n = uint32_t(skillPoints), rotated = (n << 8) | (n >> 24);
        g.write32(0x4fbaf4u, rotated ^ g.read32(0x4fe86cu));
        ilog("[in] debug skill points: %d\n", skillPoints);
        skillPoints = 0;
    }
    static const char* shopList = std::getenv("DRAKAN_DEBUG_OPEN_SHOP");
    const int shopRequest = shopList && *shopList ? std::atoi(shopList) : 0;
    if (shopRequest > 0 && gameplay && scr < 0 && g_playRun > 150 && g.read32(0x4fbafcu) == 0) {
        const int wanted = shopRequest;
        const char* next = std::strchr(shopList, ',');
        shopList = next ? next + 1 : nullptr;
        const auto ok = [](uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x100u && (a & 3u) == 0; };
        const uint32_t G = g.read32(0x5565d4u), base = ok(G) ? g.read32(G + 0x7c) : 0,
                       db = ok(G) ? g.read32(G + 0x320) : 0;
        const uint32_t handles = ok(G) ? std::min<uint32_t>(g.read32(G + 0x84) + 1, 8192) : 0;
        uint32_t chosen = 0;
        int found = 0;
        for (uint32_t h = 1; ok(base) && ok(db) && h <= handles; ++h) {
            const uint32_t e = g.read32(base + (h - 1) * 8);
            const uint32_t idx = ok(e) ? g.read16(e + 0x10) : 0xffffu;
            if (idx == 0xffffu || idx > g.read32(db + 0x10) || g.read32(g.read32(db + 8) + idx * 0x24 + 8) != 0x100acu)
                continue;
            const uint32_t holder = g.read32(e + 0xfc), obj = ok(holder) ? g.read32(holder + 0x10) : 0;
            if (!ok(obj) || g.read32(obj + 0x48) != e)
                continue;
            ++found;
            ilog("[in] debug shop %d: handle=%u entity=%x shop=%x stock=%u npc=%u enabled=%u\n", found, h, e, obj,
                 g.read32(obj + 0x1c), g.read16(obj + 0x44), g.read32(obj + 4));
            if (found == wanted)
                chosen = obj;
        }
        if (chosen) {
            ilog("[in] debug shop: opening shop %d (%x)\n", wanted, chosen);
            SET_GPR_U32(c, 4, 0);
            SET_GPR_U32(c, 5, chosen);
            c->pc = 0x33be50u; // returns to PadMgr_Update's caller
        } else
            ilog("[in] debug shop: %d shop(s) in this level, none opened\n", found);
    }
}

uint32_t cameraObject(RecompGuest& g) {
    const uint32_t ctx = g.read32(module().config().addrCtxPtr);
    const auto& layout = drakan_region::build().camera;
    const uint32_t handle = ctx ? g.read32(ctx + layout.contextCamera) : 0;
    const uint32_t inner = handle ? g.read32(handle + layout.handleInner) : 0;
    return inner ? g.read32(inner + layout.innerData) : 0;
}

void hookCameraUpdateLook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t player = getRegU32(c, 4), limits = getRegU32(c, 5);
    const bool dragon = getRegU32(c, 6) != 0;
    RecompGuest g(rdram, c, rt);
    const uint32_t cam = cameraObject(g);
    if (!dragon && module().freeCombatAllowed(g, player, cam)) {
        // Explicit target lock and alternate aiming own their native camera.
        // freeCombatAllowed excludes both before clearing free-look orbit.
        g.writeF(cam + drakan_region::build().camera.cameraOrbit,
                 0); // direct mouse follows Rynn instead of orbiting around a target
    }
    beforeCameraUpdateLook(g, dragon, player);
    g_origCameraLook(rdram, c, rt); // sets pc = $ra when done; $ra itself is preserved
    g_player = player;
    g_limits = limits;
    g_lookFrame = module().frameNo();
    g_lookCalls++;
    afterCameraUpdateLook(g, player, limits, dragon);
}
// FUN_0041f848 (a0 = camera object): mode-6 camera tick, eases the camera position behind the player.
void hookCameraFollow(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t cam = getRegU32(c, 4);
    RecompGuest g(rdram, c, rt);
    beforeCameraFollow(g, cam);
    g_origCameraFollow(rdram, c, rt);
}
// FUN_0020e6c8 (a0 = bow object): launches the nocked arrow (*(bow+0x158)) along its matrix, retargeted only for a
// locked target / homing arrow; modern bow aiming points it at the crosshair first.
void hookBowLaunch(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t bow = getRegU32(c, 4);
    RecompGuest g(rdram, c, rt);
    beforeBowLaunch(g, bow);
    g_origBowLaunch(rdram, c, rt);
}
} // namespace

namespace drakan_tab {
namespace {
std::atomic<int> g_enabled{-1};
}
bool enabled() {
    int v = g_enabled.load();
    if (v < 0) {
        const std::string mode =
            drakan_cfg::getString("general", "inventory_screen", "DRAKAN_INVENTORY_SCREEN", "native");
        v = (mode == "modern" || mode == "MODERN" || mode == "Modern") ? 1 : 0;
        g_enabled.store(v);
    }
    return v == 1;
}
void setEnabled(bool on) {
    g_enabled.store(on ? 1 : 0);
}
Shared& shared() {
    static Shared s;
    return s;
}
namespace {
bool modernSetting(const char* key, const char* env) {
    const std::string mode = drakan_cfg::getString("general", key, env, "modern");
    return !(mode == "native" || mode == "NATIVE" || mode == "Native" || mode == "0");
}
} // namespace
bool stripModern() {
    static const bool v = modernSetting("strip_screen", "DRAKAN_STRIP_SCREEN");
    return v;
}
bool mapModern() {
    static const bool v = modernSetting("map_screen", "DRAKAN_MAP_SCREEN");
    return v;
}
std::string levelDisplayName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    if (const size_t dot = stem.rfind('.'); dot != std::string::npos)
        stem.resize(dot);
    if (const size_t dash = stem.find(" - "); dash != std::string::npos)
        stem = stem.substr(dash + 3);
    return stem;
}
} // namespace drakan_tab

namespace drakan_pause_screen {
namespace {
std::atomic<int> g_enabled{-1};
}
bool enabled() {
    int v = g_enabled.load();
    if (v < 0) {
        const std::string mode = drakan_cfg::getString("general", "pause_menu", "DRAKAN_PAUSE_MENU", "native");
        v = (mode == "modern" || mode == "MODERN" || mode == "Modern") ? 1 : 0;
        g_enabled.store(v);
    }
    return v == 1;
}
void setEnabled(bool on) {
    g_enabled.store(on ? 1 : 0);
}
Shared& shared() {
    static Shared s;
    return s;
}
} // namespace drakan_pause_screen

namespace drakan_menu_screens {
namespace {
std::atomic<int> g_save{-1}, g_title{-1};
bool modernMode(std::atomic<int>& v, const char* key, const char* env) {
    int on = v.load();
    if (on < 0) {
        const std::string mode = drakan_cfg::getString("general", key, env, "native");
        on = (mode == "modern" || mode == "MODERN" || mode == "Modern") ? 1 : 0;
        v.store(on);
    }
    return on == 1;
}
} // namespace
bool saveModern() {
    return modernMode(g_save, "save_menu", "DRAKAN_SAVE_MENU");
}
void setSaveModern(bool on) {
    g_save.store(on ? 1 : 0);
}
bool titleModern() {
    return modernMode(g_title, "title_menu", "DRAKAN_TITLE_MENU");
}
void setTitleModern(bool on) {
    g_title.store(on ? 1 : 0);
}
Shared& shared() {
    static Shared s;
    return s;
}
double clockSeconds() {
    return nowS();
}
} // namespace drakan_menu_screens

#if defined(PS2X_HAVE_DRAKAN_INPUT)
// Both input integrations install the same audited camera hooks.
void drakanInstallDirectMouse(PS2Runtime& rt) {
    using namespace drakan_region;
    if (!supports(Feature::DirectMouseLook))
        return;
    module().setCameraProfile(inputCameraProfile());
    g_origCameraLook = rt.lookupFunction(guestAddress(kCameraUpdateLook));
    g_origCameraFollow = rt.lookupFunction(guestAddress(kCameraFollowTick));
    if (!g_origCameraLook || !g_origCameraFollow)
        throw std::runtime_error("Disc profile camera hook is missing");
    const int skip = std::getenv("DRAKAN_INPUT_SKIP") ? std::atoi(std::getenv("DRAKAN_INPUT_SKIP")) : 0;
    if (!(skip & 2))
        rt.replaceFunction(guestAddress(kCameraUpdateLook), &hookCameraUpdateLook);
    if (!(skip & 32))
        rt.replaceFunction(guestAddress(kCameraFollowTick), &hookCameraFollow);
    ilog("[input] %s: profile-driven direct mouse look active\n", build().id);
}

bool drakanInputGameplay(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    RecompGuest guest(ram, context, runtime);
    return inputGameplay(guest, ram, module().frameNo() + 1);
}

bool drakanInputAutostartReady(int playRun) {
    return drakan_input_flow::autostartReady(g_lookCalls.load(), playRun);
}

void drakanInputBeforePadMgr(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    RecompGuest guest(ram, context, runtime);
    module().beforePadMgrUpdate(guest, module().controlledDragon(guest));
}

void drakanInputAfterMovement(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    RecompGuest guest(ram, context, runtime);
    module().afterMovementInput(guest);
}

void drakanInstallInput(PS2Runtime& rt) {
    module().setDirectLookAvailable(drakan_region::supports(drakan_region::Feature::DirectMouseLook));
    installParryAudit(rt);
    const char* on = std::getenv("DRAKAN_INPUT");
    if (on && on[0] == '0') {
        ilog("[input] DRAKAN_INPUT=0: stock scePad stubs kept\n");
        return;
    }
    g_t0 = std::chrono::steady_clock::now();
    if (const char* lf = std::getenv("DRAKAN_INPUT_LOGFILE"))
        g_lf = std::fopen(lf, "w");
    if (const char* e = std::getenv("DRAKAN_AUTOPRESS"))
        g_autoPressMs = std::atol(e);
    if (const char* e = std::getenv("DRAKAN_AUTOPRESS_UNTIL"))
        g_autoPressUntil = std::atol(e);
    if (const char* e = std::getenv("DRAKAN_INPUT_AUTOSTART"))
        g_autoStart = std::atoi(e) != 0;
    g_debugKeys = std::getenv("DRAKAN_INPUT_DEBUGKEYS") && std::atoi(std::getenv("DRAKAN_INPUT_DEBUGKEYS")) != 0;
    if (const char* e = std::getenv("DRAKAN_INPUT_SCREEN_LAYER"))
        g_noScreenLayer = std::atoi(e) == 0;
    if (const char* e = std::getenv("DRAKAN_INPUT_MENU"))
        g_menuForce = std::atoi(e) ? 1 : 0;
    if (const char* e = std::getenv("DRAKAN_INPUT_LOG")) {
        g_logEvery = std::atoi(e);
        g_hookLog = 400;
    }
    {
        // drakan.ini next to the exe: [general] skip_memcard_warning = 1   (env DRAKAN_SKIP_MEMCARD_WARNING overrides)
        const std::string cfg = exeDirectory() + "drakan.ini";
        std::FILE* f = std::fopen(cfg.c_str(), "rb");
        if (!f) {
            if (std::FILE* o = std::fopen(cfg.c_str(), "wb")) {
                std::fputs("; drakan.ini - Drakan port settings\r\n[general]\r\n; auto-dismiss the memory card warning "
                           "(Press X to continue) at boot; card emulation is untouched\r\nskip_memcard_warning = 1\r\n",
                           o);
                std::fclose(o);
            }
        } else {
            char line[256];
            while (std::fgets(line, sizeof line, f)) {
                const char* k = std::strstr(line, "skip_memcard_warning");
                const char* eq = k ? std::strchr(k, '=') : nullptr;
                if (k && eq && line[0] != ';')
                    g_skipMemcard = std::atoi(eq + 1) != 0;
            }
            std::fclose(f);
        }
        if (const char* e = std::getenv("DRAKAN_SKIP_MEMCARD_WARNING"))
            g_skipMemcard = std::atoi(e) != 0;
    }
    std::string ini;
    if (const char* e = std::getenv("DRAKAN_INPUT_INI"))
        ini = e;
    else
        ini = exeDirectory() + "drakan_input.ini";

    const char* script = std::getenv("DRAKAN_INPUT_SCRIPT");
    if (script && script[0]) {
        std::string warn;
        Config cfg = Config::loadOrCreate(ini, &warn);
        if (!warn.empty())
            ilog("[input] %s: %s", ini.c_str(), warn.c_str());
        module().initHeadless(cfg);
        std::string err;
        if (!module().loadScript(script, &err))
            ilog("[input] script '%s' not loaded: %s\n", script, err.c_str());
        else
            ilog("[input] SCRIPTED host input from '%s'%s%s\n", script, err.empty() ? "" : "\n", err.c_str());
    } else {
        // raw input comes from the module's own message-only window; the host (raylib / D3D child) window is only
        // used for cursor clipping and hiding
#if defined(_WIN32)
        init(nullptr, ini.c_str());
        module().setCaptureWindow(hostHwnd());
#elif defined(__EMSCRIPTEN__)
        // The page's DOM events (drakan_input_web)
        drakan_input_web::install();
        init(drakan_win::handle(), ini.c_str());
#else
        // GLFW: callbacks on the game window (drakan_input_glfw)
        drakan_input_glfw::install();
        init(drakan_win::handle(), ini.c_str());
#endif
    }
    if (const char* wp = std::getenv("DRAKAN_WINDOW_POS")) // test aid: put the window somewhere nobody else covers it
    {
        int x = 0, y = 0;
        if (std::sscanf(wp, "%d,%d", &x, &y) == 2) {
            if (drakan_win::handle())
                drakan_win::setPosition(x, y);
            else
                SetWindowPosition(x, y);
        }
        makeWindowTopmost(hostHwnd());
    }
    if (!drakan_win::handle())
        SetExitKey(0); // raylib's default exit key is Esc: Esc is the game's pause key (and the module's mouse release)

    installInventoryViewHooks(rt); // first: the mouse and content hooks below wrap these
    g_origTabDialogFrame = rt.lookupFunction(0x2c94a8u);
    if (g_origTabDialogFrame)
        rt.replaceFunction(0x2c94a8u, &hookTabDialogFrame);
    installPauseScreenHooks(rt);
    installMenuScreenHooks(rt);
    installMapMouseHooks(rt);
    installInventoryContentHooks(rt);
    module().menuMouseRw().setMapStateQuery([](IGuest& guest, uint32_t record, int& state) {
        return static_cast<RecompGuest&>(guest).mapMarkerState(record, state);
    });
    g_origInventoryGrid = rt.lookupFunction(0x437170u);
    g_origInventoryCategory = rt.lookupFunction(0x42b308u);
    g_origInventoryBorder = rt.lookupFunction(0x2c8ff8u);
    g_origInventoryMaterial = rt.lookupFunction(0x42a4b0u);
    if (g_origInventoryBorder)
        rt.replaceFunction(0x2c8ff8u, &hookInventoryBorder);
    if (g_origInventoryMaterial)
        rt.replaceFunction(0x42a4b0u, &hookInventoryMaterial);
    if (g_origInventoryCategory)
        rt.replaceFunction(0x42b308u, &hookInventoryCategory);
    g_origInventorySelect = rt.lookupFunction(0x33eaf8u);
    if (g_origInventoryGrid && g_origInventorySelect) {
        rt.replaceFunction(0x437170u, &hookInventoryGrid);
        rt.replaceFunction(0x33eaf8u, &hookInventorySelect);
    }
    g_origPadMgrUpdate = rt.lookupFunction(kPadMgrUpdate);
    const int skip = g_skipBits = std::getenv("DRAKAN_INPUT_SKIP") ? std::atoi(std::getenv("DRAKAN_INPUT_SKIP"))
                                                                   : 0; // debug: 1 = PadMgr hook, 2 = camera hook
    if (!(skip & 1))
        rt.replaceFunction(kPadMgrUpdate, &hookPadMgrUpdate);
    drakanInstallDirectMouse(rt);
    g_origBowLaunch = rt.lookupFunction(kBowLaunch);
    if (g_origBowLaunch && !(skip & 64)) // 64: no modern bow aim hook
        rt.replaceFunction(kBowLaunch, &hookBowLaunch);
    rt.replaceFunction(kPadInit, &hookPadInit);
    rt.replaceFunction(kPadPortOpen, &hookPadPortOpen);
    rt.replaceFunction(kPadGetState, &hookPadGetState);
    rt.replaceFunction(kPadSetMainMode, &hookPadSetMainMode);
    rt.replaceFunction(kPadSetActDirect, &hookPadSetActDirect);
    rt.replaceFunction(kPadRead, &hookPadRead);
    ilog("[input] mouse+keyboard input active (config %s)\n", ini.c_str());
}

#else
// Retain the portable direct-camera owner and input accumulator. Only the
// platform-free modern menu hooks and their native transaction executors are
// shared here; neither host installs a second PadMgr/camera hook.
void drakanInstallPortableMenuHooks(PS2Runtime& rt) {
    g_t0 = std::chrono::steady_clock::now();
    installPauseScreenHooks(rt);
    installMenuScreenHooks(rt);
    g_origBowLaunch = rt.lookupFunction(kBowLaunch);
    if (g_origBowLaunch)
        rt.replaceFunction(kBowLaunch, &hookBowLaunch); // modern bow aim, as on Windows
}
void drakanPortableMenuBoundary(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    if (const auto request = drakan_menu_screens::shared().uiSound.exchange(0))
        tabPlaySound(ram, context, runtime, request);
}
void drakanTickPortableMenuHooks(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    RecompGuest guest(ram, context, runtime);
    drakan_menus::tick(ram, context, runtime);
    const int screen = activeScreenId(ram);
    const drakan_input_flow::Reader flow({ram, PS2_RAM_SIZE});
    drakan_pause::tickInput(module(), screen, flow.gameplay(), flow.scripted());
    pauseTick(ram, guest);
    menuScreensTick(ram, context, runtime, guest);
    const bool gameplay = screen < 0 && guest.read32(0x4fbafcu) == 0;
    quickSaveTick(ram, context, runtime, guest, screen, gameplay);
}
#endif
#endif // input / portable modern menu glue
