// Pure logic (no Win32, no guest): host input snapshot -> 32-byte scePadRead sample, and direct command events.
#pragma once
#include "input/Config.h"
#include "input/FrameInput.h"
#include <cstdint>
#include <vector>

namespace drakan_input {

struct PadSample {
    uint16_t buttons = 0; // active high (Pad::*)
    uint8_t rx = 0x80, ry = 0x80, lx = 0x80, ly = 0x80;
};

// The game maps stick byte b -> v=b-128 -> f=v/128 -> out = sign(f)*f^2 (then x sensitivity scale).
float gameStickResponse(uint8_t byte);
// Inverse: byte whose game response is `linear` (-1..1): v = 128*sqrt(|linear|). Right/down = positive.
uint8_t encodeStickLinear(float linear);
// Plain (real DualShock feel) encoding used for XInput sticks: byte = 128 + f*127.
uint8_t encodeStickRaw(float f);

struct PadSynthState {
    uint8_t last[4] = {0x80, 0x80, 0x80, 0x80}; // rx, ry, lx, ly as last emitted (for the game's jitter filter)
    bool flip[4] = {false, false, false, false};
    uint16_t pendingAttackDirections = 0; // replay a short melee attack after one neutral preparation sample
    bool boundAttackDown = false;         // keyboard/mouse Square only, before physical controller merge
    // [keyboard] directional_attack: decided when the bound attack is pressed and kept until it is released
    bool directionalLatched = false;
    uint16_t lastDirections = 0; // projected keyboard movement directions of the previous sample
    float directionAge = 1.0e9f; // seconds since a movement direction last turned on (Tap mode)
    // [gamepad] directional_attack: the same decision for the controller's own attack button and left stick
    uint16_t padPendingDirections = 0;
    bool padAttackDown = false;
    bool padLatched = false;
    uint16_t padLastDirections = 0;
    float padDirectionAge = 1.0e9f;
};

// The live controller scheme's buttons for the commands the pad synthesis rewrites (scePad masks, 0 = none: on Arokh,
// or no button in the scheme). Read from the game's keymap each update (nativePadButton).
struct PadSchemeButtons {
    uint16_t attack = 0; // Attack 0x4006 on foot
    uint16_t crouch = 0; // Crouch 0x4012 on foot
};

// Left stick deflection beyond which a direction counts for a controller directional attack (per axis).
constexpr float kPadDirectionThreshold = 0.5f;
// D-pad bits for a left stick vector (+y = up), 8-way by kPadDirectionThreshold per axis.
uint16_t padStickDirections(float lx, float ly);

// allowDirectionalAttack: the native actor may perform a directional melee move (drawn melee weapon, on foot). Whether
// held keyboard movement then turns a new keyboard / mouse attack into one follows cfg.directionalAttack: a plain
// attack never centers the keyboard left stick or adds D-pad buttons, so walking and strafing keep working.
// directLook: mouse look is applied by the camera hook, so the mouse contributes nothing to the right stick.
// The jitter filter of PadMgr_ProcessStickAxis blanks a stick whose byte is within +-47 of centre and changed by
// <4 counts since the previous sample; the synthesizer dithers such values by 4 counts so they survive.
// scheme: the live scheme's Attack / Crouch buttons. With an attack button, a controller attack follows
// cfg.padDirectionalAttack like the keyboard one (left stick for the movement keys, cfg.padDirectionalModifier for
// DIRECTIONAL_ATTACK); with a crouch button, L3 also presses it (cfg.padStickCrouch). Both only outside menus.
PadSample synthesizePad(const Config& cfg, const FrameInput& in, const std::vector<Binding>& layer, bool menuActive,
                        bool directLook, bool walk, PadSynthState& st, bool allowDirectionalAttack = false,
                        PadSchemeButtons scheme = {});

// Sony layout: {0x00,0x73, ~hi, ~lo, rx, ry, lx, ly, pressure bytes...}
void packPadSample(const PadSample& s, uint8_t out[32]);
PadSample unpackPadSample(const uint8_t in[32]); // tests

struct CommandEvent {
    bool isAction; // true: id is an action id (DIK / 0x100..); false: id is a command id
    uint32_t id;
    bool pressed;
};

// Edge generator for ACT:/CMD: bindings. Releases are always generated (the game uses them to clear hold flags).
class CommandTracker {
public:
    void update(const std::vector<Binding>& layer, const FrameInput& in, std::vector<CommandEvent>& out);
    void reset(std::vector<CommandEvent>* releases); // call when the active layer changes
private:
    std::vector<bool> prev_;
    std::vector<CommandEvent> held_;
};

// Level of a binding source this frame (tap-safe: includes a press that was released within the frame).
bool sourceLevel(const Binding& b, const FrameInput& in, bool tapSafe);

} // namespace drakan_input
