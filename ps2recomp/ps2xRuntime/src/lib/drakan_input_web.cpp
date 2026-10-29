// Drakan (browser): drakan_input's host input, see drakan_input_web.h.
#include "drakan_input_web.h"
#include "drakan_glfw_keys.h"
#include "drakan_web_events.h"
#include "drakan_web_input.h"

#include <input/FrameInput.h>
#include <input/HostInput.h>

#include <atomic>
#include <memory>

namespace drakan_input_web {
namespace {
using namespace drakan_input;

uint8_t buttonBit(int button) {
    switch (button) {
        case 0:
            return MBTN_LEFT;
        case 1:
            return MBTN_RIGHT;
        case 2:
            return MBTN_MIDDLE;
        case 3:
            return MBTN_X1;
        case 4:
            return MBTN_X2;
        default:
            return 0;
    }
}

class WebHostInput;
std::atomic<WebHostInput*> g_active{nullptr};

class WebHostInput final : public HostInputBackend {
public:
    bool init(void*, InputAccumulator* acc) override {
        acc_ = acc;
        if (!acc_)
            return false;
        webEventsEnable(WebEventConsumer::Input);
        g_active.store(this);
        return true;
    }
    void shutdown() override {
        WebHostInput* self = this;
        g_active.compare_exchange_strong(self, nullptr);
    }

    void poll(FrameInput& in, bool gamepadEnabled, float deadzone) override {
        in.focused = drakanWebFocused();
        drakanWebCursor(in.cursorX, in.cursorY);
        drakanWebSurfaceSize(in.clientW, in.clientH);
        in.captured = drakanWebPointerLocked();
        in.gamepad = GamepadState{};
        const WebGamepad pad = drakanWebGamepad();
        if (!gamepadEnabled || !pad.connected)
            return;
        GamepadState& g = in.gamepad;
        g.connected = true;
        static constexpr uint16_t kMap[16] = {Pad::CROSS, Pad::CIRCLE, Pad::SQUARE, Pad::TRIANGLE, Pad::L1, Pad::R1,
                                              Pad::L2,    Pad::R2,     Pad::SELECT, Pad::START,    Pad::L3, Pad::R3,
                                              Pad::UP,    Pad::DOWN,   Pad::LEFT,   Pad::RIGHT};
        for (int i = 0; i < 16; ++i)
            if (pad.buttons & (1u << i))
                g.buttons |= kMap[i];
        // The Gamepad API's +y is down; GamepadState's is up.
        applyRadialDeadzone(pad.axes[0], -pad.axes[1], deadzone, &g.lx, &g.ly);
        applyRadialDeadzone(pad.axes[2], -pad.axes[3], deadzone, &g.rx, &g.ry);
    }

    // Gameplay captures the mouse (pointer lock, requested by player.js on a click); menus release it.
    void setCapture(bool captured) override {
        captured_.store(captured);
        drakanWebSetMenuActive(!captured);
    }
    bool captured() const override { return captured_.load(); }
    void setMenuCursorHidden(bool hidden) override {
        if (hidden != menuHidden_.exchange(hidden))
            drakanWebSetCursorHidden(hidden);
    }

    void pump() {
        WebEvent e;
        while (webEvents(WebEventConsumer::Input).pop(e)) {
            switch (e.type) {
                case WebEvent::Key:
                    if (const int dik = drakan_keys::dikFromGlfw(e.a))
                        acc_->onKey(dik, e.b != 0);
                    break;
                case WebEvent::Button:
                    if (const uint8_t bit = buttonBit(e.a))
                        acc_->onMouseButton(bit, e.b != 0);
                    break;
                case WebEvent::Motion:
                    acc_->onMouseMove(e.a, e.b);
                    break;
                case WebEvent::Wheel:
                    acc_->onWheel(e.a);
                    break;
                case WebEvent::Focus:
                    if (!e.a)
                        acc_->releaseAll();
                    break;
                default:
                    break;
            }
        }
    }

private:
    InputAccumulator* acc_ = nullptr;
    std::atomic<bool> captured_{false}, menuHidden_{false};
};

std::unique_ptr<HostInputBackend> create() {
    return std::make_unique<WebHostInput>();
}
} // namespace

void install() {
    setHostInputBackendFactory(&create);
}

void pumpTick() {
    if (WebHostInput* host = g_active.load())
        host->pump();
}
} // namespace drakan_input_web
