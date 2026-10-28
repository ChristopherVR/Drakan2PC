# drakan_settings: the in-game PC settings overlay

A Dear ImGui menu (F1, or Options > "PC Settings" in the game's own menu) to configure display, audio, controls and general options of the
Windows port. It reads and writes `drakan.ini` and `drakan_input.ini` **without destroying comments, unknown keys, key order or the line-ending style**,
and applies what it can live through a small host interface (`ISettingsHost`) that the runtime implements.

```
include/settings/IniDoc.h     comment/format preserving ini document (std only)
include/settings/Types.h      Settings model (Video / Audio / General / Controls + bindings), enum <-> ini spelling
include/settings/Store.h      SettingsStore: load, minimal-diff save, parseSettings/writeSettings, changedKeys
include/settings/Bindings.h   action catalogue (the game's own names), key names, display names, reserved keys, built-in default input ini
include/settings/Host.h       ISettingsHost, HostCaps, ApplyResult, Paths
include/settings/Menu.h       SettingsMenu: the ImGui UI (backend agnostic) + style + fonts
include/settings/Schema.h     buildSettingsSchema / applySettingChange: the rows both settings UIs draw
include/settings/Apply.h      applySettings (the Apply path), revertVideo, page defaults, binding edits
include/settings/ModernSettings.h  glue of the modern PC Settings pages (gameui::SettingsPages)
include/settings/Overlay.h    SettingsOverlay: Win32 + Direct3D 11 glue (F1, subclass, gamepad combo, render)
../../third_party/imgui/     Dear ImGui v1.91.9 (MIT, pinned), only core + imgui_impl_win32/dx11, Roboto Medium (Apache-2.0) embedded as fallback font
../../third_party/fonts/gelasio/  Gelasio Regular / Bold (OFL 1.1), compressed sources: the UI text face (body, headings, the modern Tab screen's fonts), in place of Windows' Palatino / Georgia
tests/settings_test.cpp       unit tests (ini round trips, bindings editing, file IO, real files)        202 checks
tests/settings_schema_test.cpp schema coverage / round trips, ModernSettings apply path
tests/settings_pages_preview.cpp the modern pages over the real schema (JSON lines, render_drawlist.py)
tests/settings_demo.cpp       overlay over a dummy D3D11 window with a fake host: self test + screenshots   46 checks
docs/reference/settings-menu.md         (in docs/) how the game's menus work and the hook for the in-game entry
```

CMake targets: `drakan_settings_core` (std only) <- `drakan_settings_ui` (+ ImGui) <- `drakan_settings_d3d11` (Windows glue); `drakan_settings` is an interface
target that links all of them. A browser build keeps core + ui and replaces only `Overlay.cpp` with a WebGPU/GLFW (or Emscripten) backend (see `docs/reference/portability.md`).

## Build and test

```powershell
$cm = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cm -S runtime\settings -B build\settings -G "Visual Studio 17 2022" -A x64
& $cm --build build\settings --config Release
build\settings\Release\settings_test.exe [path\to\drakan_input.ini [path\to\drakan.ini]]   # unit tests; the optional files are round-tripped byte for byte
build\settings\Release\settings_demo.exe --screenshots build\screenshots                  # self test (key capture, apply, files) + settings_*.png
build\settings\Release\settings_demo.exe                                                  # interactive: F1 opens the menu (also copied to runtime\settings\tests\settings_demo.exe)
```
Screenshots (`build/screenshots/settings_*.png`, 1600x900 and two 1280x720): `display`, `display_textures`, `audio`, `controls_game`, `controls_conflicts`,
`capture`, `capture_conflict`, `controls_menu`, `controls_mouse`, `general`, `restart_prompt`, `revert_prompt`, `display_720p`, `controls_720p`.

## What the user sees

Pages (left column; Pg Up / Pg Dn, L2 / R2 or the mouse switch pages), bottom bar `Defaults for this page | Cancel | Apply | OK`:

* **Display**: window mode (windowed / borderless fullscreen / exclusive when the host supports it), window size presets (those that fit the monitor) or custom size, aspect
  ratio (4:3, 16:9 stretched, native, stretch to window), internal render scale (auto, 1x..8x), world frame rate (default uncapped / capped up to 60 / original 30, restart required), VSync, presentation limiter, deinterlacing, flicker filter, sharp scaling, texture
  filtering (game / point / bilinear), anisotropic filtering, texture upscaling + filter, mip bias, HD texture replacement, texture dump. Settings the host cannot do are greyed out with a tooltip.
* **Audio**: sound on/off, master volume 0..200 %, output device (enumerated by the host). Music/effects balance is separate inside the game's own audio screen and is not
  reachable from the host, so only the master level is offered.
* **Controls**: a *Keyboard and mouse / Controller* switch at the top (it drives `[general] ui_prompts`, so it is the same setting as *Button prompts* and needs a restart for the in-game prompts). *Controller* shows the
  game's PS2 pad layout (read-only) and the gamepad options. *Keyboard and mouse* has three tabs. *Gameplay keys* and *Menu keys* are full binding editors for `[bindings]` / `[menu_bindings]` with the game's own action names (Jump, Crouch, Attack,
  Use Magic / Parry, Call Arokh / Use, Quick Inventory, Toggle Targeting, Next Target / Strafe, Inventory / Map, Game Menu, weapon slots, special moves, ...). Click a slot to rebind (press a
  key, scroll the wheel, or click in the capture box with any mouse button; *Bind Esc*, *Clear*, *Cancel* buttons, Esc cancels), right click clears. Conflicts (a key used for several
  actions) are amber with a tooltip and a *Replace / Use for both / Cancel* question; keys the port reserves (F1, F11, the mouse-capture key) are red. Unknown tokens in the file (`CMD:..`) are listed under
  "Other bindings in the file". *Mouse and gamepad*: look mode (direct / stick), sensitivity X/Y (linked or separate), invert Y, capture mouse, Esc releases the mouse, capture toggle key,
  walk speed, full-speed keyboard (curve compensation), gamepad on/off and dead zone.
* **Gameplay**: skip memory card warning, button prompts (keyboard and mouse / PlayStation 2 icons), language (English / French / German / Italian / Spanish), menu size; hotkey overview.

Behaviour: **Apply** writes the files and calls the host (live where possible); a window mode / size / scale / aspect change opens "Keep display settings?" with a 12 s auto-revert; settings
that cannot be applied live (amber `*` next to the label) end in a "Restart required" dialog (`requestRestart`). **Cancel** / F1 / Esc discard edits. Everything is navigable with keyboard
(arrows, Enter, Tab) and gamepad (D-pad, A, B closes, L2/R2 pages).

## Settings reference

`Live` = applied by `ISettingsHost::apply*` while the game runs (as far as the host implements it); `restart` = in `HostCaps::restartKeys`, the menu asks for a restart. The runtime owner can change the
set per build; `video.world_rate` always requires a restart because it is read at startup. All keys are optional; a missing key means the default shown.

| File / key | Default | UI | Apply | Notes / consumer |
|---|---|---|---|---|
| `drakan.ini [video] fullscreen` + `window_mode` | 0 / windowed | Window mode | live | `windowed\|borderless\|exclusive`. The menu writes both keys (older builds only know `fullscreen`). `window_mode` is a **new key** |
| `[video] window_width/height` | 1280x960 | Window size | live | resizes the host window; in fullscreen it is the exclusive mode size |
| `[video] scale` | auto | Internal render scale | live | `auto` or 1..8; `gsr::Renderer::setInternalScale` (targets re-created at the next vsync, one frame may glitch). `auto` = the host's pick for the window size |
| `[video] aspect` | 4:3 | Aspect ratio | live | `4:3\|16:9\|native\|stretch`; `Renderer::setAspect(forcedAspect, stretch)` |
| `[video] vsync` | 1 | VSync | live | `gsr::Renderer::setVsync` |
| `[video] filter` | game | Texture filtering | live | `Renderer::setTextureFilter` |
| `[video] deinterlace`, `flicker_filter`, `sharp_present` | weave, 0, 1 | Presentation | live | `Renderer::setDeinterlace / setFlickerFilter / setSharpPresent` |
| `[video] world_rate` | uncapped | World frame rate | restart | `uncapped` (default, no fixed upper limit), `60` (up to 60 FPS), or `30` (original world cadence). CPU optimizations remain enabled in every mode; the guest clock is unchanged. The settings model uses `worldRate = 0` for `uncapped` and serializes it as `uncapped`; numeric `0` and other invalid values fall back to 30. Consumed by startup runtime configuration. VSync and the presentation limiter are separate settings. |
| `[video] frame_limit` | 0 | Presentation limiter | live | Presentation fps cap, 0 = off; implemented by the host loop. Does not raise the world update rate. |
| `[video] anisotropic` | 1 | Anisotropic | live | **new key**, 1 2 4 8 16; `Renderer::setAnisotropic` |
| `[video] texture_upscale`, `texture_upscale_filter` | 1, default | Texture upscaling | live | **new keys**; `Renderer::setTextureUpscale(factor, filter)` flushes the upscaled-texture cache (textures are decoded again, brief hitch); filter names come from `HostCaps::textureUpscaleFilters` |
| `[video] mip_bias` | 0.0 | Mip bias | live | **new key**, -2..+2 (or `auto`); `Renderer::setMipBias` |
| `[video] hd_textures` | 1 | HD texture replacement | live | **new key**; `Renderer::setTextureReplace` rescans the texture folder and flushes the caches (the hle override layer for other assets still needs a restart) |
| `[video] texture_dump` | 0 | Texture dump | live | **new key**; `Renderer::setTextureDump` |
| `[audio] enabled`, `volume` | 1, 100 | Sound, Master volume | live | % 0..200; `AudioOutput::setMuted(!enabled)` + `setMasterGain(volume/100)` (10 ms ramp, no click; also applied to the WAV dump) |
| `[audio] device` | default | Output device | live | name or part of it; the menu stores the endpoint name, `AudioOutput::setOutputDevice` re-opens the stream (list: `AudioOutput::enumerateDevices`) |
| `[general] skip_memcard_warning` | 1 | Skip memory card warning | live | |
| `[general] ui_prompts` | keyboard | Button prompts | restart | **new key**: `keyboard` or `ps2` (the existing switch is env `DRAKAN_UI_PROMPTS=0`) |
| `[general] language` | english | Language | restart | **new key**: `english\|french\|german\|italian\|spanish` |
| `[general] settings_ui_scale` | auto | Menu size | live | **new key**, `auto` or 0.75..2.5 |
| `[general] inventory_screen` | native | Gameplay > Inventory screen | next open | **new key**: `native` (original Tab screen) or `modern` (host-drawn screen, `runtime/gameui`) |
| `[general] pause_menu` | native | Gameplay > Pause menu | next Esc | **new key**: `native` (the game's pause page) or `modern` (host-drawn `gameui::PauseScreen`, `docs/reference/mouse-menus.md` section 14) |
| `[general] dual_wield` | 0 | Gameplay > Combat > Dual wielding | next weapon used | **new key**: a second one-handed weapon goes to Rynn's left hand (`drakan_dual_wield`, `docs/reference/combat-controls.md` "Dual wielding") |
| `[general] save_menu` | native | Gameplay > Save / load screens | not wired yet | **new key**: `native` (the game's slot screens 4 / 5) or `modern` (host-drawn `gameui::SaveLoadScreen`, `docs/journal/2026-10-25/modern-save-load-title.md`) |
| `[general] title_menu` | native | Gameplay > Title menu | not wired yet | **new key**: `native` (the game's title menu, screen 8) or `modern` (host-drawn `gameui::TitleScreen`) |
| `drakan_input.ini [general] capture_mouse, release_on_esc, toggle_key, look_mode` | 1, 0, F10, direct | Mouse and gamepad | live | `drakan_input::reloadConfig(path)` (swapped in between frames) |
| `[mouse] sensitivity_x/y, invert_y` | 0.0020, 0 | | live | |
| `[keyboard] walk_scale, curve_compensation` | 0.5, 1 | | live | |
| `[gamepad] enabled, deadzone` | 1, 0.15 | | live | |
| `[bindings]`, `[menu_bindings]` | see input README | key editor | live | rewritten line by line, see below |

Environment variables (`DRAKAN_VSYNC`, `PS2X_GS_SCALE`, `DRAKAN_VOLUME` ...) still **override** the files, also over what the menu writes; the menu cannot see them. If the owner wants to warn about it,
`getenv` the names from `drakan_config.cpp` in `capabilities()`.

### How the files are written

* Only keys whose value differs from what was loaded are touched (`writeSettings(before, after, ...)`). A save without changes is byte identical (tested on the real `build/recomp_out/bin` files).
* A changed key keeps its indentation, `=` spacing and trailing comment (`vsync = 0   ; keep`). A new key is appended to its section (section created at the end if missing) with a one line comment above.
* Bindings: a rebound action removes the action from the old source line (the line is deleted when it becomes empty), adds it to the line of the new source (`C = CROSS | TRIANGLE`) or appends a new
  `SOURCE = ACTION` line after the last binding. A layer that equals the built-in default and has no section in the file is not written. `ACT:0x0e` ids keep their lower case hex.
* If `drakan_input.ini` does not exist, the file is created from `ISettingsHost::defaultInputIni()` (return `drakan_input::Config::defaultIniText()`), so the file is complete.
* Writes go to `<file>.tmp` then rename.

## Integration guide for the runtime owner

Everything below uses names that exist today (`gsr::Renderer`, `GsrBridge::hostTick`, `drakan_input::*`, the `PadMgr_Update` hook of `drakan_input_glue.cpp`). Code is a sketch to paste, not compiled in the runtime tree.

### 1. Build

```cmake
add_subdirectory(${DECOMP}/runtime/settings drakan_settings)     # next to the gs / input / ui add_subdirectory calls
target_link_libraries(ps2_runtime PRIVATE drakan_settings)        # needs d3d11, dxgi, comctl32 (linked by the target)
```
`imgui` is built as `drakan_imgui` with its own include dir; if raylib's rlImGui (`ps2_debug_panel`) also uses ImGui, keep a single ImGui version: the overlay uses its **own context** and switches
`ImGui::SetCurrentContext` internally (restoring yours), so both can live side by side.

### 2. Present path: gsr overlay callback (already added to gsr)

`gsr.h` now has `struct OverlayContext {device, context, rtv, width, height}` and `Renderer::setOverlayCallback(std::function<void(const OverlayContext&)>)`; it is invoked at the end of
`presentLatched()` (live and latched present), after the game image has been drawn and just before `Present()`. In `GsrBridge::create(...)` after `gs = gsr::Renderer::create(cfg)` and after
the bridge subclassed the parent window (`ParentProc`; our `SetWindowSubclass` must be installed after it so that it runs first):

```cpp
#include "settings/Overlay.h"
static std::unique_ptr<dsettings::SettingsOverlay> g_overlay;          // owned by the bridge
static DrakanSettingsHost g_host;                                       // section 5
extern std::atomic<bool> g_openSettings;                                // set by the Options hook (docs/reference/settings-menu.md)

s.gs->setOverlayCallback([&](const gsr::OverlayContext& oc) {
    if (!g_overlay) {                                                   // lazy: needs the device, runs on the presenting (host) thread
        dsettings::OverlayInit oi;
        oi.hwnd = s.parent;                                             // the raylib/GLFW window: it receives the keyboard and the mouse (the child is HTTRANSPARENT)
        oi.device = static_cast<ID3D11Device*>(oc.device);
        oi.context = static_cast<ID3D11DeviceContext*>(oc.context);
        oi.host = &g_host;
        oi.paths.drakanIni = drakan_cfg::iniPath();
        oi.paths.inputIni  = drakan_cfg::exeDir() + "drakan_input.ini";   // or the module's resolved path
        g_overlay = dsettings::SettingsOverlay::create(oi);             // installSubclass = true: F1, mouse and keyboard need no WndProc code
        if (!g_overlay) return;
    }
    if (g_openSettings.exchange(false)) g_overlay->open();              // request from the EE thread (Options hook, gamepad, ...)
    g_overlay->render(static_cast<ID3D11RenderTargetView*>(oc.rtv), oc.width, oc.height);
});
```
`render()` returns immediately when the menu is closed (it only polls the gamepad combo Back + LB + RB, hold 0.5 s). Shut down in `GsrBridge::~GsrBridge`: `g_overlay.reset()` before the renderer.
Without a gsr callback (software renderer `PS2X_GS=sw`) the overlay is not available.

### 3. Window messages

With `OverlayInit::installSubclass = true` (default) the overlay subclasses the window (`SetWindowSubclass`), so nothing else is needed. When it is **open** it consumes key-down, char, mouse-button-down and wheel messages
(releases and mouse moves still reach the game window so GLFW never sees a stuck key) and draws its own cursor. F1 toggles it in all states. Note that the bridge polls `GetAsyncKeyState` for Alt+Enter / F11, which keeps working while the menu is open.
Manual forwarding (instead of the subclass): `intptr_t r; if (g_overlay->handleMessage(hwnd, msg, wp, lp, &r)) return r;` at the top of your window procedure.

### 4. Pause the game while the menu is open

`g_overlay->isOpenAtomic()` is the thread-safe flag. Two parts:

```cpp
// (a) input module: release the mouse, show the cursor (drakan_input::setMenuActive(true) frees ClipCursor and the capture)
struct DrakanSettingsHost : dsettings::ISettingsHost {
    void onOverlayOpened() override { drakan_input::setMenuActive(true); /* + mute/duck SPU2 if wanted */ }
    void onOverlayClosed() override { drakan_input::setMenuActive(false); }
    ...
};

// (b) EE: stop the game frame. hookPadMgrUpdate (0x1e3970, once per Engine_Tick, EE game thread) in drakan_input_glue.cpp:
void hookPadMgrUpdate(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (g_overlay_open.load()) {                          // = g_overlay->isOpenAtomic()
        while (g_overlay_open.load() && !rt->stopRequested())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        // the frame that was about to start still runs normally after the menu closes, with neutral pad state because beginFrame() has not run
    }
    beginFrame();  g_origPadMgrUpdate(rdram, c, rt);  ...     // existing code
}
```
Blocking in `PadMgr_Update` freezes the main game loop at a frame boundary (nothing is mid-frame), while the host thread keeps presenting the last latched image plus the overlay (`hostTick`
calls `gs->present()` every loop). Other EE threads (sound, streaming) and the IOP keep running, so music continues; duck it from `onOverlayOpened` if you prefer silence. Alternative: a pause flag in
`EeScheduler::checkpointDue`. Make sure no **new** guest vsync interrupts are counted as missed frames while paused (the frame is one `Engine_Tick`, so there is no catch-up burst).
The Options hook of `docs/reference/settings-menu.md` section 3.1 also runs on the EE thread; it must only set `g_openSettings`.

### 5. `ISettingsHost` mapped to what exists

```cpp
struct DrakanSettingsHost final : dsettings::ISettingsHost {
    dsettings::HostCaps capabilities() const override {
        dsettings::HostCaps c;
        c.exclusiveFullscreen = false;                        // borderless only today
        c.frameLimiter = true;                                // host loop sleeps (implement in hostTick, see applyVideo)
        c.textureUpscale = c.anisotropic = c.mipBias = true;  // gsr live setters exist
        c.hdTextures = true;  c.textureDump = true;  c.textureUpscaleFilters = {"default", "xbrz", "smooth"};
        c.languageSelect = true;  c.promptStyle = true;
        c.restartKeys.insert("general.language");  c.restartKeys.insert("general.ui_prompts");   // world_rate always needs a restart
        return c;
    }
    dsettings::MonitorInfo monitor() const override {         // MonitorFromWindow + GetMonitorInfoW + EnumDisplaySettingsW
        dsettings::MonitorInfo m; MONITORINFOEXW mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromWindow(g_parent, MONITOR_DEFAULTTONEAREST), &mi)) {
            m.nativeWidth = mi.rcMonitor.right - mi.rcMonitor.left;  m.nativeHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
        return m;
    }
    std::vector<std::string> audioDevices() const override {                                // friendly names; setOutputDevice accepts a part of the name (or the id)
        std::vector<std::string> v; for (auto& d : spu2::AudioOutput::enumerateDevices()) v.push_back(d.name); return v;
    }
    void windowSize(int& w, int& h) const override { RECT rc; GetClientRect(g_parent, &rc); w = rc.right; h = rc.bottom; }

    dsettings::ApplyResult applyVideo(const dsettings::VideoSettings& n, const dsettings::VideoSettings& p) override {
        dsettings::ApplyResult r;
        if (n.vsync != p.vsync) g_gs->setVsync(n.vsync);                                    // queued, applied at the next vsync, any thread
        g_frameLimit.store(n.frameLimit);                                                   // read by GsrBridge::hostTick
        if (n.windowMode != p.windowMode) setBorderlessFullscreen(n.windowMode != dsettings::WindowMode::Windowed);  // the code behind Alt+Enter / F11, on this (UI) thread
        if (n.windowMode == dsettings::WindowMode::Windowed && (n.windowWidth != p.windowWidth || n.windowHeight != p.windowHeight))
            SetWindowPos(g_parent, nullptr, 0, 0, clientToWindow(n.windowWidth), clientToWindow(n.windowHeight), SWP_NOMOVE | SWP_NOZORDER);
        // gsr live setters (any thread, applied together at the next frame boundary; see runtime/gs/README.md "Live settings"):
        using dsettings::Aspect;
        if (n.aspect != p.aspect)
            g_gs->setAspect(n.aspect == Aspect::Ratio16x9 ? 16.f / 9.f : n.aspect == Aspect::Ratio4x3 ? 4.f / 3.f : 0.f, n.aspect == Aspect::Stretch);
        if (n.filter != p.filter) g_gs->setTextureFilter((uint32_t)n.filter);                            // enum order = gsr: 0 game, 1 point, 2 bilinear
        if (n.deinterlace != p.deinterlace) g_gs->setDeinterlace((uint32_t)n.deinterlace);              // 0 weave, 1 bob, 2 blend
        if (n.flickerFilter != p.flickerFilter) g_gs->setFlickerFilter(n.flickerFilter);
        if (n.sharpPresent != p.sharpPresent) g_gs->setSharpPresent(n.sharpPresent);
        if (n.anisotropic != p.anisotropic) g_gs->setAnisotropic((uint32_t)n.anisotropic);
        if (n.mipBias != p.mipBias) g_gs->setMipBias(n.mipBias, false);                                   // (auto bias: setMipBias(0, true))
        if (n.textureUpscale != p.textureUpscale || n.textureUpscaleFilter != p.textureUpscaleFilter)
            g_gs->setTextureUpscale((uint32_t)n.textureUpscale,   // flushes the upscaled-texture cache; caps.textureUpscaleFilters = {"default","xbrz","smooth"}
                                    n.textureUpscaleFilter == "xbrz" ? 1u : n.textureUpscaleFilter == "smooth" ? 2u : 0u);
        if (n.hdTextures != p.hdTextures) g_gs->setTextureReplace(n.hdTextures);                  // rescans the folder
        if (n.textureDump != p.textureDump) g_gs->setTextureDump(n.textureDump);
        if (n.scaleAuto != p.scaleAuto || n.scale != p.scale) g_gs->setInternalScale(n.scaleAuto ? autoScaleForWindow() : (uint32_t)n.scale); // render targets re-created at the next vsync (one frame may glitch)
        return r;
    }
    dsettings::ApplyResult applyAudio(const dsettings::AudioSettings& n, const dsettings::AudioSettings& p) override {
        g_audio->setMuted(!n.enabled);                                                      // spu2::AudioOutput: ramped, applied to device + dump
        g_audio->setMasterGain(n.volume / 100.0f);
        if (n.device != p.device) g_audio->setOutputDevice(n.device.empty() ? "default" : n.device);   // re-opens the WASAPI stream
        return {};
    }
    void reloadBindings(const std::string& path) override { drakan_input::reloadConfig(path.c_str()); }   // swapped in between frames, capture state kept
    void regeneratePrompts() override { /* drakan_ui builds the key-cap textures when the game requests the resources: effective after the next level load */ }
    void requestRestart(const std::vector<std::string>&) override { relaunchSelf(); PostMessageW(g_parent, WM_CLOSE, 0, 0); }   // CreateProcess(same exe + args), then close
    std::string defaultInputIni() const override { return drakan_input::Config::defaultIniText(); }
    void onOverlayOpened() override { drakan_input::setMenuActive(true); }
    void onOverlayClosed() override { drakan_input::setMenuActive(false); }
};
```

**Live-apply APIs in the other modules** (all landed, standalone, tested; the host calls them from `applyVideo` / `applyAudio` / `reloadBindings` above):

| Module | API | Notes |
|---|---|---|
| `runtime/input` | `drakan_input::reloadConfig(path)` / `InputModule::reloadConfig`, atomic `setMenuActive` | thread safe; swapped in at the next `beginFrame`; capture state kept; held commands released. See `runtime/input/README.md` "Live reconfiguration" |
| `runtime/gs` | `Renderer::setAspect / setTextureFilter / setDeinterlace / setFlickerFilter / setSharpPresent / setAnisotropic / setMipBias / setTextureUpscale / setTextureReplace / setTextureDump / rescanTextures / setInternalScale / setVsync / setFrameLatency` | any thread; queued and applied together at the next frame boundary under the renderer mutex; texture settings flush the texture caches, `setInternalScale` re-creates the render targets. See `runtime/gs/README.md` "Live settings" |
| `runtime/spu2` | `AudioOutput::setMasterGain / setMuted / setOutputDevice / enumerateDevices / renderBlock` | atomic gain with a 10 ms ramp applied to device, dump and meter; live device switch by endpoint id or name. See `runtime/spu2/README.md` "Live output controls" |
| `drakan_config` | optional `reload()` (clear `g_loaded`) | not needed by the overlay: it writes the file itself; useful for a "reload from disk" debug command |

Wiring notes: `audioDevices()` returns friendly names, so `[audio] device` holds a name and `setOutputDevice(name)` matches it as a substring (ids also work). The window size / fullscreen / frame limiter
are host-side (no module API needed). `g_audio` is the `spu2::AudioOutput` the runtime created; because the old code fixed `o.volume` at start, set the initial gain from `drakan.ini` through
`Options::volume` as before and let the overlay take over afterwards.

### 6. Options > "PC Settings"

See `docs/reference/settings-menu.md` section 3.1: rename XDB string 156 ("Adjust Screen") with `overrides/INTERFCE.XDB/text.ini`, hook `0x4424e0` and set `g_openSettings` when the widget `0x2345` is activated.

### 7. Browser build

Keep `drakan_settings_core` + `drakan_settings_ui`; write a `SettingsOverlay` replacement for the new backend (`imgui_impl_wgpu` + `imgui_impl_glfw` or Emscripten events): create the context, call
`buildFonts(io)`, `applyDrakanStyle(scale)`, and per frame `NewFrame(); menu.frame(); Render()`. File IO goes through `FileIO` (OPFS / IndexedDB), window / audio effects through your `ISettingsHost`.
`SettingsMenu` itself uses no Win32, D3D or file API (the store is injected).

## API cheat sheet

```cpp
dsettings::SettingsOverlay::create(OverlayInit)  -> unique_ptr          // nullptr when ImGui backends fail
overlay->render(rtv, w, h)  /  open() / close() / toggle() / isOpen() / isOpenAtomic()
overlay->setOpenChangedCallback(fn)   overlay->menu()   overlay->store()
dsettings::SettingsStore store(Paths, FileIO, defaultInputIni); store.load(); store.loaded(); store.save(settings)
dsettings::writeSettings(before, after, drakanDoc, inputDoc)             // pure, used by save()
dsettings::IniDoc::parse(text, ";")  .get/.set/.entries/.serialize()
```

## FPS counter, Quit to Desktop (branch `drakan-win`)
* `VideoSettings::showFps` (`show_fps` 0|1|2) and `fpsPosition` (`fps_position`), Display page rows "FPS counter" / "FPS counter position".
  `SettingsOverlay::setStatsLayer(HudLayer)` + static `drawStatsPanel(lines, corner, w, h, uiScale)`: a third layer drawn every present, also while the menu is open.
  The host (`drakan_settings_host.cpp`) feeds it from `drakan_fpsmeter`.
* `ISettingsHost::requestQuit()` (default no-op) and a footer button "Quit to Desktop" with a confirmation popup; the runtime closes the game cleanly.

## Game style and F1 gating (style branch)
The menu is drawn with the game's own UI art (frame, stone, brass plates, orb knobs, flame highlight, cursor, the decorative menu font), decoded at run time from the disc through
`ISettingsHost::gameTexture` (never shipped, never stored); without it the plain style is used. New: `Skin.h` / `Skin.cpp`, `TxdImage.h`, `skin_widgets.inc`, `ISettingsHost::playUiSound`,
`SettingsOverlay::setAllowGate`. Details, measured texture rectangles and the F1 gating rules: `docs/reference/settings-menu.md` (last section). `settings_demo --styleshots <dir>` (env `DRAKAN_DEMO_TXD` = an INTERFCE.TXD extracted from
the disc) writes `r6_f1_demo_*.png` at 1080p, 4K and 720p.

PC settings click sounds are currently silent in the runtime host. Calling the
guest sound manager from the input hook's copied context can block on a semaphore
or lose its continuation, freezing the game while changing settings or pressing
Apply / OK. The host no longer queues these guest calls; native game audio is
unchanged. Restoring these optional clicks requires a scheduler-owned sound path.

Normal launch (`Play.cmd`, `drakan.exe`, or the compatibility alias `Play-RawFPS.cmd`) respects the saved world rate. Choose **Original (30 FPS)** in F1 > Display, apply and restart to persist the original cadence. `Play-Original30.cmd` overrides world rate and presentation limit to 30 for one launch without rewriting settings. All modes keep the optimized vector CPU path.

## One schema for the overlay and the modern pages (2026-10-25)
The rows of the Display, Audio, Controls (mouse and gamepad) and Gameplay pages are no longer hand-built in `Menu.cpp`:
`buildSettingsSchema(settings, {caps, monitor, audio devices, link})` (`Schema.h`, library `drakan_settings_schema`)
describes every row (key = the `changedKeys` name, label, help, choices with labels, slider range / format, restart mark,
enabled, `confirm` for the display-risky rows) as a `gameui::SettingsSchema`, and `SettingsMenu::drawSchemaRow` draws
each with the same widgets as before (the screenshots of `settings_demo --screenshots` are pixel identical). Edits go
through `applySettingChange` (the store's field table: `setSettingValue` / `settingValue`). Apply / OK, the capture
commit, the resets and the display revert call `Apply.h`, which the modern pages' glue (`ModernSettings`) uses too.
**Adding a setting**: the `Types.h` member and the `Store.cpp` field as before, then its row in `Schema.cpp`; it then
appears in the F1 overlay and in the modern pause menu's PC Settings pages (`settings_schema_test` fails while a field
has no row). The binding table, the controller layout, the hotkey list and the Controls view switch stay hand-drawn.
The contract of the modern pages: `runtime/gameui/README.md`, "PC Settings pages (modern)".
