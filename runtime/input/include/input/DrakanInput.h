// Drakan mouse + keyboard input module: integration API. See README.md for the exact wiring.
#pragma once
#include "input/Config.h"
#include "input/FrameInput.h"
#include "input/GuestInterface.h"
#include "input/HostInput.h"
#include "input/MenuMouse.h"
#include "input/MenuMouse.h"
#include "input/PadSynth.h"
#include "input/Script.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace drakan_input {

class InputModule {
public:
    InputModule();
    ~InputModule();

    // hwnd: game window (or nullptr for a private raw-input thread). Config file is created with defaults if absent.
    // Returns false only if the host side could not start (the module still works with injected frames).
    bool init(void* hwnd, const std::string& configPath);
    // Headless (tests): no Win32; frames are supplied through beginFrame(const FrameInput&).
    void initHeadless(const Config& cfg);
    void shutdown();

    bool onWindowMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);

    // Once per game frame, before the pad is read (e.g. at the first scePadRead after PadMgr_Update starts).
    void beginFrame();
    void beginFrame(const FrameInput& injected);         // tests / live window
    void beginExternalFrame(const FrameInput& injected); // supplied snapshot plus one guest-frame counter tick
    // Portable hosts own pointer lock/capture on their window thread. A value
    // supplied here overrides only capture policy, leaving keyboard/pad input
    // active. Native Windows keeps the module's default capture state machine.
    void setExternalCapture(bool captured) { externalCapture_.store(captured ? 1 : 0); }

    // scePadRead replacement. Writes the 32-byte sample for port 0 and returns true; returns false for other ports
    // (the hook should then return 0 = "no data").
    bool fillPadSample(int port, int slot, uint8_t out[32]);

    // After-hook for Camera_UpdateLook (0x44e4a8) (a0 = player, a1 = float* limits, a2 = dragon mode).
    // Returns true when mouse look was applied.
    bool afterCameraUpdateLook(IGuest& g, uint32_t playerAddr, uint32_t limitsAddr, bool dragonMode);
    // Before-hook for Camera_UpdateLook: while the mouse looks (direct mode, captured, no pad right stick) the game's
    // "stick position mode" flag is cleared for the duration of the call so the game does not re-centre the pitch;
    // afterCameraUpdateLook restores it. Supply the actual callback player to enable narrowly scoped aim-pitch
    // persistence. The legacy two-argument call only suppresses position mode; it cannot snapshot a player.
    void beforeCameraUpdateLook(IGuest& g, bool dragonMode, uint32_t playerAddr = 0);
    // Around the on-foot camera tick FUN_0041f848 (cam = its argument): the camera position eases towards its place
    // behind Rynn by min(1, cam+0x210 * average dt) per tick, with cam+0x210 near the camera's "Rubber Band Strength"
    // (2/s, a ~0.5 s lag), so turns swing round late. While mouse / keyboard steer, beforeCameraFollow turns the
    // camera's previous position round Rynn's anchor by her turn and the pitch change since the last tick, and sets
    // cam+0x210 to `[mouse] camera_follow` (the native value goes back after ownership has been gone for a while).
    // Turns and looking up / down stay locked to her; her travel keeps the native easing and collision handling.
    void beforeCameraFollow(IGuest& g, uint32_t cam);
    // ---- modern bow aiming (`[mouse] bow_aim = modern`, src/BowAim.cpp, docs/reference/input.md section 16)
    // EE thread, before every PadMgr_Update: is Rynn's bow out (player+0xe90 == 5, native sight closed, on foot in
    // gameplay, mouse / keyboard owner)? Also runs the aim zoom on the camera's target FOV (cam+0x60).
    void updateBowAim(IGuest& g);
    // Before the native arrow launch FUN_0020e6c8 (bow = its argument): points the arrow of a shot started by
    // releasing the aim button at the crosshair (the camera's centre ray), unless a target is locked.
    void beforeBowLaunch(IGuest& g, uint32_t bow);
    // While aiming: draw a crosshair at the centre of the game image (overlay thread).
    bool bowCrosshairVisible() const { return bowAiming_.load() && !hostOwned() && !menuActive_.load(); }
    // Before-hook for PadMgr_Update: A / D strafe (on foot) by pointing the left stick's X at the strafe axis (3)
    // instead of the turn axis (0). `dragon` = current controlled-actor role: the game's mapping is restored there.
    void beforePadMgrUpdate(IGuest& g, bool dragon);

    // After-hook for PadMgr_Update (0x1e3970): delivers ACT:/CMD: bindings and (optionally) menu cursor / clicks.
    void afterPadMgrUpdate(IGuest& g);

    // Tell the module that a cursor-driven menu (or the pause menu) is showing: releases the mouse, switches to the
    // [menu_bindings] layer.
    // Thread-safe (atomic). While active: gameplay bindings, mouse look and gameplay mouse buttons are ignored, held
    // gameplay commands are released at the next beginFrame, the mouse is released.
    void setMenuActive(bool active) { menuActive_.store(active); }
    // True while something that needs the mouse cursor is up: a game menu / message box, the inventory or the map (set
    // by the glue). Cut scenes and loading screens use the menu bindings but are NOT a menu context: the mouse stays
    // captured and no cursor is drawn there.
    void setMenuContext(bool ctx) { menuCtx_.store(ctx); }
    bool menuContext() const { return menuCtx_.load(); }
    // Live reconfiguration, callable from any thread (e.g. the settings overlay). Re-parses `path` (created with
    // defaults if missing) on the calling thread; the new bindings / sensitivity / invert / look mode / deadzones are
    // swapped in at the start of the next beginFrame(), i.e. between frames. Capture state (user released / focus lost)
    // and the toggle state are kept; held commands of the old bindings are released. Returns false (config untouched)
    // if the file cannot be read; parse warnings are printed to stderr and returned through *warnings if given.
    bool reloadConfig(const std::string& path, std::string* warnings = nullptr);
    // Same with an already built Config (tests / hosts that parse themselves).
    void reloadConfig(const Config& cfg);
    // Test mode: host input comes from a script (see Script.h) instead of the OS. Returns false if the file is
    // unreadable.
    bool loadScript(const std::string& path, std::string* err);
    bool scripted() const { return scripted_; }
    // EE thread: actual window extent for virtual640x512 scripts, including initHeadless().
    void setScriptClientSize(int width, int height) {
        scriptClientW_ = width;
        scriptClientH_ = height;
    }
    int frameNo() const { return frameNo_; }
    // Display refresh period (s) while presentation is vsync-locked and the engine tick is snapped to whole
    // refreshes, 0 otherwise. While set, each frame takes the mouse motion of a window that advances by whole
    // refreshes (not the jittery host time between beginFrame calls), so a steady sweep turns evenly per frame.
    void setSampleRefresh(double seconds) { sampleRefresh_.store(seconds); }
    // The mouse-motion cutoff for a frame starting at nowUs (steady clock); exposed for tests.
    int64_t mouseCutoffUs(int64_t nowUs);
    void setPlayRun(int n) { playRun_ = n; } // consecutive gameplay frames (for the script's `sync play`)
    // Active GUI screen id, -1 none (the script's `sync screen`).
    void setActiveScreen(int id) { activeScreen_ = id; }
    const ScriptEvents& scriptEvents() const { return scriptEv_; }
    // Window whose client area is clipped / whose cursor is hidden while captured (raw input itself comes from the
    // module's own message-only window). Call after init().
    void setCaptureWindow(void* hwnd);
    bool menuActive() const { return menuActive_.load(); }
    // EE thread: the bound quickload edge at the title, before host-screen input is neutralized.
    bool titleQuickLoadPressed() const { return titleQuickLoad_; }

    // ---- menu mouse (MenuMouse.h, docs/reference/mouse-menus.md) ----
    // Suspends the game input while another UI (the settings overlay) owns the keyboard / mouse / pad: the pad sample
    // is neutral, no ACT:/CMD: presses, no menu mouse. Held-command releases still reach the guest. The game keeps
    // running (menu, music). Thread-safe. Injected taps still apply.
    void setSuspended(bool s);
    bool suspended() const { return suspended_.load(); }
    // Host screen mode (the modern Tab screen, runtime/gameui): a host UI drawn over a native screen that keeps
    // running. Like suspension for the guest (neutral pad, no bindings, no menu mouse, injected taps still apply),
    // but independent of it, so the F1 overlay can open and close over the host screen. The host screen reads its
    // own input (the overlay's window messages); the native capture / steering hooks keep working. Thread-safe.
    void setHostScreen(bool on);
    bool hostScreen() const { return hostScreen_.load(); }
    // Either of the above: the guest gets neutral input.
    bool hostOwned() const { return suspended_.load() || hostScreen_.load(); }
    // Scripted runs (DRAKAN_INPUT_SCRIPT) have no window messages: while hostScreen() is on, the script's key, mouse
    // and pad edges are queued here for the host screen to drain (DIK codes, client pixels, DualShock bits).
    struct HostScreenEvent {
        enum Kind : uint8_t { Key, MouseButton, Cursor, Pad } kind = Key;
        int code = 0; // DIK / MouseBtn bit / Pad bit
        bool down = false;
        int x = 0, y = 0, clientW = 0, clientH = 0; // Cursor: client pixels
    };
    std::vector<HostScreenEvent> drainHostScreenEvents();
    // The game image inside the client area as last published by setMenuView (client pixels).
    MenuView menuView() const {
        std::lock_guard<std::mutex> lk(viewM_);
        return view_;
    }
    // Rectangle of the game image inside the client area (letterbox / pillarbox / widescreen), client pixels.
    // Thread-safe (present thread).
    void setMenuView(const MenuView& v);
    // True when the host draws the menu cursor itself (overlay layer): the OS cursor is hidden over the window while a
    // menu is open.
    void setOverlayCursorAvailable(bool a) { overlayCursor_.store(a); }
    // Cursor for the overlay: visible only while a menu is open, the mouse is in use and the input is not suspended.
    bool menuCursorVisible() const { return menuCtx_.load() && mm_.cursorVisible() && !hostOwned(); }
    int guiScreenId() const { return mm_.activeScreenId(); }
    // Taps pad buttons (Pad::* bits) for `frames` game frames on top of whatever the user presses (ignores suspension).
    void injectPadTap(uint16_t bits, int frames = 1);
    const MenuMouse& menuMouse() const { return mm_; }
    MenuMouse& menuMouseRw() {
        return mm_;
    } // hotspots / UI sound for the game menu additions (runtime drakan_menus.cpp)
    // Number of config swaps applied so far (tests).
    int configGeneration() const { return cfgGen_.load(); }

    const Config& config() const { return cfg_; }
    Config& mutableConfig() { return cfg_; }
    const FrameInput& frame() const { return frame_; }
    bool captured() const { return frame_.captured; }
    bool freeCombatActive() const { return freeCombatFrame_.load() && !menuActive_.load() && !hostOwned(); }
    // EE thread only: validates Rynn/gameplay before overriding controller combat gates.
    bool freeCombatAllowed(IGuest& g, uint32_t player, uint32_t cam);

private:
    struct AimPitchState {
        uint32_t player = 0, ctx = 0, handle = 0, inner = 0, cam = 0, heading = 0;
        uint32_t flags90 = 0, flags94 = 0, cameraFlags = 0, mode = 0;
        uint32_t target = 0, targetHandle = 0;
        uint32_t vtable = 0, pitchFn = 0, pitchThis = 0;
        bool operator==(const AimPitchState&) const = default;
    };
    bool readAimPitchState(IGuest& g, uint32_t player, AimPitchState& state);
    bool pitchAllowed(IGuest& g, uint32_t player, uint32_t cam);
    bool nativeLookControlActive() const;
    float pitchViewGain(IGuest& g, uint32_t player, uint32_t cam) const;
    void bowAimFrame(); // beginFrame, m_ held: LMB = aim while the bow is out, release = one Square tap
    bool bowLmbIsSquare() const;
    std::atomic<bool> bowReady_{false}, bowAiming_{false};
    int bowShotFrames_ = 0; // frames during which a launch belongs to a shot released from the aim
    bool bowZoomActive_ = false;
    uint32_t bowZoomCam_ = 0;
    float bowZoomNative_ = 0, bowZoomTarget_ = 0;
    bool bowShoulderActive_ = false;
    uint32_t bowShoulderCam_ = 0;
    float bowShoulderNative_[2] = {};
    bool plainPitchBranch(IGuest& g, uint32_t player, uint32_t cam) const;
    bool followOwned_ = false; // cam+0x210 of followCam_ is ours; followNative_ is the game's value
    int followBailTicks_ = 0;
    uint32_t followCam_ = 0;
    float followNative_ = 0;
    std::atomic<double> sampleRefresh_{0.0};
    int64_t mouseCutoff_ = 0, lastFrameUs_ = 0;
    bool orbitValid_ = false;
    uint32_t orbitCam_ = 0;
    float orbitHeading_ = 0; // Rynn's ground-plane heading at the previous camera tick
    float orbitPitch_ = 0;   // cam+0x84 at the previous camera tick
    AimPitchState aimPitchState_, savedAimPitchState_;
    bool aimPitchOwned_ = false, aimPitchSaved_ = false;
    float savedAimPitch_ = 0;
    void finishFrame();
    void applyPendingConfig(); // takes m_
    void applyPendingLocked();
    std::mutex m_; // guest hooks may run on different EE host threads
    Config cfg_;
    bool mouseKeyboardOwner_ = true;
    std::atomic<int> externalCapture_{-1};
    std::atomic<bool> freeCombatFrame_{false};
    InputAccumulator acc_;
    std::unique_ptr<HostInput> host_;
    FrameInput frame_;
    PadSynthState ps_;
    bool walkHeld_ = false, walkLatched_ = false, walkActive_ = false;
    bool walkCommandSent_ = false;
    CommandTracker tracker_;
    std::vector<CommandEvent> pending_;
    std::atomic<bool> menuActive_{false};
    std::atomic<bool> menuCtx_{false};
    bool modeFlagSaved_ = false;
    uint32_t modeFlagValue_ = 0;
    bool dragon_ = false; // controlled-actor role reported through beforePadMgrUpdate
    bool strafeActive_ = false;
    int32_t strafeOrigAxis_ = 0;
    uint32_t strafeOrigScale_ = 0;
    std::atomic<bool> suspended_{false};
    std::atomic<bool> hostScreen_{false};
    void releaseOwnedInputLocked();
    std::mutex hostEvM_;
    std::vector<HostScreenEvent> hostEv_;
    uint16_t hostPadPrev_ = 0;
    int hostCursorX_ = -1, hostCursorY_ = -1;
    void queueHostScreenEvents(const FrameInput& f);
    std::atomic<bool> overlayCursor_{false};
    MenuMouse mm_;
    mutable std::mutex viewM_;
    MenuView view_;
    std::mutex tapM_;
    struct Tap {
        uint16_t bits;
        int left;
    };
    std::vector<Tap> taps_;
    uint16_t tapNow_ = 0;
    std::mutex pendM_;
    std::unique_ptr<Config> pending_cfg_;
    std::atomic<bool> hasPending_{false};
    std::atomic<int> cfgGen_{0};
    bool userReleased_ = false;
    bool focusLost_ = false;
    uint8_t recaptureButtons_ = 0; // click restoring gameplay capture is consumed until physical release
    bool layerIsMenu_ = false;
    bool inputWasSuspended_ = false;
    bool titleQuickLoad_ = false;
    bool inputWasCaptured_ = false;
    int pendingDx_ = 0, pendingDy_ = 0;
    bool padReady_ = false;
    bool inventoryBack_ = false; // a bound START is sent as SELECT (inventory "back") until its keys are released
    uint8_t padSample_[32] = {};
    InputScript script_;
    ScriptEvents scriptEv_;
    bool scripted_ = false;
    int frameNo_ = 0;
    std::chrono::steady_clock::time_point lastFrameAt_{}; // host time of the previous beginFrame()
    bool haveFrameAt_ = false;
    int playRun_ = 0;
    int activeScreen_ = -1;
    int scriptClientW_ = 0, scriptClientH_ = 0;
    uint32_t mouseBowCamera_ = 0; // sight opened while RMB was held
    bool mouseBowClosePulse_ = false;
};

// ---- Process-wide convenience wrappers (one global module) --------------------------------------------------------
bool init(void* hwnd, const char* configPath);
void shutdown();
bool onWindowMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);
void beginFrame();
bool fillPadSample(int port, int slot, uint8_t out[32]);
bool afterCameraUpdateLook(IGuest& g, uint32_t playerAddr, uint32_t limitsAddr, bool dragonMode);
void beforeCameraUpdateLook(IGuest& g, bool dragonMode, uint32_t playerAddr = 0);
void beforePadMgrUpdate(IGuest& g, bool dragon);
void afterPadMgrUpdate(IGuest& g);
void beforeCameraFollow(IGuest& g, uint32_t cam);
void updateBowAim(IGuest& g);
void beforeBowLaunch(IGuest& g, uint32_t bow);
void setMenuActive(bool active);
bool reloadConfig(const char* path); // see InputModule::reloadConfig
InputModule& module();
// Directory of the running executable with trailing separator ("" if unknown).
std::string exeDirectory();

} // namespace drakan_input
