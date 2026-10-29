// Drakan: DrakanSettingsHost - see drakan_settings_host.h and decomp/runtime/settings/README.md.
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
#include "drakan_fmv_hd.h"
#include "drakan_save_previews.h"
#include "drakan_tab_screen.h"
#include "drakan_inventory_views_policy.h"
#include "drakan_tab_preview_policy.h"

#include <gsr/gsr.h>
#include <spu2/AudioOutput.h>
#include <input/DrakanInput.h>
#include <input/Config.h>
#include <settings/GameUiRenderer.h>
#include <settings/Menu.h>
#include <settings/ModernSettings.h>
#include <settings/Overlay.h>
#if DRAKAN_SETTINGS_WEBGPU
#include <settings/OverlayWebGpu.h>
#endif
#include <settings/TxdImage.h>
#include <gameui/MapScreen.h>
#include <gameui/PauseScreen.h>
#include <gameui/Screen.h>
#include <gameui/StripScreen.h>
#include <imgui.h>
#include <hle/IsoFs.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <cmath>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include "drakan_web_events.h"
#include <emscripten.h>
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fstream>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace drakan_settings_host {
namespace {
std::atomic<bool> g_open{false};
std::atomic<bool> g_openRequest{false};
std::atomic<bool> g_openControls{false}; // the next open shows the Controls page
std::atomic<int> g_frameLimit{0};
std::atomic<spu2::AudioOutput*> g_audio{nullptr};
gsr::Renderer* g_gs = nullptr;
void* g_parent = nullptr; // the game window (drakan_window)
WindowOps g_ops;
std::unique_ptr<dsettings::SettingsOverlay> g_overlay;
bool g_overlayFailed = false;
gsr::OverlayContext g_overlayView; // presentation thread only

// ---- the game's UI art (INTERFCE.TXD), captured as the game loads it ----
std::mutex g_artM;
std::map<int, std::vector<uint8_t>> g_art;
hle::IsoFs* g_artFs = nullptr;
bool g_artRead = false;
// Menu skin (118 frame, 88 stone, 128 orb, 197 / 198 arrows, 103 check box, 28 fire, 61 cursor, 96 font) and the
// modern screens' optional pieces (94 icon atlas, 129 gem, 86 parchment); dsettings::GameSkin.
const int kArtIds[] = {118, 88, 128, 197, 198, 103, 28, 61, 96, 94, 129, 86};

// Reads INTERFCE.TXD from the disc once and keeps the entries the menu style needs (g_artM held).
void readArtFromDisc() {
    if (g_artRead || !g_artFs)
        return;
    g_artRead = true;
    hle::IsoFs::Node n;
    if (!g_artFs->lookup("COMMON\\INTERFCE\\INTERFCE.TXD", n) || n.dir) {
        std::fprintf(stderr, "[settings] game art: INTERFCE.TXD not found on the disc\n");
        return;
    }
    const std::vector<uint8_t> f = g_artFs->readAll(n);
    uint32_t idx = 0, cnt = 0;
    if (f.size() < 16 || std::memcmp(f.data(), "SRSC", 4) != 0)
        return;
    std::memcpy(&idx, &f[6], 4);
    std::memcpy(&cnt, &f[10], 4);
    for (uint32_t i = 0; i < cnt && idx + (size_t)(i + 1) * 14 <= f.size(); ++i) {
        const size_t o = idx + (size_t)i * 14;
        uint16_t t, id;
        uint32_t off, sz;
        std::memcpy(&t, &f[o], 2);
        std::memcpy(&id, &f[o + 2], 2);
        std::memcpy(&off, &f[o + 6], 4);
        std::memcpy(&sz, &f[o + 10], 4);
        if (t != 0x40 || (size_t)off + sz > f.size())
            continue;
        for (int a : kArtIds)
            if (a == id)
                g_art[a].assign(f.begin() + off, f.begin() + off + sz);
    }
    std::fprintf(stderr, "[settings] game art: %zu UI textures read from INTERFCE.TXD\n", g_art.size());
}

// Test aid: DRAKAN_OVERLAY_SHOT_DIR=<dir>. When <dir>\\shot.txt exists (its first line = a file name without extension)
// the presented frame, with the overlay, is written to <dir>\\<name>.bmp and shot.txt is deleted. Works without the
// window being visible / focused.
void maybeShot(const gsr::OverlayContext& oc) {
    static const char* dir = std::getenv("DRAKAN_OVERLAY_SHOT_DIR");
    static int tick = 0;
    if (!dir || !*dir || (++tick % 6) != 0 || !oc.rtv || !g_overlay)
        return;
    { // state.txt: "open=<0|1> <flow description>" (rewritten when it changes) so a test driver need not parse the
      // (lagging) log
        static std::string last;
        const std::string st =
            std::string("open=") + (g_overlay && g_overlay->isOpen() ? "1" : "0") + " " +
            drakan_pause::flowDescription().substr(0, drakan_pause::flowDescription().find(", last sample"));
        if (st != last) {
            last = st;
            if (FILE* sf = std::fopen((std::string(dir) + "\\state.txt").c_str(), "wb")) {
                std::fwrite(st.data(), 1, st.size(), sf);
                std::fclose(sf);
            }
        }
    }
    const std::string req = std::string(dir) + "\\shot.txt";
    FILE* rf = std::fopen(req.c_str(), "rb");
    if (!rf)
        return;
    char name[200] = {};
    const size_t n = std::fread(name, 1, sizeof name - 1, rf);
    std::fclose(rf);
    std::remove(req.c_str());
    std::string nm(name, n);
    while (!nm.empty() && (nm.back() == '\n' || nm.back() == '\r' || nm.back() == ' '))
        nm.pop_back();
    if (nm.empty())
        nm = "shot";
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!g_overlay->renderer().readTarget(oc.rtv, w, h, rgba))
        return;
    const std::string path = std::string(dir) + "\\" + nm + ".bmp";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        const uint32_t bytes = w * h * 4;
        uint8_t hdr[54] = {'B', 'M'};
        const uint32_t fs = 54 + bytes, off = 54, dib = 40, planes = 1, bpp = 32;
        std::memcpy(hdr + 2, &fs, 4);
        std::memcpy(hdr + 10, &off, 4);
        std::memcpy(hdr + 14, &dib, 4);
        std::memcpy(hdr + 18, &w, 4);
        const int32_t nh = -(int32_t)h;
        std::memcpy(hdr + 22, &nh, 4);
        std::memcpy(hdr + 26, &planes, 2);
        std::memcpy(hdr + 28, &bpp, 2);
        std::fwrite(hdr, 1, 54, f);
        std::vector<uint8_t> row(w * 4);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* src = rgba.data() + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; ++x) {
                row[x * 4 + 0] = src[x * 4 + 2];
                row[x * 4 + 1] = src[x * 4 + 1];
                row[x * 4 + 2] = src[x * 4 + 0];
                row[x * 4 + 3] = 255;
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        std::fprintf(stderr, "[settings] shot %s (%ux%u)\n", path.c_str(), w, h);
    }
}

// Starts a new instance of this exe (same command line); the new process waits for this one to exit (DRAKAN_WAIT_PID).
void relaunchSelf() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    if (!n)
        return;
    std::wstring cmd = GetCommandLineW();
    SetEnvironmentVariableW(L"DRAKAN_WAIT_PID", std::to_wstring(GetCurrentProcessId()).c_str());
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else
        std::fprintf(stderr, "[settings] restart failed (error %lu)\n", GetLastError());
    SetEnvironmentVariableW(L"DRAKAN_WAIT_PID", nullptr);
#elif defined(__EMSCRIPTEN__)
    MAIN_THREAD_ASYNC_EM_ASM({ location.reload(); }); // the page starts the game again
#else
    // The same executable with the same arguments (/proc/self/cmdline: NUL-separated).
    std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
    std::vector<std::string> args;
    for (std::string arg; std::getline(cmdline, arg, '\0');)
        args.push_back(arg);
    if (args.empty())
        return;
    std::vector<char*> argv;
    for (std::string& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    setenv("DRAKAN_WAIT_PID", std::to_string(getpid()).c_str(), 1);
    const pid_t child = fork();
    if (child == 0) {
        execv("/proc/self/exe", argv.data());
        _exit(127);
    }
    if (child < 0)
        std::perror("[settings] restart failed");
    unsetenv("DRAKAN_WAIT_PID");
#endif
}

// ---- the modern Tab screen (runtime/gameui), drawn by the overlay's screen layer on the present thread ----
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
TabUi g_tabUi;

// Native GUI units (W x H frame) <-> back buffer pixels of the inventory page, which the renderer centres and
// compresses by hudScaleX (CentredPage, hud::inventoryMap) inside the game image rectangle.
struct PageMap {
    float vx = 0, vy = 0, vw = 1, vh = 1, hs = 1, W = 512, H = 512;
    float x(float gx) const { return vx + vw * (0.5f + (gx / W - 0.5f) * hs); }
    float y(float gy) const { return vy + vh * gy / H; }
    float gx(float x) const { return (0.5f + ((x - vx) / vw - 0.5f) / hs) * W; }
    float gy(float y) const { return (y - vy) / vh * H; }
};
PageMap tabPageMap(const gsr::OverlayContext& oc, float w, float h, unsigned frameW, unsigned frameH) {
    PageMap m;
    m.vx = oc.viewW > 0 ? oc.viewX : 0;
    m.vy = oc.viewH > 0 ? oc.viewY : 0;
    m.vw = oc.viewW > 0 ? oc.viewW : w;
    m.vh = oc.viewH > 0 ? oc.viewH : h;
    m.hs = oc.hudScaleX > 0.2f && oc.hudScaleX <= 1.0f ? oc.hudScaleX : 1.0f;
    m.W = float(frameW ? frameW : 512);
    m.H = float(frameH ? frameH : 512);
    return m;
}

// The modern PC Settings pages' key capture (SettingsPages::capturing()): keyboard keys (and the middle / side mouse
// buttons) are not navigation then but the key to bind, reported in g_capturedKey as its drakan_input.ini name; Esc
// and the pad stay navigation (Esc / B cancel). Set by the screen's draw before it reads the input.
bool g_capture = false;
std::string g_capturedKey;

gameui::UiInput tabInput(bool take) {
    using gameui::UiKey;
    gameui::UiInput in;
    g_capturedKey.clear();
    if (!take)
        return in;
    ImGuiIO& io = ImGui::GetIO();
    gameui::KeyGate& gate = g_tabUi.gate;
    bool pad = false, kbm = false;
    if (g_capture) {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart && g_capturedKey.empty(); ++key)
            if (key != ImGuiKey_Escape && ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false))
                if (const char* name = dsettings::iniNameOfImGuiKey(key))
                    g_capturedKey = name;
        static const char* const kButtons[] = {"MOUSE3", "MOUSE4", "MOUSE5"};
        for (int b = 2; b <= 4 && g_capturedKey.empty(); ++b)
            if (ImGui::IsMouseClicked(b))
                g_capturedKey = kButtons[b - 2];
        if (!g_capturedKey.empty())
            kbm = true;
    }
    // `vk`: the key's physical state comes from the OS (ImGui's is cleared when the screen takes the input, and the
    // key-down message of the opening press can arrive after that: raw input, which the game reads, runs on its own
    // thread). Pad buttons: ImGui's state is the backend's XInput poll.
    const auto k = [&](ImGuiKey key, int vk, UiKey u, bool repeat) {
        if (g_capture && key < ImGuiKey_GamepadStart && key != ImGuiKey_Escape)
            return; // capturing: the keyboard binds, it does not navigate
        const bool down = ImGui::IsKeyDown(key) || (vk && drakan_win::keyDown(vk));
        if (gate.accept(static_cast<int>(key), down, ImGui::IsKeyPressed(key, repeat))) {
            in.keys.push_back(u);
            (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown ? pad : kbm) = true;
        }
    };
    k(ImGuiKey_UpArrow, drakan_win::vk::Up, UiKey::Up, true);
    k(ImGuiKey_W, 'W', UiKey::Up, true);
    k(ImGuiKey_GamepadDpadUp, 0, UiKey::Up, true);
    k(ImGuiKey_GamepadLStickUp, 0, UiKey::Up, true);
    k(ImGuiKey_DownArrow, drakan_win::vk::Down, UiKey::Down, true);
    k(ImGuiKey_S, 'S', UiKey::Down, true);
    k(ImGuiKey_GamepadDpadDown, 0, UiKey::Down, true);
    k(ImGuiKey_GamepadLStickDown, 0, UiKey::Down, true);
    k(ImGuiKey_LeftArrow, drakan_win::vk::Left, UiKey::Left, true);
    k(ImGuiKey_A, 'A', UiKey::Left, true);
    k(ImGuiKey_GamepadDpadLeft, 0, UiKey::Left, true);
    k(ImGuiKey_GamepadLStickLeft, 0, UiKey::Left, true);
    k(ImGuiKey_RightArrow, drakan_win::vk::Right, UiKey::Right, true);
    k(ImGuiKey_D, 'D', UiKey::Right, true);
    k(ImGuiKey_GamepadDpadRight, 0, UiKey::Right, true);
    k(ImGuiKey_GamepadLStickRight, 0, UiKey::Right, true);
    k(ImGuiKey_Enter, drakan_win::vk::Enter, UiKey::Confirm, false);
    k(ImGuiKey_KeypadEnter, drakan_win::vk::Enter, UiKey::Confirm, false);
    k(ImGuiKey_Space, drakan_win::vk::Space, UiKey::Confirm, false);
    k(ImGuiKey_GamepadFaceDown, 0, UiKey::Confirm, false);
    k(ImGuiKey_Escape, drakan_win::vk::Escape, UiKey::Back, false);
    k(ImGuiKey_Backspace, drakan_win::vk::Backspace, UiKey::Back, false);
    k(ImGuiKey_GamepadFaceRight, 0, UiKey::Back, false);
    k(ImGuiKey_Tab, drakan_win::vk::Tab, UiKey::Close, false);
    k(ImGuiKey_I, 'I', UiKey::Close, false);
    k(ImGuiKey_M, 'M', UiKey::Close, false);
    k(ImGuiKey_GamepadStart, 0, UiKey::Back, false);
    k(ImGuiKey_GamepadBack, 0, UiKey::Close, false);
    k(ImGuiKey_X, 'X', UiKey::Secondary, false);
    k(ImGuiKey_Delete, drakan_win::vk::Delete, UiKey::Secondary, false);
    k(ImGuiKey_GamepadFaceUp, 0, UiKey::Secondary, false);
    k(ImGuiKey_F, 'F', UiKey::Tertiary, false);
    k(ImGuiKey_GamepadFaceLeft, 0, UiKey::Tertiary, false);
    k(ImGuiKey_Q, 'Q', UiKey::PagePrev, false);
    k(ImGuiKey_GamepadL1, 0, UiKey::PagePrev, false);
    k(ImGuiKey_E, 'E', UiKey::PageNext, false);
    k(ImGuiKey_GamepadR1, 0, UiKey::PageNext, false);
    k(ImGuiKey_Z, 'Z', UiKey::FilterPrev, false);
    k(ImGuiKey_GamepadL2, 0, UiKey::FilterPrev, false);
    k(ImGuiKey_C, 'C', UiKey::FilterNext, false);
    k(ImGuiKey_GamepadR2, 0, UiKey::FilterNext, false);
    const ImVec2 mp = io.MousePos;
    in.pointerValid =
        ImGui::IsMousePosValid(&mp) && mp.x >= 0 && mp.y >= 0 && mp.x < io.DisplaySize.x && mp.y < io.DisplaySize.y;
    in.pointerX = mp.x;
    in.pointerY = mp.y;
    in.pointerMoved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    constexpr int kGateLeft = gameui::KeyGate::kKeys - 2, kGateRight = gameui::KeyGate::kKeys - 1;
    const auto button = [](ImGuiMouseButton b, int vk) { return ImGui::IsMouseDown(b) || drakan_win::keyDown(vk); };
    in.leftDown =
        button(ImGuiMouseButton_Left, drakan_win::vk::LeftButton); // slider drags (a drag starts only on a click)
    in.leftClick = gate.accept(kGateLeft, in.leftDown, ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    in.doubleClick = in.leftClick && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    in.rightClick = gate.accept(kGateRight, button(ImGuiMouseButton_Right, drakan_win::vk::RightButton),
                                ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    in.wheel = io.MouseWheel;
    if (in.pointerMoved || in.leftClick || in.rightClick || in.wheel != 0)
        kbm = true;
    if (pad)
        g_tabUi.device = gameui::InputDevice::Gamepad;
    else if (kbm)
        g_tabUi.device = gameui::InputDevice::KeyboardMouse;
    in.device = g_tabUi.device;
    if (in.device == gameui::InputDevice::Gamepad)
        in.pointerMoved = false; // a resting mouse must not steal the pad's focus
    return in;
}

// Scripted runs (DRAKAN_INPUT_SCRIPT): the script's edges, queued by the input module while the host screen is up.
gameui::UiInput tabScriptInput(bool take, float bbW, float bbH) {
    using drakan_input::InputModule;
    using gameui::UiKey;
    static float px = -1, py = -1, clickX = 0, clickY = 0;
    static double clickT = -10;
    gameui::UiInput in;
    g_capturedKey.clear();
    const auto events = drakan_input::module().drainHostScreenEvents();
    // The same KeyGate rule as for the real input, on the script's own up / down state (ids: keys 0..255, mouse
    // buttons 256 + code, pad 300 + bit).
    static std::bitset<gameui::KeyGate::kKeys> held;
    gameui::KeyGate& gate = g_tabUi.gate;
    const auto gateId = [](const InputModule::HostScreenEvent& e) {
        switch (e.kind) {
            case InputModule::HostScreenEvent::Key:
                return e.code & 255;
            case InputModule::HostScreenEvent::MouseButton:
                return 256 + (e.code & 31);
            case InputModule::HostScreenEvent::Pad: {
                int bit = 0;
                while (bit < 16 && !((e.code >> bit) & 1))
                    ++bit;
                return 300 + bit;
            }
            default:
                return -1;
        }
    };
    const auto counts = [&](const InputModule::HostScreenEvent& e) {
        const int id = gateId(e);
        if (id < 0)
            return true;
        const bool was = held[size_t(id)];
        held[size_t(id)] = e.down;
        if (!e.down) {
            gate.accept(id, false, false);
            return false;
        }
        gate.accept(id, was, false); // the state before this press: held over or released
        return take && gate.accept(id, true, true);
    };
    if (!take) {
        for (const InputModule::HostScreenEvent& e : events)
            (void)counts(e);
        return in;
    }
    bool pad = false, kbm = false;
    for (const InputModule::HostScreenEvent& e : events) {
        if (e.kind != InputModule::HostScreenEvent::Cursor && !counts(e))
            continue;
        switch (e.kind) {
            case InputModule::HostScreenEvent::Cursor:
                if (e.clientW > 0 && e.clientH > 0) {
                    px = float(e.x) * bbW / float(e.clientW);
                    py = float(e.y) * bbH / float(e.clientH);
                    in.pointerMoved = kbm = true;
                }
                break;
            case InputModule::HostScreenEvent::Key: {
                if (!e.down)
                    break;
                if (g_capture && e.code != 0x01) { // capturing: the key to bind (Esc still cancels)
                    if (const char* name = drakan_input::nameOfKey(e.code); name && g_capturedKey.empty())
                        g_capturedKey = name;
                    kbm = true;
                    break;
                }
                struct M {
                    int dik;
                    UiKey key;
                };
                static const M map[] = {{0xC8, UiKey::Up},         {0x11, UiKey::Up},        {0xD0, UiKey::Down},
                                        {0x1F, UiKey::Down},       {0xCB, UiKey::Left},      {0x1E, UiKey::Left},
                                        {0xCD, UiKey::Right},      {0x20, UiKey::Right},     {0x1C, UiKey::Confirm},
                                        {0x9C, UiKey::Confirm},    {0x39, UiKey::Confirm},   {0x01, UiKey::Back},
                                        {0x0E, UiKey::Back},       {0x0F, UiKey::Close},     {0x17, UiKey::Close},
                                        {0x32, UiKey::Close},      {0x2D, UiKey::Secondary}, {0xD3, UiKey::Secondary},
                                        {0x21, UiKey::Tertiary},   {0x10, UiKey::PagePrev},  {0x12, UiKey::PageNext},
                                        {0x2C, UiKey::FilterPrev}, {0x2E, UiKey::FilterNext}};
                for (const M& m : map)
                    if (m.dik == e.code) {
                        in.keys.push_back(m.key);
                        kbm = true;
                    }
                break;
            }
            case InputModule::HostScreenEvent::MouseButton:
                if (!e.down)
                    break;
                kbm = true;
                if (e.code == drakan_input::MBTN_LEFT) {
                    in.leftClick = true;
                    const double t = ImGui::GetTime();
                    in.doubleClick = t - clickT < 0.5 && std::fabs(px - clickX) < 8 && std::fabs(py - clickY) < 8;
                    clickT = in.doubleClick ? -10 : t;
                    clickX = px;
                    clickY = py;
                } else if (e.code == drakan_input::MBTN_RIGHT) {
                    in.rightClick = true;
                } else if (g_capture && g_capturedKey.empty()) {
                    g_capturedKey = e.code == drakan_input::MBTN_MIDDLE ? "MOUSE3"
                                    : e.code == drakan_input::MBTN_X1   ? "MOUSE4"
                                                                        : "MOUSE5";
                }
                break;
            case InputModule::HostScreenEvent::Pad: {
                if (!e.down)
                    break;
                namespace P = drakan_input::Pad;
                struct M {
                    int bit;
                    UiKey key;
                };
                static const M map[] = {{P::UP, UiKey::Up},
                                        {P::DOWN, UiKey::Down},
                                        {P::LEFT, UiKey::Left},
                                        {P::RIGHT, UiKey::Right},
                                        {P::CROSS, UiKey::Confirm},
                                        {P::CIRCLE, UiKey::Back},
                                        {P::TRIANGLE, UiKey::Secondary},
                                        {P::SQUARE, UiKey::Tertiary},
                                        {P::L1, UiKey::PagePrev},
                                        {P::R1, UiKey::PageNext},
                                        {P::L2, UiKey::FilterPrev},
                                        {P::R2, UiKey::FilterNext},
                                        {P::SELECT, UiKey::Close},
                                        {P::START, UiKey::Back}};
                for (const M& m : map)
                    if (m.bit == e.code) {
                        in.keys.push_back(m.key);
                        pad = true;
                    }
                break;
            }
        }
    }
    in.leftDown = held[size_t(256 + (drakan_input::MBTN_LEFT & 31))]; // slider drags
    in.pointerValid = px >= 0 && py >= 0;
    in.pointerX = px;
    in.pointerY = py;
    if (pad)
        g_tabUi.device = gameui::InputDevice::Gamepad;
    else if (kbm)
        g_tabUi.device = gameui::InputDevice::KeyboardMouse;
    in.device = g_tabUi.device;
    if (in.device == gameui::InputDevice::Gamepad)
        in.pointerMoved = false;
    return in;
}

dsettings::ScreenFontSizes tabFontSizes(float w, float h) {
    const gameui::LayoutFrame lf = gameui::computeFrame({0, 0, w, h});
    const gameui::Theme th;
    dsettings::ScreenFontSizes s;
    const float design[] = {th.sizeBody, th.sizeHeading, th.sizeTitle, th.sizeNumeric, th.sizeSmall};
    for (int i = 0; i < dsettings::UiFonts::kScreenFonts; ++i)
        s.px[i] = std::round(design[i] * lf.scale);
    return s;
}

// The bound host screen, whichever it is.
bool hostOpen(const TabUi& ui) {
    switch (ui.kind) {
        case drakan_tab::HostKind::Strip:
            return ui.strip.isOpen();
        case drakan_tab::HostKind::Map:
            return ui.map.isOpen();
        default:
            return ui.screen.isOpen();
    }
}
bool hostClosePending(const TabUi& ui) {
    switch (ui.kind) {
        case drakan_tab::HostKind::Strip:
            return ui.strip.closePending();
        case drakan_tab::HostKind::Map:
            return false;
        default:
            return ui.screen.closePending();
    }
}
void hostOpenScreen(TabUi& ui, const gameui::Snapshot& s) {
    switch (ui.kind) {
        case drakan_tab::HostKind::Strip:
            ui.strip.open(s);
            break;
        case drakan_tab::HostKind::Map:
            ui.map.open(s);
            break;
        default:
            ui.screen.open(s);
            break;
    }
}
void hostCloseScreen(TabUi& ui) {
    ui.screen.close();
    ui.strip.close();
    ui.map.close();
}

void tabDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    using drakan_tab::HostKind;
    auto& sh = drakan_tab::shared();
    TabUi& ui = g_tabUi;
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    const auto snap = sh.model.acquire();
    const bool active = sh.active.load();
    // DRAKAN_DEBUG_TAB_HIDE=1 (test aid): the host screen never draws, so the EE watchdog's native fallback runs.
    static const bool debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TAB_HIDE");
        return e && std::atoi(e) > 0;
    }();
    // Closing: the host screen stays up (last picture, no input) until screen 0x15 is gone, which the views (Tab
    // screen, strip, map) make take 2 EE frames by finishing its close animation at once (drakan_inventory_views.inl
    // hookViewDraw), then fades out quickly (~40 ms); the HUD switches in the middle of that fade (Shared::hostFade,
    // hookViewHud).
    const bool closing = sh.closing.load();
    const bool show = active && snap && snap->native.valid && !debugHide;
    // Keep the modern menu behind native help / confirmation boxes. Present their text and buttons with the menu's
    // own renderer (or reapply the native crop for unsupported controls); native widgets still own input. Until bounds
    // arrive, step aside so an unavailable crop can never cover the native buttons.
    const bool box = sh.boxUp.load();
    const drakan_tab::NativeDialog dialog = sh.nativeDialog();
    const bool compositeDialog = box && dialog.valid && f.frame && f.frameW > 0 && f.frameH > 0;
    const bool visible = show && (!box || compositeDialog);
    constexpr float fadeOut = 24.0f;
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 7.0f) : std::max(0.0f, ui.fade - dt * fadeOut);
    sh.hostFade.store(ui.fade);

    ui.renderer.setFonts(*f.fonts);
    ui.renderer.setSkin(f.skin);
    if (show && !closing && sh.session.load() != ui.session) {
        ui.session = sh.session.load();
        ui.kind = sh.kind.load();
        gameui::ScreenConfig cfg;
        cfg.measure = ui.renderer.measure();
        hostCloseScreen(ui);
        ui.screen = gameui::InventoryScreen(cfg);
        ui.strip = gameui::StripScreen(cfg);
        ui.map = gameui::MapScreen(cfg);
        hostOpenScreen(ui, *snap);
        ui.gridSerial = 0;
        ui.gridTime = -10;
        ui.mapSerial = 0;
        ui.mapTime = -10;
        ui.gate.reset(); // the key that opened the screen is held over: it counts only after its release
        // Start mostly opaque: the first frames under the screen are the native open (the page window cleared, the
        // world crop settling), which a slow fade-in would show.
        ui.fade = std::max(ui.fade, 0.6f);
        sh.hostFade.store(ui.fade);
        std::fprintf(stderr, "[tab-screen] overlay: session %u opened (host screen %d)\n", ui.session,
                     static_cast<int>(ui.kind));
    }
    while (auto r = sh.results.pop()) {
        if (r->kind == gameui::ActionKind::Close && r->outcome != gameui::Outcome::Confirmed)
            std::fprintf(stderr, "[tab-screen] overlay: close %s, screen back\n", gameui::outcomeName(r->outcome));
        if (ui.kind == HostKind::Strip)
            ui.strip.onResult(*r);
        else if (ui.kind == HostKind::Tab)
            ui.screen.onResult(*r);
    }
    if (show && !closing && snap->native.inventoryOpen && !hostOpen(ui) && !hostClosePending(ui)) {
        // Bound and not closing, but the host screen is closed (it must never stay invisible over the hidden page).
        std::fprintf(stderr, "[tab-screen] overlay: session %u reopened\n", ui.session);
        hostOpenScreen(ui, *snap);
    }
    if (ui.fade <= 0.0f) {
        if (!active && hostOpen(ui))
            hostCloseScreen(ui);
        ui.took = false;
        (void)drakan_input::module().drainHostScreenEvents();
        return;
    }

    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = f.input && visible && !box && !closing;
    if (take && !ui.took)
        ui.gate.reset(); // input back (F1 menu, native box, fade-in): keys held meanwhile wait for their release
    ui.took = take;
    const gameui::UiInput in = drakan_input::module().scripted() ? tabScriptInput(take, f.w, f.h) : tabInput(take);
    ui.renderer.setGamepadPrompts(ui.device == gameui::InputDevice::Gamepad);
    const drakan_tab::MapCapture mc = sh.mapCapture();
    if (visible && hostOpen(ui)) {
        gameui::FrameOutput out;
        if (ui.kind == HostKind::Strip) {
            out = ui.strip.frame(*snap, in, vp);
        } else if (ui.kind == HostKind::Map) {
            // The markers the EE drew (GUI units of the centred page) -> back-buffer pixels.
            gameui::MapInput mi;
            mi.location = sh.locationName();
            const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
            for (const drakan_tab::MapMarkerGui& m : sh.mapMarkers())
                mi.markers.push_back({m.quest, mm.x(m.x), mm.y(m.y)});
            out = ui.map.frame(*snap, mi, in, vp);
            if (out.wantsClose) { // the native view's own close (Select)
                gameui::UiAction close;
                close.kind = gameui::ActionKind::Close;
                out.actions.push_back(close);
            }
        } else {
            out = ui.screen.frame(*snap, in, vp);
        }
        // With a native box up, input is empty. Passive selection requests can queue here; the EE executor
        // waits for the box to close before applying them.
        for (const gameui::UiAction& a : out.actions)
            sh.actions.push(a);
        if (!box && out.sound != gameui::UiSound::None)
            sh.sound.store(0x2au); // the game's focus tick ("Next Option"), as the native menus play on a move
        if (out.wantsClose)
            sh.closing.store(true);
        else
            ui.last = std::move(out.draw);
    }

    // Where the native grid goes (under the host grid: Inventory / Character page, the strip's bar), where the native
    // map goes (Map page, the M view left of the objectives panel) and where Rynn stands (Tab screen).
    const drakan_tab::NativeGrid ng = sh.nativeGrid();
    const PageMap pm = tabPageMap(g_overlayView, f.w, f.h, ng.frameW, ng.frameH);
    {
        const gameui::LayoutFrame lf = gameui::computeFrame(vp);
        drakan_tab::GridTarget t;
        drakan_tab::MapTarget mt;
        drakan_tab::PreviewTarget p;
        const bool placed = hostOpen(ui) || hostClosePending(ui); // kept through the close animation
        // Targets stay inside the native window, which the GS front end clips: a letterboxed game picture (21:9 /
        // 32:9 on a 16:9 output) put the host's design rectangles partly above / beside it (the Map page lost the
        // map's left part, the 32:9 grid its icons). The grid is moved / shrunk whole (the crops come from the cells
        // where it was drawn, NativeGrid), the map area clipped (the map is fitted into it).
        constexpr float kWindowMargin = 8.0f;
        const auto gridBox = [&](const gameui::Rect& a, const gameui::Rect& b) {
            const drakan_views::Box k = drakan_views::keepInWindow(
                {pm.gx(a.x), pm.gy(a.y), pm.gx(b.right()), pm.gy(b.bottom())}, pm.W, pm.H, kWindowMargin);
            return drakan_tab::GridTarget{true, k.x0, k.y0, k.x1, k.y1};
        };
        if (ui.kind == HostKind::Tab) {
            // The native grid stays under the host grid on the Inventory page, and under the opaque centre column on
            // the Character page (its equipped gear shows the same crops).
            const gameui::Page page = ui.screen.page();
            if (placed && (page == gameui::Page::Inventory || page == gameui::Page::Character))
                t = gridBox(lf.map(gameui::design::gridCell(0)), lf.map(gameui::design::gridCell(23)));
            if (placed && page == gameui::Page::Map) {
                const gameui::Rect a = lf.map(gameui::design::mapImageArea());
                const drakan_views::Box k = drakan_views::clipToWindow(
                    {pm.gx(a.x), pm.gy(a.y), pm.gx(a.right()), pm.gy(a.bottom())}, pm.W, pm.H, kWindowMargin);
                if (k.w() > 32 && k.h() > 32)
                    mt = {true, k.x0, k.y0, k.x1, k.y1};
            }
            // Rynn's centre: the column's, kept inside the game picture (a 4:3 pillarbox cut her in half); her
            // on-screen half width follows the picture height.
            const gameui::Rect pr = ui.screen.previewRect();
            if (!pr.empty())
                p = {true, drakan_tab::detail::previewCentreInPicture(pr.cx(), pm.vx, pm.vw, 0.2f * pm.vh), 1.0f};
        } else if (ui.kind == HostKind::Strip) {
            // At about the Tab screen's scale, not at the (smaller) host cells': the native mesh draw skips an item
            // whose texture is not resident at a small size (the elixir never appeared at the cells' scale); the
            // crops are fitted into the host cells. With the renderer's grid capture (d3d11) the grid is kept whole
            // inside the native window, which the GS front end clips (a 21:9 / 32:9 picture on a 16:9 output put the
            // plate below the picture: the dagger row, at 32:9 every icon, was cut), and the renderer removes it from
            // the presented frame (it showed over the open world where the plate does not cover it). Without the
            // capture it stays under the strip's opaque plate.
            const gameui::Rect a = gameui::StripScreen::layoutFor(vp).nativeGrid;
            if (g_overlayView.gridCapture)
                t = gridBox(a, a);
            else
                t = {true, pm.gx(a.x), pm.gy(a.y), pm.gx(a.right()), pm.gy(a.bottom())};
        } else {
            // The M view: the native map fitted into the part of the screen left of the objectives panel (inside the
            // native window, which the GS front end clips at its edges).
            const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
            const gameui::Rect panel = gameui::MapScreen::panelRectFor(vp);
            const float margin = 12.0f * f.h / 1080.0f, top = 40.0f * f.h / 1080.0f;
            const float x0 = std::max(mm.gx(vp.x + margin), 8.0f);
            const float x1 = std::min(mm.gx(panel.x - 2 * margin), mm.W - 8);
            const float y0 = std::max(mm.gy(vp.y + top), 8.0f), y1 = std::min(mm.gy(vp.h - top), mm.H - 8);
            if (x1 > x0 + 32 && y1 > y0 + 32)
                mt = {true, x0, y0, x1, y1};
        }
        std::lock_guard<std::mutex> l(sh.gridM);
        sh.gridTarget = t;
        sh.mapTarget = mt;
        sh.preview = p;
    }
    if (mc.serial != ui.mapSerial) {
        ui.mapSerial = mc.serial;
        ui.mapTime = now;
        // On-screen aspect of the drawn map: GUI units are not square (the page is compressed by hudScaleX).
        const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
        const float cw = mm.x(mc.x1) - mm.x(mc.x0), ch = mm.y(mc.y1) - mm.y(mc.y0);
        ui.screen.setMapImageSize(mc.imageW, mc.imageH, ch > 0 ? cw / ch : 0.0f);
    }
    const bool frozen = closing || !active; // the frame copy is held (ScreenLayer::freezeFrame): its crops stay valid
    const bool mapFresh = (frozen && ui.mapTime > 0) || now - ui.mapTime < 0.3;
    if (ng.serial != ui.gridSerial) {
        ui.gridSerial = ng.serial;
        ui.gridTime = now;
    }
    const bool gridFresh = (frozen && ui.gridTime > 0) || now - ui.gridTime < 0.3;
    const dsettings::IconSource icons = [&](const gameui::ImageRef& ref, dsettings::IconCrop& c) {
        if (ref.kind == gameui::ImageKind::MapBackground) {
            // The native map image, drawn by the EE where the host's map area is (Map page).
            if (!mapFresh || f.frameW <= 0)
                return false;
            const PageMap mm = tabPageMap(g_overlayView, f.w, f.h, mc.frameW, mc.frameH);
            const float x0 = mm.x(mc.x0), y0 = mm.y(mc.y0), x1 = mm.x(mc.x1), y1 = mm.y(mc.y1);
            if (!(x1 > x0 && y1 > y0))
                return false;
            c.uv0 = ImVec2(x0 / f.frameW, y0 / f.frameH);
            c.uv1 = ImVec2(x1 / f.frameW, y1 / f.frameH);
            c.w = x1 - x0;
            c.h = y1 - y0;
            c.opaque = true;
            return true;
        }
        if (ref.kind != gameui::ImageKind::ItemIcon || !gridFresh || !snap || ref.index >= 24 || f.frameW <= 0)
            return false;
        const int slot = static_cast<int>(ref.index);
        const gameui::ItemEntry* it = snap->itemAtCell(slot);
        const int iw = it ? std::max(1, it->width) : 1, ih = it ? std::max(1, it->height) : 1;
        const int last = std::min(23, slot + (ih - 1) * 8 + (iw - 1));
        const drakan_tab::NativeCell& c0 = ng.cells[size_t(slot)];
        const drakan_tab::NativeCell& c1 = ng.cells[size_t(last)];
        const gsr::OverlayContext& oc = g_overlayView;
        if (ui.kind == HostKind::Strip && oc.gridSrv && oc.gridFrameW > 0 && oc.gridFrameH > 0) {
            // The renderer's off-screen copy of the grid (the presented frame no longer shows it): native target
            // pixels through the inventory page map, normalised to the copy.
            const float fw = float(oc.gridFrameW), fh = float(oc.gridFrameH);
            const float kx = ng.frameW ? fw / float(ng.frameW) : 1.0f, ky = ng.frameH ? fh / float(ng.frameH) : 1.0f;
            const float hs = oc.gridHudScale > 0.2f && oc.gridHudScale <= 1.0f ? oc.gridHudScale : 1.0f;
            const float anchor = oc.gridCentred ? 0.5f * fw : 0.0f;
            const auto u = [&](float gx) { return (anchor + (gx * kx - anchor) * hs) / fw; };
            const float u0 = u(c0.x0), u1 = u(c1.x1), v0 = c0.y0 * ky / fh, v1 = c1.y1 * ky / fh;
            if (!(u1 > u0 && v1 > v0))
                return false;
            c.uv0 = ImVec2(u0, v0);
            c.uv1 = ImVec2(u1, v1);
            // Its on-screen size for the aspect fit: the same cells through the presented page.
            c.w = pm.x(c1.x1) - pm.x(c0.x0);
            c.h = pm.y(c1.y1) - pm.y(c0.y0);
            c.texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(oc.gridSrv));
            return c.w > 0 && c.h > 0;
        }
        if (ui.kind == HostKind::Strip && oc.gridCapture)
            return false; // not captured yet: the frame copy does not hold the grid
        const float x0 = pm.x(c0.x0), y0 = pm.y(c0.y0), x1 = pm.x(c1.x1), y1 = pm.y(c1.y1);
        if (!(x1 > x0 && y1 > y0))
            return false;
        c.uv0 = ImVec2(x0 / f.frameW, y0 / f.frameH);
        c.uv1 = ImVec2(x1 / f.frameW, y1 / f.frameH);
        c.w = x1 - x0;
        c.h = y1 - y0;
        return true;
    };
    dsettings::GameUiCanvas canvas;
    canvas.frame = f.frame;
    canvas.frameW = f.frameW;
    canvas.frameH = f.frameH;
    canvas.additive = f.additive;
    canvas.opaque = f.opaque;
    // DRAKAN_DEBUG_TAB_PEEK=1 (test aid): the host screen runs (input, grid / map targets, watchdog) but draws nothing,
    // so screenshots show the native frame its icon and map crops are taken from.
    static const bool debugPeek = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TAB_PEEK");
        return e && std::atoi(e) > 0;
    }();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!debugPeek)
        ui.renderer.draw(dl, ui.last, canvas, icons, now, ui.fade);
    if (!debugPeek && compositeDialog) {
        const PageMap dm = tabPageMap(g_overlayView, f.w, f.h, dialog.frameW, dialog.frameH);
        // Use the rendered page's anchor: a box can be on top while the inventory remains the drawing screen.
        const float anchor = g_overlayView.hudAnchorX;
        const auto x = [&](float gx) { return dm.vx + dm.vw * (anchor + (gx / dm.W - anchor) * dm.hs); };
        const float x0 = std::clamp(x(dialog.x0), 0.0f, f.frameW);
        const float y0 = std::clamp(dm.y(dialog.y0), 0.0f, f.frameH);
        const float x1 = std::clamp(x(dialog.x1), 0.0f, f.frameW);
        const float y1 = std::clamp(dm.y(dialog.y1), 0.0f, f.frameH);
        if (x1 > x0 && y1 > y0) {
            if (dialog.content.valid) {
                auto content = dialog.content;
                for (auto& b : content.buttons) {
                    const gameui::Rect r = b.rect;
                    b.rect = {x(r.x), dm.y(r.y), x(r.right()) - x(r.x), dm.y(r.bottom()) - dm.y(r.y)};
                }
                const auto draw = gameui::drawDialog(content, {x0, y0, x1 - x0, y1 - y0}, vp, ui.renderer.measure());
                ui.renderer.draw(dl, draw, canvas, icons, now, 1.0f);
            } else {
                if (f.opaque)
                    dl->AddCallback(f.opaque, nullptr);
                dl->AddImage(f.frame, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x0 / f.frameW, y0 / f.frameH),
                             ImVec2(x1 / f.frameW, y1 / f.frameH));
                if (f.opaque)
                    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
            }
        }
    }
    if (visible && ui.last.size() > 0)
        sh.hostDrawn.fetch_add(1); // the EE watchdog (drakan_tab_screen.inl tabWatchdog)

    if (in.pointerValid && ui.device == gameui::InputDevice::KeyboardMouse && f.input && !sh.boxUp.load())
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}

// ---- the modern Esc (pause) menu (gameui PauseScreen; EE side drakan_pause_screen.inl) ----
struct PauseUi {
    gameui::PauseScreen screen;
    gameui::DrawList last;
    uint32_t session = 0, reactivated = 0;
    float fade = 0;
    double lastTime = -1;
};
PauseUi g_pauseUi;

bool pauseShown() {
    return drakan_pause_screen::shared().active.load() || g_pauseUi.fade > 0.0f;
}

// ---- the modern PC Settings pages (inside the modern pause / title menus): dsettings::ModernSettings shares the F1
// overlay's host and store (runtime/gameui/README.md, "PC Settings glue contract") ----
std::unique_ptr<dsettings::ModernSettings> g_pcs;
bool g_pcsOpen = false, g_pcsOverlayOpen = false;

// The schema for this frame's PauseState / TitleState (null without the pages): the files are read again when the
// pages open and after the F1 overlay was closed over them (it may have saved).
const gameui::SettingsSchema* pcSettingsSchema(bool pagesOpen, double now) {
    if (!g_pcs)
        return nullptr;
    const bool overlay = g_overlay && g_overlay->isOpen();
    if (pagesOpen && (!g_pcsOpen || (g_pcsOverlayOpen && !overlay)))
        g_pcs->open();
    g_pcsOpen = pagesOpen;
    g_pcsOverlayOpen = overlay;
    g_pcs->tick(now);
    return &g_pcs->schema();
}

// The input of a host screen's frame: the real devices or the script's events. `capture`: the PC Settings pages wait
// for a key (g_capturedKey).
gameui::UiInput hostScreenInput(bool take, bool capture, float w, float h) {
    g_capture = capture;
    gameui::UiInput in = drakan_input::module().scripted() ? tabScriptInput(take, w, h) : tabInput(take);
    g_capture = false;
    return in;
}

// Feedback sounds of the host-drawn menus (title, pause, Options pages, Save / Load) through the game's own UI sounds
// (drakan_menu_screens::Shared::uiSound, played by the EE glue). A frame whose actions make the glue press or tap a
// native widget leaves the sound to that widget (its click, its check box, START's back sound), so nothing plays
// twice.
void hostUiSound(gameui::UiSound s, bool nativeSound) {
    using S = gameui::UiSound;
    if (s == S::None || nativeSound)
        return;
    const uint32_t property = s == S::Confirm  ? 0x24u
                              : s == S::Back   ? 0x2du
                              : s == S::Slider ? 0x27u
                              : s == S::Toggle ? 0x28u
                                               : 0x2au; // Move, Page: the focus tick
    drakan_menu_screens::shared().uiSound.store(property);
}
// Pause / Options actions the glue carries out with a native press or tap (drakan_pause_screen.inl).
bool nativeSound(const std::vector<gameui::PauseAction>& actions) {
    using K = gameui::PauseActionKind;
    for (const gameui::PauseAction& a : actions)
        switch (a.kind) {
            case K::Resume:
            case K::OpenSave:
            case K::OpenLoad:
            case K::OpenOptions:
            case K::Quit:
            case K::OpenController:
            case K::OpenAudio:
            case K::DefaultSettings:
            case K::EnterPage:
            case K::LeavePage:
            case K::SetSubtitles:
            case K::SetStereo:
            case K::SetVibration:
            case K::SetReversePitch:
            case K::SetControllerScheme:
            case K::RestoreDefaults:
            case K::QuitToTitle:
                return true;
            default:
                break;
        }
    return false;
}

// `background`: another host screen is on top this frame (the modern Save / Load screen over the pause or the title):
// this one only fades and draws, it reads no input (the events are the top screen's).
void pauseDraw(const dsettings::SettingsOverlay::ScreenFrame& f, bool background = false) {
    auto& sh = drakan_pause_screen::shared();
    PauseUi& ui = g_pauseUi;
    TabUi& tu = g_tabUi; // shared renderer, key gate and device
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    const auto snap = sh.model.acquire();
    const bool active = sh.active.load();
    // DRAKAN_DEBUG_PAUSE_HIDE (test aid): 1 = the host pause never draws, so the EE watchdog's native fallback runs;
    // 2 = it stops drawing once a host page (Options, Audio, Controller) is open, i.e. while an owned native options
    // screen is up under it.
    static const int debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_PAUSE_HIDE");
        return e ? std::atoi(e) : 0;
    }();
    const bool child = sh.childUp.load(), resuming = sh.resuming.load();
    const bool hidden = debugHide == 1 || (debugHide == 2 && ui.session == sh.session.load() && ui.screen.isOpen() &&
                                           ui.screen.page() != gameui::PausePage::Root);
    const bool show = active && snap && !hidden;
    const bool visible = show && !child && !resuming;
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 8.0f) : std::max(0.0f, ui.fade - dt * 10.0f);
    sh.hostFade.store(ui.fade);
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    if (show && sh.session.load() != ui.session) {
        ui.session = sh.session.load();
        ui.reactivated = sh.reactivated.load();
        gameui::PauseConfig cfg;
        cfg.screen.measure = tu.renderer.measure();
        // Stage 2 / 3: host Options / Audio / Controller pages over the hidden native screens 9 / 13 / 10, the host
        // Default Settings and Quit cards (drakan_pause_screen.inl executes their actions).
        cfg.hostPages = true;
        cfg.quitCard = true;
        // One Options screen: the PC settings pages with the game options folded in (F1 stays the overlay).
        cfg.pcSettingsPages = cfg.unifiedOptions = g_pcs != nullptr;
        ui.screen = gameui::PauseScreen(cfg);
        ui.screen.open();
        tu.gate.reset(); // Esc / START that opened the pause count only after their release
        ui.fade = std::max(ui.fade, 0.5f);
        std::fprintf(stderr, "[pause-screen] overlay: session %u opened\n", ui.session);
    }
    if (const uint32_t r = sh.reactivated.load(); r != ui.reactivated) {
        ui.reactivated = r;
        ui.screen.reactivate(); // back from a native child (or its row was refused)
    }
    if (show && !resuming && !ui.screen.isOpen()) {
        std::fprintf(stderr, "[pause-screen] overlay: session %u reopened\n", ui.session);
        ui.screen.open();
    }
    if (ui.fade <= 0.0f) {
        if (!active && ui.screen.isOpen())
            ui.screen.close();
        if (!background) {
            tu.took = false;
            (void)drakan_input::module().drainHostScreenEvents();
        }
        return;
    }
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = !background && f.input && visible;
    const bool capture = ui.screen.pcSettingsOpen() && ui.screen.pcSettings().capturing();
    gameui::UiInput in;
    if (!background) {
        if (take && !tu.took)
            tu.gate.reset(); // input back (F1 menu, native child closed): held keys wait for their release
        tu.took = take;
        in = hostScreenInput(take, capture, f.w, f.h);
    }
    tu.renderer.setGamepadPrompts(tu.device == gameui::InputDevice::Gamepad);
    if (visible && ui.screen.isOpen() && snap) {
        gameui::PauseState st = sh.pauseState();
        st.pcSettings = pcSettingsSchema(ui.screen.pcSettingsOpen(), now);
        st.now = now;
        st.capturedKey = take && capture ? g_capturedKey : std::string();
        gameui::PauseFrameOutput out = ui.screen.frame(*snap, st, in, vp);
        for (const gameui::PauseAction& a : out.actions)
            sh.actions.push(a);
        hostUiSound(out.sound, nativeSound(out.actions));
        if (out.wantsResume)
            sh.resuming.store(true);
        if (g_pcs && !out.settings.empty())
            g_pcs->applyAll(out.settings, ui.screen.pcSettings());
        ui.last = std::move(out.draw);
    }
    dsettings::GameUiCanvas canvas;
    canvas.frame = f.frame;
    canvas.frameW = f.frameW;
    canvas.frameH = f.frameH;
    canvas.additive = f.additive;
    canvas.opaque = f.opaque;
    const dsettings::IconSource none = [](const gameui::ImageRef&, dsettings::IconCrop&) { return false; };
    tu.renderer.draw(ImGui::GetBackgroundDrawList(), ui.last, canvas, none, now, ui.fade);
    if (visible && ui.last.size() > 0)
        sh.hostDrawn.fetch_add(1); // the EE watchdog (drakan_pause_screen.inl pauseTick)
    if (in.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse && f.input && visible)
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}

// ---- the modern title menu and Save / Load screens (gameui TitleScreen / SaveLoadScreen; EE side
// drakan_menu_screens.inl) ----

// A slot's picture as a texture: the HD companion when the save has one, else the native 128 x 128 thumbnail (a 4:3
// picture: the native grid draws it at 128 x 96). The companions (about 3 MB of PNG each) are decoded on a worker
// thread (std::async) so neither the present thread nor the screen waits for them; the native thumbnail (or the
// crest) stands in until the decode is done. A decode whose slot changed meanwhile is dropped when it finishes.
struct PreviewDecode {
    gsr::Image img;
    unsigned x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the picture inside the companion's black bars
    double ms = 0;
};
PreviewDecode decodePreview(std::string path) {
    PreviewDecode r;
    const auto t0 = std::chrono::steady_clock::now();
    if (gsr::readImage(path.c_str(), r.img) && r.img) {
        const gsr::Image& img = r.img;
        unsigned x0 = img.width, y0 = img.height, x1 = 0, y1 = 0;
        for (unsigned y = 0; y < img.height; y += 2)
            for (unsigned x = 0; x < img.width; x += 2) {
                const uint8_t* q = img.rgba.data() + (size_t(y) * img.width + x) * 4;
                if (q[0] + q[1] + q[2] > 24) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x + 2);
                    y1 = std::max(y1, y + 2);
                }
            }
        if (x1 <= x0 || y1 <= y0)
            x0 = y0 = 0, x1 = img.width, y1 = img.height;
        r.x0 = x0;
        r.y0 = y0;
        r.x1 = std::min(x1, img.width);
        r.y1 = std::min(y1, img.height);
    } else {
        r.img = {};
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

struct PreviewTexture {
    uint64_t key = 0;
    std::string hd;
    ImTextureID tex = 0; // the overlay renderer's texture
    bool hdShown = false;
    float w = 0, h = 0;
    ImVec2 uv0{0, 0}, uv1{1, 1};
    std::future<PreviewDecode> decode; // the HD companion being read
};
std::vector<std::pair<int, PreviewTexture>> g_previews;
std::vector<std::future<PreviewDecode>> g_previewDrops; // decodes of replaced entries, dropped once finished
uint64_t g_previewSerial = ~0ull;

ImTextureID makeTexture(const void* rgba, unsigned w, unsigned h) {
    return g_overlay && rgba && w && h ? g_overlay->renderer().createTexture(rgba, w, h) : 0;
}
void dropTexture(ImTextureID& tex) {
    if (tex && g_overlay)
        g_overlay->renderer().releaseTexture(tex);
    tex = 0;
}

bool futureReady(const std::future<PreviewDecode>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// Present thread, before this frame's draw lists (an SRV replaced here is not referenced by a pending draw).
void updatePreviewTextures() {
    std::erase_if(g_previewDrops, [](const std::future<PreviewDecode>& f) { return !f.valid() || futureReady(f); });
    auto& sh = drakan_menu_screens::shared();
    std::vector<std::pair<int, drakan_menu_screens::SlotPicture>> pics;
    {
        std::lock_guard<std::mutex> l(sh.m);
        if (sh.picturesSerial != g_previewSerial) {
            g_previewSerial = sh.picturesSerial;
            pics = sh.pictures;
        }
    }
    for (auto& [n, pic] : pics) {
        PreviewTexture* t = nullptr;
        for (auto& p : g_previews)
            if (p.first == n)
                t = &p.second;
        if (!t) {
            g_previews.emplace_back(n, PreviewTexture{});
            t = &g_previews.back().second;
        }
        if (t->key == pic.key && t->hd == pic.hdPath &&
            (t->tex || t->decode.valid() || (!pic.key && pic.hdPath.empty())))
            continue;
        dropTexture(t->tex);
        if (t->decode.valid())
            g_previewDrops.push_back(std::move(t->decode));
        *t = {};
        t->key = pic.key;
        t->hd = pic.hdPath;
        if (!pic.hdPath.empty())
            t->decode = std::async(std::launch::async, decodePreview, pic.hdPath);
        if (pic.key && pic.pixels.size() == 128 * 128) { // the native thumbnail meanwhile (or for good)
            t->tex = makeTexture(pic.pixels.data(), 128, 128);
            // Rows top down, as stored (compared with the native grid's picture of the same save).
            t->uv0 = ImVec2(0, 0);
            t->uv1 = ImVec2(1, 1);
            t->w = 128;
            t->h = 96;
        }
        if (pic.hdPath.empty())
            std::fprintf(stderr, "[menu-screens] overlay: slot %d picture %s\n", n,
                         t->tex ? "native thumbnail" : "none");
    }
    for (auto& [n, t] : g_previews) {
        if (!futureReady(t.decode))
            continue;
        PreviewDecode r = t.decode.get();
        ImTextureID tex = r.img ? makeTexture(r.img.rgba.data(), r.img.width, r.img.height) : 0;
        std::fprintf(stderr, "[menu-screens] overlay: slot %d picture %s (decoded in %.0f ms on a worker, t=%.3f)\n", n,
                     tex ? "HD companion" : (t.tex ? "native thumbnail (HD failed)" : "none"), r.ms,
                     drakan_menu_screens::clockSeconds());
        if (!tex)
            continue;
        dropTexture(t.tex);
        t.tex = tex;
        t.hdShown = true;
        t.uv0 = ImVec2(float(r.x0) / r.img.width, float(r.y0) / r.img.height);
        t.uv1 = ImVec2(float(r.x1) / r.img.width, float(r.y1) / r.img.height);
        t.w = float(r.x1 - r.x0);
        t.h = float(r.y1 - r.y0);
    }
}

bool previewCrop(const gameui::ImageRef& ref, dsettings::IconCrop& c) {
    if (ref.kind != gameui::ImageKind::SavePreview)
        return false; // TitleArt: the native art shows under the menu (TitleState::backdrop stays None)
    for (const auto& [n, t] : g_previews)
        if (n == static_cast<int>(ref.index) && t.tex) {
            c.texture = t.tex;
            c.uv0 = t.uv0;
            c.uv1 = t.uv1;
            c.w = t.w;
            c.h = t.h;
            return true;
        }
    return false;
}

void drawHostList(const dsettings::SettingsOverlay::ScreenFrame& f, const gameui::DrawList& list, double now,
                  float fade, const dsettings::IconSource& icons) {
    dsettings::GameUiCanvas canvas;
    canvas.frame = f.frame;
    canvas.frameW = f.frameW;
    canvas.frameH = f.frameH;
    canvas.additive = f.additive;
    canvas.opaque = f.opaque;
    g_tabUi.renderer.draw(ImGui::GetBackgroundDrawList(), list, canvas, icons, now, fade);
}

struct TitleUi {
    gameui::TitleScreen screen;
    gameui::DrawList last;
    uint32_t session = 0, reactivated = 0;
    float fade = 0;
    double lastTime = -1;
};
TitleUi g_titleUi;

bool titleShown() {
    return drakan_menu_screens::shared().titleActive.load() || g_titleUi.fade > 0.0f;
}

void titleDraw(const dsettings::SettingsOverlay::ScreenFrame& f, bool background = false) {
    auto& sh = drakan_menu_screens::shared();
    TitleUi& ui = g_titleUi;
    TabUi& tu = g_tabUi;
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    // DRAKAN_DEBUG_TITLE_HIDE=1 (test aid): the host title never draws, so the EE watchdog's native fallback runs.
    static const bool debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_TITLE_HIDE");
        return e && std::atoi(e) > 0;
    }();
    const bool active = sh.titleActive.load();
    const bool show = active && !debugHide;
    const bool visible = show && !sh.titleChild.load() && !sh.titleCovered.load();
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 8.0f) : std::max(0.0f, ui.fade - dt * 10.0f);
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    if (show && sh.titleSession.load() != ui.session) {
        ui.session = sh.titleSession.load();
        ui.reactivated = sh.titleReactivated.load();
        gameui::TitleConfig cfg;
        cfg.screen.measure = tu.renderer.measure();
        cfg.pcSettingsPages = cfg.unifiedOptions = g_pcs != nullptr;
        ui.screen = gameui::TitleScreen(cfg);
        ui.screen.open();
        tu.gate.reset();
        ui.fade = std::max(ui.fade, 0.5f);
        std::fprintf(stderr, "[menu-screens] overlay: title session %u opened\n", ui.session);
    }
    if (const uint32_t r = sh.titleReactivated.load(); r != ui.reactivated) {
        ui.reactivated = r;
        ui.screen.reactivate();
    }
    if (show && !ui.screen.isOpen())
        ui.screen.open();
    if (ui.fade <= 0.0f) {
        if (!active && ui.screen.isOpen())
            ui.screen.close();
        if (!background) {
            tu.took = false;
            (void)drakan_input::module().drainHostScreenEvents();
        }
        return;
    }
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = !background && f.input && visible;
    const bool capture = ui.screen.pcSettingsOpen() && ui.screen.pcSettings().capturing();
    gameui::UiInput in;
    if (!background) {
        if (take && !tu.took)
            tu.gate.reset();
        tu.took = take;
        in = hostScreenInput(take, capture, f.w, f.h);
    }
    tu.renderer.setGamepadPrompts(tu.device == gameui::InputDevice::Gamepad);
    if (visible && ui.screen.isOpen()) {
        gameui::TitleState st = sh.titleState();
        st.pcSettings = pcSettingsSchema(ui.screen.pcSettingsOpen(), now);
        st.now = now;
        st.capturedKey = take && capture ? g_capturedKey : std::string();
        gameui::TitleFrameOutput out = ui.screen.frame(st, in, vp);
        for (const gameui::TitleAction& a : out.actions) {
            std::fprintf(stderr, "[menu-screens] overlay t=%.3f: title action %s(%d)\n",
                         drakan_menu_screens::clockSeconds(), gameui::titleActionName(a.kind), a.value);
            sh.titleActions.push(a);
        }
        for (const gameui::PauseAction& a : out.optionActions)
            sh.titleOptionActions.push(a);
        // Title rows other than Quit to Desktop are pressed natively (focus + Cross: the row's own click).
        bool titleNative = nativeSound(out.optionActions);
        for (const gameui::TitleAction& a : out.actions)
            titleNative = titleNative || a.kind != gameui::TitleActionKind::QuitToDesktop;
        hostUiSound(out.sound, titleNative);
        if (g_pcs && !out.settings.empty())
            g_pcs->applyAll(out.settings, ui.screen.pcSettings());
        ui.last = std::move(out.draw);
    }
    drawHostList(f, ui.last, now, ui.fade, [](const gameui::ImageRef&, dsettings::IconCrop&) { return false; });
    if (visible && ui.last.size() > 0)
        sh.titleDrawn.fetch_add(1); // the EE watchdog (drakan_menu_screens.inl titleTick)
    if (in.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse && f.input && visible)
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}

struct SlotsUi {
    gameui::SaveLoadScreen screen;
    gameui::DrawList last;
    uint32_t session = 0, reactivated = 0;
    float fade = 0;
    double lastTime = -1;
};
SlotsUi g_slotsUi;

bool slotsShown() {
    return drakan_menu_screens::shared().slotsActive.load() || g_slotsUi.fade > 0.0f;
}

void slotsDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    auto& sh = drakan_menu_screens::shared();
    SlotsUi& ui = g_slotsUi;
    TabUi& tu = g_tabUi;
    const double now = ImGui::GetTime();
    const float dt = ui.lastTime < 0 ? 0.0f : std::clamp(static_cast<float>(now - ui.lastTime), 0.0f, 0.1f);
    ui.lastTime = now;
    // DRAKAN_DEBUG_SLOTS_HIDE=1 (test aid): the host slot screen never draws, so the EE watchdog's fallback runs.
    static const bool debugHide = [] {
        const char* e = std::getenv("DRAKAN_DEBUG_SLOTS_HIDE");
        return e && std::atoi(e) > 0;
    }();
    const bool active = sh.slotsActive.load(), leaving = sh.slotsLeaving.load();
    const bool show = active && !debugHide;
    const bool visible = show && !sh.slotsChild.load() && !leaving;
    ui.fade = visible ? std::min(1.0f, ui.fade + dt * 8.0f) : std::max(0.0f, ui.fade - dt * 10.0f);
    tu.renderer.setFonts(*f.fonts);
    tu.renderer.setSkin(f.skin);
    if (show && sh.slotsSession.load() != ui.session) {
        ui.session = sh.slotsSession.load();
        ui.reactivated = sh.slotsReactivated.load();
        gameui::SaveLoadConfig cfg;
        cfg.screen.measure = tu.renderer.measure();
        ui.screen = gameui::SaveLoadScreen(cfg);
        gameui::SaveLoadMode mode;
        gameui::SaveLoadContext ctx;
        {
            std::lock_guard<std::mutex> l(sh.m);
            mode = sh.mode;
            ctx = sh.context;
        }
        ui.screen.open(mode, ctx);
        tu.gate.reset();
        ui.fade = std::max(ui.fade, 0.5f);
        std::fprintf(stderr, "[menu-screens] overlay: slot session %u opened\n", ui.session);
    }
    if (const uint32_t r = sh.slotsReactivated.load(); r != ui.reactivated) {
        ui.reactivated = r;
        if (!ui.screen.isOpen()) { // Back was not taken: the screen again
            std::lock_guard<std::mutex> l(sh.m);
            ui.screen.open(sh.mode, sh.context);
        }
        ui.screen.reactivate();
    }
    if (ui.fade <= 0.0f) {
        if (!active && ui.screen.isOpen())
            ui.screen.close();
        tu.took = false;
        (void)drakan_input::module().drainHostScreenEvents();
        return;
    }
    updatePreviewTextures();
    const gameui::Viewport vp{0, 0, f.w, f.h};
    const bool take = f.input && visible;
    if (take && !tu.took)
        tu.gate.reset();
    tu.took = take;
    const gameui::UiInput in = hostScreenInput(take, false, f.w, f.h);
    tu.renderer.setGamepadPrompts(tu.device == gameui::InputDevice::Gamepad);
    if (visible && ui.screen.isOpen()) {
        const gameui::SaveSlotsState st = sh.slotsState();
        gameui::SaveLoadFrameOutput out = ui.screen.frame(st, in, vp);
        for (const gameui::SaveLoadAction& a : out.actions) {
            std::fprintf(stderr, "[menu-screens] overlay t=%.3f: slot action %s(%d)\n",
                         drakan_menu_screens::clockSeconds(), gameui::saveLoadActionName(a.kind), a.value);
            sh.slotActions.push(a);
        }
        if (out.wantsBack)
            sh.slotsLeaving.store(true);
        hostUiSound(out.sound, out.wantsBack); // Back is START natively: the screen's own back sound
        ui.last = std::move(out.draw);
    }
    drawHostList(f, ui.last, now, ui.fade, &previewCrop);
    if (visible && ui.last.size() > 0)
        sh.slotsDrawn.fetch_add(1); // the EE watchdog (drakan_menu_screens.inl slotsTick)
    if (in.pointerValid && tu.device == gameui::InputDevice::KeyboardMouse && f.input && visible)
        dsettings::drawMenuCursor(in.pointerX, in.pointerY, std::clamp(f.h / 1080.0f, 0.62f, 3.0f));
}

// The overlay has one screen layer. The modern Save / Load screen is drawn over its parent (the pause or the title,
// fading out under it), else the title, the pause menu, or the inventory hosts.
void hostScreenDraw(const dsettings::SettingsOverlay::ScreenFrame& f) {
    if (const int quick = drakan_menu_screens::shared().quickOperation.load()) {
        // Keep the native pause, scan and card screens behind one progress card, from the shortcut until gameplay.
        auto& ui = g_tabUi;
        ui.renderer.setFonts(*f.fonts);
        ui.renderer.setSkin(f.skin);
        gameui::SaveLoadConfig cfg;
        cfg.screen.measure = ui.renderer.measure();
        gameui::SaveLoadScreen progress(cfg);
        progress.open(quick == 1 ? gameui::SaveLoadMode::Save : gameui::SaveLoadMode::Load,
                      gameui::SaveLoadContext::Pause);
        gameui::SaveSlotsState st;
        st.quickOperation = quick;
        st.busy = true;
        const auto out = progress.frame(st, {}, {0, 0, f.w, f.h});
        drawHostList(f, out.draw, ImGui::GetTime(), 1.0f, {});
        drakan_menu_screens::shared().slotsDrawn.fetch_add(1);
        drakan_pause_screen::shared().hostDrawn.fetch_add(1);
        return;
    }
    { // Test aid: DRAKAN_MENU_SHOTS=<ms> writes a window shot every <ms> while a title / Save / Load screen is up
      // and for 4 s after (<DRAKAN_SHOT_DIR>/ms_<milliseconds of the log clock>.png): frame series of the menus.
        static const int every = [] {
            const char* e = std::getenv("DRAKAN_MENU_SHOTS");
            return e ? std::atoi(e) : 0;
        }();
        static double next = 0, until = 0;
        if (every > 0) {
            const double t = drakan_menu_screens::clockSeconds();
            if (slotsShown() || titleShown())
                until = t + 4.0;
            if (t < until && t >= next) {
                next = t + every / 1000.0;
                const char* dir = std::getenv("DRAKAN_SHOT_DIR");
                char nm[600];
                std::snprintf(nm, sizeof nm, "%s/ms_%07.0f.png", dir ? dir : ".", t * 1000.0);
                gsr::requestWindowShot(nm);
            }
        }
    }
    if (slotsShown()) {
        if (pauseShown())
            pauseDraw(f, true);
        else if (titleShown())
            titleDraw(f, true);
        slotsDraw(f);
    } else if (titleShown()) {
        titleDraw(f);
    } else if (pauseShown()) {
        pauseDraw(f);
    } else {
        tabDraw(f);
    }
}

struct DrakanSettingsHost final : CommonSettingsHost {
    DrakanSettingsHost() : CommonSettingsHost(g_audio) {}
    dsettings::HostCaps capabilities() const override {
        dsettings::HostCaps c = commonCapabilities();
        c.exclusiveFullscreen = false;
        c.frameLimiter = true;
        c.textureUpscale = c.anisotropic = c.mipBias = true;
        c.textureDump = true;
        c.textureUpscaleFilters = {"default", "xbrz", "smooth"};
        c.languageSelect = true;
        c.promptStyle = true;
        c.dlss = drakan_dlss::available();
        c.dlssStatus = drakan_dlss::status();
        c.dlssNeural = drakan_dlss::neuralAvailable();
        c.dlssNeuralStatus = drakan_dlss::neuralStatus();
        c.modernWater = g_gs && g_gs->waterRenderingAvailable(c.modernWaterStatus);
        if (g_gs && c.modernWater)
            c.modernWaterStatus = g_gs->waterRenderingStatus();
        if (g_gs)
            c.rayTracingStatus = g_gs->rayTracingStatus();
#if defined(PS2X_GSR_RENDERER_CHOICE)
        c.rendererChoice = true;
#endif
        c.audioDeviceSelect = true;
        return c;
    }

    dsettings::MonitorInfo monitor() const override {
        dsettings::MonitorInfo m;
        int w = 0, h = 0;
        if (drakan_win::monitorSize(w, h)) {
            m.nativeWidth = w;
            m.nativeHeight = h;
            const double refresh = drakan_win::refreshSeconds();
            if (refresh > 0)
                m.refreshHz = static_cast<int>(std::lround(1.0 / refresh));
        }
        return m;
    }

    void windowSize(int& w, int& h) const override {
        int cw = 0, ch = 0;
        if (g_parent && drakan_win::clientSize(cw, ch)) {
            w = cw;
            h = ch;
        }
    }

    dsettings::ApplyResult applyVideo(const dsettings::VideoSettings& n, const dsettings::VideoSettings& p) override {
        using dsettings::Aspect;
        using dsettings::WindowMode;
        dsettings::ApplyResult r;
        if (!g_gs)
            return r;
        if (n.vsync != p.vsync)
            g_gs->setVsync(n.vsync);
        g_frameLimit.store(std::max(0, n.frameLimit));
        if (n.windowMode != p.windowMode && g_ops.requestFullscreen)
            g_ops.requestFullscreen(g_ops.ctx, n.windowMode != WindowMode::Windowed);
        if (n.windowMode == WindowMode::Windowed &&
            (n.windowWidth != p.windowWidth || n.windowHeight != p.windowHeight ||
             p.windowMode != WindowMode::Windowed) &&
            g_ops.requestWindowSize)
            g_ops.requestWindowSize(g_ops.ctx, n.windowWidth, n.windowHeight);
        applyCommonVideo(n, p);
        if (n.aspect != p.aspect)
            drakan_ws::setAspectText(dsettings::toIni(
                n.aspect)); // drives the projection hook AND the presenter (4:3 pillarbox on menus / FMV / loading)
        if (n.hudAspectCorrect != p.hudAspectCorrect)
            drakan_ws::setHudCorrect(n.hudAspectCorrect);
        if (n.hudScale != p.hudScale)
            drakan_ws::setHudScale(n.hudScale);
        if (n.smoothMenuHighlight != p.smoothMenuHighlight)
            drakan_ws::setSmoothHighlight(n.smoothMenuHighlight);
        if (n.filter != p.filter)
            g_gs->setTextureFilter((uint32_t)n.filter);
        if (n.deinterlace != p.deinterlace)
            g_gs->setDeinterlace((uint32_t)n.deinterlace);
        if (n.flickerFilter != p.flickerFilter)
            g_gs->setFlickerFilter(n.flickerFilter);
        if ((n.deinterlace != p.deinterlace || n.flickerFilter != p.flickerFilter) && g_ops.presentationFieldMode)
            g_ops.presentationFieldMode(g_ops.ctx, (uint32_t)n.deinterlace, n.flickerFilter,
                                        (n.deinterlace != p.deinterlace ? 3u : 0u) |
                                            (n.flickerFilter != p.flickerFilter ? 4u : 0u));
        if (n.sharpPresent != p.sharpPresent)
            g_gs->setSharpPresent(n.sharpPresent);
        if (n.anisotropic != p.anisotropic)
            g_gs->setAnisotropic((uint32_t)std::max(0, n.anisotropic));
        if (n.mipBias != p.mipBias)
            g_gs->setMipBias(n.mipBias, false);
        if (n.textureUpscale != p.textureUpscale || n.textureUpscaleFilter != p.textureUpscaleFilter)
            g_gs->setTextureUpscale((uint32_t)std::max(0, n.textureUpscale), n.textureUpscaleFilter == "xbrz"     ? 1u
                                                                             : n.textureUpscaleFilter == "smooth" ? 2u
                                                                                                                  : 0u);
        if (n.dlss != p.dlss || n.dlssPreset != p.dlssPreset || n.dlssSharpness != p.dlssSharpness)
            drakan_dlss::setMode(n.dlss, n.dlssPreset, n.dlssSharpness);
        if (n.dlssNeuralResolution != p.dlssNeuralResolution)
            drakan_dlss::setNeuralResolution(n.dlssNeuralResolution);
        if (n.dlssNeural != p.dlssNeural)
            drakan_dlss::setNeural(n.dlssNeural);
        if (n.modernWater != p.modernWater || n.rayReflections != p.rayReflections || n.rayShadows != p.rayShadows)
            g_gs->setWaterRendering(n.modernWater, n.rayReflections, n.rayShadows);
        if (n.hdTextures != p.hdTextures)
            g_gs->setTextureReplace(n.hdTextures);
        if (n.textureDump != p.textureDump)
            g_gs->setTextureDump(n.textureDump);
        if ((n.scaleAuto != p.scaleAuto || n.scale != p.scale) && g_ops.requestInternalScale)
            g_ops.requestInternalScale(g_ops.ctx, n.scaleAuto ? 0u : (uint32_t)std::clamp(n.scale, 1, 8));
        return r;
    }

    void reloadBindings(const std::string& path) override { drakan_input::reloadConfig(path.c_str()); }
    void regeneratePrompts() override {
        // The key-cap prompts are generated when the game requests the UI resources (next level load / restart).
        std::fprintf(
            stderr,
            "[settings] bindings changed: on-screen button prompts update after the next level load or restart\n");
    }
    void requestRestart(const std::vector<std::string>&) override {
        relaunchSelf();
        drakan_win::requestClose();
    }
    void requestQuit() override {
        std::fprintf(stderr, "[settings] Quit to Desktop\n");
        drakan_win::requestClose();
    }
    bool gameTexture(int id, dsettings::GameImage& out) const override {
        std::lock_guard<std::mutex> l(g_artM);
        readArtFromDisc();
        auto it = g_art.find(id);
        return it != g_art.end() && dsettings::decodeTxdBlob(it->second.data(), it->second.size(), out);
    }
    void playUiSound(dsettings::UiSound) override {
        // PC settings run outside the guest scheduler. Playing guest UI sounds
        // through RecompGuest's copied context can enter WaitSema in the sound
        // manager and strand the interrupted PadMgr_Update continuation. Keep
        // these optional clicks silent until a scheduler-owned sound path exists.
    }
    void onOverlayOpened() override { drakan_input::setMenuActive(true); }
    void onOverlayClosed() override { drakan_input::setMenuActive(false); }
};

DrakanSettingsHost g_host;

void overlayCallback(const gsr::OverlayContext& oc) {
    drakan_fpsmeter::onPresent();
    if (!g_overlay) {
        if (g_overlayFailed)
            return;
        dsettings::OverlayInit oi;
        oi.hwnd = g_parent;
#if defined(__EMSCRIPTEN__)
        // The browser has no window events of its own: the overlay reads the page's DOM events.
        webEventsEnable(WebEventConsumer::Overlay);
        oi.events.next = [](dsettings::OverlayEvent& out) {
            WebEvent e;
            while (webEvents(WebEventConsumer::Overlay).pop(e)) {
                if (e.type == WebEvent::Motion)
                    continue; // pointer-lock deltas are for the game
                out.type = e.type == WebEvent::Key      ? dsettings::OverlayEvent::Key
                           : e.type == WebEvent::Button ? dsettings::OverlayEvent::Button
                           : e.type == WebEvent::Cursor ? dsettings::OverlayEvent::Cursor
                           : e.type == WebEvent::Wheel  ? dsettings::OverlayEvent::Wheel
                           : e.type == WebEvent::Char   ? dsettings::OverlayEvent::Char
                                                        : dsettings::OverlayEvent::Focus;
                out.a = e.a;
                out.b = e.b;
                return true;
            }
            return false;
        };
        oi.events.pointer = [](int& x, int& y, int& w, int& h) {
            drakanWebCursor(x, y);
            return drakan_win::clientSize(w, h);
        };
        oi.events.gamepadButtons = [] {
            const WebGamepad pad = drakanWebGamepad();
            return pad.connected ? pad.buttons : 0u;
        };
        oi.events.setCursorHidden = [](bool hidden) { drakanWebSetCursorHidden(hidden); };
#endif
        if (oc.api == gsr::OverlayApi::WebGpu) {
#if DRAKAN_SETTINGS_WEBGPU
            oi.renderer = dsettings::createWebGpuOverlayRenderer(*static_cast<gsr::rhi::Device*>(oc.device));
#endif
        } else {
            oi.device = static_cast<ID3D11Device*>(oc.device);
            oi.context = static_cast<ID3D11DeviceContext*>(oc.context);
        }
        oi.host = &g_host;
        oi.paths.drakanIni = drakan_cfg::iniPath();
        const char* ii = std::getenv("DRAKAN_INPUT_INI");
        oi.paths.inputIni = (ii && *ii) ? std::string(ii) : drakan_cfg::exeDir() + "drakan_input.ini";
        g_overlay = dsettings::SettingsOverlay::create(oi);
        if (!g_overlay) {
            g_overlayFailed = true;
            std::fprintf(stderr, "[settings] overlay unavailable\n");
            return;
        }
        g_overlay->setOpenChangedCallback([](bool open) {
            // The game keeps running over one of its own menus (music continues); only its input is swallowed. Without
            // a game menu (cut scene, loading ...) the EE is held as before (drakan_pause::overlayHolds()).
            if (open)
                drakan_pause::overlayOpened();
            else
                drakan_pause::overlayClosed();
            drakan_input::module().setSuspended(open);
            g_open.store(open);
        });
        drakan_subtitles::installHudLayer();
        installSharedHudLayers();
        // The modern PC Settings pages share the overlay's host and store (the F1 overlay stays as it is).
        g_pcs = std::make_unique<dsettings::ModernSettings>(g_host, g_overlay->store());
        g_pcs->open();
        { // the host screens: the modern Tab screen, the I strip, the M map panel, the modern pause menu
            dsettings::SettingsOverlay::ScreenLayer tl;
            tl.active = []() {
                return drakan_tab::shared().active.load() || g_tabUi.fade > 0.0f || pauseShown() || titleShown() ||
                       slotsShown() || drakan_menu_screens::shared().quickOperation.load() != 0;
            };
            tl.wantsInput = []() {
                const auto& ms = drakan_menu_screens::shared();
                if (ms.quickOperation.load())
                    return false;
                if (ms.slotsActive.load())
                    return !ms.slotsChild.load() && !ms.slotsLeaving.load();
                if (ms.titleActive.load())
                    return !ms.titleChild.load() && !ms.titleCovered.load();
                const auto& ps = drakan_pause_screen::shared();
                if (ps.active.load())
                    return !ps.childUp.load() && !ps.resuming.load();
                const auto& sh = drakan_tab::shared();
                return sh.active.load() && !sh.boxUp.load() && !sh.closing.load();
            };
            // Closing / closed: the screen fades out over a still copy of the frame it showed (the native close
            // animation and the screen's end change the live picture under it).
            tl.freezeFrame = []() {
                if (drakan_menu_screens::shared().quickOperation.load())
                    return true;
                if (slotsShown()) {
                    const auto& ms = drakan_menu_screens::shared();
                    return ms.slotsLeaving.load() || !ms.slotsActive.load();
                }
                if (titleShown())
                    return false; // the native title art stays live under the menu
                if (pauseShown()) {
                    const auto& ps = drakan_pause_screen::shared();
                    return ps.resuming.load() || !ps.active.load();
                }
                const auto& sh = drakan_tab::shared();
                return sh.closing.load() || !sh.active.load();
            };
            tl.fontSizes = &tabFontSizes;
            tl.draw = &hostScreenDraw;
            g_overlay->setScreenLayer(std::move(tl));
            drakan_tab::shared().layerReady.store(true);
        }
        // F1 / the gamepad combo are ignored during movies, the start-up logos / intro, in-engine cut scenes and
        // loading screens
        g_overlay->setAllowGate([]() {
            const bool ok = drakan_pause::f1Allowed();
            std::fprintf(stderr, "[settings] t=%.1f F1 %s (%s)\n", drakan_menu_screens::clockSeconds(),
                         ok ? "allowed" : "ignored", drakan_pause::flowDescription().c_str());
            return ok;
        });
        // F1 / the gamepad combo during gameplay: the game pauses itself (START -> its own pause menu) first, then the
        // overlay opens over it
        g_overlay->setOpenInterceptor([]() {
            if (drakan_pause::guiScreen() >= 0 || !drakan_pause::gameplay())
                return true; // a game menu is up (open over it), or the game cannot pause now (cut scene, loading):
                             // open, EE held
            drakan_pause::requestPauseThenOpen();
            return false;
        });
        { // game menu cursor (mouse over the game's own menus), drawn in the overlay layer
            dsettings::HudLayer cl;
            cl.active = []() { return drakan_input::module().menuCursorVisible() || drakan_menus::pager().visible; };
            cl.draw = [](const dsettings::HudView& view) {
                const float w = view.w, h = view.h, scale = view.uiScale;
                const auto p = drakan_menus::pager();
                const int screen = drakan_pause::guiScreen();
                if (p.visible && (screen == 4 || screen == 5)) {
                    const auto& v = g_overlayView;
                    const float vw = v.viewW > 0 ? v.viewW : w, vh = v.viewH > 0 ? v.viewH : h;
                    const float sy = vh / p.height;
                    const auto px = [&](int x) {
                        return v.viewX + vw * (0.5f + (static_cast<float>(x) / p.width - 0.5f) * v.hudScaleX);
                    };
                    const float py = v.viewY + p.y * sy;
                    const int hover = drakan_input::module().menuCursorVisible()
                                          ? drakan_input::module().menuMouse().hoveredHotspot()
                                          : -1;
                    g_overlay->drawNativeMenuText(p.back, px(p.xBack), py, p.lineHeight * sy, hover == 100);
                    g_overlay->drawNativeMenuText(p.next, px(p.xNext), py, p.lineHeight * sy, hover == 101);
                    g_overlay->drawNativeMenuText(p.page, px(p.xPage), py, p.lineHeight * sy, false);
                }
                float x = 0, y = 0;
                if (drakan_input::module().menuCursorVisible() && g_overlay &&
                    g_overlay->pointerInBackbuffer(static_cast<uint32_t>(w), static_cast<uint32_t>(h), &x, &y))
                    dsettings::drawMenuCursor(x, y, scale);
            };
            dsettings::hudLayers().set(dsettings::HudSlot::Cursor, std::move(cl));
            drakan_input::module().setOverlayCursorAvailable(true);
        }
        std::fprintf(stderr, "[settings] overlay ready (F1 / Back+LB+RB / Options > PC Settings)\n");
    }
    if (g_openRequest.exchange(false) && drakan_pause::f1Allowed()) {
        if (g_openControls.exchange(false))
            g_overlay->menu().setPage(dsettings::Page::Controls);
        g_overlay->open();
    }
    // a movie / cut scene / loading screen started while the overlay was open (over a menu): close it, the state stays
    // consistent
    if (g_overlay->isOpen() && drakan_pause::flowBlockedFresh()) {
        std::fprintf(stderr, "[settings] overlay closed: %s\n", drakan_pause::flowDescription().c_str());
        g_overlay->close();
    }
    if (oc.viewW > 0.0f && oc.viewH > 0.0f &&
        g_parent) { // where the game image sits in the window (letterbox / pillarbox / widescreen): the menu mouse maps
                    // the cursor through it
        int cw = 0, ch = 0;
        if (drakan_win::clientSize(cw, ch) && cw > 0 && ch > 0 && oc.width && oc.height) {
            const float sx = static_cast<float>(cw) / static_cast<float>(oc.width),
                        sy = static_cast<float>(ch) / static_cast<float>(oc.height);
            drakan_input::MenuView v;
            v.x = oc.viewX * sx;
            v.y = oc.viewY * sy;
            v.w = oc.viewW * sx;
            v.h = oc.viewH * sy;
            v.hudScaleX = oc.hudScaleX;
            v.hudAnchorX = oc.hudAnchorX;
            // The renderer maps the entire post-world inventory page with this
            // same left-anchored X factor, including its native 3D grid. Publish it
            // separately so Equipment hit testing can undo that page transform.
            v.inventoryPageScaleX = oc.hudScaleX;
            v.valid = true;
            drakan_input::module().setMenuView(v);
        }
    }
    g_overlayView = oc;
    dsettings::HudView image;
    image.viewX = oc.viewX;
    image.viewY = oc.viewY;
    image.viewW = oc.viewW;
    image.viewH = oc.viewH;
    g_overlay->render(oc.rtv, oc.width, oc.height, image);
    maybeShot(oc);
    g_open.store(g_overlay->isOpenAtomic());
}
} // namespace

void attachRenderer(gsr::Renderer* gs, void* parentHwnd, const WindowOps& ops) {
    g_gs = gs;
    drakan_save_previews::attach(gs);
    drakan_fmv_hd::attach(gs);
    gs->setWaterRendering(drakan_cfg::getBool("video", "modern_water", "GSR_MODERN_WATER", false),
                          drakan_cfg::getBool("video", "ray_reflections", "GSR_RAY_REFLECTIONS", true),
                          drakan_cfg::getBool("video", "ray_shadows", "GSR_RAY_SHADOWS", false));
    g_parent = parentHwnd;
    g_ops = ops;
    g_frameLimit.store(std::max(0, drakan_cfg::getInt("video", "frame_limit", "DRAKAN_FRAME_LIMIT", 0)));
    gs->setOverlayCallback(&overlayCallback);
}

void detachRenderer() {
    drakan_save_previews::attach(nullptr);
    drakan_fmv_hd::attach(nullptr);
    if (g_gs)
        g_gs->setOverlayCallback(nullptr);
    for (auto& p : g_previews) {
        dropTexture(p.second.tex);
        if (p.second.decode.valid())
            p.second.decode.wait(); // a worker decode ends before the renderer goes
    }
    g_previews.clear();
    g_previewDrops.clear(); // waits for the dropped decodes
    g_pcs.reset();          // it refers to the overlay's store
    g_overlay.reset();
    g_open.store(false);
    g_gs = nullptr;
}

void setAudio(spu2::AudioOutput* audio) {
    g_audio.store(audio);
}
void setArtSource(hle::IsoFs* fs) {
    std::lock_guard<std::mutex> l(g_artM);
    g_artFs = fs;
}
bool isOpen() {
    return g_open.load();
}
void requestOpen() {
    g_openRequest.store(true);
}
void requestOpenControls() {
    g_openControls.store(true);
    g_openRequest.store(true);
}
int frameLimit() {
    return g_frameLimit.load();
}
} // namespace drakan_settings_host
#endif // PS2X_HAVE_DRAKAN_SETTINGS
