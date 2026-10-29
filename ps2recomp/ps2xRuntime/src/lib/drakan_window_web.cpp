// Drakan: the game window in the browser, see drakan_window.h. The window is the page's #canvas: its drawing size comes
// from player.js (drakan_web_resize), and the renderer's GPU thread takes it over as an OffscreenCanvas. Events arrive
// through the drakan_web_* exports (drakan_web.cpp); pump() hands the queued ones to the input module.
#include "drakan_window.h"
#include "drakan_glfw_keys.h"
#include "drakan_input_web.h"
#include "drakan_web_events.h"
#include "drakan_web_input.h"

#include <atomic>
#include <emscripten.h>

void drakanWebSurfaceSize(int& width, int& height); // drakan_web.cpp
bool drakanWebFocused();
bool drakanWebStopRequested();
bool drakanWebKeyDown(int key);
bool drakanWebMouseDown(int button);
extern "C" void drakan_web_stop();

namespace drakan_win {
namespace {
bool g_created = false;
bool g_fullscreen = false;
int g_window = 1; // handle(): any stable non-null pointer
} // namespace

bool create(const char*, int, int) {
    g_created = true;
    return true;
}
void show() {}
void* handle() {
    return g_created ? &g_window : nullptr;
}
void destroy() {
    g_created = false;
}
void pump() {
    drakan_input_web::pumpTick(); // DOM events queued since the last host frame
}
bool closeRequested() {
    return drakanWebStopRequested();
}
void requestClose() {
    drakan_web_stop();
}
void setModalTick(void (*)(void*), void*) {}
void setHideCursor(bool hide) {
    drakanWebSetCursorHidden(hide);
}
void setPosition(int, int) {}

Surface surface() {
    Surface s;
    if (g_created) {
        s.kind = Surface::Kind::Canvas;
        s.canvasSelector = "#canvas";
    }
    return s;
}
bool clientSize(int& w, int& h) {
    if (!g_created)
        return false;
    drakanWebSurfaceSize(w, h);
    return true;
}
bool minimized() {
    return false;
}
bool focused() {
    return g_created && drakanWebFocused();
}
void setFullscreen(bool on) {
    g_fullscreen = on;
    drakanWebRequestFullscreen(on);
}
bool fullscreen() {
    return g_fullscreen;
}
bool monitorSize(int& w, int& h) {
    w = MAIN_THREAD_EM_ASM_INT({ return Math.round(screen.width * (window.devicePixelRatio || 1)); });
    h = MAIN_THREAD_EM_ASM_INT({ return Math.round(screen.height * (window.devicePixelRatio || 1)); });
    return w > 0 && h > 0;
}
void setClientSize(int, int) {} // the page lays out the canvas
double refreshSeconds() {
    return 0.0;
}
bool keyDown(int code) {
    switch (code) {
        case vk::LeftButton:
            return drakanWebMouseDown(0);
        case vk::RightButton:
            return drakanWebMouseDown(1);
        case vk::Alt:
            return drakanWebKeyDown(GLFW_KEY_LEFT_ALT) || drakanWebKeyDown(GLFW_KEY_RIGHT_ALT);
        case vk::Control:
            return drakanWebKeyDown(GLFW_KEY_LEFT_CONTROL) || drakanWebKeyDown(GLFW_KEY_RIGHT_CONTROL);
        case vk::Enter:
            return drakanWebKeyDown(GLFW_KEY_ENTER) || drakanWebKeyDown(GLFW_KEY_KP_ENTER);
        default: {
            const int key = drakan_keys::glfwFromVk(code);
            return key >= 0 && drakanWebKeyDown(key);
        }
    }
}
} // namespace drakan_win
