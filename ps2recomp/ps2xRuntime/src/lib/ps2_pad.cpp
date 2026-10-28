#include "drakan_pad_seq.h"
#include "runtime/ps2_pad.h"
#include "ps2_host_pad.h"
#if !defined(_WIN32)
#include "input/FrameInput.h"
#include "portable_host_keys.h"
#endif
#include "drakan_controlled_actor.h"
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include "host_mouse_capture.h"
#include <atomic>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#endif
#if defined(__EMSCRIPTEN__)
#include "drakan_web_input.h"
#include <emscripten/html5.h>
#endif
#include "ps2_host_backend.h"
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <array>

namespace {
constexpr uint8_t kPadAnalogMarker = 0x73;
constexpr uint8_t kPadStickCenter = 0x80;

constexpr uint16_t PAD_LEFT = 0x0080u;
constexpr uint16_t PAD_DOWN = 0x0040u;
constexpr uint16_t PAD_RIGHT = 0x0020u;
constexpr uint16_t PAD_UP = 0x0010u;
constexpr uint16_t PAD_START = 0x0008u;
constexpr uint16_t PAD_R3 = 0x0004u;
constexpr uint16_t PAD_L3 = 0x0002u;
constexpr uint16_t PAD_SELECT = 0x0001u;
constexpr uint16_t PAD_SQUARE = 0x8000u;
constexpr uint16_t PAD_CROSS = 0x4000u;
constexpr uint16_t PAD_CIRCLE = 0x2000u;
constexpr uint16_t PAD_TRIANGLE = 0x1000u;
constexpr uint16_t PAD_R1 = 0x0800u;
constexpr uint16_t PAD_L1 = 0x0400u;
constexpr uint16_t PAD_R2 = 0x0200u;
constexpr uint16_t PAD_L2 = 0x0100u;
} // namespace

namespace {
HostPadState g_hostPad;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
std::atomic<bool> g_hostGameplay{false};
HostMouseCapture g_hostCapture;
std::atomic<unsigned> g_nativeMousePresses{0};
GLFWwindow* g_hostMouseWindow = nullptr;
GLFWmousebuttonfun g_previousMouseButton = nullptr;
void hostMouseButton(GLFWwindow* window, int button, int action, int modifiers) {
    if (window == g_hostMouseWindow && static_cast<unsigned>(button) < 5 && action == GLFW_PRESS)
        g_nativeMousePresses.fetch_or(1u << button, std::memory_order_relaxed);
    if (g_previousMouseButton)
        g_previousMouseButton(window, button, action, modifiers);
}
#endif
thread_local const uint8_t* g_hostPadMemory = nullptr;
uint32_t hostPadContext() {
    if (!g_hostPadMemory)
        return 0;
    uint32_t screen = 0;
    std::memcpy(&screen, g_hostPadMemory + 0x4fb60c, 4);
    return screen >= 0x100000u && screen < 32u * 1024u * 1024u - 0x100u ? screen : 0;
}
} // namespace

void ps2SetHostPadMemory(const uint8_t* memory) {
    // Called by scePadRead on the guest executor, where GUI memory is stable.
    g_hostPadMemory = memory;
}

bool ps2HostMouseLookActive() {
    return g_hostPad.mouseLookActive();
}

HostMouseFrame ps2HostMouseFrame() {
    return g_hostPad.mouseFrame();
}

void ps2BeginHostPadFrame(const uint8_t* memory, bool directMouse, bool captureMouse) {
    (void)captureMouse; // Native Linux consumes this in its window-thread capture policy.
    g_hostPadMemory = memory;
    const uint32_t context = hostPadContext();
#if defined(__EMSCRIPTEN__)
    drakanWebSetMenuActive(context != 0);
#endif
    struct MemoryView {
        const uint8_t* memory;
        uint32_t read32(uint32_t address) {
            uint32_t value;
            std::memcpy(&value, memory + (address & 0x1ffffffu), 4);
            return value;
        }
        uint16_t read16(uint32_t address) {
            uint16_t value;
            std::memcpy(&value, memory + (address & 0x1ffffffu), 2);
            return value;
        }
    } guest{memory};
    const auto actor = drakan_controlled_actor::read(guest);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    g_hostGameplay.store(captureMouse && context == 0 && guest.read32(0x4fb730u) != 0 && guest.read32(0x4fbafcu) == 0,
                         std::memory_order_release);
#endif
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const float seconds =
        last.time_since_epoch().count() ? std::chrono::duration<float>(now - last).count() : 1.0f / 30.0f;
    last = now;
    g_hostPad.latchFrame(context, directMouse && !actor.dragon, seconds);
}

void ps2PollHostPad() {
#if defined(__EMSCRIPTEN__)
    int width = 0, height = 0;
    if (drakanWebTakeResize(width, height)) {
        if (GetScreenWidth() != width || GetScreenHeight() != height)
            SetWindowSize(width, height);
        // GLFW changes the canvas attributes directly. The SDK API also resizes
        // the proxied GL color/depth back buffer, which otherwise crops HiDPI.
        emscripten_set_canvas_element_size("#canvas", width, height);
    }
#endif
    std::array<uint8_t, 32> sample{};
    uint8_t* data = sample.data();
    data[0] = 1;
    data[1] = kPadAnalogMarker;
    for (int i = 4; i < 8; ++i)
        data[i] = kPadStickCenter;

#if !defined(__EMSCRIPTEN__)
    unsigned nativeMousePresses = 0;
#if !defined(_WIN32)
    if (GLFWwindow* window = glfwGetCurrentContext(); window && window != g_hostMouseWindow) {
        g_hostMouseWindow = window;
        g_previousMouseButton = glfwSetMouseButtonCallback(window, &hostMouseButton);
    }
    // Raylib's final button level loses down/up pairs drained in one poll.
    // Retain the press while chaining its callback to preserve normal state.
    nativeMousePresses = g_nativeMousePresses.exchange(0, std::memory_order_acq_rel);
#endif
    std::array<bool, 512> tappedKeys{};
    for (int key = GetKeyPressed(); key > 0; key = GetKeyPressed())
        if (static_cast<unsigned>(key) < tappedKeys.size())
            tappedKeys[key] = true;
#endif
    bool keyboardActivity = false, mouseButtonActivity = false;
    std::array<int8_t, 512> keySamples;
    keySamples.fill(-1);
    const auto keyActive = [&](int key) {
        if (keySamples[key] >= 0)
            return keySamples[key] != 0;
#if defined(__EMSCRIPTEN__)
        const bool tapped = drakanWebTakeKeyPress(key);
        const bool active = drakanWebKeyDown(key) || tapped;
#else
        const bool active = IsKeyDown(key) || IsKeyPressed(key) || tappedKeys[key];
#endif
        keyboardActivity |= active;
        keySamples[key] = int8_t(active);
        return active;
    };
    std::array<int8_t, 5> mouseSamples;
    mouseSamples.fill(-1);
    const auto mouseActive = [&](int button) {
        if (mouseSamples[button] >= 0)
            return mouseSamples[button] != 0;
#if defined(__EMSCRIPTEN__)
        const bool tapped = drakanWebTakeMousePress(button);
        const bool active = drakanWebMouseDown(button) || tapped;
#else
        const bool active =
            IsMouseButtonDown(button) || IsMouseButtonPressed(button) || (nativeMousePresses & (1u << button)) != 0;
#endif
        mouseButtonActivity |= active;
        mouseSamples[button] = int8_t(active);
        return active;
    };
    uint16_t btns = 0xFFFFu;
    constexpr int kGamepad = 0;
    const bool useGamepad = IsGamepadAvailable(kGamepad);
    auto clearBit = [&btns](uint16_t mask) { btns &= ~mask; };

    if (useGamepad) {
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_UP))
            clearBit(PAD_UP);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
            clearBit(PAD_DOWN);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT))
            clearBit(PAD_LEFT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
            clearBit(PAD_RIGHT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            clearBit(PAD_CROSS);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))
            clearBit(PAD_CIRCLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
            clearBit(PAD_SQUARE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_UP))
            clearBit(PAD_TRIANGLE);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1))
            clearBit(PAD_L1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))
            clearBit(PAD_R1);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_2))
            clearBit(PAD_L2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))
            clearBit(PAD_R2);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT))
            clearBit(PAD_START);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_MIDDLE_LEFT))
            clearBit(PAD_SELECT);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_THUMB))
            clearBit(PAD_L3);
        if (IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_RIGHT_THUMB))
            clearBit(PAD_R3);

        float lx = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_X);
        float ly = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_Y);
        float rx = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_X);
        float ry = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_Y);
        const auto deadzone = [](float& x, float& y) {
            const float magnitude = std::hypot(x, y);
            if (magnitude <= 0.18f) {
                x = y = 0;
                return;
            }
            const float scale = std::min(1.0f, (magnitude - 0.18f) / 0.82f) / magnitude;
            x *= scale;
            y *= scale;
        };
        deadzone(lx, ly);
        deadzone(rx, ry);
        data[6] = static_cast<uint8_t>(128 + lx * 127);
        data[7] = static_cast<uint8_t>(128 + ly * 127);
        data[4] = static_cast<uint8_t>(128 + rx * 127);
        data[5] = static_cast<uint8_t>(128 + ry * 127);
    }
#if !defined(_WIN32)
    drakan_input::FrameInput hostInput;
    hostInput.gamepad.connected = useGamepad;
    hostInput.gamepad.buttons = uint16_t((uint16_t(~btns) << 8) | (uint16_t(~btns) >> 8));
    const auto axis = [](uint8_t value) { return std::clamp((float(value) - 128.f) / 127.f, -1.f, 1.f); };
    hostInput.gamepad.rx = axis(data[4]);
    hostInput.gamepad.ry = -axis(data[5]);
    hostInput.gamepad.lx = axis(data[6]);
    hostInput.gamepad.ly = -axis(data[7]);
#endif
    const bool controllerActivity =
        useGamepad && (btns != 0xffffu || data[4] != 128 || data[5] != 128 || data[6] != 128 || data[7] != 128);
    {
        if (keyActive(KEY_UP))
            clearBit(PAD_UP);
        if (keyActive(KEY_DOWN))
            clearBit(PAD_DOWN);
        if (keyActive(KEY_LEFT))
            clearBit(PAD_LEFT);
        if (keyActive(KEY_RIGHT))
            clearBit(PAD_RIGHT);
        if (keyActive(KEY_X))
            clearBit(PAD_CROSS);
        if (keyActive(KEY_C))
            clearBit(PAD_CIRCLE);
        if (keyActive(KEY_Z) || keyActive(KEY_KP_0))
            clearBit(PAD_SQUARE);
        if (keyActive(KEY_V) || keyActive(KEY_KP_1))
            clearBit(PAD_TRIANGLE);
        if (keyActive(KEY_RIGHT_SHIFT))
            clearBit(PAD_R2);
        if (keyActive(KEY_ENTER))
            clearBit(PAD_START);
        if (keyActive(KEY_ESCAPE) || keyActive(KEY_BACKSPACE)) {
            clearBit(PAD_START);
            clearBit(PAD_CIRCLE);
        }
        if (keyActive(KEY_TAB))
            clearBit(PAD_SELECT);
        if (keyActive(KEY_LEFT_CONTROL))
            clearBit(PAD_L3);
        if (keyActive(KEY_RIGHT_CONTROL))
            clearBit(PAD_R3);
    }

    // Keyboard and mouse merge with a controller. WASD drives the analog
    // movement stick; the arrow keys remain native menu/d-pad navigation.
    const float x = float(keyActive(KEY_D)) - float(keyActive(KEY_A));
    const float y = float(keyActive(KEY_S)) - float(keyActive(KEY_W));
    const float length = std::sqrt(x * x + y * y);
    if (length > 0.0f) {
        const float gain = keyActive(KEY_LEFT_SHIFT) ? 0.5f : 1.0f;
        data[6] = static_cast<uint8_t>(128.0f + 127.0f * gain * x / length);
        data[7] = static_cast<uint8_t>(128.0f + 127.0f * gain * y / length);
    }
    const bool leftMouse = mouseActive(MOUSE_BUTTON_LEFT);
    if (mouseActive(MOUSE_BUTTON_RIGHT))
        clearBit(PAD_CIRCLE);
    if (mouseActive(MOUSE_BUTTON_MIDDLE))
        clearBit(PAD_R2);
    if (keyActive(KEY_F))
        clearBit(PAD_CROSS);
    if (keyActive(KEY_SPACE))
        clearBit(PAD_TRIANGLE);
    if (keyActive(KEY_E))
        clearBit(PAD_L1);
    if (keyActive(KEY_Q))
        clearBit(PAD_R1);
    if (keyActive(KEY_R))
        clearBit(PAD_L2);
#if defined(__EMSCRIPTEN__)
    const WebMouseMotion motion = drakanWebTakeMouseMotion();
    const Vector2 mouse{static_cast<float>(motion.x), static_cast<float>(motion.y)};
    const bool captured = motion.captured;
    const bool toggleCapture = drakanWebTakeKeyPress(KEY_F10);
#else
    Vector2 mouse = GetMouseDelta();
    bool captured = IsCursorHidden();
    const bool toggleCapture = IsKeyPressed(KEY_F10) || tappedKeys[KEY_F10];
#endif
    bool suppressLeft = false;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const auto capture =
        g_hostCapture.update(IsWindowFocused(), g_hostGameplay.load(std::memory_order_acquire), captured, toggleCapture,
                             IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || (nativeMousePresses & 1u) != 0,
                             IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    suppressLeft = capture.suppressLeft;
    if (capture.capture != captured) {
        if (capture.capture)
            DisableCursor();
        else
            EnableCursor();
        captured = capture.capture;
        mouse = {}; // Discard the cursor-warp delta at a capture transition.
    }
#else
    if (toggleCapture) {
        if (captured)
            EnableCursor();
        else
            DisableCursor();
    }
#endif
    if (leftMouse && !suppressLeft)
        clearBit(PAD_SQUARE);
    data[2] = static_cast<uint8_t>(btns & 0xff);
    data[3] = static_cast<uint8_t>(btns >> 8);
#if defined(__EMSCRIPTEN__)
    const bool focused = drakanWebFocused();
#else
    const bool focused = IsWindowFocused();
#endif
#if !defined(_WIN32)
    for (const auto& [key, dik] : kPortableHostKeys)
        hostInput.keyDown.set(unsigned(dik), keyActive(key));
    for (int button = 0; button < 5; ++button)
        if (mouseActive(button) && !(button == 0 && suppressLeft))
            hostInput.mouseDown |= uint8_t(1u << button);
    hostInput.focused = focused;
    hostInput.captured = captured;
    hostInput.clientW = GetScreenWidth();
    hostInput.clientH = GetScreenHeight();
    const auto cursor = GetMousePosition();
    hostInput.cursorX = int(cursor.x);
    hostInput.cursorY = int(cursor.y);
    hostInput.wheel = int(std::lround(GetMouseWheelMove()));
    drakanPortablePublishInput(hostInput);
#endif
    g_hostPad.publish(sample, mouse.x, mouse.y, captured,
                      keyboardActivity || mouseButtonActivity || (captured && (mouse.x != 0 || mouse.y != 0)),
                      controllerActivity, focused);
}

bool PSPadBackend::readState(int port, int slot, uint8_t* data, size_t size) {
    if (!data || size < 32)
        return false;
#if !defined(_WIN32)
    const bool portable = drakanPortablePadSample(port, slot, data);
#else
    const bool portable = false;
#endif
    if (!portable) {
        const auto sample = g_hostPad.readFrame(hostPadContext(), port, slot);
        std::memcpy(data, sample.data(), sample.size());
    }
    uint16_t btns = uint16_t(data[2]) | (uint16_t(data[3]) << 8);
    const auto clearBit = [&btns](uint16_t mask) { btns &= ~mask; };

    {
        // Drakan test aid: DRAKAN_AUTOPRESS=<period ms> taps Cross (and Start if DRAKAN_AUTOSTART is set) for 150 ms
        // every period.
        static const long periodMs = []() {
            const char* e = std::getenv("DRAKAN_AUTOPRESS");
            return e ? std::atol(e) : 0L;
        }();
        static const bool withStart = std::getenv("DRAKAN_AUTOSTART") != nullptr;
        if (periodMs > 0) {
            static const auto t0 = std::chrono::steady_clock::now();
            const long t = static_cast<long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
            // DRAKAN_AUTOPRESS_UNTIL=<ms since start>: stop tapping after that time (to rest on a menu for a
            // screenshot)
            static const long untilMs = []() {
                const char* e = std::getenv("DRAKAN_AUTOPRESS_UNTIL");
                return e ? std::atol(e) : 0L;
            }();
            if (t > 3000 && (untilMs <= 0 || t < untilMs) && (t % periodMs) < 150) {
                clearBit(PAD_CROSS);
                if (withStart)
                    clearBit(PAD_START);
            }
        }
    }
    btns = static_cast<uint16_t>(btns & ~drakan_ui_aid::padSeqMask()); // DRAKAN_PADSEQ test aid
    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
