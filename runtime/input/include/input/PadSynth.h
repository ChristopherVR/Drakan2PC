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
};

// directLook: mouse look is applied by the camera hook, so the mouse contributes nothing to the right stick.
// The jitter filter of PadMgr_ProcessStickAxis blanks a stick whose byte is within +-47 of centre and changed by
// <4 counts since the previous sample; the synthesizer dithers such values by 4 counts so they survive.
PadSample synthesizePad(const Config& cfg, const FrameInput& in, const std::vector<Binding>& layer, bool menuActive,
                        bool directLook, bool walk, PadSynthState& st);

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
