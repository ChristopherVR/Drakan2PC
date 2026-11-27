// Drakan: private header of the PC Settings host (drakan_settings_host.cpp and the drakan_settings_*.cpp files split
// from it). Not for use outside those files: the public API is drakan_settings_host.h.
//
// Everything here lives in drakan_settings_host::detail. Each .cpp keeps its own helpers in an anonymous namespace;
// only what another file of the host really uses is declared below, grouped by the file that defines it.
#pragma once
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host.h"
#include "drakan_settings_common.h"
#include "drakan_config.h"
#include "drakan_widescreen.h"
#include "drakan_dlss.h"
#include "drakan_dual_wield.h"
#include "drakan_pause.h"
#include "drakan_subtitles.h"
#include "drakan_fpsmeter.h"
#include "drakan_window.h"
#include "drakan_menus.h"
#include "drakan_netplay.h"
#include "drakan_netplay_policy.h"
#include "drakan_playtest.h"
#include "drakan_game.h"
#include "drakan_issue.h"
#include "drakan_arena.h"
#include "drakan_modes.h"
#include "drakan_coop_character.h"
#include "drakan_ghost.h"
#include "settings/CharacterPreview.h"
#include "drakan_world.h"
#include "drakan_fmv_hd.h"
#include "drakan_intro_movie.h"
#include "drakan_save_previews.h"
#include "drakan_tab_screen.h"
#include "drakan_inventory_views_policy.h"
#include "drakan_tab_preview_policy.h"
#include "drakan_map_preview_policy.h"
#include "drakan_title_picture_policy.h"
#include "gs/gs_frame_limit.h"

#include <gsr/gsr.h>
#include <spu2/AudioOutput.h>
#include <input/DrakanInput.h>
#include <input/Config.h>
#include <settings/GameUiRenderer.h>
#include <settings/Menu.h>
#include <gameui/SettingPreview.h>
#include <settings/ModernSettings.h>
#include <settings/Overlay.h>
#if DRAKAN_SETTINGS_WEBGPU
#include <settings/OverlayWebGpu.h>
#endif
#include <settings/TxdImage.h>
#include <gameui/MapScreen.h>
#include <gameui/UiSkin.h>
#include <gameui/Multiplayer.h>
#include <net/ArenaMaps.h>
#include <gameui/PauseScreen.h>
#include <gameui/Screen.h>
#include <gameui/StripScreen.h>
#include <imgui.h>
#include <hle/IsoFs.h>
#include <hle/ModPack.h>
#include <settings/ModDiscovery.h>
#include <settings/ModsModel.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <cmath>
#include <vector>

namespace drakan_settings_host::detail {

// ---- drakan_settings_host.cpp: the overlay, its open requests and the process ----
void requestOpenMods(); // thread safe; opens the overlay at the next present on its Mods page
extern std::unique_ptr<dsettings::SettingsOverlay> g_overlay;
extern gsr::OverlayContext g_overlayView; // presentation thread only
// Starts a new instance of this exe (same command line); the new process waits for this one to exit (DRAKAN_WAIT_PID).
void relaunchSelf();

// ---- drakan_settings_tab.cpp: the modern Tab screen, the I strip and the M map panel ----
// The EE side (drakan_tab_screen.inl) publishes snapshots and executes the actions pushed here.
// One host screen at a time is bound to the open inventory screen 0x15 (drakan_tab::Shared::kind): the Tab screen,
// the I strip or the M map's objectives panel. They share the session, fade, input and close handling below.
struct TabUi {
    drakan_tab::HostKind kind = drakan_tab::HostKind::Tab;
    gameui::InventoryScreen screen;
    gameui::StripScreen strip;
    gameui::MapScreen map;
    dsettings::GameUiRenderer renderer;
    gameui::DrawList last; // drawn while the screen fades out after closing
    uint32_t session = 0;
    float fade = 0;
    double lastTime = -1;
    gameui::InputDevice device = gameui::InputDevice::KeyboardMouse;
    uint64_t gridSerial = 0;
    double gridTime = -10;
    uint64_t mapSerial = 0;
    double mapTime = -10;
    // Keys / buttons held when the screen opened (Tab, I, Select ...) or when it got the input back count only after
    // their release (gameui::KeyGate): the opening press must not close it again.
    gameui::KeyGate gate;
    bool took = false; // the screen took the input last frame
};
extern TabUi g_tabUi;
void tabDraw(const dsettings::SettingsOverlay::ScreenFrame& f);

// ---- drakan_settings_screen_input.cpp: the host screens' keyboard / mouse / pad (or scripted) input ----
// The input of a host screen's frame (`take` false: none, the events are only drained): the real devices, or the
// script's events in a scripted run. `capture`: the PC Settings pages wait for a key to bind (capturedKey()).
// `textEdit`: a multiplayer text field is being edited (typed characters in, letter keys out of navigation).
gameui::UiInput hostScreenInput(bool take, bool capture, float w, float h, bool textEdit = false);
// The key the last hostScreenInput with `capture` caught, as its drakan_input.ini name ("" = none).
const std::string& capturedKey();

// ---- drakan_settings_screens.cpp: the screen layer (which host screen draws) and what the screens share ----
extern std::unique_ptr<dsettings::ModernSettings> g_pcs;
const gameui::SettingsSchema* pcSettingsSchema(bool pagesOpen, double now);
void hostUiSound(gameui::UiSound s, bool nativeSound);
bool nativeSound(const std::vector<gameui::PauseAction>& actions);
dsettings::ScreenFontSizes tabFontSizes(float w, float h);
void drawHostList(const dsettings::SettingsOverlay::ScreenFrame& f, const gameui::DrawList& list, double now,
                  float fade, const dsettings::IconSource& icons);
void hostScreenDraw(const dsettings::SettingsOverlay::ScreenFrame& f);

// ---- drakan_settings_pause.cpp: the modern Esc (pause) menu ----
bool pauseShown();
void pauseDraw(const dsettings::SettingsOverlay::ScreenFrame& f, bool background = false);

// ---- drakan_settings_previews.cpp: save slot pictures, map / cover pictures, setting shots, character models ----
ImTextureID makeTexture(const void* rgba, unsigned w, unsigned h);
void dropTexture(ImTextureID& tex);
void updatePreviewTextures();
bool mapAutoCapture();
std::string capturedPreviewDir();
std::string mapSafeName(const std::string& mapName);
uint32_t mapPreviewId(const std::string& mapName);
uint32_t titleArtPictureId(float viewAspect);
drakan_title_picture::Load pictureLoad(uint32_t id);
uint32_t settingShotPicture(const std::string& key, const std::string& value);
void updateMapPreviews(); // queues and uploads the pictures the screens asked for (settings pictures too)
bool characterPreviewReady(int classId);
bool previewCrop(const gameui::ImageRef& ref, dsettings::IconCrop& c); // a picture's texture
std::string previewPath(uint32_t id);                                  // the file behind a picture id of mapPreviewId
// A setting shot is settled once uploaded, or once its decode finished without a texture (a bad file shows the empty
// box, not the previous value's picture). Until then the help panel keeps the last picture up (no blank flash).
bool settingShotSettled(uint32_t id);
void releasePreviewTextures(); // every picture texture, after their decodes ended (detachRenderer)

// ---- drakan_settings_multiplayer.cpp: the Multiplayer menu glue (title pages <-> drakan_netplay) ----
gameui::mp::MpState mpState(bool wantLan);
void mpApply(const gameui::mp::MpAction& a, gameui::mp::MultiplayerScreen& screen);

// ---- drakan_settings_mp_hud.cpp: the in-game multiplayer HUD ----
// The chat line is open: the screen layer takes the window messages (WM_CHAR is only fed to ImGui then).
extern std::atomic<bool> g_mpChatOpen;
bool mpHudActive();
bool mpResultsUp();
void mpHudDraw(const dsettings::SettingsOverlay::ScreenFrame& f);

// ---- drakan_settings_title.cpp: the modern title menu, the Switch Campaigns button, the title backdrop ----
bool titleShown();
void titleDraw(const dsettings::SettingsOverlay::ScreenFrame& f, bool background = false);
bool switchButtonShown();
void switchButtonDraw(const dsettings::SettingsOverlay::ScreenFrame& f);
void syncTitleBackdrop();
void previewTitleBackdrop(int game); // bind a chosen game's title art at the choice

// ---- drakan_settings_slots.cpp: the modern Save / Load screen ----
bool slotsShown();
void slotsDraw(const dsettings::SettingsOverlay::ScreenFrame& f);

// ---- drakan_settings_mods.cpp: the Mods page model and the title's new packs card ----
dsettings::ModsModel& harnessMods();                // the Mods page model
void titleModsAction(const gameui::TitleAction& a); // the title's new mod packs card

} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
