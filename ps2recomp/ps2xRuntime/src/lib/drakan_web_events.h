// Drakan (browser): DOM input as an ordered event stream for the shared input module and the settings overlay.
// player.js calls the drakan_web_* exports on the browser main thread, which must never block, so every event goes into
// lock-free single-producer / single-consumer rings, one per consumer, drained on the consumers' own threads. Key codes
// are GLFW's (player.js keyCodes), mouse buttons 0 left, 1 right, 2 middle, 3 back, 4 forward.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

struct WebEvent {
    enum Type : uint8_t { Key, Button, Cursor, Motion, Wheel, Char, Focus } type = Key;
    int a = 0, b = 0; // Key: key, down. Button: button, down. Cursor: x, y (canvas pixels). Motion: dx, dy (pointer
                      // lock). Wheel: notches (+ away from the user). Char: code point. Focus: focused.
};

class WebEventRing {
public:
    bool push(const WebEvent& e) {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= kSize)
            return false; // full: the consumer is not draining (dropped)
        events_[h % kSize] = e;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(WebEvent& e) {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false;
        e = events_[t % kSize];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 4096;
    std::array<WebEvent, kSize> events_{};
    std::atomic<uint32_t> head_{0}, tail_{0};
};

enum class WebEventConsumer { Input, Overlay };
WebEventRing& webEvents(WebEventConsumer consumer);
// Only consumers that registered receive events (an unread ring would fill up and drop).
void webEventsEnable(WebEventConsumer consumer);

// The first standard-mapping gamepad (navigator.getGamepads, polled by player.js on the main thread).
struct WebGamepad {
    bool connected = false;
    uint32_t buttons = 0; // bit i = standard button i (0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT, 8 Back, 9 Start,
                          // 10 LS, 11 RS, 12 up, 13 down, 14 left, 15 right)
    float axes[4] = {};   // lx, ly, rx, ry (-1..1, +y down)
};
WebGamepad drakanWebGamepad();
void drakanWebCursor(int& x, int& y); // last cursor position over the canvas (canvas pixels)
bool drakanWebPointerLocked();
void drakanWebRequestFullscreen(bool on);
void drakanWebSetCursorHidden(bool hidden); // CSS cursor over the canvas
