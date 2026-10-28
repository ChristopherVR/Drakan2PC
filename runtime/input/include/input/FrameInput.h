// Host-side input snapshot (platform independent) and the thread-safe accumulator that produces it.
#pragma once
#include <bitset>
#include <cstdint>
#include <deque>
#include <mutex>

namespace drakan_input {

// DualShock button mask, active HIGH (the sample on the wire is the complement).
namespace Pad {
enum : uint16_t {
    L2 = 0x1,
    R2 = 0x2,
    L1 = 0x4,
    R1 = 0x8,
    TRIANGLE = 0x10,
    CIRCLE = 0x20,
    CROSS = 0x40,
    SQUARE = 0x80,
    SELECT = 0x100,
    L3 = 0x200,
    R3 = 0x400,
    START = 0x800,
    UP = 0x1000,
    RIGHT = 0x2000,
    DOWN = 0x4000,
    LEFT = 0x8000
};
}

enum MouseBtn : uint8_t { MBTN_LEFT = 1, MBTN_RIGHT = 2, MBTN_MIDDLE = 4, MBTN_X1 = 8, MBTN_X2 = 16 };

struct GamepadState {
    bool connected = false;
    uint16_t buttons = 0;                 // DualShock mask (active high), already translated from XInput
    float lx = 0, ly = 0, rx = 0, ry = 0; // -1..1, +y = up (deadzone already applied)
};

struct FrameInput {
    std::bitset<256> keyDown, keyPressed, keyReleased;          // index = DirectInput scancode (E0 keys = 0x80|code)
    uint8_t mouseDown = 0, mousePressed = 0, mouseReleased = 0; // MouseBtn bits
    int wheel = 0;                                              // notches this frame, + = away from the user
    int dx = 0, dy = 0;                // raw relative counts since the previous frame (+x right, +y down)
    float frameSeconds = 1.0f / 30.0f; // host time the counts cover (the original 30 Hz tick by default)
    int cursorX = 0, cursorY = 0, clientW = 0, clientH = 0; // OS cursor in window client coords
    bool focused = true;
    bool captured = false;
    GamepadState gamepad;
};

// Written from the window / raw-input thread, drained once per game frame.
class InputAccumulator {
public:
    void onKey(int dik, bool down);
    void onMouseButton(uint8_t mbBit, bool down);
    void onMouseMove(int dx, int dy); // stamped with the steady clock on arrival
    void onMouseMove(int dx, int dy, int64_t tUs);
    void onWheel(int notches);
    void releaseAll(); // focus lost / ownership change: synthesises releases; keys still held count after release
    // Fills `out`, advancing at most one queued transition per source; clears
    // the delivered edges and motion/wheel deltas, retaining later transitions.
    void consume(FrameInput& out);
    // Same, but motion stamped after cutoffUs (steady clock, microseconds) stays queued for the next frame.
    void consume(FrameInput& out, int64_t cutoffUs);
    static int64_t nowUs();

private:
    std::mutex m_;
    std::bitset<256> down_, pressed_, released_;
    uint8_t mDown_ = 0, mPressed_ = 0, mReleased_ = 0;
    // Physical levels above deduplicate Raw Input repeats. Delivered levels below
    // advance at most one transition per source per frame: every click gets a
    // down sample and a distinct up sample, even across a slow game frame.
    std::bitset<256> deliveredKeys_;
    // Keys physically down when releaseAll ran (focus loss, the overlay or the host screen took the input): their
    // repeats are ignored until the key is released, so a held key (Tab that opened and closed the modern screen,
    // a combat key held through F1) does not come back as a fresh press.
    std::bitset<256> heldOver_;
    uint8_t deliveredMouse_ = 0;
    std::deque<bool> keyEdges_[256], mouseEdges_[5];
    struct Motion {
        int64_t tUs;
        int dx, dy;
    };
    std::deque<Motion> motion_;
    int wheel_ = 0;
};

} // namespace drakan_input
