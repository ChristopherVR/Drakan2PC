// Drakan: the game window on GLFW without a client API (Linux), see drakan_window.h. The WebGPU surface is created on
// its X11 window. GLFW is the copy raylib builds (X11); its window queries are main-thread only, so the state other
// threads read (size, focus, keys) is cached from the event callbacks. Input callbacks installed later (HostInput, the
// settings overlay) chain to the ones set here.
#include "drakan_window.h"
#include "drakan_glfw_keys.h"
#include "drakan_input_glfw.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

namespace drakan_win {
namespace {
GLFWwindow* g_window = nullptr;
std::atomic<bool> g_close{false};
std::atomic<bool> g_hideCursor{false};
bool g_cursorHidden = false;
std::atomic<int> g_fbW{0}, g_fbH{0};
std::atomic<bool> g_focused{false}, g_iconified{false};
// Key / button levels from the callbacks (GLFW key codes; mouse buttons after GLFW_KEY_LAST).
constexpr int kMouseBase = GLFW_KEY_LAST + 1;
std::atomic<bool> g_down[kMouseBase + GLFW_MOUSE_BUTTON_LAST + 1];
bool g_fullscreen = false;
int g_savedX = 0, g_savedY = 0, g_savedW = 0, g_savedH = 0;
std::atomic<double> g_refresh{0.0};
std::atomic<int> g_monW{0}, g_monH{0};

// Windows virtual-key code -> GLFW key, or a mouse button slot; -1 = none.
int glfwKeyOf(int vk) {
    if (vk == vk::LeftButton)
        return kMouseBase + GLFW_MOUSE_BUTTON_LEFT;
    if (vk == vk::RightButton)
        return kMouseBase + GLFW_MOUSE_BUTTON_RIGHT;
    return drakan_keys::glfwFromVk(vk);
}

// The monitor whose area holds the window's centre (GLFW only reports the monitor of a fullscreen window).
GLFWmonitor* currentMonitor() {
    if (!g_window)
        return glfwGetPrimaryMonitor();
    if (GLFWmonitor* m = glfwGetWindowMonitor(g_window))
        return m;
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(g_window, &wx, &wy);
    glfwGetWindowSize(g_window, &ww, &wh);
    const int cx = wx + ww / 2, cy = wy + wh / 2;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitors[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (mode && cx >= mx && cy >= my && cx < mx + mode->width && cy < my + mode->height)
            return monitors[i];
    }
    return glfwGetPrimaryMonitor();
}

void refreshMonitor() {
    const GLFWvidmode* mode = glfwGetVideoMode(currentMonitor());
    g_refresh.store(mode && mode->refreshRate > 0 ? 1.0 / mode->refreshRate : 0.0);
    g_monW.store(mode ? mode->width : 0);
    g_monH.store(mode ? mode->height : 0);
}

void onFramebufferSize(GLFWwindow*, int w, int h) {
    g_fbW.store(w);
    g_fbH.store(h);
}
void onFocus(GLFWwindow*, int focused) {
    g_focused.store(focused != 0);
    if (!focused)
        for (auto& d : g_down)
            d.store(false);
}
void onIconify(GLFWwindow*, int iconified) {
    g_iconified.store(iconified != 0);
}
void onClose(GLFWwindow* w) {
    g_close.store(true); // the host loop shuts everything down in order, then destroy()
    glfwSetWindowShouldClose(w, GLFW_FALSE);
}
void onKey(GLFWwindow*, int key, int, int action, int) {
    if (key >= 0 && key <= GLFW_KEY_LAST && action != GLFW_REPEAT)
        g_down[key].store(action == GLFW_PRESS);
}
void onButton(GLFWwindow*, int button, int action, int) {
    if (button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST)
        g_down[kMouseBase + button].store(action == GLFW_PRESS);
}
void onWindowPos(GLFWwindow*, int, int) {
    refreshMonitor();
}
void onError(int code, const char* text) {
    std::fprintf(stderr, "[window] GLFW error %d: %s\n", code, text ? text : "");
}
} // namespace

bool create(const char* utf8Title, int clientW, int clientH) {
    if (g_window)
        return true;
    glfwSetErrorCallback(onError);
    if (!glfwInit())
        return false;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // the WebGPU surface owns presentation
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (const char* e = std::getenv("DRAKAN_NO_ACTIVATE"); e && *e == '1')
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    if (const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor())) {
        clientW = std::min(clientW, mode->width);
        clientH = std::min(clientH, mode->height);
    }
    g_close.store(false);
    g_window = glfwCreateWindow(std::max(clientW, 16), std::max(clientH, 16), utf8Title ? utf8Title : "Drakan", nullptr,
                                nullptr);
    if (!g_window) {
        glfwTerminate();
        return false;
    }
    if (const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor())) {
        int ww = 0, wh = 0;
        glfwGetWindowSize(g_window, &ww, &wh);
        int mx = 0, my = 0;
        glfwGetMonitorPos(glfwGetPrimaryMonitor(), &mx, &my);
        glfwSetWindowPos(g_window, mx + std::max(0, (mode->width - ww) / 2), my + std::max(0, (mode->height - wh) / 2));
    }
    glfwSetFramebufferSizeCallback(g_window, onFramebufferSize);
    glfwSetWindowFocusCallback(g_window, onFocus);
    glfwSetWindowIconifyCallback(g_window, onIconify);
    glfwSetWindowCloseCallback(g_window, onClose);
    glfwSetKeyCallback(g_window, onKey);
    glfwSetMouseButtonCallback(g_window, onButton);
    glfwSetWindowPosCallback(g_window, onWindowPos);
    int fw = 0, fh = 0, ww = 0, wh = 0;
    glfwGetFramebufferSize(g_window, &fw, &fh);
    glfwGetWindowSize(g_window, &ww, &wh);
    onFramebufferSize(g_window, fw, fh);
    refreshMonitor();
    std::fprintf(stderr, "[window] GLFW %s window: requested %dx%d, window %dx%d, framebuffer %dx%d, monitor %dx%d\n",
                 glfwGetPlatform() == GLFW_PLATFORM_X11 ? "X11" : "other", clientW, clientH, ww, wh, fw, fh,
                 g_monW.load(), g_monH.load());
    return true;
}

void show() {
    if (!g_window)
        return;
    glfwShowWindow(g_window);
    g_focused.store(glfwGetWindowAttrib(g_window, GLFW_FOCUSED) != 0);
}

void* handle() {
    return g_window;
}

void destroy() {
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
        glfwTerminate();
    }
}

void pump() {
    if (!g_window)
        return;
    glfwPollEvents();
    drakan_input_glfw::mainThreadTick();
    const bool hide = g_hideCursor.load();
    if (hide != g_cursorHidden) {
        g_cursorHidden = hide;
        glfwSetInputMode(g_window, GLFW_CURSOR, hide ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
    }
}

bool closeRequested() {
    return g_close.load();
}

void requestClose() {
    g_close.store(true);
    if (g_window)
        glfwPostEmptyEvent(); // thread safe; wakes a waiting pump
}

void setModalTick(void (*)(void*), void*) {} // GLFW has no modal move / size loop

void setHideCursor(bool hide) {
    g_hideCursor.store(hide);
}

void setPosition(int x, int y) {
    if (g_window)
        glfwSetWindowPos(g_window, x, y);
}

Surface surface() {
    Surface s;
    if (g_window && glfwGetPlatform() == GLFW_PLATFORM_X11) {
        s.kind = Surface::Kind::Xlib;
        s.display = glfwGetX11Display();
        s.xlibWindow = static_cast<uint64_t>(glfwGetX11Window(g_window));
    }
    return s;
}

bool clientSize(int& w, int& h) {
    if (!g_window)
        return false;
    w = g_fbW.load();
    h = g_fbH.load();
    return true;
}

bool minimized() {
    return g_window && (g_iconified.load() || !g_fbW.load() || !g_fbH.load());
}

bool focused() {
    return g_window && g_focused.load();
}

void setFullscreen(bool on) {
    if (!g_window || on == g_fullscreen)
        return;
    if (on) {
        glfwGetWindowPos(g_window, &g_savedX, &g_savedY);
        glfwGetWindowSize(g_window, &g_savedW, &g_savedH);
        GLFWmonitor* monitor = currentMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode)
            return;
        // The monitor's current mode: no mode switch, a borderless window covering it.
        glfwSetWindowMonitor(g_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(g_window, nullptr, g_savedX, g_savedY, std::max(g_savedW, 16), std::max(g_savedH, 16),
                             GLFW_DONT_CARE);
    }
    g_fullscreen = on;
    setHideCursor(on);
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(g_window, &fw, &fh);
    onFramebufferSize(g_window, fw, fh);
    refreshMonitor();
}

bool fullscreen() {
    return g_fullscreen;
}

bool monitorSize(int& w, int& h) {
    w = g_monW.load();
    h = g_monH.load();
    return w > 0 && h > 0;
}

void setClientSize(int w, int h) {
    if (g_window && !g_fullscreen && w > 0 && h > 0)
        glfwSetWindowSize(g_window, w, h);
}

double refreshSeconds() {
    return g_refresh.load();
}

bool keyDown(int code) {
    const auto down = [](int glfwKey) { return g_down[glfwKey].load(); };
    switch (code) { // keys with a left and a right variant
        case vk::Alt:
            return down(GLFW_KEY_LEFT_ALT) || down(GLFW_KEY_RIGHT_ALT);
        case vk::Control:
            return down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL);
        case vk::Enter:
            return down(GLFW_KEY_ENTER) || down(GLFW_KEY_KP_ENTER);
        default: {
            const int key = glfwKeyOf(code);
            return key >= 0 && down(key);
        }
    }
}
} // namespace drakan_win
