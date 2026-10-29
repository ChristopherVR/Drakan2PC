// SettingsOverlay's GLFW window platform (Linux): imgui_impl_glfw fed from the overlay's own callbacks (which select
// the overlay's ImGui context and chain to the callbacks installed before them: the game input and the window layer),
// F1 and the gamepad combo. GLFW calls happen on the window thread, which is also the present thread here.
#include "OverlayImpl.h"

#include <chrono>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"

namespace dsettings {

namespace {

class GlfwPlatform;
GlfwPlatform* g_platform = nullptr; // one overlay per process

class GlfwPlatform final : public OverlayPlatform {
public:
    bool init(SettingsOverlay& overlay, SettingsOverlay::Impl& impl) override {
        overlay_ = &overlay;
        m_ = &impl;
        window_ = static_cast<GLFWwindow*>(impl.init.hwnd);
        // The init.hotkeyVk is a Windows virtual key; F1..F12 map to GLFW's function keys.
        hotkey_ = impl.init.hotkeyVk >= 0x70 && impl.init.hotkeyVk <= 0x7B ? GLFW_KEY_F1 + (impl.init.hotkeyVk - 0x70)
                                                                           : GLFW_KEY_F1;
        if (!window_ || !ImGui_ImplGlfw_InitForOther(window_, false))
            return false;
        up_ = true;
        // The game input owns the cursor mode (capture, its menu cursor); newFrame only hides it under the menu.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        g_platform = this;
        prevKey_ = glfwSetKeyCallback(window_, onKey);
        prevChar_ = glfwSetCharCallback(window_, onChar);
        prevButton_ = glfwSetMouseButtonCallback(window_, onButton);
        prevCursor_ = glfwSetCursorPosCallback(window_, onCursor);
        prevScroll_ = glfwSetScrollCallback(window_, onScroll);
        prevFocus_ = glfwSetWindowFocusCallback(window_, onFocus);
        prevEnter_ = glfwSetCursorEnterCallback(window_, onEnter);
        return true;
    }
    void shutdown() override {
        if (g_platform == this) {
            glfwSetKeyCallback(window_, prevKey_);
            glfwSetCharCallback(window_, prevChar_);
            glfwSetMouseButtonCallback(window_, prevButton_);
            glfwSetCursorPosCallback(window_, prevCursor_);
            glfwSetScrollCallback(window_, prevScroll_);
            glfwSetWindowFocusCallback(window_, prevFocus_);
            glfwSetCursorEnterCallback(window_, prevEnter_);
            g_platform = nullptr;
        }
        if (up_)
            ImGui_ImplGlfw_Shutdown();
        up_ = false;
    }

    void poll() override { pollGamepadCombo(); }

    void newFrame(uint32_t w, uint32_t h) override {
        ImGui_ImplGlfw_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        float x = 0, y = 0;
        if (pointerInBackbuffer(w, h, &x, &y))
            io.AddMousePosEvent(x, y);
        // The menu draws its own cursor (MouseDrawCursor): hide the system one over it while it is shown normally,
        // and give back only what was hidden here (the game input owns capture and its menu cursor).
        const int mode = glfwGetInputMode(window_, GLFW_CURSOR);
        if (m_->menu->isOpen() && mode == GLFW_CURSOR_NORMAL) {
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            hidCursor_ = true;
        } else if (!m_->menu->isOpen() && hidCursor_) {
            if (mode == GLFW_CURSOR_HIDDEN)
                glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            hidCursor_ = false;
        }
    }

    bool pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const override {
        double cx = 0, cy = 0;
        int ww = 0, wh = 0;
        glfwGetCursorPos(window_, &cx, &cy);
        glfwGetWindowSize(window_, &ww, &wh);
        if (ww <= 0 || wh <= 0 || cx < 0 || cy < 0 || cx >= ww || cy >= wh)
            return false;
        *x = static_cast<float>(cx) * static_cast<float>(w) / static_cast<float>(ww);
        *y = static_cast<float>(cy) * static_cast<float>(h) / static_cast<float>(wh);
        return true;
    }

    uint32_t nowMs() const override {
        return static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

private:
    // The ImGui half of a callback: only while the menu or the screen layer takes the input, with the overlay's
    // context.
    template <typename Fn>
    static void feed(Fn&& fn) {
        GlfwPlatform* p = g_platform;
        if (!p || (!p->m_->menu->isOpen() && !p->m_->screenInput.load()))
            return;
        ImGuiCtxScope scope(p->m_->ctx);
        fn(*p);
    }
    // Cursor position in back buffer pixels (window coordinates scaled to the last rendered size).
    void mappedPos(double x, double y) {
        int ww = 0, wh = 0;
        glfwGetWindowSize(window_, &ww, &wh);
        const uint32_t bw = m_->lastW.load(), bh = m_->lastH.load();
        float px = static_cast<float>(x), py = static_cast<float>(y);
        if (bw && bh && ww > 0 && wh > 0) {
            px = px * static_cast<float>(bw) / static_cast<float>(ww);
            py = py * static_cast<float>(bh) / static_cast<float>(wh);
        }
        ImGui::GetIO().AddMousePosEvent(px, py);
    }

    static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods) {
        if (GlfwPlatform* p = g_platform) {
            if (key == p->hotkey_ && action == GLFW_PRESS) {
                ImGuiCtxScope scope(p->m_->ctx);
                p->m_->hotkey(*p->overlay_);
            } else if (key != p->hotkey_) {
                feed([&](GlfwPlatform&) { ImGui_ImplGlfw_KeyCallback(w, key, scancode, action, mods); });
            }
            if (p->prevKey_)
                p->prevKey_(w, key, scancode, action, mods);
        }
    }
    static void onChar(GLFWwindow* w, unsigned int c) {
        if (GlfwPlatform* p = g_platform) {
            feed([&](GlfwPlatform&) { ImGui_ImplGlfw_CharCallback(w, c); });
            if (p->prevChar_)
                p->prevChar_(w, c);
        }
    }
    static void onButton(GLFWwindow* w, int button, int action, int mods) {
        if (GlfwPlatform* p = g_platform) {
            feed([&](GlfwPlatform& self) {
                // The click lands where the cursor is now, not where the next frame's poll finds it.
                double x = 0, y = 0;
                glfwGetCursorPos(w, &x, &y);
                self.mappedPos(x, y);
                ImGui_ImplGlfw_MouseButtonCallback(w, button, action, mods);
            });
            if (p->prevButton_)
                p->prevButton_(w, button, action, mods);
        }
    }
    static void onCursor(GLFWwindow* w, double x, double y) {
        if (GlfwPlatform* p = g_platform) {
            feed([&](GlfwPlatform& self) { self.mappedPos(x, y); });
            if (p->prevCursor_)
                p->prevCursor_(w, x, y);
        }
    }
    static void onScroll(GLFWwindow* w, double dx, double dy) {
        if (GlfwPlatform* p = g_platform) {
            feed([&](GlfwPlatform&) { ImGui_ImplGlfw_ScrollCallback(w, dx, dy); });
            if (p->prevScroll_)
                p->prevScroll_(w, dx, dy);
        }
    }
    static void onFocus(GLFWwindow* w, int focused) {
        if (GlfwPlatform* p = g_platform) {
            {
                ImGuiCtxScope scope(p->m_->ctx);
                ImGui_ImplGlfw_WindowFocusCallback(w, focused);
            }
            if (p->prevFocus_)
                p->prevFocus_(w, focused);
        }
    }
    static void onEnter(GLFWwindow* w, int entered) {
        if (GlfwPlatform* p = g_platform) {
            {
                ImGuiCtxScope scope(p->m_->ctx);
                ImGui_ImplGlfw_CursorEnterCallback(w, entered);
            }
            if (p->prevEnter_)
                p->prevEnter_(w, entered);
        }
    }

    // Back + LB + RB held for 0.5 s toggles the menu (as on Windows).
    void pollGamepadCombo() {
        if (!m_->init.enableGamepadCombo)
            return;
        bool combo = false;
        for (int j = GLFW_JOYSTICK_1; j <= GLFW_JOYSTICK_LAST && !combo; ++j) {
            GLFWgamepadstate st{};
            if (glfwJoystickIsGamepad(j) && glfwGetGamepadState(j, &st))
                combo = st.buttons[GLFW_GAMEPAD_BUTTON_BACK] && st.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] &&
                        st.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER];
        }
        const uint32_t now = nowMs();
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
    GLFWwindow* window_ = nullptr;
    int hotkey_ = GLFW_KEY_F1;
    bool up_ = false;
    bool hidCursor_ = false;
    uint32_t comboSince_ = 0;
    bool comboFired_ = false;
    GLFWkeyfun prevKey_ = nullptr;
    GLFWcharfun prevChar_ = nullptr;
    GLFWmousebuttonfun prevButton_ = nullptr;
    GLFWcursorposfun prevCursor_ = nullptr;
    GLFWscrollfun prevScroll_ = nullptr;
    GLFWwindowfocusfun prevFocus_ = nullptr;
    GLFWcursorenterfun prevEnter_ = nullptr;
};

} // namespace

std::unique_ptr<OverlayPlatform> createOverlayPlatform() {
    return std::make_unique<GlfwPlatform>();
}

} // namespace dsettings
