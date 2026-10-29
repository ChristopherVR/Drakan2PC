// SettingsOverlay's event-fed window platform (the browser): ImGui input from the host's OverlayEventSource, F1 and the
// gamepad combo. Everything runs on the present thread; the host's queue is filled elsewhere.
#include "OverlayImpl.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace dsettings {

namespace {

constexpr int kGlfwF1 = 290;

// GLFW key code -> ImGuiKey (the keys the menus and text fields use).
ImGuiKey imguiKey(int key) {
    if (key >= 'A' && key <= 'Z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'A'));
    if (key >= '0' && key <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));
    if (key >= kGlfwF1 && key < kGlfwF1 + 12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - kGlfwF1));
    switch (key) {
        case 32:
            return ImGuiKey_Space;
        case 39:
            return ImGuiKey_Apostrophe;
        case 44:
            return ImGuiKey_Comma;
        case 45:
            return ImGuiKey_Minus;
        case 46:
            return ImGuiKey_Period;
        case 47:
            return ImGuiKey_Slash;
        case 59:
            return ImGuiKey_Semicolon;
        case 61:
            return ImGuiKey_Equal;
        case 91:
            return ImGuiKey_LeftBracket;
        case 92:
            return ImGuiKey_Backslash;
        case 93:
            return ImGuiKey_RightBracket;
        case 96:
            return ImGuiKey_GraveAccent;
        case 256:
            return ImGuiKey_Escape;
        case 257:
            return ImGuiKey_Enter;
        case 258:
            return ImGuiKey_Tab;
        case 259:
            return ImGuiKey_Backspace;
        case 260:
            return ImGuiKey_Insert;
        case 261:
            return ImGuiKey_Delete;
        case 262:
            return ImGuiKey_RightArrow;
        case 263:
            return ImGuiKey_LeftArrow;
        case 264:
            return ImGuiKey_DownArrow;
        case 265:
            return ImGuiKey_UpArrow;
        case 266:
            return ImGuiKey_PageUp;
        case 267:
            return ImGuiKey_PageDown;
        case 268:
            return ImGuiKey_Home;
        case 269:
            return ImGuiKey_End;
        case 335:
            return ImGuiKey_KeypadEnter;
        case 340:
            return ImGuiKey_LeftShift;
        case 341:
            return ImGuiKey_LeftCtrl;
        case 342:
            return ImGuiKey_LeftAlt;
        case 343:
            return ImGuiKey_LeftSuper;
        case 344:
            return ImGuiKey_RightShift;
        case 345:
            return ImGuiKey_RightCtrl;
        case 346:
            return ImGuiKey_RightAlt;
        case 347:
            return ImGuiKey_RightSuper;
        default:
            return ImGuiKey_None;
    }
}

uint32_t steadyMs() {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

class EventPlatform final : public OverlayPlatform {
public:
    bool init(SettingsOverlay& overlay, SettingsOverlay::Impl& impl) override {
        overlay_ = &overlay;
        m_ = &impl;
        events_ = impl.init.events;
        if (!events_.next)
            return false;
        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = "drakan_events";
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        last_ = std::chrono::steady_clock::now();
        return true;
    }
    void shutdown() override {}
    // Every presented frame, also while nothing is drawn: F1 and the combo must work then, so the queue is read here
    // and what ImGui needs is kept for newFrame.
    void poll() override {
        pollGamepadCombo();
        const bool feed = m_->menu->isOpen() || m_->screenInput.load();
        OverlayEvent e;
        while (events_.next(e)) {
            if (e.type == OverlayEvent::Key && e.a == kGlfwF1) {
                if (e.b)
                    m_->hotkey(*overlay_);
                continue;
            }
            if ((feed || e.type == OverlayEvent::Focus) && pending_.size() < 4096)
                pending_.push_back(e); // otherwise the game has the input
        }
    }

    void newFrame(uint32_t w, uint32_t h) override {
        ImGuiIO& io = ImGui::GetIO();
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::max(1e-4f, std::chrono::duration<float>(now - last_).count());
        last_ = now;
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        const bool feed = m_->menu->isOpen() || m_->screenInput.load();
        std::vector<OverlayEvent> events;
        events.swap(pending_);
        for (const OverlayEvent& e : events) {
            if (e.type == OverlayEvent::Focus) {
                io.AddFocusEvent(e.a != 0);
                continue;
            }
            switch (e.type) {
                case OverlayEvent::Key: {
                    const ImGuiKey key = imguiKey(e.a);
                    if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)
                        io.AddKeyEvent(ImGuiMod_Ctrl, e.b != 0);
                    else if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift)
                        io.AddKeyEvent(ImGuiMod_Shift, e.b != 0);
                    else if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
                        io.AddKeyEvent(ImGuiMod_Alt, e.b != 0);
                    if (key != ImGuiKey_None)
                        io.AddKeyEvent(key, e.b != 0);
                    break;
                }
                case OverlayEvent::Button:
                    if (e.a >= 0 && e.a < 3) {
                        float x = 0, y = 0;
                        if (pointerInBackbuffer(w, h, &x, &y))
                            io.AddMousePosEvent(x, y); // the click lands where the cursor is now
                        io.AddMouseButtonEvent(e.a, e.b != 0);
                    }
                    break;
                case OverlayEvent::Wheel:
                    io.AddMouseWheelEvent(0.0f, static_cast<float>(e.a));
                    break;
                case OverlayEvent::Char:
                    io.AddInputCharacter(static_cast<unsigned>(e.a));
                    break;
                default:
                    break;
            }
        }
        float x = 0, y = 0;
        if (pointerInBackbuffer(w, h, &x, &y))
            io.AddMousePosEvent(x, y);
        if (feed)
            feedGamepad(io);
        // The menu draws its own cursor (MouseDrawCursor): hide the system one while it is shown.
        const bool hide = m_->menu->isOpen();
        if (hide != cursorHidden_ && events_.setCursorHidden) {
            events_.setCursorHidden(hide);
            cursorHidden_ = hide;
        }
    }

    bool pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const override {
        int cx = 0, cy = 0, cw = 0, ch = 0;
        if (!events_.pointer || !events_.pointer(cx, cy, cw, ch) || cw <= 0 || ch <= 0)
            return false;
        if (cx < 0 || cy < 0 || cx >= cw || cy >= ch)
            return false;
        *x = static_cast<float>(cx) * static_cast<float>(w) / static_cast<float>(cw);
        *y = static_cast<float>(cy) * static_cast<float>(h) / static_cast<float>(ch);
        return true;
    }

    uint32_t nowMs() const override { return steadyMs(); }

private:
    uint32_t pad() const { return events_.gamepadButtons ? events_.gamepadButtons() : 0; }

    void feedGamepad(ImGuiIO& io) {
        const uint32_t b = pad();
        const auto key = [&](ImGuiKey k, int bit) { io.AddKeyEvent(k, (b >> bit) & 1u); };
        key(ImGuiKey_GamepadFaceDown, 0);
        key(ImGuiKey_GamepadFaceRight, 1);
        key(ImGuiKey_GamepadFaceLeft, 2);
        key(ImGuiKey_GamepadFaceUp, 3);
        key(ImGuiKey_GamepadL1, 4);
        key(ImGuiKey_GamepadR1, 5);
        key(ImGuiKey_GamepadL2, 6);
        key(ImGuiKey_GamepadR2, 7);
        key(ImGuiKey_GamepadBack, 8);
        key(ImGuiKey_GamepadStart, 9);
        key(ImGuiKey_GamepadL3, 10);
        key(ImGuiKey_GamepadR3, 11);
        key(ImGuiKey_GamepadDpadUp, 12);
        key(ImGuiKey_GamepadDpadDown, 13);
        key(ImGuiKey_GamepadDpadLeft, 14);
        key(ImGuiKey_GamepadDpadRight, 15);
    }

    // Back + LB + RB held for 0.5 s toggles the menu (as on Windows).
    void pollGamepadCombo() {
        if (!m_->init.enableGamepadCombo)
            return;
        constexpr uint32_t kCombo = (1u << 8) | (1u << 4) | (1u << 5);
        const bool combo = (pad() & kCombo) == kCombo;
        const uint32_t now = steadyMs();
        if (!combo) {
            comboSince_ = 0;
            comboFired_ = false;
            return;
        }
        if (!comboSince_)
            comboSince_ = now ? now : 1;
        if (!comboFired_ && now - comboSince_ >= 500) {
            comboFired_ = true;
            m_->hotkey(*overlay_);
        }
    }

    SettingsOverlay* overlay_ = nullptr;
    SettingsOverlay::Impl* m_ = nullptr;
    OverlayEventSource events_;
    std::vector<OverlayEvent> pending_; // read by poll(), fed to ImGui by newFrame()
    std::chrono::steady_clock::time_point last_;
    bool cursorHidden_ = false;
    uint32_t comboSince_ = 0;
    bool comboFired_ = false;
};

} // namespace

std::unique_ptr<OverlayPlatform> createOverlayPlatform() {
    return std::make_unique<EventPlatform>();
}

} // namespace dsettings
