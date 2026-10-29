// Drakan: drakan_input's host input on GLFW, see drakan_input_glfw.h. GLFW's callbacks and queries run on the window
// thread; HostInput::poll runs on the game thread and only reads the snapshot taken by mainThreadTick().
#include "drakan_input_glfw.h"
#include "drakan_glfw_keys.h"

#include <input/FrameInput.h>
#include <input/HostInput.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace drakan_input_glfw {
namespace {
using namespace drakan_input;

class GlfwHostInput;
GlfwHostInput* g_active = nullptr; // window thread callbacks and mainThreadTick act on it

class GlfwHostInput final : public HostInputBackend {
public:
    bool init(void* window, InputAccumulator* acc) override {
        window_ = static_cast<GLFWwindow*>(window);
        acc_ = acc;
        if (!window_ || !acc_)
            return false;
        // Chained: the window layer's callbacks (and the overlay's, installed later, chain to these) still run.
        prevKey_ = glfwSetKeyCallback(window_, onKey);
        prevButton_ = glfwSetMouseButtonCallback(window_, onButton);
        prevCursor_ = glfwSetCursorPosCallback(window_, onCursor);
        prevScroll_ = glfwSetScrollCallback(window_, onScroll);
        g_active = this;
        return true;
    }
    void shutdown() override {
        if (g_active == this)
            g_active = nullptr;
    }

    void poll(FrameInput& in, bool gamepadEnabled, float deadzone) override {
        std::lock_guard<std::mutex> lock(m_);
        in.focused = focused_;
        in.cursorX = cursorX_;
        in.cursorY = cursorY_;
        in.clientW = clientW_;
        in.clientH = clientH_;
        in.gamepad = GamepadState{};
        if (!gamepadEnabled || !padConnected_)
            return;
        GamepadState& g = in.gamepad;
        g.connected = true;
        const auto& b = pad_.buttons;
        const auto m = [&](int button, uint16_t mask) {
            if (b[button] == GLFW_PRESS)
                g.buttons |= mask;
        };
        m(GLFW_GAMEPAD_BUTTON_A, Pad::CROSS);
        m(GLFW_GAMEPAD_BUTTON_B, Pad::CIRCLE);
        m(GLFW_GAMEPAD_BUTTON_X, Pad::SQUARE);
        m(GLFW_GAMEPAD_BUTTON_Y, Pad::TRIANGLE);
        m(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, Pad::L1);
        m(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, Pad::R1);
        m(GLFW_GAMEPAD_BUTTON_BACK, Pad::SELECT);
        m(GLFW_GAMEPAD_BUTTON_START, Pad::START);
        m(GLFW_GAMEPAD_BUTTON_LEFT_THUMB, Pad::L3);
        m(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB, Pad::R3);
        m(GLFW_GAMEPAD_BUTTON_DPAD_UP, Pad::UP);
        m(GLFW_GAMEPAD_BUTTON_DPAD_DOWN, Pad::DOWN);
        m(GLFW_GAMEPAD_BUTTON_DPAD_LEFT, Pad::LEFT);
        m(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, Pad::RIGHT);
        // Triggers run -1 (released) .. 1; the XInput host's threshold is 30 / 255.
        constexpr float kTrigger = -1.0f + 2.0f * 30.0f / 255.0f;
        if (pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > kTrigger)
            g.buttons |= Pad::L2;
        if (pad_.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > kTrigger)
            g.buttons |= Pad::R2;
        // GLFW's +y is down; GamepadState's is up.
        applyRadialDeadzone(pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_X], -pad_.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], deadzone, &g.lx,
                            &g.ly);
        applyRadialDeadzone(pad_.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], -pad_.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], deadzone,
                            &g.rx, &g.ry);
    }

    void setCapture(bool captured) override { wantCapture_.store(captured); }
    bool captured() const override { return wantCapture_.load(); }
    void setMenuCursorHidden(bool hidden) override { wantMenuHidden_.store(hidden); }

    // Window thread.
    void tick() {
        const bool capture = wantCapture_.load(), menuHidden = wantMenuHidden_.load();
        if (capture != appliedCapture_ || menuHidden != appliedMenuHidden_) {
            appliedCapture_ = capture;
            appliedMenuHidden_ = menuHidden;
            if (capture && glfwRawMouseMotionSupported())
                glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            glfwSetInputMode(window_, GLFW_CURSOR,
                             capture      ? GLFW_CURSOR_DISABLED
                             : menuHidden ? GLFW_CURSOR_HIDDEN
                                          : GLFW_CURSOR_NORMAL);
            haveLast_ = false; // the cursor position jumps with the mode: no motion from it
        }
        const bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
        if (focused_ != focused && !focused)
            acc_->releaseAll();
        GLFWgamepadstate pad{};
        bool connected = false;
        for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST && !connected; ++j)
            connected = glfwJoystickIsGamepad(j) && glfwGetGamepadState(j, &pad);
        double cx = 0, cy = 0;
        glfwGetCursorPos(window_, &cx, &cy);
        int w = 0, h = 0;
        glfwGetWindowSize(window_, &w, &h);
        std::lock_guard<std::mutex> lock(m_);
        focused_ = focused;
        padConnected_ = connected;
        pad_ = pad;
        cursorX_ = static_cast<int>(cx);
        cursorY_ = static_cast<int>(cy);
        clientW_ = w;
        clientH_ = h;
    }

private:
    static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
        if (GlfwHostInput* s = g_active) {
            if (const int dik = drakan_keys::dikFromGlfw(key); dik && action != GLFW_REPEAT)
                s->acc_->onKey(dik, action == GLFW_PRESS);
            if (s->prevKey_)
                s->prevKey_(w, key, scancode, action, mods);
        }
    }
    static void onButton(GLFWwindow* w, int button, int action, int mods) {
        if (GlfwHostInput* s = g_active) {
            const uint8_t bit = button == GLFW_MOUSE_BUTTON_LEFT     ? MBTN_LEFT
                                : button == GLFW_MOUSE_BUTTON_RIGHT  ? MBTN_RIGHT
                                : button == GLFW_MOUSE_BUTTON_MIDDLE ? MBTN_MIDDLE
                                : button == GLFW_MOUSE_BUTTON_4      ? MBTN_X1
                                : button == GLFW_MOUSE_BUTTON_5      ? MBTN_X2
                                                                     : 0;
            if (bit)
                s->acc_->onMouseButton(bit, action == GLFW_PRESS);
            if (s->prevButton_)
                s->prevButton_(w, button, action, mods);
        }
    }
    static void onCursor(GLFWwindow* w, double x, double y) {
        if (GlfwHostInput* s = g_active) {
            if (s->haveLast_) {
                // Whole counts; the fraction carries over (raw motion is integral, scaled desktops are not).
                s->restX_ += x - s->lastX_;
                s->restY_ += y - s->lastY_;
                const int dx = static_cast<int>(s->restX_), dy = static_cast<int>(s->restY_);
                s->restX_ -= dx;
                s->restY_ -= dy;
                if (dx || dy)
                    s->acc_->onMouseMove(dx, dy);
            }
            s->lastX_ = x;
            s->lastY_ = y;
            s->haveLast_ = true;
            if (s->prevCursor_)
                s->prevCursor_(w, x, y);
        }
    }
    static void onScroll(GLFWwindow* w, double dx, double dy) {
        if (GlfwHostInput* s = g_active) {
            s->restWheel_ += dy;
            const int notches = static_cast<int>(s->restWheel_);
            s->restWheel_ -= notches;
            if (notches)
                s->acc_->onWheel(notches);
            if (s->prevScroll_)
                s->prevScroll_(w, dx, dy);
        }
    }

    GLFWwindow* window_ = nullptr;
    InputAccumulator* acc_ = nullptr;
    GLFWkeyfun prevKey_ = nullptr;
    GLFWmousebuttonfun prevButton_ = nullptr;
    GLFWcursorposfun prevCursor_ = nullptr;
    GLFWscrollfun prevScroll_ = nullptr;
    std::atomic<bool> wantCapture_{false}, wantMenuHidden_{false};
    bool appliedCapture_ = false, appliedMenuHidden_ = false;
    // window thread only
    bool haveLast_ = false;
    double lastX_ = 0, lastY_ = 0, restX_ = 0, restY_ = 0, restWheel_ = 0;
    // snapshot for poll()
    mutable std::mutex m_;
    bool focused_ = false, padConnected_ = false;
    GLFWgamepadstate pad_{};
    int cursorX_ = 0, cursorY_ = 0, clientW_ = 0, clientH_ = 0;
};

std::unique_ptr<HostInputBackend> create() {
    return std::make_unique<GlfwHostInput>();
}
} // namespace

void install() {
    setHostInputBackendFactory(&create);
}

void mainThreadTick() {
    if (g_active)
        g_active->tick();
}
} // namespace drakan_input_glfw
