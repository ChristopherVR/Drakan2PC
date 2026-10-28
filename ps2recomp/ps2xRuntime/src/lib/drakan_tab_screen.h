// Modern Tab screen (runtime/gameui, docs/reference/mouse-menus.md section 13): state shared between the EE-side glue
// (drakan_tab_screen.inl, inside drakan_input_glue.cpp) and the overlay on the present thread
// (drakan_settings_host.cpp). std + gameui only.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "gameui/Actions.h"
#include "gameui/Dialog.h"
#include "gameui/Model.h"
#include "gameui/PauseScreen.h"
#include "gameui/SaveLoadScreen.h"
#include "gameui/TitleScreen.h"

namespace drakan_tab {

// [general] inventory_screen = modern (read at start, changed live by the F1 settings).
bool enabled();
void setEnabled(bool on);
// [general] strip_screen / map_screen = modern | native (env DRAKAN_STRIP_SCREEN / DRAKAN_MAP_SCREEN, default modern):
// the I strip and the M map view as host-drawn screens (gameui StripScreen / MapScreen) or the native-drawn views.
bool stripModern();
bool mapModern();

// Which host screen is bound to the open inventory screen 0x15 (the overlay's dispatcher draws that one).
enum class HostKind : uint8_t { Tab = 0, Strip = 1, Map = 2 };

// One native inventory cell as drawn this frame: the projected quad's bounds in native GUI units (W x H frame).
struct NativeCell {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct NativeGrid {
    uint64_t serial = 0; // capture count (0 = never)
    unsigned frameW = 512, frameH = 512;
    std::array<NativeCell, 24> cells{};
};

// Where the host screen wants the native grid: its 8 x 3 cell rectangle in native GUI units, published by the overlay
// (it knows the layout and the page mapping), applied by the EE at the grid draw.
struct GridTarget {
    bool valid = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
// Where Rynn should stand: normalised x of the output picture (0.5 = centre) and her zoom (1 = the native preview).
struct PreviewTarget {
    bool valid = false;
    float centreX = 0.5f;
    float zoom = 1.0f;
};

// Map page: where the host wants the native map image (GUI units), and where the EE drew it (GUI units, the image's
// native size in pixels).
struct MapTarget {
    bool valid = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct MapCapture {
    uint64_t serial = 0;
    unsigned frameW = 512, frameH = 512;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float imageW = 0, imageH = 0;
};

// The native dialog's painted frame, including its ornamental border (GUI units).
// Captured at 2c94a8, since widget bounds do not include the frame or all its text.
struct NativeDialog {
    bool valid = false;
    unsigned frameW = 512, frameH = 512;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    gameui::DialogContent content;
};

// M view: a marker the EE drew this frame (native GUI units, centre) and the record it belongs to.
struct MapMarkerGui {
    int quest = -1;
    float x = 0, y = 0;
};

struct Shared {
    gameui::ModelExchange model;
    std::atomic<HostKind> kind{HostKind::Tab}; // set with `session` when a host screen binds
    gameui::ActionQueue actions;
    gameui::ResultQueue results;
    std::atomic<bool> layerReady{false}; // the overlay draws the screen (without it Tab stays native)
    std::atomic<bool> active{false};     // the modern screen is bound to an open inventory screen (EE writes)
    std::atomic<bool> boxUp{false};      // a native message box is stacked on it (help, Yes / No, apply points)
    std::atomic<uint32_t> session{0};    // incremented on every open: the UI resets its page / focus
    std::atomic<bool> closing{false};    // the close was requested; the UI stops drawing / taking input
    std::atomic<uint32_t> sound{0};      // UI sound property to play (0x2a tick), overlay writes, EE plays
    std::atomic<uint64_t> hostDrawn{0};  // frames the overlay drew the screen (the EE watchdog watches it move)
    std::atomic<float> hostFade{0.0f};   // the overlay's opacity of the screen: the HUD switches at 0.5 (cross-fade)

    mutable std::mutex gridM;
    NativeGrid grid;                   // EE writes (grid draw), overlay reads
    GridTarget gridTarget;             // overlay writes, EE reads
    PreviewTarget preview;             // overlay writes, EE reads
    MapTarget mapTarget;               // overlay writes, EE reads
    MapCapture map;                    // EE writes (map background draw), overlay reads
    NativeDialog dialog;               // EE writes (message-box frame draw), overlay reads
    std::vector<MapMarkerGui> markers; // EE writes (M view marker draws), overlay reads
    uint64_t markerSerial = 0;
    std::string location; // the level's name (ctx+0x98 path stem), EE writes at bind

    NativeGrid nativeGrid() const {
        std::lock_guard<std::mutex> l(gridM);
        return grid;
    }
    GridTarget target() const {
        std::lock_guard<std::mutex> l(gridM);
        return gridTarget;
    }
    MapTarget mapPlace() const {
        std::lock_guard<std::mutex> l(gridM);
        return mapTarget;
    }
    MapCapture mapCapture() const {
        std::lock_guard<std::mutex> l(gridM);
        return map;
    }
    NativeDialog nativeDialog() const {
        std::lock_guard<std::mutex> l(gridM);
        return dialog;
    }
    PreviewTarget previewTarget() const {
        std::lock_guard<std::mutex> l(gridM);
        return preview;
    }
    std::vector<MapMarkerGui> mapMarkers(uint64_t* serial = nullptr) const {
        std::lock_guard<std::mutex> l(gridM);
        if (serial)
            *serial = markerSerial;
        return markers;
    }
    std::string locationName() const {
        std::lock_guard<std::mutex> l(gridM);
        return location;
    }
};
Shared& shared();

// "cdrom0:/English/Levels/Level 1A - Surdana/Level 1A - Surdana.lvl" -> "Surdana" (the file stem after " - ").
std::string levelDisplayName(const std::string& path);

} // namespace drakan_tab

// Modern Esc (pause) menu (gameui PauseScreen, docs/reference/mouse-menus.md section 14): EE glue in
// drakan_pause_screen.inl, overlay in drakan_settings_host.cpp.
namespace drakan_pause_screen {

// [general] pause_menu = native | modern (env DRAKAN_PAUSE_MENU, default native); read on each open.
bool enabled();
void setEnabled(bool on);

struct Shared {
    gameui::ModelExchange model; // status strip (stats, quests), read outside screen 0x15
    gameui::PauseActionQueue actions;
    std::atomic<bool> active{false};      // bound to the open pause screen 0x17 (EE writes)
    std::atomic<bool> childUp{false};     // a native child (Save, Load, Options tree, Quit box) is on top
    std::atomic<uint32_t> session{0};     // incremented on every bind: the overlay opens the screen
    std::atomic<uint32_t> reactivated{0}; // incremented when the pause is the active screen again after a child
    std::atomic<bool> resuming{false};    // Resume was chosen: no input, the screen fades with the native close
    std::atomic<uint64_t> hostDrawn{0};   // the EE watchdog watches it move
    std::atomic<float> hostFade{0.0f};
    mutable std::mutex m;
    gameui::PauseState state; // EE writes (enable states, location), overlay reads
    gameui::PauseState pauseState() const {
        std::lock_guard<std::mutex> l(m);
        return state;
    }
};
Shared& shared();

} // namespace drakan_pause_screen

// Modern title menu and Save / Load screens (gameui TitleScreen / SaveLoadScreen, docs/reference/mouse-menus.md
// section 15): EE glue in drakan_menu_screens.inl, overlay in drakan_settings_host.cpp.
namespace drakan_menu_screens {

// [general] save_menu / title_menu = native | modern (env DRAKAN_SAVE_MENU / DRAKAN_TITLE_MENU, default native).
bool saveModern();
void setSaveModern(bool on);
bool titleModern();
void setTitleModern(bool on);

// A slot's picture: the native 128 x 128 thumbnail (RGBA8, rows as stored in the entry's image) and the save's HD
// companion (drakan_save_previews::previewPath), whichever exists.
struct SlotPicture {
    uint64_t key = 0;             // content hash of `pixels` (0 = no native thumbnail yet)
    std::vector<uint32_t> pixels; // 128 x 128, 0xAABBGGRR
    std::string hdPath;           // empty = none
};

struct Shared {
    std::atomic<int> quickOperation{0}; // 1 save, 2 load: progress across the entire native pause/slot transaction
    // ---- title (screen 8) ----
    std::atomic<bool> titleActive{false};  // bound to the open title, its panel and rows hidden (EE writes)
    std::atomic<bool> titleChild{false};   // a native child (credits, a native box or load screen) is on top
    std::atomic<bool> titleCovered{false}; // the modern Save / Load screen is on top (it draws instead)
    std::atomic<uint32_t> titleSession{0}, titleReactivated{0};
    std::atomic<uint64_t> titleDrawn{0};
    gameui::TitleActionQueue titleActions;
    gameui::PauseActionQueue titleOptionActions; // the embedded Options pages (pause stage 2 executor)
    // ---- save / load (screens 4 / 5) ----
    std::atomic<bool> slotsActive{false};  // bound to an open slot screen, its page hidden
    std::atomic<bool> slotsChild{false};   // a native box / the Saving screen is on top
    std::atomic<bool> slotsLeaving{false}; // Back / load / save chosen: no input, the screen fades with the native one
    std::atomic<uint32_t> slotsSession{0}, slotsReactivated{0};
    std::atomic<uint64_t> slotsDrawn{0};
    gameui::SaveLoadActionQueue slotActions;
    // ---- feedback sounds of the title, pause, Options and Save / Load screens ----
    // UI sound property (0x24 click, 0x2a focus tick, 0x2d back, 0x27 / 0x28 slider / check box) the overlay asks for;
    // the next PadMgr update plays it through the game's sound manager as a guest invocation (tabPlaySound). The
    // newest wins: one sound per EE frame.
    std::atomic<uint32_t> uiSound{0};

    mutable std::mutex m;
    gameui::TitleState title;                                         // EE writes, overlay reads
    gameui::SaveSlotsState slots;                                     // EE writes, overlay reads
    gameui::SaveLoadMode mode = gameui::SaveLoadMode::Load;           // set at bind
    gameui::SaveLoadContext context = gameui::SaveLoadContext::Pause; // set at bind
    std::vector<std::pair<int, SlotPicture>> pictures;                // by slot number, EE writes
    uint64_t picturesSerial = 0;

    gameui::TitleState titleState() const {
        std::lock_guard<std::mutex> l(m);
        return title;
    }
    gameui::SaveSlotsState slotsState() const {
        std::lock_guard<std::mutex> l(m);
        return slots;
    }
};
Shared& shared();

// Seconds since the input module started (the clock of the log's "[in] t=" and "[menu-screens] ... t=" stamps).
double clockSeconds();

} // namespace drakan_menu_screens
