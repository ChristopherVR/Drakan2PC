// MenuMouse: mouse support for the game's own GUI screens (title menu, Options, pause menu, load / save, message boxes
// ...).
//
// The game is a PC-lineage engine: every GUI screen (layout resource type 0x600) is a window object with a list of
// widget objects, and the screen manager ships the PC mouse path (hit test, focus on press, press / release on the
// widget, activation message to the screen handler). On the PS2 nothing ever feeds it. This module feeds it, from the
// host mouse, and adds what the PC build did not have: hover moves the highlight (the same focus change the D-pad does,
// with the game's own menu tick sound).
//
// Everything works on guest memory through IGuest, so the logic is independent of the Win32 host and of PS2Recomp; the
// host only has to supply the cursor in client pixels and the rectangle of the game image inside the window (letterbox
// / pillarbox / widescreen). Reverse engineering notes (SCES_500.06): docs/reference/mouse-menus.md.
#pragma once
#include "input/GuestInterface.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace drakan_input {

struct Config;

// A rectangle (GUI units, 512 x 512 space) the host registers for a screen that has no widgets of its own to click: the
// slot grid and the page buttons of the load / save screens (drakan_menus.cpp). `id` is opaque to MenuMouse.
struct Hotspot {
    int id = 0;
    int x = 0, y = 0, w = 0, h = 0;
};
enum class HotspotEvent {
    Hover,
    Press
}; // Hover: the pointer moved onto the hotspot; Press: left button went down over it
using HotspotHandler = std::function<void(IGuest&, int id, HotspotEvent ev)>;

// Where the game image sits inside the window client area, in client pixels. valid == false: the image fills the client
// area.
struct MenuView {
    float x = 0, y = 0, w = 0, h = 0;
    float hudAnchorX = 0.5f; // normalized origin of the affine UI transform
    float hudScaleX =
        1.0f; // UI sprites are compressed about the image centre by this factor (gsr hud_aspect_correct, widescreen)
    bool valid = false;
    // Authoritative renderer metadata: scale actually applied to the whole 3D inventory page.
    // Independent of HUD sprite correction; 1 means inventory geometry was not transformed.
    // Backend/host bridge must publish this from the presented UI scope, not the user setting.
    float inventoryPageScaleX = 1.0f;
};

// One host sample per game frame (cursor in client pixels, buttons as MouseBtn bits).
struct MenuMouseSample {
    bool focused = true;
    bool inside = false;  // cursor inside the client area
    float cx = 0, cy = 0; // client pixels
    float clientW = 0, clientH = 0;
    uint8_t down = 0, pressed = 0, released = 0;
    int wheel = 0; // notches, + = away from the user
};

struct MenuMouseAddrs {
    uint32_t activeScreenVar = 0x4fb60c;   // *(u32*) -> active GUI screen (window object)
    uint32_t screenObjVar = 0x5565c8;      // *(u32*)+0xf8/+0xfc = GUI coordinate space size (512 x 512)
    uint32_t ctxVar = 0x5565d4;            // game context (+0x32c = sound manager, +0x320 = UI property table)
    uint32_t mouseCallback = 0x2cc7c0;     // GUI mouse handler (type, button id, x, y)
    uint32_t uiEntry = 0x2bec48;           // () -> UI property table entry
    uint32_t capturedWidgetVar = 0x4fb19c; // widget that owns the pressed button
    uint32_t soundPos = 0x4e8c28;          // 3-vector the game passes to its UI sounds
};

// Pad bits (drakan_input::Pad) the module wants tapped for the next frame (wheel -> D-pad, click on a screen without
// widgets -> Cross).
struct MenuMouseResult {
    uint16_t padTap = 0;
};

// The inventory / map screen (screen id 0x15) is a GUI window with ONE full-screen image widget; the five sections (0
// Equipment, 1 Spells, 2 Skills, 3 Map, 4 Journal; current index = screen + 0x154) are drawn by their own code in 3D
// and navigate with the D-pad / L1 / R1 only (docs/reference/mouse-menus.md section 8). Equipment uses the projected
// native 3D grid. Selection is requested through a native pad navigation event and the native selector hook; activation
// waits for observed selection. Captured native category boxes queue L1/R1 navigation on all sections. Map uses fresh
// submitted native marker bounds and acknowledged arrow selection. Remaining section contents retain relative
// navigation; uncertified clicks do nothing.
constexpr int kInventoryScreenId = 0x15;

struct MenuMouseOptions {
    bool inventoryMouse = true;
    bool inventoryClickSquare =
        true; // Equipment: left click = Square (use / equip; Cross is "hot slot"); false = Cross everywhere
};

class MenuMouse {
public:
    void setOptions(const MenuMouseOptions& o) { opt_ = o; }
    int inventorySection() const { return invSection_.load(); } // -1 when the inventory is not the active screen
    explicit MenuMouse(const MenuMouseAddrs& a = MenuMouseAddrs()) : a_(a) {}
    void setAddrs(const MenuMouseAddrs& a) { a_ = a; }

    // EE thread, once per frame after PadMgr_Update. Does nothing (and drops pending presses) while there is no active
    // GUI screen.
    MenuMouseResult update(IGuest& g, const MenuMouseSample& s, const MenuView& view, float idleHideMs);

    // Thread-safe state for the overlay that draws the cursor.
    bool cursorVisible() const { return visible_.load(); }
    int activeScreenId() const { return screenId_.load(); } // -1 = none
    // Cancel a press in progress (overlay opened, window lost focus ...).
    void reset();

    // ---- hotspots (EE thread) ----
    // The host publishes the rectangles of the active screen every frame (setHotspots); they are used while they are at
    // most a few updates old and `screen` is still the active screen. Over a hotspot the normal widget logic is skipped
    // (the screens that use this have no focusable widgets). The handler runs inside update() on the EE thread: Hover
    // when the pointer moved onto a hotspot, Press on a left click.
    void setHotspotHandler(HotspotHandler h);
    void setHotspots(uint32_t screen, const std::vector<Hotspot>& list);
    int hoveredHotspot() const { return hotHover_.load(); } // id under the pointer, -1 = none
    // The game's UI sound `property` (0x24 click, 0x2a tick) through the sound manager.
    void playSound(IGuest& g, uint32_t property) { playUiSound(g, property); }

    // ---- pure helpers (unit tests) ----
    // Client pixels -> GUI coordinates (screenW x screenH). Returns false when the point is outside the game image.
    static bool mapToGui(const MenuMouseSample& s, const MenuView& v, int screenW, int screenH, int* gx, int* gy);

    // EE thread, from native Equipment grid draw (437170), while its camera is installed.
    // Read-only projection; never calls guest code or changes the selected item directly.
    void captureInventoryGrid(IGuest& g, uint32_t section);
    // EE thread, at native 42b308(iconRecord, drawParameters) entry while the
    // identity UI camera is installed. Reads the icon background's certified
    // cached box; no guest calls or hardcoded screen-space hit rectangles.
    void captureInventoryCategory(IGuest& g, uint32_t iconRecord);
    // EE draw thread only; validates the existing hit capture, never navigates.
    bool inventoryCategoryHovered(IGuest& g, uint32_t iconRecord) const;
    // Native 33eaf8 selector hook: replace its destination only for an outstanding mouse request.
    int takeInventorySelection(IGuest& g, uint32_t controller);
    // Native Map draw start, then only the image marker's submitted GS vertices.
    // No guest writes/calls; capture coordinates before host page mapping.
    void beginInventoryMap(IGuest& g, uint32_t section);
    void captureInventoryMapMarker(IGuest& g, uint32_t section, unsigned index, uint32_t image, uint32_t vertices,
                                   unsigned count);
    using MapStateQuery = std::function<bool(IGuest&, uint32_t, int&)>;
    void setMapStateQuery(MapStateQuery query) { mapStateQuery_ = std::move(query); }
    // EE thread, at native Spells cell draw 4481e8(section) entry: project the 5x2 spell cells (known spells only).
    void captureInventorySpells(IGuest& g, uint32_t section);
    // Native 3422c8(controller, index, commit) selector hook: the requested spell for an outstanding mouse request
    // (only for navigation, commit == 0), else -1.
    int takeSpellSelection(IGuest& g, uint32_t controller, uint32_t commit);
    // EE thread, at native Skills text draw 445b78(section) entry: project the three skill rows (label and level).
    void captureInventorySkills(IGuest& g, uint32_t section);
    // Journal (section 4): page clip rectangle (local viewport units) and its arrow glyphs, drawn between
    // beginInventoryJournal and the next update. Glyph: 0 previous region, 1 next region, 2 scroll up, 3 scroll down.
    void beginInventoryJournal(IGuest& g, uint32_t section);
    void captureJournalPage(IGuest& g, uint32_t clipRect);
    void captureJournalGlyph(IGuest& g, unsigned glyph, int x, int y);
    // Shop list (shop mode section 2): 44a678(section) entry starts a frame; each drawn stock item reports its list
    // index, cell size and drawn {column, row} (434e80 / 4380b0 from 44a678).
    void beginInventoryShop(IGuest& g, uint32_t section);
    void captureShopItem(IGuest& g, uint32_t section, unsigned index, unsigned width, unsigned height, float column,
                         float row);
    // Native 33e960(controller, index, buy) selector hook: the requested stock index for an outstanding mouse request
    // (navigation only, buy == 0), else -1.
    int takeShopSelection(IGuest& g, uint32_t controller, uint32_t buy);
    // EE thread: an Order-of-the-Flame style view of the inventory screen is up (0 none, 1 full-screen map, 2 Equipment
    // strip; drakan_inventory_views.inl). Its tabs are hidden (no category hits), the wheel does not switch sections,
    // and the renderer centres its page on the screen (inventoryPageScaleX about the centre, not the left edge).
    void setInventoryView(int view) { inventoryView_ = view; }
    int inventoryView() const { return inventoryView_; }
    // Section-plane point -> native GUI pixels through the camera installed right now (projectPlane).
    bool projectInventoryPlane(IGuest& g, uint32_t transform, float x, float y, float& sx, float& sy) const;

    // Last values, for logging / tests.
    struct Debug {
        int gx = -1, gy = -1;
        uint32_t hover = 0, focus = 0, pressed = 0;
        int focusMoves = 0, presses = 0, releases = 0, taps = 0;
        int inventoryCell = -1, inventoryTarget = -1;
        unsigned inventoryCaptures = 0;
        unsigned inventoryClickSerial = 0, clickGridAge = 0;
        MenuMouseSample clickSample;
        MenuView clickView;
        int clickW = 0, clickH = 0, clickRawCell = -1, clickItem = -1, clickSelected = -1;
        bool clickInGui = false, clickAbsoluteGrid = false;
        float gridLeft = 0, gridTop = 0, gridRight = 0, gridBottom = 0;
        unsigned categoryCaptures = 0;
        int capturedCategory = -1, categoryHit = -1, categoryTarget = -1;
        float categoryLeft = 0, categoryTop = 0, categoryRight = 0, categoryBottom = 0;
        int mapHit = -1, mapTarget = -1;
        unsigned mapCaptures = 0, mapAge = 100;
        // Section content (Spells, Skills, Journal): last capture and pointer state.
        int contentKind = 0, contentHit = -1, contentTarget = -1;
        unsigned contentCaptures = 0, contentItems = 0;
        float contentLeft = 0, contentTop = 0, contentRight = 0, contentBottom = 0;
    };
    const Debug& debug() const { return dbg_; }

private:
    uint32_t hitTest(IGuest& g, uint32_t screen, int gx, int gy) const;
    bool canFocus(IGuest& g, uint32_t widget) const;
    bool insideLabel(IGuest& g, uint32_t widget, int gx, int x, int w) const;
    bool hasFocusable(IGuest& g, uint32_t screen) const;
    void setFocus(IGuest& g, uint32_t screen, uint32_t widget);
    void playUiSound(IGuest& g, uint32_t property);
    bool isChild(IGuest& g, uint32_t screen, uint32_t widget) const;
    void mouseEvent(IGuest& g, uint32_t type, uint32_t button, int gx, int gy);

    MenuMouseResult updateInventory(IGuest& g, uint32_t scr, const MenuMouseSample& s, const MenuView& view,
                                    bool moved);
    struct GridPoint {
        float x = 0, y = 0;
    };
    int inventoryView_ = 0;
    GridPoint inventoryCells_[24][4]{};
    uint32_t gridScreen_ = 0, gridController_ = 0;
    unsigned gridAge_ = 100;
    int inventoryTarget_ = -1, inventoryWait_ = 0;
    uint32_t inventoryItem_ = 0;
    bool inventoryActivate_ = false;
    int inventoryOrigin(IGuest& g, int cell) const;
    int inventoryHit(int x, int y) const;
    struct CategoryRect {
        float left = 0, top = 0, right = 0, bottom = 0;
        uint32_t record = 0, node = 0;
        unsigned age = 100;
    };
    CategoryRect categories_[5]{};
    uint32_t categoryScreen_ = 0;
    int categoryTarget_ = -1, categoryWait_ = 0;
    struct MapRect {
        float left = 0, top = 0, right = 0, bottom = 0;
        unsigned index = 0;
        uint32_t image = 0;
        uint32_t imageVtable = 0, imageWidth = 0, imageHeight = 0;
        uint64_t identity = 0;
    };
    std::vector<MapRect> mapRects_;
    uint32_t mapScreen_ = 0, mapSection_ = 0, mapTable_ = 0, mapCount_ = 0, mapRegion_ = 0;
    unsigned mapAge_ = 100, mapWait_ = 0;
    int mapTarget_ = -1;
    uint64_t mapTargetIdentity_ = 0;
    float mapRequestX_ = 0, mapRequestY_ = 0;
    uint32_t mapLastSelected_ = 0;
    unsigned mapStall_ = 0;
    MapStateQuery mapStateQuery_;
    bool mapCurrent(IGuest& g, uint32_t scr) const;
    uint64_t mapIdentity(IGuest& g, unsigned index) const;
    MenuMouseResult updateMap(IGuest& g, uint32_t scr, const MenuMouseSample& s, bool inGui, int gx, int gy);
    // Inventory plane point (section-plane units) through native 42ab78/42ac68 projection into absolute GUI units, with
    // the draw camera that is installed right now. False for points at or behind the camera.
    bool projectPlane(IGuest& g, uint32_t transform, float x, float y, GridPoint& out) const;
    static bool insideQuad(const GridPoint (&quad)[4], float x, float y);
    // Captured clickable content of the current inventory section (Spells cells, Skills rows, Journal arrows / page).
    enum class Content { None, Spells, Skills, Journal, Shop };
    struct ContentItem {
        GridPoint quad[4];
        int index = 0;
    };
    Content contentKind_ = Content::None;
    std::vector<ContentItem> content_;
    unsigned contentCount_ = 0; // items the section has (all drawn = no scrolling)
    uint32_t contentScreen_ = 0, contentSection_ = 0, contentController_ = 0;
    unsigned contentAge_ = 100;
    int contentSectionIndex_ = -1;
    int contentTarget_ = -1, contentWait_ = 0;
    uint16_t contentAction_ = 0; // pad tap once the native selection equals contentTarget_
    // Journal page clip (wheel scrolls inside it); glyph rectangles are content_ items 0..3 by index.
    bool journalPage_ = false;
    GridPoint journalPageQuad_[4]{};
    void startContent(Content kind, uint32_t screen, uint32_t section, uint32_t controller, int index);
    bool addContent(IGuest& g, uint32_t transform, int index, float left, float top, float right, float bottom);
    void addContentItem(const ContentItem& item);
    bool contentDrawn(int index) const;
    bool contentCurrent(IGuest& g, uint32_t scr) const;
    int contentSelected(IGuest& g) const;
    MenuMouseResult updateContent(IGuest& g, uint32_t scr, const MenuMouseSample& s, bool inGui, int gx, int gy,
                                  bool moved);
    // Local viewport units of 2D inventory draws (286170 text, 4387b0 clip rectangles) to absolute GUI units.
    bool localToGui(IGuest& g, float x, float y, GridPoint& out) const;
    void resetContent();
    MenuMouseOptions opt_;
    std::atomic<int> invSection_{-1};
    MenuMouseAddrs a_;
    std::atomic<bool> visible_{false};
    std::atomic<int> screenId_{-1};
    uint32_t screen_ = 0;
    bool havePrev_ = false;
    float prevCx_ = 0, prevCy_ = 0;
    std::chrono::steady_clock::time_point lastMove_{};
    bool everMoved_ = false;
    uint32_t pressedWidget_ = 0; // widget that received the native press (0 = none)
    bool pressActive_ = false;
    // Widget handlers that can wait for a sound resource (sliders, check boxes, selectors) run as posted guest calls,
    // strictly one after the other. The "captured widget" variable stays set until the release has actually run.
    std::deque<GuestCall> ops_;
    bool clearCaptured_ = false;
    void queueOp(const GuestCall& c, bool coalesce);
    void pumpOps(IGuest& g);
    Debug dbg_;
    std::mutex hotM_;
    std::vector<Hotspot> hot_;
    uint32_t hotScreen_ = 0;
    uint32_t hotFrame_ = 0, frame_ = 0;
    HotspotHandler hotFn_;
    std::atomic<int> hotHover_{-1};
    int lastHotHover_ = -1;
};

} // namespace drakan_input
