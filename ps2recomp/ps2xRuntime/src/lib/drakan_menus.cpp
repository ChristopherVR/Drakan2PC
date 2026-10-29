// Drakan: game menu additions (title "Quit", save pages). See drakan_menus.h and docs/reference/save-slots.md.
#include "drakan_menus.h"
#include "drakan_save_previews.h"
#include "drakan_region.h"

#if defined(PS2X_HAVE_DRAKAN_INPUT) || defined(PS2X_HAVE_PORTABLE_UI)
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "drakan_config.h"
#include "input/DrakanInput.h"
#if defined(PS2X_HAVE_DRAKAN_SETTINGS) || defined(PS2X_HAVE_PORTABLE_UI)
#include "settings/Store.h"
#endif
#if defined(PS2X_HAVE_GSR)
#include "drakan_window.h"
#include <gsr/gsr.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <array>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

namespace drakan_menus {
namespace {
using drakan_input::GuestCall;
using drakan_input::IGuest;

// ---- SCES_500.06 addresses (docs/reference/save-slots.md)
// ------------------------------------------------------------------------------------
constexpr uint32_t kSlotKeyHandler =
    0x43b1e8u; // load / save screens: keyboard / pad key (screen + 0x50 = key id), arrows move the slot selection
constexpr uint32_t kSprintf = 0x4ce688u; // sprintf(dst, fmt, ...)
constexpr uint32_t kAnySave =
    0x37e810u; // (0) = "is there a save game on the card" (1 = yes), (1) = settings file check
constexpr uint32_t kTitleHandler = 0x43e4c8u;  // title menu (screen 8) event handler: (screen, widget, message)
constexpr uint32_t kSlotNameCells = 0x5325c8u; // "Slot1" .. "Slot4", 8 bytes each (folder = <saveId> + cell)
constexpr uint32_t kSlotNamePtrs = 0x4fe3b8u;  // char *[4] -> the cells above
constexpr uint32_t kFmtSlotLoad = 0x54e3a0u;   // "%d - %s" (slot caption on the load / save screens)
constexpr uint32_t kFmtSlotSave = 0x54f040u;   // "%d - %s" (confirm text of the save screen)
constexpr uint32_t kActiveScreenVar = 0x4fb60cu;
constexpr uint32_t kGuiSizeObj = 0x5565c8u;      // *(u32*) + 0xf8 / 0xfc = GUI coordinate space (512 x 512)
constexpr uint32_t kFnLineHeight = 0x288658u;    // (font) -> line height
constexpr uint32_t kFnTextWidth = 0x2885c8u;     // (str, font, 0, len + 1) -> width in pixels
constexpr uint32_t kFnSetFocus = 0x2be528u;      // (screen, widget) focus a widget (0 = clear)
constexpr uint32_t kFnScreenFactory = 0x2c4458u; // (manager, screen id, parent, request, 1): screen 1 = yes / no box,
                                                 // the answer is message 0x10 to the parent
constexpr uint32_t kScreenManager = 0x4fb600u;
constexpr uint32_t kReqPage = 0xFF000u; // unused page below the ELF (0x100000): the quit box request + text
constexpr uint32_t kSaveScreenCtor =
    0x4442e0u; // (screen, ..., request) save screen; request {mode, char* text, callback node*} -> screen + 0x1e4

constexpr int kScreenLoad = 4, kScreenSave = 5, kScreenTitle = 8;
constexpr uint32_t kWidgetQuit = 0x2303u, kWidgetYes = 0x238bu;
constexpr int kHotBack = 100, kHotNext = 101;
constexpr int kKeyPrev = 0xd3,
              kKeyNext = 0xd1; // GUI key ids: Delete (pad L1, PgUp in the menu bindings) and PgDn (pad R1, PgDn)

uint8_t* g_ram = nullptr;
std::atomic<int> g_page{0};
std::mutex g_pagerMutex;
Pager g_pager;
std::array<uint64_t, 4> g_thumbnailSeen{};
bool g_slotClosing = false; // EE thread; suppress cached pager as load starts
bool g_slotsHosted = false; // EE thread; the modern Save / Load screen owns the slot screen (no pager)
uint32_t g_hotspotScreen = 0;
int g_pages = 4;
bool g_quickPage = false;
int g_quickPreviousPage = 0;
bool g_titleQuit = true;
bool g_namesChecked = false, g_namesOk = false;
uint32_t g_quitBoxScreen = 0, g_titleDone = 0;
PS2Runtime::RecompiledFunction g_origKey = nullptr, g_origSprintf = nullptr, g_origAnySave = nullptr,
                               g_origTitle = nullptr;
PS2Runtime::RecompiledFunction g_origText = nullptr, g_origTextWidth = nullptr;
PS2Runtime::RecompiledFunction g_origWidgetText = nullptr;
PS2Runtime::RecompiledFunction g_origSaveCtor = nullptr;
int g_loadingLabelX = 0;

void loadingText(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x286170u && (GPR_U32(c, 31) == 0x43d878u || GPR_U32(c, 31) == 0x43d8a4u)) {
        SET_GPR_U32(c, 8, 1); // same decorative face as the native menu
        SET_GPR_U32(c, 4, g_loadingLabelX);
    }
    g_origText(ram, c, rt);
}
void loadingWidth(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == kFnTextWidth && GPR_U32(c, 31) == 0x43d828u)
        SET_GPR_U32(c, 5, 1);
    g_origTextWidth(ram, c, rt);
}

inline uint8_t* P(uint32_t a) {
    return g_ram + (a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1));
}
inline bool okPtr(uint32_t p) {
    return p >= 0x00100000u && p + 0x100u < PS2_RAM_SIZE;
}
inline uint32_t rd32(uint32_t a) {
    uint32_t v;
    std::memcpy(&v, P(a), 4);
    return v;
}
inline uint16_t rd16(uint32_t a) {
    uint16_t v;
    std::memcpy(&v, P(a), 2);
    return v;
}

void leave(R5900Context* c, uint32_t v) {
    SET_GPR_S32(c, 2, static_cast<int32_t>(v));
    c->pc = getRegU32(c, 31);
}

// Guest access for hooks: nested calls run on a copy of the hook's context, with a stack placed well below the real one
// so the scratch area [sp - 0x400, sp - 0x100) (strings, colours for the text routine) stays intact.
class MGuest final : public IGuest {
public:
    MGuest(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt)
        : ram_(ram), ctx_(ctx), rt_(rt), sp_(getRegU32(ctx, 29) & ~15u) {}
    uint8_t read8(uint32_t a) override { return ram_[a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)]; }
    uint32_t read32(uint32_t a) override {
        uint32_t v;
        std::memcpy(&v, ram_ + (a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)), 4);
        return v;
    }
    void write8(uint32_t a, uint8_t v) override { ram_[a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)] = v; }
    void write32(uint32_t a, uint32_t v) override { std::memcpy(ram_ + (a & 0x1FFFFFFFu & (PS2_RAM_SIZE - 1)), &v, 4); }
    uint32_t scratch(uint32_t off) const { return sp_ - 0x400u + off; }
    uint32_t call(const GuestCall& c) override { return callWith(nullptr, c); }
    // `first` overrides the function table entry for the first invocation (the original of a replaced function).
    uint32_t callWith(PS2Runtime::RecompiledFunction first, const GuestCall& c) {
        if (c.addr == 0 || (!first && !rt_->hasFunction(c.addr)))
            return 0;
        auto tmp = std::make_unique<R5900Context>(*ctx_);
        for (int i = 0; i < c.ni; ++i)
            SET_GPR_U32(tmp.get(), 4 + i, c.i[i]);
        for (int i = 0; i < c.nf; ++i)
            tmp->f[12 + i] = c.f[i];
        SET_GPR_U32(tmp.get(), 29, sp_ - 0x800u);
        SET_GPR_U32(tmp.get(), 31, 0);
        tmp->pc = c.addr;
        tmp->in_delay_slot = false;
        ps2xNoPreemptEnter();
        for (int guard = 0; guard < 64 && tmp->pc != 0u; ++guard) {
            PS2Runtime::RecompiledFunction fn = first;
            first = nullptr;
            if (!fn) {
                if (!rt_->hasFunction(tmp->pc))
                    break;
                fn = rt_->lookupFunction(tmp->pc);
            }
            fn(ram_, tmp.get(), rt_);
        }
        ps2xNoPreemptLeave();
        if (tmp->pc != 0u)
            std::fprintf(stderr, "[menus] nested call 0x%x did not finish (pc=0x%x)\n", c.addr, tmp->pc);
        return getRegU32(tmp.get(), 2);
    }

private:
    uint8_t* ram_;
    R5900Context* ctx_;
    PS2Runtime* rt_;
    uint32_t sp_;
};

void write16(IGuest& g, uint32_t a, uint16_t v) {
    g.write8(a, static_cast<uint8_t>(v & 0xff));
    g.write8(a + 1, static_cast<uint8_t>(v >> 8));
}

void putStr(IGuest& g, uint32_t a, const char* s) {
    do
        g.write8(a++, static_cast<uint8_t>(*s));
    while (*s++);
}

// Virtual call: records {s16 this adjust, fn} at vt + slot - 4 / vt + slot of the vtable pointer stored at obj + vtOfs.
uint32_t vcall(IGuest& g, uint32_t obj, uint32_t vtOfs, uint32_t slot, uint32_t a1, int nargs) {
    const uint32_t vt = g.read32(obj + vtOfs);
    if (!okPtr(vt))
        return 0;
    GuestCall c(g.read32(vt + slot));
    if (c.addr < 0x100000u || c.addr >= PS2_RAM_SIZE)
        return 0;
    c.arg(obj + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(g.read16(vt + slot - 4)))));
    if (nargs > 0)
        c.arg(a1);
    return g.call(c);
}

uint32_t findWidget(IGuest& g, uint32_t screen, uint16_t id) {
    uint32_t w = g.read32(screen + 0x7c);
    for (int guard = 0; w && guard < 256 && okPtr(w); ++guard) {
        if (g.read16(w + 0x14) == id)
            return w;
        w = g.read32(w + 4);
    }
    return 0;
}

void playSound(IGuest& g, uint32_t property) {
    drakan_input::module().menuMouseRw().playSound(g, property);
}

// ---- language
// ----------------------------------------------------------------------------------------------------------------------
struct Labels {
    const char *back, *next, *page, *quitPrompt;
};

const Labels& labels() {
    // Windows-1252 bytes: the game font is a bitmap font with those glyphs.
    static const Labels en{"< Back", "Next >", "Page", "Are you sure you want to quit?"};
    static const Labels de{"< Zur\xFC"
                           "ckkehren",
                           "Weiter >", "Seite", "Sind Sie sicher, dass Sie das Spiel verlassen wollen?"};
    static const Labels fr{"< Retour", "Suivant >", "Page", "Voulez-vous vraiment quitter?"};
    static const Labels it{"< Ritorna", "Avanti >", "Pagina", "Vuoi davvero uscire?"};
    static const Labels es{"< Volver", "Siguiente >",
                           "P\xE1"
                           "gina",
                           "\xBF"
                           "Seguro que quieres abandonar?"};
    static const Labels* sel = nullptr;
    if (!sel) {
        std::string l = drakan_cfg::getString("general", "language", "DRAKAN_LANG", "english");
        std::transform(l.begin(), l.end(), l.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        while (!l.empty() && (l[0] == '\\' || l[0] == '/'))
            l.erase(0, 1);
        sel = l.rfind("ger", 0) == 0 || l.rfind("deu", 0) == 0   ? &de
              : l.rfind("fre", 0) == 0 || l.rfind("fra", 0) == 0 ? &fr
              : l.rfind("ita", 0) == 0                           ? &it
              : l.rfind("spa", 0) == 0 || l.rfind("esp", 0) == 0 ? &es
                                                                 : &en;
    }
    return *sel;
}

std::string labelUtf8(const char* text) {
    std::string result;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p < 0x80)
            result += static_cast<char>(*p);
        else {
            result += static_cast<char>(0xc0 | (*p >> 6));
            result += static_cast<char>(0x80 | (*p & 0x3f));
        }
    }
    return result;
}

// ---- save pages
// --------------------------------------------------------------------------------------------------------------------
// The slot folders are <saveId> (drakan_region.h) + the string the pointer table at 0x4fe3b8 names. The table points
// at four 8 byte cells in .data, so rewriting the cells is the whole page switch: every consumer (directory scan,
// thumbnail / save read, save write, delete, free space check, confirm texts) uses the same table. Verified against
// the decompile: the table is used by 0x43b6f0 (scan), 0x4442e0 class (save), 0x37e810.
bool namesUsable(IGuest& g) {
    if (!g_namesChecked) {
        g_namesChecked = true;
        g_namesOk = true;
        for (uint32_t i = 0; i < 4; ++i)
            if (g.read32(kSlotNamePtrs + 4 * i) != kSlotNameCells + 8 * i)
                g_namesOk = false;
        const char* want = "Slot1";
        for (uint32_t b = 0; b < 6; ++b)
            if (g.read8(kSlotNameCells + b) != static_cast<uint8_t>(want[b]))
                g_namesOk = false;
        if (!g_namesOk)
            std::fprintf(stderr, "[menus] slot name table not as expected: save pages disabled\n");
    }
    return g_namesOk;
}

void writeSlotNames(IGuest& g, int page) {
    for (int i = 0; i < 4; ++i) {
        char buf[8] = {};
        std::snprintf(buf, sizeof buf, "Slot%d", page * 4 + i + 1);
        for (uint32_t b = 0; b < 8; ++b)
            g.write8(kSlotNameCells + 8u * static_cast<uint32_t>(i) + b, static_cast<uint8_t>(buf[b]));
    }
}

// New page: rewrite the folder names and ask the screen to rescan. The screen handler (message 10) rebuilds the slot
// list when bit 3 of the u16 at screen + 0x9c is set (the same bit the screen sets itself when it opens): it frees the
// entries and thumbnails and runs the scan again.
void changePage(IGuest& g, uint32_t screen, int delta, uint32_t sound) {
    if (g_quickPage || g_pages <= 1 || !namesUsable(g))
        return;
    const int p = ((g_page.load() + delta) % g_pages + g_pages) % g_pages;
    g_page.store(p);
    writeSlotNames(g, p);
    write16(g, screen + 0x9c, static_cast<uint16_t>(g.read16(screen + 0x9c) | 8u));
    if (sound)
        playSound(g, sound);
    std::fprintf(stderr, "[menus] save page %d/%d (folders Slot%d..Slot%d)\n", p + 1, g_pages, p * 4 + 1, p * 4 + 4);
}

bool isSlotScreen(IGuest& g, uint32_t scr) {
    if (!okPtr(scr))
        return false;
    // Save mode 1 (screen + 0x1e4, the save offered at a level change) shows the same slot grid and takes a slot
    // choice, so it gets the pager and the mouse hotspots like the pause-menu save.
    const uint32_t id = g.read32(scr + 0x18);
    return id == static_cast<uint32_t>(kScreenLoad) || id == static_cast<uint32_t>(kScreenSave);
}

// Key handler of the slot screens: PgDn / R1 (GUI key 0xd1) = next page, PgUp / L1 (Delete, 0xd3) = previous page.
void hookSlotKey(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    MGuest g(rdram, c, rt);
    const uint32_t scr = getRegU32(c, 4);
    if (isSlotScreen(g, scr) && g.read32(scr + 0xc0) != 0) {
        const uint32_t key = g.read32(scr + 0x50);
        if (key == 0x1cu && g.read32(scr + 0x18) == kScreenLoad) {
            const uint32_t selected = g.read32(scr + 0xa4), count = g.read32(scr + 0xc0),
                           entries = g.read32(scr + 0xb8);
            if (selected < count && selected < 4 && okPtr(entries) && g.read8(entries + selected * 0x168u) != 0) {
                g_slotClosing = true;
                std::lock_guard<std::mutex> lock(g_pagerMutex);
                g_pager.visible = false;
            }
        }
        if (key == static_cast<uint32_t>(kKeyNext) || key == static_cast<uint32_t>(kKeyPrev)) {
            changePage(g, scr, key == static_cast<uint32_t>(kKeyNext) ? 1 : -1, 0x2a);
            leave(c, 0);
            return;
        }
    }
    g_origKey(rdram, c, rt);
}

// "1 - Surdana": the caption number is the loop index; add the page offset so page 2 reads 5 - 8.
void hookSprintf(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t fmt = getRegU32(c, 5);
    if ((fmt == kFmtSlotLoad || fmt == kFmtSlotSave) && g_page.load() > 0)
        SET_GPR_U32(c, 6, getRegU32(c, 6) + static_cast<uint32_t>(g_page.load()) * 4u);
    g_origSprintf(rdram, c, rt);
}

// The title and pause menus grey out "Load Game" unless a save exists on the card: ask for every page, not just the
// visible one.
void hookAnySave(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (getRegU32(c, 4) != 0u || g_quickPage) {
        g_origAnySave(rdram, c, rt);
        return;
    }
    MGuest g(rdram, c, rt);
    if (!namesUsable(g)) {
        g_origAnySave(rdram, c, rt);
        return;
    }
    const int keep = g_page.load();
    uint32_t result = 0;
    bool first = true;
    for (int p = 0; p < g_pages; ++p) {
        const int q = (keep + p) % g_pages; // the visible page first
        writeSlotNames(g, q);
        const uint32_t r = g.callWith(g_origAnySave, GuestCall(kAnySave).arg(0));
        if (first)
            result = r;
        first = false;
        if (r == 1u) {
            result = 1u;
            break;
        }
    }
    // A card containing only the reserved quicksave must still enable Load Game.
    if (result != 1u) {
        writeSlotNames(g, 8);
        if (g.callWith(g_origAnySave, GuestCall(kAnySave).arg(0)) == 1u)
            result = 1u;
    }
    writeSlotNames(g, keep);
    leave(c, result);
}

// ---- pager layout and mouse targets; text is drawn on the presentation thread
// ------------------------------------------------------
int measure(MGuest& g, uint32_t str, int len) {
    return static_cast<int>(g.call(GuestCall(kFnTextWidth).arg(str).arg(0).arg(0).arg(static_cast<uint32_t>(len + 1))));
}

void drawPager(MGuest& g, uint32_t scr) {
    if (!isSlotScreen(g, scr))
        return;
    const uint32_t entries = g.read32(scr + 0xb8);
    const int count = static_cast<int>(g.read32(scr + 0xc0));
    if (count <= 0 || !okPtr(entries))
        return;
#if defined(PS2X_HAVE_GSR)
    // The four screenshots are photographic CT16 images, not pixel-art UI.
    // Register their decoded contents before the grid uploads/draws them.
    for (int i = 0; i < std::min(count, 4); ++i) {
        const uint32_t image = g.read32(entries + i * 0x168u + 0x164);
        if (!okPtr(image) || g.read32(image + 0x24) != 128 || g.read32(image + 0x28) != 128 ||
            g.read32(image + 0x38) != 16)
            continue;
        const uint32_t data = g.read32(image + 0x50);
        if (data < 0x100000u || data > PS2_RAM_SIZE - 128u * 128u * 2u)
            continue;
        std::vector<uint32_t> pixels(128 * 128);
        for (uint32_t n = 0; n < pixels.size(); ++n) {
            const uint32_t v = g.read16(data + n * 2);
            pixels[n] = ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) | 0x80000000u;
        }
        const uint64_t tag = gsr::textureContentHash(pixels.data(), 128, 128) ^ (uint64_t(g_page.load()) << 56) ^
                             drakan_save_previews::generation();
        if (g_thumbnailSeen[i] == tag)
            continue;
        g_thumbnailSeen[i] = tag;
        drakan_save_previews::registerSlot(g_page.load() * 4 + i + 1, pixels);
        for (int orientation = 0; orientation < 2; ++orientation) {
            for (uint32_t alpha : {0u, 0x80000000u}) {
                for (auto& p : pixels)
                    p = (p & 0xffffffu) | alpha;
                gsr::preferPhotographicTexture(gsr::textureContentHash(pixels.data(), 128, 128));
            }
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 128; ++x)
                    std::swap(pixels[y * 128 + x], pixels[(127 - y) * 128 + x]);
        }
    }
#endif
    const uint32_t so = g.read32(kGuiSizeObj);
    if (!okPtr(so))
        return;
    const int W = static_cast<int>(g.read32(so + 0xf8)), H = static_cast<int>(g.read32(so + 0xfc));
    if (W < 64 || H < 64)
        return;
    int lh = static_cast<int>(g.call(GuestCall(kFnLineHeight).arg(0)));
    if (lh < 8 || lh > 64)
        lh = 20;

    const Labels& lb = labels();
    const uint32_t sBack = g.scratch(0), sNext = g.scratch(0x40), sPage = g.scratch(0x80);
    char page[48];
    std::snprintf(page, sizeof page, "%s %d/%d", lb.page, g_page.load() + 1, g_pages);
    putStr(g, sBack, lb.back);
    putStr(g, sNext, lb.next);
    putStr(g, sPage, page);

    const int wBack = measure(g, sBack, static_cast<int>(std::strlen(lb.back)));
    const int wNext = measure(g, sNext, static_cast<int>(std::strlen(lb.next)));
    const int wPage = measure(g, sPage, static_cast<int>(std::strlen(page)));
    const int margin = std::max(24, (W - 0x198) / 2);
    // below the second row of slots (rows: (H - 0x10b) / 2 + row * 0x9a, caption at +0x72, details one line further)
    const int y0 = std::min(H - lh - 40, (H - 0x10b) / 2 + 0x9a + 0x72 + 2 * lh + 8);
    const int y = y0;
    const int xBack = margin, xNext = W - margin - wNext, xPage = (W - wPage) / 2;

    const int pad = 6;
    std::vector<drakan_input::Hotspot> hs;
    for (int i = 0; i < 4 && i < count; ++i) {
        drakan_input::Hotspot h;
        h.id = i;
        h.x = static_cast<int>(g.read32(entries + static_cast<uint32_t>(i) * 0x168u + 0x15c));
        h.y = static_cast<int>(g.read32(entries + static_cast<uint32_t>(i) * 0x168u + 0x160));
        h.w = 0xb8;
        h.h = 0x8c;
        hs.push_back(h);
    }
    hs.push_back({kHotBack, xBack - pad, y - 4, wBack + 2 * pad, lh + 8});
    hs.push_back({kHotNext, xNext - pad, y - 4, wNext + 2 * pad, lh + 8});
    drakan_input::module().menuMouseRw().setHotspots(scr, hs);
    g_hotspotScreen = scr;

    // Draw on every host present, outside the guest's alternating GS field /
    // draw-buffer state. Publishing input geometry remains on the EE thread.
    std::lock_guard<std::mutex> lock(g_pagerMutex);
    g_pager = {true, W, H, lh, xBack, xNext, xPage, y, labelUtf8(lb.back), labelUtf8(lb.next), labelUtf8(page)};
}

#if defined(PS2X_HAVE_DRAKAN_SETTINGS) || defined(PS2X_HAVE_PORTABLE_UI)
struct NativeOption {
    const char* key;
    uint32_t offset;
    int lo, hi;
    bool real = false;
};
// The twelve serialized options in the retail table at 0x54ba00. Never persist
// the options object wholesale: it also contains guest pointers and allocations.
constexpr NativeOption kNativeOptions[] = {{"sensitivity", 0x78, 30, 200, true},
                                           {"vibrate", 0x6c, 0, 1},
                                           {"subtitles", 0x70, 0, 1},
                                           {"stereo", 0x74, 0, 1},
                                           {"auto_mount_dragon", 0x60, 0, 1},
                                           {"invert_y", 0x64, 0, 1},
                                           {"controller_scheme", 0x2e8, 0, 2},
                                           {"sound_volume", 0x8c, 0, 100},
                                           {"music_volume", 0x90, 0, 100},
                                           {"difficulty", 0x94, 0, 2},
                                           {"screen_x_offset", 0x2ec, -128, 128},
                                           {"screen_y_offset", 0x2f0, -128, 128}};
std::array<uint32_t, 12> g_savedOptions{};
bool g_optionsReady = false;
constexpr const char* kGuestOptionNames[] = {"Sensitivity",     "Vibrate",    "Subtitles",        "Stereo",
                                             "AutoMountDragon", "FlipYAxis",  "JoyConfig_Scheme", "SoundVolume",
                                             "MusicVolume",     "Difficulty", "ScreenXOffset",    "ScreenYOffset"};

bool validOption(const NativeOption& opt, const std::string& value) {
    char* end = nullptr;
    const double n = std::strtod(value.c_str(), &end);
    const double low = opt.real ? opt.lo * 0.01 : opt.lo, high = opt.real ? opt.hi * 0.01 : opt.hi;
    return end != value.c_str() && !*end && std::isfinite(n) && n >= low && n <= high &&
           (opt.real || n == std::floor(n));
}

bool writeGuestOptions(const dsettings::IniDoc& ini) {
    namespace fs = std::filesystem;
    const auto& paths = PS2Runtime::getIoPaths();
    const fs::path root = paths.mcRoot.empty() ? paths.elfDirectory / "mc0" : paths.mcRoot;
    if (root.empty())
        return false;
    const fs::path file = root / (std::string(drakan_region::build().saveId) + "Settings") / "Drakan 2.cfg";
    const auto io = dsettings::FileIO::native();
    std::string old;
    io.read(file.string(), old);
    // Preserve any native button bindings and unknown settings in an existing
    // options file. Replace only the twelve fields exposed by these menus.
    std::istringstream input(old);
    std::string output = old.empty() ? "// PC automatically saved options\nVersion 278\n" : "", line;
    while (std::getline(input, line)) {
        std::istringstream parts(line);
        std::string verb, key;
        parts >> verb >> key;
        bool ours = false;
        if (verb == "set")
            for (const char* name : kGuestOptionNames)
                if (key == name)
                    ours = true;
        if (!ours)
            output += line + "\n";
    }
    for (size_t i = 0; i < g_savedOptions.size(); ++i) {
        const auto& opt = kNativeOptions[i];
        const std::string value = ini.get("options", opt.key);
        if (!validOption(opt, value))
            return false;
        output += "set " + std::string(kGuestOptionNames[i]) + " \"" + value + "\"\n";
    }
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    return !error && io.write(file.string(), output);
}

// drakan_native_options.ini next to the exe; DRAKAN_NATIVE_OPTIONS_INI=<path> moves it (test runs that change options
// keep the player's file untouched).
std::string nativeOptionsPath() {
    const char* e = std::getenv("DRAKAN_NATIVE_OPTIONS_INI");
    return e && *e ? std::string(e) : drakan_cfg::exeDir() + "drakan_native_options.ini";
}

void installNativeOptions() {
    std::string text;
    if (dsettings::FileIO::native().read(nativeOptionsPath(), text) &&
        writeGuestOptions(dsettings::IniDoc::parse(text)))
        std::fprintf(stderr, "[menus] native options prepared for the game's normal startup loader\n");
}

void nativeOptionsTick(MGuest& g, int screen) {
    if (!g_optionsReady && screen != kScreenTitle)
        return;
    const std::string path = nativeOptionsPath();
    const auto io = dsettings::FileIO::native();
    if (!g_optionsReady) {
        // The native startup loader already restored and applied these values.
        // Never invoke the blocking audio/controller apply routine from a
        // nested input hook: its semaphore waits need the real guest scheduler.
        for (size_t i = 0; i < g_savedOptions.size(); ++i)
            g_savedOptions[i] = g.read32(0x4fb6d0u + kNativeOptions[i].offset);
        g_optionsReady = true;
    }
    std::array<uint32_t, 12> current;
    for (size_t i = 0; i < current.size(); ++i)
        current[i] = g.read32(0x4fb6d0u + kNativeOptions[i].offset);
    if (current == g_savedOptions)
        return;
    auto ini = dsettings::IniDoc::parse("");
    for (size_t i = 0; i < current.size(); ++i) {
        const auto& opt = kNativeOptions[i];
        std::string value;
        if (opt.real) {
            float f;
            std::memcpy(&f, &current[i], 4);
            if (!std::isfinite(f))
                return;
            char b[32];
            std::snprintf(b, sizeof b, "%.9g", f);
            value = b;
        } else
            value = std::to_string(static_cast<int32_t>(current[i]));
        ini.set("options", opt.key, value);
    }
    if (writeGuestOptions(ini) && io.write(path, ini.serialize())) {
        g_savedOptions = current;
        std::fprintf(stderr, "[menus] native options automatically saved\n");
    }
}
#endif

void optionsLayout(MGuest& g, uint32_t scr) {
    const uint32_t save = findWidget(g, scr, 0x2343);
    if (!save)
        return;
    vcall(g, save, 0x54, 0x6c, 0, 1);
    vcall(g, save, 0x54, 0x64, 0, 1);
    constexpr uint16_t ids[] = {0x2341, 0x2342, 0x2345, 0x2344, 0x2346};
    const uint32_t first = findWidget(g, scr, ids[0]), second = findWidget(g, scr, ids[1]);
    const uint32_t so = g.read32(kGuiSizeObj);
    if (!first || !second || !okPtr(so))
        return;
    const int step = static_cast<int16_t>(g.read16(second + 0x1e)) - static_cast<int16_t>(g.read16(first + 0x1e));
    if (step < 16 || step > 100)
        return;
    const int top = (static_cast<int>(g.read32(so + 0xfc)) - (5 * step - 15)) / 2;
    for (int i = 0; i < 5; ++i)
        if (const uint32_t w = findWidget(g, scr, ids[i]))
            write16(g, w + 0x1e, static_cast<uint16_t>(top + i * step));
}

// Mouse: slot rectangles (hover = select, click = select + Cross) and the two page buttons.
void onHotspot(IGuest& g, int id, drakan_input::HotspotEvent ev) {
    const uint32_t scr = g.read32(kActiveScreenVar);
    if (!isSlotScreen(g, scr))
        return;
    if (id == kHotBack || id == kHotNext) {
        if (ev == drakan_input::HotspotEvent::Press)
            changePage(g, scr, id == kHotNext ? 1 : -1, 0x24);
        return;
    }
    if (id < 0 || id > 3 || id >= static_cast<int>(g.read32(scr + 0xc0)))
        return;
    if (static_cast<int>(g.read32(scr + 0xa4)) != id) {
        g.write32(scr + 0xa4, static_cast<uint32_t>(id));
        playSound(g, 0x2a);
    }
    if (ev == drakan_input::HotspotEvent::Press)
        drakan_input::module().injectPadTap(drakan_input::Pad::CROSS, 1);
}

// Test aid (DRAKAN_TEST_LEVEL_SAVE=1): every save screen opens in mode 1, the save offered at a level change. The level
// exit (0x361f60) builds the request {1, empty text, its callback node}; here the node is null, which the screen checks
// before every use. Lets a harness run reach the level-change save from the pause menu's Save Game instead of playing
// a level to its end (scripts/harness/recipes/level_save_mouse.txt).
void hookSaveCtor(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t req = kReqPage + 0x100;
    const uint32_t mode = 1, text = req + 12, node = 0;
    std::memcpy(P(req), &mode, 4);
    std::memcpy(P(req + 4), &text, 4);
    std::memcpy(P(req + 8), &node, 4);
    std::memset(P(text), 0, 4);
    std::fprintf(stderr, "[menus] save screen opened in level-change mode (DRAKAN_TEST_LEVEL_SAVE)\n");
    SET_GPR_U32(c, 7, req);
    g_origSaveCtor(rdram, c, rt);
}

// ---- title menu: Quit
// ---------------------------------------------------------------------------------------------------------------
void requestQuit(PS2Runtime* rt) {
    std::fprintf(stderr, "[menus] title menu Quit confirmed: closing the game\n");
#if defined(PS2X_HAVE_GSR)
    drakan_win::requestClose();
#else
    rt->requestStop();
#endif
}

// After the stock message 3 (screen opened): the table at 0x54e768 already lists Quit (0x2303) as the fifth row below
// Credits and positions it, then the handler hides and disables it (widget vslots +0x6c / +0x64 with 0). Undo that,
// move the widget to the end of the child list (it is the second one in the layout, so the focus chain would visit it
// after Load Game) and centre the five rows.
void titleOpened(IGuest& g, uint32_t scr) {
    const uint32_t q = findWidget(g, scr, static_cast<uint16_t>(kWidgetQuit));
    if (!q)
        return;
    vcall(g, q, 0x54, 0x6c, 1, 1);
    vcall(g, q, 0x54, 0x64, 1, 1);
    // relink to the end of the child list (widget +0 = previous, +4 = next; screen +0x7c = first, +0x80 = last)
    if (g.read32(q + 4) != 0) {
        const uint32_t prev = g.read32(q), next = g.read32(q + 4);
        if (prev)
            g.write32(prev + 4, next);
        else
            g.write32(scr + 0x7c, next);
        g.write32(next, prev); // next's previous pointer
        const uint32_t last = g.read32(scr + 0x80);
        g.write32(last + 4, q);
        g.write32(q, last);
        g.write32(q + 4, 0);
        g.write32(scr + 0x80, q);
    }
    // centre five rows instead of four (step = row distance, the stock code centred four rows)
    static const uint16_t ids[5] = {0x2302, 0x2304, 0x2305, 0x2306, 0x2303};
    const uint32_t wCredits = findWidget(g, scr, ids[3]);
    if (wCredits) {
        const int step = static_cast<int16_t>(g.read16(q + 0x1e)) - static_cast<int16_t>(g.read16(wCredits + 0x1e));
        if (step > 4 && step < 200)
            for (uint16_t id : ids)
                if (const uint32_t w = findWidget(g, scr, id))
                    write16(g, w + 0x1e, static_cast<uint16_t>(static_cast<int16_t>(g.read16(w + 0x1e)) - step / 2));
    }
}

void hookTitle(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t scr = getRegU32(c, 4), widget = getRegU32(c, 5), msg = getRegU32(c, 6);
    uint32_t wid = 0;
    if ((msg == 1u || msg == 0x10u) && okPtr(widget))
        wid = rd16(widget + 0x14);
    if (msg == 1u && wid == kWidgetQuit) {
        MGuest g(rdram, c, rt);
        g.call(GuestCall(kFnSetFocus).arg(scr).arg(0)); // as the pause menu does before its quit box
        g_quitBoxScreen = scr;
        // The stock helper (0x427680, property 0x14a) fetches its text from an XDB group that is not resident on the
        // title screen and then silently creates nothing. Build the same box request ourselves ({text, default widget
        // (No), -1, -1.0f}) in an unused low RAM page and tail-call the screen factory (0x2c4458) on the real context:
        // it blocks on the allocator semaphore, which a nested call on a copied context cannot survive (the
        // thread-switch unwind). The activation message is queued and its result ignored.
        const uint32_t req = kReqPage, txt = kReqPage + 0x40;
        putStr(g, txt, labels().quitPrompt);
        g.write32(req, txt);
        g.write32(req + 4, 0x238du);
        g.write32(req + 8, 0xffffffffu);
        g.write32(req + 12, 0xbf800000u);
        SET_GPR_U32(c, 4, kScreenManager);
        SET_GPR_U32(c, 5, 1);
        SET_GPR_U32(c, 6, scr);
        SET_GPR_U32(c, 7, req);
        SET_GPR_U32(c, 8, 1);
        c->pc = kFnScreenFactory;
        return;
    }
    if (msg == 0x10u && g_quitBoxScreen == scr && wid != 0) {
        g_quitBoxScreen = 0;
        if (wid == kWidgetYes)
            requestQuit(rt);
        else {
            MGuest g(rdram, c, rt);
            if (const uint32_t q = findWidget(g, scr, static_cast<uint16_t>(kWidgetQuit)))
                g.call(GuestCall(kFnSetFocus)
                           .arg(scr)
                           .arg(q)); // back to the Quit row, like the pause menu returns to its first row
        }
        leave(c, 0);
        return;
    }
    // The stock message 3 handler can yield (it waits for the font) and then resumes without passing through this
    // wrapper again, so the post-processing cannot hang on its return: it runs once per screen object on the first
    // message after the screen was opened.
    if (msg == 3u)
        g_titleDone = 0;
    g_origTitle(rdram, c, rt);
    if (msg != 3u && g_titleDone != scr && okPtr(scr)) {
        g_titleDone = scr;
        MGuest g(rdram, c, rt);
        titleOpened(g, scr);
    }
}

} // namespace

int page() {
    return g_page.load();
}
float loadingGroupOffset(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    MGuest g(ram, ctx, rt);
    const uint32_t self = GPR_U32(ctx, 4) & 0x1fffffffu;
    if (!okPtr(self) || self > PS2_RAM_SIZE - 640)
        return 0;
    const uint32_t state = g.read32(0x5565d4u);
    const bool alternate = g.read32(self + 0xa8) == 3 || (okPtr(state) && ((g.read32(state + 0x4c4) >> 3) & 1));
    const uint32_t text = self + (alternate ? 0x1acu : 0xacu);
    unsigned length = 0;
    while (length < 127 && g.read8(text + length))
        ++length;
    const uint32_t size = g.read32(kGuiSizeObj);
    if (!okPtr(size))
        return 0;
    const float width = static_cast<float>(g.call(GuestCall(kFnTextWidth).arg(text).arg(1).arg(0).arg(length + 1)));
    g_loadingLabelX = int(g.read32(size + 0xf8)) - 15 - 178 - 10 - int(width);
    // Retail bar: two floor(128*.7) halves, right margin 15; label gap 10.
    return g.read32(size + 0xf8) * .5f - 15.0f - 89.0f - 5.0f - width * .5f;
}
int pageCount() {
    return g_pages;
}
Pager pager() {
    std::lock_guard<std::mutex> lock(g_pagerMutex);
    return g_pager;
}

void tick(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    MGuest g(ram, ctx, rt);
    const uint32_t scr = g.read32(kActiveScreenVar);
    const int id = okPtr(scr) ? static_cast<int>(g.read32(scr + 0x18)) : -1;
#if defined(PS2X_HAVE_DRAKAN_SETTINGS) || defined(PS2X_HAVE_PORTABLE_UI)
    nativeOptionsTick(g, id);
#endif
    if (id == 9)
        optionsLayout(g, scr);
    const bool slots = isSlotScreen(g, scr);
    if (!slots) {
        g_slotClosing = false;
        g_thumbnailSeen.fill(0);
    }
    if (!g_quickPage && g_pages > 1 && slots && !g_slotClosing && !g_slotsHosted && namesUsable(g))
        drawPager(g, scr);
    else {
        if (g_slotsHosted && g_hotspotScreen) { // the host screen takes the mouse: no native slot / page hotspots
            drakan_input::module().menuMouseRw().setHotspots(g_hotspotScreen, {});
            g_hotspotScreen = 0;
        }
        std::lock_guard<std::mutex> lock(g_pagerMutex);
        g_pager.visible = false;
    }
}

void setSlotsHosted(bool on) {
    g_slotsHosted = on;
}

bool beginQuickPage(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt) {
    MGuest g(ram, ctx, rt);
    if (g_quickPage || !namesUsable(g))
        return false;
    g_quickPreviousPage = g_page.load();
    g_quickPage = true;
    g_page.store(8);
    writeSlotNames(g, 8);
    return true;
}

void endQuickPage(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt, uint32_t screen) {
    if (!g_quickPage)
        return;
    MGuest g(ram, ctx, rt);
    g_page.store(g_quickPreviousPage);
    writeSlotNames(g, g_quickPreviousPage);
    g_quickPage = false;
    if (isSlotScreen(g, screen))
        write16(g, screen + 0x9c, static_cast<uint16_t>(g.read16(screen + 0x9c) | 8u));
}

bool showPage(uint8_t* ram, R5900Context* ctx, PS2Runtime* rt, uint32_t screen, int p) {
    MGuest g(ram, ctx, rt);
    if (g_pages <= 1 || !namesUsable(g) || !isSlotScreen(g, screen))
        return false;
    p = std::clamp(p, 0, g_pages - 1);
    if (p != g_page.load())
        changePage(g, screen, p - g_page.load(), 0);
    return true;
}

bool titleQuit() {
    return g_titleQuit;
}

// Selected labels use the same HD atlas/metrics as their unselected rows.
// A warm gold gradient keeps focus visible without the retail 128px fire
// intermediate, whose different raster face produces blurred, uneven letters.
void crispSelectedText(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t widget = GPR_U32(c, 4) & 0x1fffffffu;
    const uint32_t parent = okPtr(widget) ? rd32(widget + 8) : 0;
    const int screen = okPtr(parent) ? int(rd32(parent + 0x18)) : -1;
    const bool menu = screen == 1 || screen == 2 || screen == 3 || screen == 4 || screen == 5 || screen == 8 ||
                      screen == 9 || screen == 10 || screen == 13 || screen == 23 || screen == 27 || screen == 28;
    const bool selected = c->pc == 0x2c8728u && menu && parent == rd32(kActiveScreenVar) &&
                          (rd32(widget + 0x10) & 0x40u) && rd32(widget + 0x40) && rd32(widget + 0x5c);
    if (!selected) {
        g_origWidgetText(ram, c, rt);
        return;
    }
    const uint32_t flags = rd32(widget + 0x10), direct = flags & ~0x40u;
    uint32_t colors[4];
    std::memcpy(colors, ram + widget + 0x64, sizeof colors);
    const uint32_t gold[4] = {0x803c6c7fu, 0x803c6c7fu, 0x80123860u, 0x80123860u};
    std::memcpy(ram + widget + 0x10, &direct, 4);
    std::memcpy(ram + widget + 0x64, gold, sizeof gold);
    // The widget is edited only for the duration of this call. A scheduler checkpoint inside it would unwind the host
    // stack: this wrapper would return and restore the flag/colours while the guest resumes the original function
    // mid-way, which then takes the retail fire-text path for that frame (a flicker every time the title menu ran
    // faster than the original cadence). Run the call to completion instead.
    ps2xNoPreemptEnter();
    g_origWidgetText(ram, c, rt);
    ps2xNoPreemptLeave();
    std::memcpy(ram + widget + 0x64, colors, sizeof colors);
    const uint32_t restored = rd32(widget + 0x10) | (flags & 0x40u);
    std::memcpy(ram + widget + 0x10, &restored, 4);
}

void install(PS2Runtime& rt) {
    g_ram = rt.memory().getRDRAM();
#if defined(PS2X_HAVE_DRAKAN_SETTINGS) || defined(PS2X_HAVE_PORTABLE_UI)
    installNativeOptions();
#endif
    g_pages = std::clamp(drakan_cfg::getInt("general", "save_pages", "DRAKAN_SAVE_PAGES", 4), 1, 8);
    g_titleQuit = drakan_cfg::getBool("general", "title_quit", "DRAKAN_TITLE_QUIT", true);
    auto hook = [&](uint32_t addr, PS2Runtime::RecompiledFunction& orig, PS2Runtime::RecompiledFunction fn,
                    const char* name) {
        if (!rt.hasFunction(addr)) {
            std::fprintf(stderr, "[menus] %s (0x%x) not found, feature disabled\n", name, addr);
            return;
        }
        orig = rt.lookupFunction(addr);
        rt.replaceFunction(addr, fn);
    };
    if (g_pages > 1) {
        hook(kSlotKeyHandler, g_origKey, &hookSlotKey, "slot key handler");
        hook(kSprintf, g_origSprintf, &hookSprintf, "sprintf");
        drakan_input::module().menuMouseRw().setHotspotHandler(&onHotspot);
    }
    hook(kAnySave, g_origAnySave, &hookAnySave, "save exists check");
    hook(0x2c8728u, g_origWidgetText, &crispSelectedText, "selected menu typography");
    hook(0x286170u, g_origText, &loadingText, "loading font");
    hook(kFnTextWidth, g_origTextWidth, &loadingWidth, "loading font metrics");
    if (g_titleQuit)
        hook(kTitleHandler, g_origTitle, &hookTitle, "title handler");
    if (const char* levelSave = std::getenv("DRAKAN_TEST_LEVEL_SAVE"); levelSave && *levelSave == '1')
        hook(kSaveScreenCtor, g_origSaveCtor, &hookSaveCtor, "save screen (level-change mode)");
    std::fprintf(stderr, "[menus] save pages: %d, title Quit: %s\n", g_pages, g_titleQuit ? "on" : "off");
}

} // namespace drakan_menus

#else // no input module: nothing to hook into (the menus need MenuMouse / GuestInterface)

namespace drakan_menus {
void install(PS2Runtime&) {}
void tick(uint8_t*, R5900Context*, PS2Runtime*) {}
float loadingGroupOffset(uint8_t*, R5900Context*, PS2Runtime*) {
    return 0;
}
Pager pager() {
    return {};
}
int page() {
    return 0;
}
int pageCount() {
    return 1;
}
void setSlotsHosted(bool) {}
bool beginQuickPage(uint8_t*, R5900Context*, PS2Runtime*) {
    return false;
}
void endQuickPage(uint8_t*, R5900Context*, PS2Runtime*, uint32_t) {}
bool showPage(uint8_t*, R5900Context*, PS2Runtime*, uint32_t, int) {
    return false;
}
bool titleQuit() {
    return true;
}
} // namespace drakan_menus

#endif
