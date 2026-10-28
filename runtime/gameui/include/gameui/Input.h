// Per-frame UI input, device neutral. The glue maps keyboard, mouse and gamepad onto it (recommended mapping in
// README.md); the screen never sees raw keys, so the browser build feeds the same struct from DOM / Gamepad API events.
#pragma once
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gameui {

enum class UiKey : uint8_t {
    Up,
    Down,
    Left,
    Right,
    Confirm,    // primary action: equip / use / select / spend
    Back,       // close the screen
    Secondary,  // drop / summon / return a point
    Tertiary,   // hot slot / sell
    PagePrev,   // previous page (LB / Q)
    PageNext,   // next page (RB / E)
    FilterPrev, // previous category filter (LT / Z)
    FilterNext, // next category filter (RT / C)
    Close,      // Tab / I / Select: close immediately
};

enum class InputDevice : uint8_t { KeyboardMouse, Gamepad };

struct UiInput {
    std::vector<UiKey> keys;                         // presses this frame (auto-repeat already applied), in order
    InputDevice device = InputDevice::KeyboardMouse; // last used: picks the prompt glyphs and hover behaviour
    bool pointerValid = false;                       // pointer inside the viewport
    float pointerX = 0, pointerY = 0;                // back-buffer pixels
    bool pointerMoved = false;
    bool leftClick = false;   // press this frame
    bool doubleClick = false; // second press of a double click (leftClick is set too)
    bool rightClick = false;
    bool leftDown = false; // left button held this frame (drags: the pause menu's sliders); false = released
    float wheel = 0;       // notches, + = up

    bool has(UiKey k) const {
        for (UiKey x : keys)
            if (x == k)
                return true;
        return false;
    }
};

// Press rules for a screen that opens on a key press, so the key that opened it (Tab, I, Select ...) cannot also close
// it. reset() when the screen opens; then per frame and key / button, accept(key, down, pressed) with the host's
// physical state (`down`) and its press edge(s) this frame (`pressed`, auto-repeat included if the host reports it):
//  * a key first seen down after reset() is held over from before the screen: it is not armed, and arms only once it
//    has been seen released; its presses (late key-down messages, auto-repeat) never count meanwhile;
//  * an armed key's presses count.
// Keys are host ids in [0, kKeys).
class KeyGate {
public:
    static constexpr int kKeys = 1024;
    void reset() {
        seen_.reset();
        armed_.reset();
    }
    bool accept(int key, bool down, bool pressed) {
        if (key < 0 || key >= kKeys)
            return pressed;
        const size_t k = static_cast<size_t>(key);
        if (!seen_[k]) {
            seen_[k] = true;
            armed_[k] = !down;
        }
        if (!armed_[k]) {
            if (!down)
                armed_[k] = true; // released: the next press counts
            return false;
        }
        return pressed;
    }
    bool armed(int key) const {
        return key >= 0 && key < kKeys && seen_[static_cast<size_t>(key)] && armed_[static_cast<size_t>(key)];
    }

private:
    std::bitset<kKeys> seen_, armed_;
};

} // namespace gameui
