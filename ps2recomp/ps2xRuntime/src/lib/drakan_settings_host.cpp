// Drakan: DrakanSettingsHost - see drakan_settings_host.h and decomp/runtime/settings/README.md.
// This file: the overlay itself (created on the first present, overlayCallback), the dsettings::ISettingsHost
// implementation on top of gsr / spu2 / drakan_input, the test harness inbox and the public API. The host screens drawn
// by the overlay's screen layer live in the drakan_settings_*.cpp files (see drakan_settings_host_internal.h).
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

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
#include <sys/types.h>
#include <unistd.h>
#endif

namespace drakan_settings_host::detail {

std::atomic<bool> g_openRequest{false};

std::atomic<bool> g_openMods{false};   // the next open shows the Mods page (the title's new packs card)
std::atomic<bool> g_modsNotice{false}; // the title shows the new packs card in this run (sticky)

std::unique_ptr<dsettings::SettingsOverlay> g_overlay;

gsr::OverlayContext g_overlayView; // presentation thread only

namespace {
std::atomic<bool> g_open{false};

std::atomic<bool> g_openControls{false}; // the next open shows the Controls page

std::atomic<int> g_frameLimit{0};
static_assert(dsettings::kFrameLimitAuto == drakan_pacing::kFrameLimitAuto, "frame_limit = auto has one value");
std::atomic<spu2::AudioOutput*> g_audio{nullptr};
gsr::Renderer* g_gs = nullptr;
void* g_parent = nullptr; // the game window (drakan_window)
WindowOps g_ops;

bool g_overlayFailed = false;

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
    // Harness inbox: processed by the render thread, including while the EE is held.
    // Commands use the real hotkey and Apply paths, never desktop input injection.
    const auto commandPath = std::filesystem::path(dir) / "command.txt";
    if (std::ifstream command{commandPath}; command) {
        std::string id, op, key, value;
        command >> id >> op >> key >> value;
        command.close();
        std::error_code ec;
        std::filesystem::remove(commandPath, ec);
        bool ok = true;
        if (op == "toggle")
            g_overlay->hotkey();
        else if (op == "close")
            g_overlay->close();
        else if (op == "apply" && g_overlay->isOpen()) {
            bool linkSensitivity = false;
            ok = dsettings::applySettingChange(g_overlay->menu().pendingMut(), linkSensitivity, key, value);
            if (ok)
                g_overlay->menu().applyPending(false);
        } else if (op == "controls")
            g_overlay->menu().setPage(dsettings::Page::Controls);
        else if (op == "mods")
            g_overlay->menu().setPage(dsettings::Page::Mods);
        else if (op == "mod" && g_overlay->isOpen()) // "mod <row key> <value>": a Mods page row
            ok = harnessMods().apply(key, value);
        else if (op == "pad" && !g_overlay->isOpen()) {
            uint16_t button = 0;
            ok = drakan_input::padButtonFromName(key, &button);
            if (ok)
                drakan_input::module().injectPadTap(button, 4);
        } else
            ok = false;
        std::ofstream(commandPath.parent_path() / "ack.txt") << id << " " << (ok ? "ok" : "error");
        std::fprintf(stderr, "[settings-harness] %s %s %s\n", id.c_str(), op.c_str(), ok ? "ok" : "error");
    }
    { // state.txt: "open=<0|1> <flow description>" (rewritten when it changes) so a test driver need not parse the
      // (lagging) log
        static std::string last;
        const std::string st =
            std::string("open=") + (g_overlay && g_overlay->isOpen() ? "1" : "0") + " " +
            drakan_pause::flowDescription().substr(0, drakan_pause::flowDescription().find(", last sample")) +
            " samples=" + std::to_string(drakan_pause::inputSamples()) +
            " hold=" + (drakan_pause::holdRequested() ? "1" : "0");
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
} // namespace

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

namespace {
// The pre-rendered intro movie's picture (HudSlot::Movie layer, present thread).
struct MovieTexture {
    ImTextureID id = 0;
    uint64_t serial = 0;
    uint32_t w = 0, h = 0;
};
MovieTexture g_movieTex;

struct DrakanSettingsHost final : CommonSettingsHost {
    dsettings::ModsModel* mods() override { return &g_modsModel; }
    DrakanSettingsHost() : CommonSettingsHost(g_audio) {}
    dsettings::HostCaps capabilities() const override {
        dsettings::HostCaps c = commonCapabilities();
        c.exclusiveFullscreen = false;
        c.frameLimiter = true;
        c.textureUpscale = c.anisotropic = c.mipBias = true;
        c.textureDump = true;
        c.textureUpscaleFilters = {"default", "xbrz", "smooth"};
        c.languageSelect = drakan_region::build().languageCount > 1;
        c.dlss = drakan_dlss::available();
        c.dlssStatus = drakan_dlss::status();
        c.dlssNvidia = g_gs && g_gs->api() == gsr::Config::Api::D3D11;
        c.terrainDetail = g_gs && g_gs->api() == gsr::Config::Api::D3D11;
        c.materialDetail = g_gs && g_gs->api() == gsr::Config::Api::D3D11;
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
        g_frameLimit.store(n.frameLimit == dsettings::kFrameLimitAuto ? n.frameLimit : std::max(0, n.frameLimit));
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
        if (n.hardwareTextureFilter != p.hardwareTextureFilter)
            g_gs->setHardwareTextureFilter(n.hardwareTextureFilter);
        if (n.mipBiasAuto != p.mipBiasAuto || n.mipBias != p.mipBias)
            g_gs->setMipBias(n.mipBias, n.mipBiasAuto);
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
        if (n.modernWater != p.modernWater || n.rayReflections != p.rayReflections || n.rayShadows != p.rayShadows ||
            n.rayGlobalIllumination != p.rayGlobalIllumination) {
            // One setter carries all four: a set environment variable keeps deciding its own effect (as at start,
            // attachRenderer), so changing ray shadows live does not turn on the INI's modern water over
            // GSR_MODERN_WATER=0.
            auto envOr = [](const char* env, bool value) {
                const char* e = std::getenv(env);
                return e && *e ? drakan_cfg::getBool("video", "", env, value) : value;
            };
            g_gs->setWaterRendering(
                envOr("GSR_MODERN_WATER", n.modernWater), envOr("GSR_RAY_REFLECTIONS", n.rayReflections),
                envOr("GSR_RAY_SHADOWS", n.rayShadows), envOr("GSR_RAY_GI", n.rayGlobalIllumination));
        }
        if (n.terrainDetail != p.terrainDetail || n.terrainCliffs != p.terrainCliffs ||
            n.terrainDetailStrength != p.terrainDetailStrength)
            g_gs->setTerrainDetail(n.terrainDetail, n.terrainCliffs, n.terrainDetailStrength);
        if (n.materialDetail != p.materialDetail || n.materialDetailStrength != p.materialDetailStrength ||
            n.materialSpecular != p.materialSpecular)
            g_gs->setMaterialDetail(n.materialDetail == "all"    ? 2u
                                    : n.materialDetail == "hard" ? 1u
                                                                 : 0u,
                                    n.materialDetailStrength, n.materialSpecular);
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

std::mutex g_harnessSettingsM;
std::vector<std::pair<std::string, std::string>> g_harnessSettings;

void drainHarnessSettings() {
    std::vector<std::pair<std::string, std::string>> now;
    {
        std::lock_guard<std::mutex> l(g_harnessSettingsM);
        now.swap(g_harnessSettings);
    }
    for (const auto& [key, value] : now) {
        gameui::SettingsAction a;
        a.kind = gameui::SettingsActionKind::Change;
        a.key = key;
        a.value = value;
        a.final = false; // applied live, never written to the INI
        const gameui::SettingsFeedback fb = g_pcs->apply(a);
        std::fprintf(stderr, "[settings-harness] setting %s=%s %s%s\n", key.c_str(), value.c_str(),
                     fb.ok ? "ok" : "error: ", fb.error.c_str());
    }
}

void overlayCallback(const gsr::OverlayContext& oc) {
    drakan_fpsmeter::onPresent();
    syncTitleBackdrop();
    if (g_pcs)
        drainHarnessSettings();
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
        if (drakan_world::enabled())
            g_overlay->setMenuExtra(&drakan_world::drawPanel);
        // The modern PC Settings pages share the overlay's host and store (the F1 overlay stays as it is).
        gameui::setSettingShotResolver(
            [](const std::string& key, const std::string& value) { return settingShotPicture(key, value); });
        // Settled = uploaded, or its decode finished without a texture (a bad file shows the empty box, not the
        // previous value's picture). Until then the help panel keeps the last picture up (no blank flash).
        gameui::setSettingShotReady([](uint32_t id) {
            const auto it = g_mapPreviews.find(id);
            return it != g_mapPreviews.end() && (it->second.tex || !it->second.decode.valid());
        });
        g_pcs = std::make_unique<dsettings::ModernSettings>(g_host, g_overlay->store());
        g_pcs->open();
        { // the host screens: the modern Tab screen, the I strip, the M map panel, the modern pause menu
            dsettings::SettingsOverlay::ScreenLayer tl;
            tl.active = []() {
                return drakan_tab::shared().active.load() || g_tabUi.fade > 0.0f || pauseShown() || titleShown() ||
                       switchButtonShown() || slotsShown() ||
                       drakan_menu_screens::shared().quickOperation.load() != 0 || mpHudActive();
            };
            tl.wantsInput = []() {
                const auto& ms = drakan_menu_screens::shared();
                if (ms.quickOperation.load())
                    return false;
                if (g_mpChatOpen.load() || mpResultsUp())
                    return true;
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
        }
        { // the pre-rendered Order of the Flame opening (drakan_intro_movie.h): a black cover with the picture on it
            dsettings::HudLayer ml;
            ml.active = []() {
                if (drakan_intro_movie::coverActive())
                    return true;
                dropTexture(g_movieTex.id); // the cover is gone: free the picture texture
                g_movieTex = MovieTexture{};
                return false;
            };
            ml.draw = [](const dsettings::HudView& view) {
                MovieTexture& mt = g_movieTex;
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                dl->AddRectFilled(ImVec2(0, 0), ImVec2(view.w, view.h), IM_COL32(0, 0, 0, 255));
                float alpha = 0.0f;
                const auto frame = drakan_intro_movie::currentFrame(alpha);
                if (frame && frame->serial != mt.serial && frame->width && frame->height && g_overlay) {
                    // One dynamic texture rewritten per picture; backends without one create a texture each time.
                    if (mt.id && (frame->width != mt.w || frame->height != mt.h))
                        dropTexture(mt.id);
                    if (!mt.id)
                        mt.id = g_overlay->renderer().createDynamicTexture(frame->width, frame->height);
                    if (!(mt.id && g_overlay->renderer().updateTexture(mt.id, frame->rgba.data(), frame->width,
                                                                       frame->height))) {
                        dropTexture(mt.id);
                        mt.id = makeTexture(frame->rgba.data(), frame->width, frame->height);
                    }
                    mt.serial = frame->serial;
                    mt.w = frame->width;
                    mt.h = frame->height;
                }
                if (!mt.id || alpha <= 0.0f || !mt.w || !mt.h)
                    return;
                const float scale = std::min(view.w / static_cast<float>(mt.w), view.h / static_cast<float>(mt.h));
                const float dw = static_cast<float>(mt.w) * scale, dh = static_cast<float>(mt.h) * scale;
                const float x0 = (view.w - dw) * 0.5f, y0 = (view.h - dh) * 0.5f;
                dl->AddImage(mt.id, ImVec2(x0, y0), ImVec2(x0 + dw, y0 + dh), ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
            };
            dsettings::hudLayers().set(dsettings::HudSlot::Movie, std::move(ml));
            drakan_input::module().setOverlayCursorAvailable(true);
        }
        std::fprintf(stderr, "[settings] overlay ready (F1 / Back+LB+RB / Options > PC Settings)\n");
    }
    if (g_openRequest.exchange(false) && drakan_pause::f1Allowed()) {
        if (g_openControls.exchange(false))
            g_overlay->menu().setPage(dsettings::Page::Controls);
        if (g_openMods.exchange(false))
            g_overlay->menu().setPage(dsettings::Page::Mods);
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
} // namespace drakan_settings_host::detail

namespace drakan_settings_host {
using namespace detail;

void attachRenderer(gsr::Renderer* gs, void* parentHwnd, const WindowOps& ops) {
    g_gs = gs;
    drakan_save_previews::attach(gs);
    drakan_fmv_hd::attach(gs);
    drakan_intro_movie::attach(gs);
    gs->setWaterRendering(drakan_cfg::getBool("video", "modern_water", "GSR_MODERN_WATER", false),
                          drakan_cfg::getBool("video", "ray_reflections", "GSR_RAY_REFLECTIONS", false),
                          drakan_cfg::getBool("video", "ray_shadows", "GSR_RAY_SHADOWS", false),
                          drakan_cfg::getBool("video", "ray_gi", "GSR_RAY_GI", false));
    g_parent = parentHwnd;
    g_ops = ops;
    {
        // auto (the default) = the display refresh while vsync is off; 0 = off; a number = that many fps.
        const std::string fl = drakan_cfg::getString("video", "frame_limit", "DRAKAN_FRAME_LIMIT", nullptr);
        g_frameLimit.store(fl.empty() || drakan_cfg::compareNoCase(fl.c_str(), "auto") == 0
                               ? dsettings::kFrameLimitAuto
                               : std::max(0, std::atoi(fl.c_str())));
    }
    gs->setOverlayCallback(&overlayCallback);
}

void detachRenderer() {
    drakan_save_previews::attach(nullptr);
    drakan_fmv_hd::attach(nullptr);
    drakan_intro_movie::attach(nullptr);
    if (g_gs)
        g_gs->setOverlayCallback(nullptr);
    for (auto& p : g_previews) {
        dropTexture(p.second.tex);
        if (p.second.decode.valid())
            p.second.decode.wait(); // a worker decode ends before the renderer goes
    }
    g_previews.clear();
    for (auto& p : g_mapPreviews) {
        if (p.second.decode.valid())
            p.second.decode.wait();
        dropTexture(p.second.tex);
    }
    g_mapPreviews.clear();
    for (auto& [id, preview] : g_characterModels) {
        if (preview.decode.valid())
            preview.decode.wait();
        if (preview.model)
            preview.model->release([](ImTextureID texture) { dropTexture(texture); });
    }
    g_characterModels.clear();
    g_previewDrops.clear(); // waits for the dropped decodes
    g_pcs.reset();          // it refers to the overlay's store
    g_overlay.reset();
    g_open.store(false);
    g_gs = nullptr;
}

void setAudio(spu2::AudioOutput* audio) {
    g_audio.store(audio);
    drakan_intro_movie::setAudio(audio);
}
void harnessSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> l(g_harnessSettingsM);
    g_harnessSettings.emplace_back(key, value);
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
void syncTitleBackdropNow() {
    syncTitleBackdrop();
}
} // namespace drakan_settings_host
#endif // PS2X_HAVE_DRAKAN_SETTINGS
