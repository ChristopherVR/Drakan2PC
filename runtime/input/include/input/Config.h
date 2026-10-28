// drakan_input.ini: configuration + bindings.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace drakan_input {

enum class LookMode { Direct, Stick };    // Direct = raw angles into camera/player, Stick = emulated right stick
enum class CallAbi { FprFloat, GprBits }; // how the float angle is passed to FUN_0013ee90

struct Binding {
    enum Src { Key, Mouse, WheelUp, WheelDown } src = Key;
    int code = 0; // Key: DIK scancode; Mouse: MouseBtn bit
    enum Act { PadBtn, LStick, RStick, Walk, Action, Command, View, Quick, Save } act = PadBtn;
    uint32_t value = 0; // PadBtn: Pad mask; L/RStick: 0 up,1 down,2 left,3 right; Action: action id; Command: command
                        // id; View: kViewMap / kViewStrip; Quick: quick-slot number 1..kQuickSlotMax;
                        // Save: kQuickSave / kQuickLoad
};
// VIEW_MAP / VIEW_STRIP: open (gameplay) or close an Order-of-the-Flame style view of the inventory screen. Handled by
// the integration (drakan_inventory_views.inl), not by the pad synthesis.
constexpr uint32_t kViewMap = 1, kViewStrip = 2;
// QUICK1..QUICK9: use / equip the Nth item of the game's own quick inventory (QuickSlots.h). Handled by the
// integration through the native Quick Inventory bar, not by the pad synthesis.
constexpr uint32_t kQuickSlotMax = 9;
constexpr uint32_t kQuickSave = 1, kQuickLoad = 2;

struct Config {
    bool freeCombat =
        true; // [mouse] free_combat: free combat look outside explicit targeting and native alternate aiming
    // [general]
    bool captureMouse = true;
    bool releaseOnEsc = false; // Esc toggles capture off/on (the game opens its pause menu on Esc)
    int toggleKeyDik = 0x44;   // F10: manual capture toggle (-1 = none)
    LookMode lookMode = LookMode::Direct;
    // [mouse]
    float sensX = 0.0020f; // radians per raw count (Direct mode)
    float sensY = 0.0020f;
    bool invertX = false;
    bool invertY = false;
    float pitchHoldMs = 1200.0f; // DEPRECATED (ignored since controls v2): superseded by pitchPersist
    bool dragonStick = true;     // Arokh flight: the mouse steers through the game's own right-stick path (rate based)
                                 // instead of the direct camera / heading write
    bool pitchPersist = true;    // direct mode: the game's pitch re-centring is switched off while the mouse look is
                                 // active, so the pitch stays where the mouse left it
    float yawSign =
        -1.0f; // angle passed to the player turn function = yawSign * dx * sensX   (verified: mouse right turns right)
    float pitchSign = -1.0f;   // pitch += pitchSign * dy * sensY. A negative camera pitch looks DOWN (verified with
                               // screenshots): mouse up (dy < 0) raises it with -1
    float cameraFollow = 5.0f; // [mouse] camera_follow: while mouse / keyboard steer on foot, turns carry the camera
                               // round Rynn at once and its travel behind her eases at this rate (1/s); 0 = the
                               // game's own rubber band (turns swing round ~0.5 s late)
    bool bowAimModern = true;  // [mouse] bow_aim = modern: hold LMB with the bow out to aim (zoom + crosshair), release
                               // to shoot at the crosshair; classic = the game's own (LMB shoots along Rynn's aim)
    float bowZoom = 0.85f;     // [mouse] bow_zoom: field of view factor while aiming (1 = no zoom)
    float pitchViewGain = 1.8f;    // [mouse] pitch_view_gain: the on-foot camera shows 1.8 x cam+0x84; mouse pitch is
                                   // divided by it so a count turns the view as far up / down as sideways (1 = off)
    float stickFullCounts = 40.0f; // Stick mode: counts per 1/30 s that equal full deflection
    float stickSignX = 1.0f, stickSignY = 1.0f;
    // [keyboard]
    float walkScale = 0.5f; // left-stick magnitude while a WALK-bound key is held
    bool toggleRun = false; // WALK switches between run and walk on each press instead of while held
    bool strafeKeys = true; // A / D (left stick x) strafe instead of turn, like Drakan: Order of the Flame (on foot;
                            // the Arokh flight keeps the game's mapping)
    bool curveCompensation = true; // pre-invert the game's pow(|v|,2) so keyboard movement is linear/full speed
    // [gamepad]
    bool gamepadEnabled = true;
    float gamepadDeadzone = 0.15f;
    // [menu]
    bool menuCursor = false; // push host cursor into InputDev_SetCursorPos (UNVERIFIED, off by default)
    bool menuClicks = false; // synthesize 0x100/0x101 GUI events + action bits (UNVERIFIED, off by default)
    bool menuMouse = true;   // hover / click / wheel on the game's own menus (MenuMouse, docs/reference/mouse-menus.md)
    bool inventoryMouse = true;       // inventory / map screens: pointer -> D-pad steps, wheel = sections, click = use
                                      // (MenuMouse::updateInventory)
    bool inventoryClickSquare = true; // Equipment: left click = Square (use / equip); false = Cross
    bool menuDrawCursor =
        true; // draw a cursor in the overlay layer while the mouse is used in a menu (the OS cursor is hidden then)
    float menuCursorIdleMs =
        5000.0f; // the drawn cursor fades out this long after the last mouse movement (pad / keyboard users)
    // [menu] map_view: VIEW_MAP shows the map full screen (false: the normal screen on its Map page)
    bool mapView = true;
    // [menu] inventory_strip: VIEW_STRIP shows Equipment as a strip along the bottom (false: the normal screen)
    bool inventoryStrip = true;
    // [advanced] guest addresses / ABI (SCES_500.06)
    bool checkTurnGate = true; // replicate the game's own "may the player turn" vcall check
    CallAbi turnCallAbi = CallAbi::FprFloat;
    uint32_t addrCtxPtr = 0x5565d4;    // *(u32*) = game context
    uint32_t addrGuiPtr = 0x5565d8;    // GUI event manager pointer variable
    uint32_t addrScreenPtr = 0x5565c8; // *(u32*)+0xf8/+0xfc = screen w/h
    uint32_t addrPadMgr = 0x4e90c0;
    uint32_t addrTurnFn = 0x13ee90;
    uint32_t addrTurnUpVec = 0x4e8c48;
    uint32_t addrInvokeCallback = 0x131fa8;
    uint32_t addrTimerMs = 0x4af7f8;
    uint32_t guiVtableOffset = 0; // where the vtable pointer sits in the GUI event manager (UNVERIFIED)
    uint32_t addrActiveScreenVar =
        0x4fb60c; // *(u32*) = active GUI screen (id at +0x18, widget list head at +0x7c, focus at +0x84)
    uint32_t addrMouseCallback = 0x2cc7c0; // GUI mouse handler (type, button id, x, y): the game's own PC mouse path
    uint32_t addrUiEntry = 0x2bec48;       // returns the UI property table entry (sounds)
    uint32_t addrCapturedWidgetVar = 0x4fb19c; // widget that owns the pressed mouse button
    uint32_t addrStickModeFlag = 0x4e9594; // PadMgr+0x4d4: non-zero = "stick position mode" (pitch re-centres); cleared
                                           // around Camera_UpdateLook while the mouse looks
    uint32_t addrStickMap = 0x4e9558;      // PadMgr+0x498: 4 x {s32 axisId, float out, float scale} (lx, ly, rx, ry)
    int controlsVersion = 0;               // [general] controls_version (0 = file predates the OotF-style controls)

    std::vector<Binding> game; // [bindings]
    std::vector<Binding> menu; // [menu_bindings]
    // [bindings] keys written as "<key> = NONE": migrate() never binds them
    std::vector<int> gameKeysNone;

    // Built from defaultIniText().
    static Config defaults();
    // Parses text on top of cfg (a bindings section replaces the built-in set as soon as it appears).
    // Problems are appended to *warnings (one per line) and never abort the parse.
    static void parse(const std::string& text, Config& cfg, std::string* warnings);
    // Adds the default Enter / keypad Enter = CROSS bindings to files that predate them, M = VIEW_MAP when the file
    // does not mention M, turns the old default I = ACT:0x17 into I = VIEW_STRIP and the old slot actions
    // ACT:0x02..ACT:0x06 (commands 0x403b..0x403f, no native handler) into QUICK1..QUICK5 (called at the end of
    // parse()).
    static void migrate(Config& cfg);
    // Loads path; if it does not exist, writes the default file there and returns defaults.
    static Config loadOrCreate(const std::string& path, std::string* warnings);
    // Appends the controls-v2 marker block to an older ini (see Config.cpp).
    static void upgradeFile(const std::string& path, const std::string& text);
    static const char* defaultIniText();
};

// Name tables (exposed for tests / the live window).
bool keyFromName(const std::string& name, int* dik);
const char* nameOfKey(int dik);
bool padButtonFromName(const std::string& name, uint16_t* mask);

} // namespace drakan_input
