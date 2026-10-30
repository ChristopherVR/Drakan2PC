#include "drakan_settings_host.h"
#include "drakan_settings_common.h"
#include "drakan_subtitles.h"
#include "drakan_software_frame_clock.h"
#include "drakan_config.h"
#include "drakan_tab_screen.h"
#include "drakan_pause.h"
#include "drakan_menus.h"
#include "drakan_portable_camera.h"
#include "drakan_dual_wield.h"
#include "drakan_widescreen.h"
#include "ps2_runtime.h"
#include "gs/gs_host_scanout.h"
#include "input/DrakanInput.h"
#include "portable_host_keys.h"
#include "settings/GameUiRenderer.h"
#include "settings/ModernSettings.h"
#include "settings/TxdImage.h"
#include "hle/IsoFs.h"
#include "spu2/AudioOutput.h"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/threading.h>
#else
#include <unistd.h>
#endif

namespace drakan_settings_host {
namespace {
std::atomic<bool> g_open{false}, g_request{false};
std::atomic<bool> g_controlsPage{false};
std::atomic<int> g_limit{0};
std::atomic<spu2::AudioOutput*> g_audio{nullptr};
std::mutex g_artMutex;
hle::IsoFs* g_disc = nullptr;
std::map<int, dsettings::GameImage> g_art;
bool g_artRead = false;
PS2Runtime* g_runtime = nullptr; // Window thread only.
bool g_restart = false;
bool g_sharp = true;
dsettings::HudView g_image; // window thread: where drawPortableFrame put the game image
Shader g_present{};
bool g_presentAttempted = false;

void readArt() {
    if (g_artRead || !g_disc)
        return;
    g_artRead = true;
    hle::IsoFs::Node node;
    if (!g_disc->lookup("COMMON\\INTERFCE\\INTERFCE.TXD", node) || node.dir)
        return;
    const auto file = g_disc->readAll(node);
    const auto u32 = [&](size_t offset) {
        uint32_t value = 0;
        if (offset + 4 <= file.size())
            std::memcpy(&value, file.data() + offset, 4);
        return value;
    };
    if (file.size() < 16 || std::memcmp(file.data(), "SRSC", 4))
        return;
    const auto index = u32(6), count = u32(10);
    for (uint32_t i = 0; i < count && uint64_t(index) + uint64_t(i + 1) * 14 <= file.size(); ++i) {
        const size_t entry = size_t(index) + size_t(i) * 14;
        uint16_t type = 0, id = 0;
        std::memcpy(&type, file.data() + entry, 2);
        std::memcpy(&id, file.data() + entry + 2, 2);
        const auto offset = u32(entry + 6), size = u32(entry + 10);
        dsettings::GameImage image;
        if (type == 0x40 && uint64_t(offset) + size <= file.size() &&
            dsettings::decodeTxdBlob(file.data() + offset, size, image))
            g_art.emplace(id, std::move(image));
    }
}

struct Host final : CommonSettingsHost {
    Host() : CommonSettingsHost(g_audio) {}
    dsettings::MonitorInfo monitor() const override {
        dsettings::MonitorInfo result;
        const int monitor = GetCurrentMonitor();
        result.nativeWidth = GetMonitorWidth(monitor);
        result.nativeHeight = GetMonitorHeight(monitor);
        result.refreshHz = GetMonitorRefreshRate(monitor);
        return result;
    }
    dsettings::HostCaps capabilities() const override {
        dsettings::HostCaps caps = commonCapabilities();
        caps.internalResolution = false;
        caps.unavailableSettings.insert({"video.aspect", "video.smooth_menu_highlight", "video.hud_scale",
                                         "video.hud_aspect_correct", "video.filter", "video.deinterlace",
                                         "general.inventory_screen"});
#if defined(__EMSCRIPTEN__)
        caps.audioDeviceSelect = false; // the browser picks the output device
#else
        caps.audioDeviceSelect = true;
#endif
#if defined(__EMSCRIPTEN__)
        // The browser owns its window dimensions; canvas resizing follows it.
        caps.unavailableSettings.insert("video.window_size");
        caps.unavailableSettings.insert("video.window_custom");
#endif
        caps.dlssStatus = "DLSS is unavailable on this renderer.";
        caps.rayTracingStatus = "Ray tracing is unavailable on this renderer.";
        return caps;
    }
    void windowSize(int& w, int& h) const override {
        w = GetScreenWidth();
        h = GetScreenHeight();
    }
    dsettings::ApplyResult applyVideo(const dsettings::VideoSettings& n, const dsettings::VideoSettings& p) override {
        if (n.scale != p.scale || n.scaleAuto != p.scaleAuto)
            return {false, false, "Higher-resolution scene rendering is not available yet."};
        if (n.windowMode != p.windowMode) {
            const bool fullscreen = n.windowMode != dsettings::WindowMode::Windowed;
            if (fullscreen != IsWindowFullscreen())
                ToggleFullscreen();
        }
#if !defined(__EMSCRIPTEN__)
        if (n.windowWidth != p.windowWidth || n.windowHeight != p.windowHeight)
            SetWindowSize(n.windowWidth, n.windowHeight);
#endif
        if (n.vsync != p.vsync) {
            if (n.vsync)
                SetWindowState(FLAG_VSYNC_HINT);
            else
                ClearWindowState(FLAG_VSYNC_HINT);
        }
        g_limit.store(std::max(0, n.frameLimit));
        g_sharp = n.sharpPresent;
        gs_cpu::setFlickerFilter(n.flickerFilter);
        applyCommonVideo(n, p);
        SetTargetFPS(std::max(0, n.frameLimit));
        return {};
    }
    void reloadBindings(const std::string& path) override { drakanPortableReloadControls(path); }
    bool gameTexture(int id, dsettings::GameImage& out) const override {
        std::lock_guard lock(g_artMutex);
        readArt();
        const auto found = g_art.find(id);
        if (found == g_art.end())
            return false;
        out = found->second;
        return true;
    }
    void requestQuit() override {
        if (g_runtime)
            g_runtime->requestStop();
    }
    void requestRestart(const std::vector<std::string>&) override {
        g_restart = true;
        requestQuit();
    }
};

struct Ui {
    Host host;
    dsettings::SettingsStore store{[] {
        const char* input = std::getenv("DRAKAN_INPUT_INI");
        return dsettings::Paths{drakan_cfg::iniPath(),
                                input && *input ? std::string(input) : drakan_cfg::exeDir() + "drakan_input.ini"};
    }()};
    dsettings::ModernSettings settings{host, store};
    dsettings::GameUiRenderer renderer;
    dsettings::GameSkin skin;
    dsettings::UiFonts fonts;
    ImGuiContext* context = nullptr;
    std::vector<Texture2D> textures;
    Texture2D fontTexture{};
    gameui::TitleScreen title;
    gameui::PauseScreen pause;
    gameui::SaveLoadScreen slots;
    gameui::SettingsPages standalone;
    gameui::KeyGate gate;
    uint32_t titleSession = 0, titleReactivated = 0, pauseSession = 0, pauseReactivated = 0;
    uint32_t slotSession = 0, slotReactivated = 0;
    int width = 0, height = 0, top = 0;
    bool settingsWereOpen = false;
    std::map<int, Texture2D> previews;
    uint64_t previewSerial = UINT64_MAX;
    double lastTime = 0;
    gameui::InputDevice device = gameui::InputDevice::KeyboardMouse;
    drakan_input::FrameInput physical;
    std::array<double, 256> repeatAt{};
    std::array<double, 16> padRepeatAt{};
    uint16_t lastPad = 0;
    float previousX = 0, previousY = 0;
    float clickX = 0, clickY = 0;
    double lastClick = -1;
};
std::unique_ptr<Ui> g_ui;

Texture2D upload(const dsettings::GameImage& image) {
    Image raw{const_cast<uint32_t*>(image.rgba.data()), image.w, image.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    auto texture = LoadTextureFromImage(raw);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

void fonts(Ui& ui, int w, int h) {
    if (ui.fontTexture.id)
        UnloadTexture(ui.fontTexture);
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    const auto layout = gameui::computeFrame({0, 0, float(w), float(h)});
    const gameui::Theme theme;
    const float sizes[] = {theme.sizeBody, theme.sizeHeading, theme.sizeTitle, theme.sizeNumeric, theme.sizeSmall};
    dsettings::ScreenFontSizes screen;
    for (int i = 0; i < dsettings::UiFonts::kScreenFonts; ++i)
        screen.px[i] = std::round(sizes[i] * layout.scale);
    ui.fonts = dsettings::buildFonts(io, {}, {}, ui.skin.fontImage(), screen);
    unsigned char* data = nullptr;
    int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&data, &fw, &fh);
    Image raw{data, fw, fh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    ui.fontTexture = LoadTextureFromImage(raw);
    SetTextureFilter(ui.fontTexture, TEXTURE_FILTER_BILINEAR);
    io.Fonts->SetTexID(ImTextureID(ui.fontTexture.id));
    ui.skin.font = ui.fonts.game;
    ui.renderer.setFonts(ui.fonts);
    ui.renderer.setSkin(&ui.skin);
    ui.width = w;
    ui.height = h;
}

gameui::UiInput input(Ui& ui, int w, int h) {
    gameui::UiInput result;
    const auto& frame = ui.physical;
    if (!frame.focused) {
        ui.gate.reset();
        ui.lastPad = frame.gamepad.buttons;
        return result;
    }
    const float sx = float(w) / std::max(1, frame.clientW), sy = float(h) / std::max(1, frame.clientH);
    result.pointerX = frame.cursorX * sx;
    result.pointerY = frame.cursorY * sy;
    result.pointerValid = result.pointerX >= 0 && result.pointerY >= 0 && result.pointerX < w && result.pointerY < h;
    result.pointerMoved = result.pointerX != ui.previousX || result.pointerY != ui.previousY;
    ui.previousX = result.pointerX;
    ui.previousY = result.pointerY;
    result.leftDown = (frame.mouseDown & drakan_input::MBTN_LEFT) != 0;
    result.leftClick = ui.gate.accept(900, result.leftDown, (frame.mousePressed & drakan_input::MBTN_LEFT) != 0);
    result.rightClick = ui.gate.accept(901, (frame.mouseDown & drakan_input::MBTN_RIGHT) != 0,
                                       (frame.mousePressed & drakan_input::MBTN_RIGHT) != 0);
    result.wheel = float(frame.wheel);
    // Match Windows tabInput/tabScriptInput, including the keys printed in
    // the shared footer. Page Up/Down remain convenient additional aliases.
    const std::pair<int, gameui::UiKey> keys[] = {
        {0xc8, gameui::UiKey::Up},         {0x11, gameui::UiKey::Up},         {0xd0, gameui::UiKey::Down},
        {0x1f, gameui::UiKey::Down},       {0xcb, gameui::UiKey::Left},       {0x1e, gameui::UiKey::Left},
        {0xcd, gameui::UiKey::Right},      {0x20, gameui::UiKey::Right},      {0x1c, gameui::UiKey::Confirm},
        {0x9c, gameui::UiKey::Confirm},    {0x39, gameui::UiKey::Confirm},    {0x01, gameui::UiKey::Back},
        {0x0e, gameui::UiKey::Back},       {0x0f, gameui::UiKey::Close},      {0x17, gameui::UiKey::Close},
        {0x32, gameui::UiKey::Close},      {0x2d, gameui::UiKey::Secondary},  {0xd3, gameui::UiKey::Secondary},
        {0x21, gameui::UiKey::Tertiary},   {0x10, gameui::UiKey::PagePrev},   {0x12, gameui::UiKey::PageNext},
        {0x2c, gameui::UiKey::FilterPrev}, {0x2e, gameui::UiKey::FilterNext}, {0xc9, gameui::UiKey::PagePrev},
        {0xd1, gameui::UiKey::PageNext}};
    const double now = GetTime();
    if (result.leftClick) {
        const float dx = result.pointerX - ui.clickX, dy = result.pointerY - ui.clickY;
        result.doubleClick = now - ui.lastClick < .30 && dx * dx + dy * dy < 36;
        ui.lastClick = result.doubleClick ? -1 : now;
        ui.clickX = result.pointerX;
        ui.clickY = result.pointerY;
    }
    for (const auto& [key, mapped] : keys) {
        const bool down = frame.keyDown[key];
        bool pressed = frame.keyPressed[key];
        if (pressed)
            ui.repeatAt[key] = now + .30;
        else if (!down)
            ui.repeatAt[key] = 0;
        else if (ui.repeatAt[key] > 0 && now >= ui.repeatAt[key] &&
                 (mapped == gameui::UiKey::Up || mapped == gameui::UiKey::Down || mapped == gameui::UiKey::Left ||
                  mapped == gameui::UiKey::Right || mapped == gameui::UiKey::PagePrev ||
                  mapped == gameui::UiKey::PageNext)) {
            pressed = true;
            ui.repeatAt[key] = now + .065;
        }
        if (ui.gate.accept(key, down, pressed))
            result.keys.push_back(mapped);
    }
    if (result.pointerMoved || result.leftClick || !result.keys.empty())
        ui.device = gameui::InputDevice::KeyboardMouse;
    uint16_t pad = frame.gamepad.connected ? frame.gamepad.buttons : 0;
    if (frame.gamepad.connected) {
        if (frame.gamepad.lx < -.5f)
            pad |= drakan_input::Pad::LEFT;
        if (frame.gamepad.lx > .5f)
            pad |= drakan_input::Pad::RIGHT;
        if (frame.gamepad.ly < -.5f)
            pad |= drakan_input::Pad::DOWN;
        if (frame.gamepad.ly > .5f)
            pad |= drakan_input::Pad::UP;
    }
    const std::pair<uint16_t, gameui::UiKey> buttons[] = {{drakan_input::Pad::UP, gameui::UiKey::Up},
                                                          {drakan_input::Pad::DOWN, gameui::UiKey::Down},
                                                          {drakan_input::Pad::LEFT, gameui::UiKey::Left},
                                                          {drakan_input::Pad::RIGHT, gameui::UiKey::Right},
                                                          {drakan_input::Pad::CROSS, gameui::UiKey::Confirm},
                                                          {drakan_input::Pad::CIRCLE, gameui::UiKey::Back},
                                                          {drakan_input::Pad::L1, gameui::UiKey::PagePrev},
                                                          {drakan_input::Pad::R1, gameui::UiKey::PageNext},
                                                          {drakan_input::Pad::TRIANGLE, gameui::UiKey::Secondary},
                                                          {drakan_input::Pad::SQUARE, gameui::UiKey::Tertiary},
                                                          {drakan_input::Pad::L2, gameui::UiKey::FilterPrev},
                                                          {drakan_input::Pad::R2, gameui::UiKey::FilterNext},
                                                          {drakan_input::Pad::SELECT, gameui::UiKey::Close},
                                                          {drakan_input::Pad::START, gameui::UiKey::Back}};
    for (unsigned i = 0; i < std::size(buttons); ++i) {
        const auto [button, mapped] = buttons[i];
        const bool down = (pad & button) != 0;
        bool pressed = (pad & button & ~ui.lastPad) != 0;
        if (pressed)
            ui.padRepeatAt[i] = now + .30;
        else if (!down)
            ui.padRepeatAt[i] = 0;
        else if (i < 4 && ui.padRepeatAt[i] > 0 && now >= ui.padRepeatAt[i]) {
            pressed = true;
            ui.padRepeatAt[i] = now + .065;
        }
        if (ui.gate.accept(512 + int(i), down, pressed)) {
            result.keys.push_back(mapped);
            ui.device = gameui::InputDevice::Gamepad;
        }
    }
    ui.lastPad = pad;
    result.device = ui.device;
    if (ui.device == gameui::InputDevice::Gamepad)
        result.pointerValid = false;
    return result;
}

std::string capturedKey(const Ui& ui) {
    for (unsigned key = 0; key < 256; ++key)
        if (key != 1 && ui.physical.keyPressed[key])
            if (const auto* name = drakan_input::nameOfKey(int(key)))
                return name;
    for (unsigned button = 0; button < 5; ++button)
        if (ui.physical.mousePressed & (1u << button))
            return "MOUSE" + std::to_string(button + 1);
    if (ui.physical.wheel > 0)
        return "WHEELUP";
    if (ui.physical.wheel < 0)
        return "WHEELDOWN";
    return {};
}

void renderDrawData(ImDrawData* data, int height) {
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlEnableScissorTest();
    for (const auto* list : data->CmdLists) {
        for (const auto& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
                    cmd.UserCallback(list, &cmd);
                continue;
            }
            rlDrawRenderBatchActive();
            rlScissor(int(cmd.ClipRect.x), height - int(cmd.ClipRect.w),
                      std::max(0, int(cmd.ClipRect.z - cmd.ClipRect.x)),
                      std::max(0, int(cmd.ClipRect.w - cmd.ClipRect.y)));
            rlBegin(RL_TRIANGLES);
            rlSetTexture(unsigned(cmd.GetTexID()));
            for (unsigned i = 0; i < cmd.ElemCount; ++i) {
                const auto& v = list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset + i] + cmd.VtxOffset];
                rlColor4ub(v.col & 255, (v.col >> 8) & 255, (v.col >> 16) & 255, v.col >> 24);
                rlTexCoord2f(v.uv.x, v.uv.y);
                rlVertex2f(v.pos.x, v.pos.y);
            }
            rlEnd();
            rlDrawRenderBatchActive();
        }
    }
    rlSetTexture(0);
    rlDisableScissorTest();
    rlEnableBackfaceCulling();
}

void sound(gameui::UiSound value) {
    if (value != gameui::UiSound::None)
        drakan_menu_screens::shared().uiSound.store(value == gameui::UiSound::Back ? 0x2du : 0x2au);
}
} // namespace

void drawPortableUi(PS2Runtime& runtime, int w, int h, unsigned frameTexture) {
    g_runtime = &runtime;
    if (!g_ui) {
        g_ui = std::make_unique<Ui>();
        auto& ui = *g_ui;
        ui.context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendRendererName = "drakan-raylib";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ui.store.load();
        auto initial = ui.store.loaded();
        g_sharp = initial.video.sharpPresent;
        gs_cpu::setFlickerFilter(initial.video.flickerFilter);
        bool migrate = false;
        if (drakan_cfg::getString("general", "title_menu", "DRAKAN_TITLE_MENU", "").empty()) {
            initial.general.titleMenu = dsettings::TitleMenu::Modern;
            migrate = true;
        }
        if (drakan_cfg::getString("general", "pause_menu", "DRAKAN_PAUSE_MENU", "").empty()) {
            initial.general.pauseMenu = dsettings::PauseMenu::Modern;
            migrate = true;
        }
        if (drakan_cfg::getString("general", "save_menu", "DRAKAN_SAVE_MENU", "").empty()) {
            initial.general.saveMenu = dsettings::SaveMenu::Modern;
            migrate = true;
        }
        if (migrate)
            ui.store.save(initial);
        drakan_menu_screens::setTitleModern(initial.general.titleMenu == dsettings::TitleMenu::Modern);
        drakan_menu_screens::setSaveModern(initial.general.saveMenu == dsettings::SaveMenu::Modern);
        drakan_pause_screen::setEnabled(initial.general.pauseMenu == dsettings::PauseMenu::Modern);
        ui.settings.open();
        ui.skin.fetch(ui.host);
        ui.skin.upload([&](const dsettings::GameImage& image) {
            auto texture = upload(image);
            ui.textures.push_back(texture);
            return ImTextureID(texture.id);
        });
        fonts(ui, w, h);
        gameui::SettingsPagesConfig standalone;
        standalone.screen.measure = ui.renderer.measure();
        ui.standalone = gameui::SettingsPages(standalone);
        drakan_tab::shared().layerReady.store(true);
        drakan_subtitles::installHudLayer();
        installSharedHudLayers();
        std::fprintf(stderr, "[portable-ui] modern menu renderer ready at %dx%d\n", w, h);
    }
    auto& ui = *g_ui;
    ImGui::SetCurrentContext(ui.context);
    if (w != ui.width || h != ui.height)
        fonts(ui, w, h);
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(w), float(h));
    const double now = GetTime();
    io.DeltaTime = ui.lastTime == 0 ? 1.f / 60 : std::clamp(float(now - ui.lastTime), 0.001f, 0.1f);
    ui.lastTime = now;
    ui.physical = drakanPortableTakeUiInput();
    drakan_fpsmeter::setWorldFrames(drakan_presentation::g_frameClock.completedFrames());
    drakan_fpsmeter::onPresent();
    if (ui.physical.keyPressed[0x3c]) // F2, as on Windows: cycle the FPS counter for this session
        std::fprintf(stderr, "[portable-ui] fps counter mode %d\n", drakan_fpsmeter::cycleMode());
    ImGui::NewFrame();
    const bool f1 = ui.physical.keyPressed[0x3b];
    const bool requested = g_request.exchange(false);
    auto& ms = drakan_menu_screens::shared();
    auto& ps = drakan_pause_screen::shared();
    if (f1 && !g_open.load() && drakan_pause::f1Allowed() && drakan_pause::gameplay())
        drakan_pause::requestPauseThenOpen();
    else if ((f1 && (g_open.load() || drakan_pause::f1Allowed())) || requested) {
        if (!g_open.load()) {
            ui.standalone.open(g_controlsPage.exchange(false) ? "game" : "");
            ui.settings.open();
            g_open.store(true);
        } else {
            ui.standalone.close();
            g_open.store(false);
        }
    }
    const int top = g_open.load()           ? 4
                    : ms.slotsActive.load() ? 3
                    : ms.titleActive.load() ? 1
                    : ps.active.load()      ? 2
                                            : 0;
    if (top != ui.top) {
        ui.gate.reset();
        ui.lastClick = -1;
        ui.top = top;
    }
    auto in = input(ui, w, h);
    const bool capturing = top == 4   ? ui.standalone.capturing()
                           : top == 1 ? ui.title.pcSettings().capturing()
                           : top == 2 ? ui.pause.pcSettings().capturing()
                                      : false;
    if (capturing)
        std::erase_if(in.keys, [](auto key) { return key != gameui::UiKey::Back; });
    const auto captured = capturing ? capturedKey(ui) : std::string();
    ui.renderer.setGamepadPrompts(in.device == gameui::InputDevice::Gamepad);
    const gameui::Viewport viewport{0, 0, float(w), float(h)};
    gameui::DrawList draw;
    bool pagesOpen = top == 4;
    if (top == 1 && !ms.titleChild.load() && !ms.titleCovered.load()) {
        if (ui.titleSession != ms.titleSession.load()) {
            ui.titleSession = ms.titleSession.load();
            gameui::TitleConfig cfg;
            cfg.screen.measure = ui.renderer.measure();
            cfg.pcSettingsPages = cfg.unifiedOptions = true;
            ui.title = gameui::TitleScreen(cfg);
            ui.title.open();
            ui.gate.reset();
        }
        if (ui.titleReactivated != ms.titleReactivated.load()) {
            ui.titleReactivated = ms.titleReactivated.load();
            ui.title.reactivate();
        }
        auto state = ms.titleState();
        state.now = now;
        state.pcSettings = &ui.settings.schema();
        state.capturedKey = captured;
        auto out = ui.title.frame(state, in, viewport);
        for (const auto& action : out.actions)
            ms.titleActions.push(action);
        for (const auto& action : out.optionActions)
            ms.titleOptionActions.push(action);
        ui.settings.applyAll(out.settings, ui.title.pcSettings());
        if (out.actions.empty() && out.optionActions.empty())
            sound(out.sound);
        draw = std::move(out.draw);
        pagesOpen = ui.title.pcSettingsOpen();
        ms.titleDrawn.fetch_add(1);
    } else if (top == 2 && !ps.childUp.load() && !ps.resuming.load()) {
        if (ui.pauseSession != ps.session.load()) {
            ui.pauseSession = ps.session.load();
            gameui::PauseConfig cfg;
            cfg.screen.measure = ui.renderer.measure();
            cfg.hostPages = cfg.quitCard = cfg.pcSettingsPages = cfg.unifiedOptions = true;
            ui.pause = gameui::PauseScreen(cfg);
            ui.pause.open();
            ui.gate.reset();
        }
        if (ui.pauseReactivated != ps.reactivated.load()) {
            ui.pauseReactivated = ps.reactivated.load();
            ui.pause.reactivate();
        }
        if (auto snapshot = ps.model.acquire()) {
            auto state = ps.pauseState();
            state.now = now;
            state.pcSettings = &ui.settings.schema();
            state.capturedKey = captured;
            auto out = ui.pause.frame(*snapshot, state, in, viewport);
            for (const auto& action : out.actions)
                ps.actions.push(action);
            ui.settings.applyAll(out.settings, ui.pause.pcSettings());
            if (out.wantsResume)
                ps.resuming.store(true);
            if (out.actions.empty())
                sound(out.sound);
            draw = std::move(out.draw);
            pagesOpen = ui.pause.pcSettingsOpen();
            ps.hostFade.store(1);
            ps.hostDrawn.fetch_add(1);
        }
    } else if (top == 3 && !ms.slotsChild.load()) {
        if (ui.slotSession != ms.slotsSession.load()) {
            ui.slotSession = ms.slotsSession.load();
            gameui::SaveLoadConfig cfg;
            cfg.screen.measure = ui.renderer.measure();
            ui.slots = gameui::SaveLoadScreen(cfg);
            std::lock_guard lock(ms.m);
            ui.slots.open(ms.mode, ms.context);
            ui.gate.reset();
        }
        if (ui.slotReactivated != ms.slotsReactivated.load()) {
            ui.slotReactivated = ms.slotsReactivated.load();
            ui.slots.reactivate();
        }
        auto out = ui.slots.frame(ms.slotsState(), ms.slotsLeaving.load() ? gameui::UiInput{} : in, viewport);
        for (const auto& action : out.actions)
            ms.slotActions.push(action);
        if (out.actions.empty())
            sound(out.sound);
        draw = std::move(out.draw);
        ms.slotsDrawn.fetch_add(1);
    } else if (top == 4) {
        auto out = ui.standalone.frame(ui.settings.schema(), {in, now, captured}, viewport);
        ui.settings.applyAll(out.actions, ui.standalone);
        sound(out.sound);
        draw = std::move(out.draw);
        if (out.wantsClose)
            g_open.store(false);
    }
    if (pagesOpen && !ui.settingsWereOpen)
        ui.settings.open();
    ui.settingsWereOpen = pagesOpen;
    ui.settings.tick(now);
    {
        std::vector<std::pair<int, drakan_menu_screens::SlotPicture>> pictures;
        {
            std::lock_guard lock(ms.m);
            if (ui.previewSerial != ms.picturesSerial) {
                ui.previewSerial = ms.picturesSerial;
                pictures = ms.pictures;
            }
        }
        for (const auto& [slot, picture] : pictures) {
            if (auto found = ui.previews.find(slot); found != ui.previews.end()) {
                UnloadTexture(found->second);
                ui.previews.erase(found);
            }
            if (picture.key && picture.pixels.size() == 128 * 128) {
                dsettings::GameImage image;
                image.w = image.h = 128;
                image.rgba = picture.pixels;
                ui.previews.emplace(slot, upload(image));
            }
        }
    }
    dsettings::GameUiCanvas canvas;
    // Frame crops are wired separately from the native 4:3 scanout. Until a
    // crop is available the shared renderer uses its explicit fallback art.
    (void)frameTexture;
    const dsettings::IconSource icons = [&](const gameui::ImageRef& ref, dsettings::IconCrop& crop) {
        if (ref.kind != gameui::ImageKind::SavePreview)
            return false;
        const auto found = ui.previews.find(int(ref.index));
        if (found == ui.previews.end())
            return false;
        crop.texture = ImTextureID(found->second.id);
        crop.uv0 = {0, 0};
        crop.uv1 = {1, 1};
        crop.w = 128;
        crop.h = 96;
        return true;
    };
    ui.renderer.draw(ImGui::GetBackgroundDrawList(), draw, canvas, icons, now, 1);
    {
        dsettings::HudView view = g_image;
        view.w = float(w);
        view.h = float(h);
        view.uiScale = dsettings::hudScaleFor(view.h);
        dsettings::hudLayers().draw(view, top != 0);
    }
    if (top && in.pointerValid)
        ui.skin.cursor(ImGui::GetForegroundDrawList(), {in.pointerX, in.pointerY}, std::max(20.f, h / 36.f));
    ImGui::Render();
    renderDrawData(ImGui::GetDrawData(), h);
}

void shutdownPortableUi() {
    if (!g_ui)
        return;
    drakan_tab::shared().layerReady.store(false);
    for (auto texture : g_ui->textures)
        UnloadTexture(texture);
    for (const auto& [slot, texture] : g_ui->previews)
        UnloadTexture(texture);
    if (g_ui->fontTexture.id)
        UnloadTexture(g_ui->fontTexture);
    ImGui::DestroyContext(g_ui->context);
    g_ui.reset();
    g_open.store(false);
    g_runtime = nullptr;
    if (g_present.id && g_present.id != rlGetShaderIdDefault())
        UnloadShader(g_present);
    g_present = {};
    g_presentAttempted = false;
    if (g_restart) {
#if defined(__EMSCRIPTEN__)
        MAIN_THREAD_EM_ASM({ Module.drakanRestartRequested = true; });
#else
        std::atexit([] {
            char* args[] = {const_cast<char*>("drakan"), nullptr};
            execv("/proc/self/exe", args);
        });
#endif
    }
}

void drawPortableFrame(unsigned texture, unsigned width, unsigned height, float x, float y, float w, float h) {
    g_image.viewX = x;
    g_image.viewY = y;
    g_image.viewW = w;
    g_image.viewH = h;
    if (!g_presentAttempted) {
        g_presentAttempted = true;
#if defined(__EMSCRIPTEN__)
        const std::string prefix = "#version 100\nprecision highp float;\nvarying vec2 fragTexCoord;\nvarying vec4 "
                                   "fragColor;\n#define SAMPLE texture2D\n#define RESULT gl_FragColor\n";
#else
        const std::string prefix = "#version 330\nin vec2 fragTexCoord;\nin vec4 fragColor;\nout vec4 "
                                   "finalColor;\n#define SAMPLE texture\n#define RESULT finalColor\n";
#endif
        // Same reconstruction as Windows' presentTaps/PSPresent. Restrict
        // interpolation to one output pixel on enlargement; average covered
        // source pixels on reduction. This preserves edges without pretending
        // to add high-resolution scene geometry.
        const auto source = prefix + R"GLSL(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 sourceSize;
uniform vec2 targetSize;
float weight(float p,float s,float start,int tap) {
    if(s>=1.0){float f=clamp((p-0.5-start-0.5)*s+0.5,0.0,1.0);return tap==0?1.0-f:tap==1?f:0.0;}
    float halfWidth=0.5/s;
    return max(0.0,min(start+float(tap)+1.0,p+halfWidth)-max(start+float(tap),p-halfWidth));
}
void main() {
    vec2 scale=targetSize/sourceSize;
    vec2 p=fragTexCoord*sourceSize;
    vec3 value=vec3(0.0);
    if(scale.x>=1.0 && scale.y>=1.0) {
        vec2 base=floor(p-0.5);
        vec2 f=clamp((p-0.5-base-0.5)*scale+0.5,0.0,1.0);
        value=SAMPLE(texture0,(base+f+0.5)/sourceSize).rgb;
    } else {
        vec2 start=floor(p-mix(0.5/scale,vec2(0.5),step(vec2(1.0),scale)));
        float total=0.0;
        // Combine adjacent taps with the texture unit's linear interpolation.
        // Typical modest reductions need four fetches instead of evaluating
        // the entire 8x8 footprint, including zero-weight samples.
        for(int iy=0;iy<8;iy+=2) {
            float wy0=weight(p.y,scale.y,start.y,iy),wy1=weight(p.y,scale.y,start.y,iy+1);
            float wy=wy0+wy1;if(wy<=0.0)continue;
            for(int ix=0;ix<8;ix+=2) {
                float wx0=weight(p.x,scale.x,start.x,ix),wx1=weight(p.x,scale.x,start.x,ix+1);
                float wx=wx0+wx1;if(wx<=0.0)continue;
                float coverage=wx*wy;
                vec2 cell=start+vec2(float(ix),float(iy))+0.5+vec2(wx1/wx,wy1/wy);
                cell=clamp(cell,vec2(0.5),sourceSize-0.5);
                value+=SAMPLE(texture0,cell/sourceSize).rgb*coverage;total+=coverage;
            }
        }
        value/=max(total,0.000001);
    }
    RESULT=vec4(value,1.0)*fragColor*colDiffuse;
}
)GLSL";
        g_present = LoadShaderFromMemory(nullptr, source.c_str());
    }
    const bool shader = g_sharp && g_present.id && g_present.id != rlGetShaderIdDefault();
    if (shader) {
        const float source[] = {float(width), float(height)}, target[] = {w, h};
        SetShaderValue(g_present, GetShaderLocation(g_present, "sourceSize"), source, SHADER_UNIFORM_VEC2);
        SetShaderValue(g_present, GetShaderLocation(g_present, "targetSize"), target, SHADER_UNIFORM_VEC2);
        BeginShaderMode(g_present);
    }
    Texture2D frame{texture, int(width), int(height), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    DrawTexturePro(frame, {0, 0, float(width), float(height)}, {x, y, w, h}, {0, 0}, 0, WHITE);
    if (shader)
        EndShaderMode();
}
void attachRenderer(gsr::Renderer*, void*, const WindowOps&) {}
void detachRenderer() {}
void setAudio(spu2::AudioOutput* audio) {
    g_audio.store(audio);
}
void setArtSource(hle::IsoFs* fs) {
    std::lock_guard lock(g_artMutex);
    g_disc = fs;
}
bool isOpen() {
    return g_open.load();
}
void requestOpen() {
    g_request.store(true);
}
void requestOpenControls() {
    g_controlsPage.store(true);
    g_request.store(true);
}
int frameLimit() {
    return g_limit.load();
}
} // namespace drakan_settings_host
