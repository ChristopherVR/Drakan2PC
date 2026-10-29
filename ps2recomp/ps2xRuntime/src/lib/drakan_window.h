// Drakan: the game window, one interface per platform (docs/reference/portability.md, "window / present"):
//   * drakan_window.cpp       Win32 top-level window (the D3D11 flip-model swapchain or the WebGPU surface is created
//                             directly on it, so driver overlays - NVIDIA / AMD / Steam / RTSS / capture - see an
//                             ordinary game window),
//   * drakan_window_glfw.cpp  GLFW window without a client API (Linux; the WebGPU surface is created on its X11
//                             window),
//   * drakan_window_web.cpp   the page's canvas (browser; the renderer's GPU thread owns it as an OffscreenCanvas).
// It owns title, icon, placement, the event pump, the close request (X button, Alt+F4, WM_QUIT), fullscreen and the
// cursor shape. Keyboard / mouse for the game come from drakan_input (HostInput); the settings overlay hooks the
// window.
#pragma once
#include <cstdint>

namespace drakan_win {
// Creates the window (client area clientW x clientH, centred on the primary monitor's work area, shrunk to fit). Call
// once, on the thread that will pump events. Returns false when the window cannot be created. The window starts
// hidden: apply the start-up window mode (borderless fullscreen) first, then show().
bool create(const char* utf8Title, int clientW, int clientH);
// Shows the window and brings it to the foreground (flashes the taskbar button when Windows refuses).
void show();
void* handle(); // HWND / GLFWwindow*, nullptr before create()
void destroy(); // destroys the window and restores the timer resolution
// Dispatches every pending event of this thread (never blocks). Call once per host frame.
void pump();
// True once the user asked to close (X button, Alt+F4, taskbar, WM_QUIT) or requestClose() was called. The window
// itself stays up until destroy().
bool closeRequested();
void requestClose(); // thread safe: ends the host loop as if the X button was pressed
// While Windows runs its modal move / size loop the message pump does not return; this callback (window thread, ~125
// Hz) keeps presenting. Unused elsewhere.
void setModalTick(void (*fn)(void*), void* ctx);
void setHideCursor(bool hide);  // fullscreen: no arrow over the game
void setPosition(int x, int y); // outer window position (test aid DRAKAN_WINDOW_POS)

// ---- what the renderer needs ----
// Where a GPU surface is created.
struct Surface {
    enum class Kind : uint8_t { None, Win32, Xlib, Wayland, Canvas } kind = Kind::None;
    void* display = nullptr;              // Xlib: Display*, Wayland: wl_display*
    void* window = nullptr;               // Win32: HWND, Wayland: wl_surface*
    uint64_t xlibWindow = 0;              // Xlib: Window
    const char* canvasSelector = nullptr; // Canvas: CSS selector
};
Surface surface();
bool clientSize(int& w, int& h); // drawable pixels; false before create()
bool minimized();
bool focused(); // the window (or one of its children) has the keyboard focus
// Borderless fullscreen on the monitor the window is on, and back to the saved windowed placement.
void setFullscreen(bool on);
bool fullscreen();
bool monitorSize(int& w, int& h); // pixels of the monitor the window is on
void setClientSize(int w, int h); // windowed mode only
// Exact refresh period (seconds) of the monitor showing the window, 0 if unknown.
double refreshSeconds();

// Physical key / mouse button state now (not edges), by Windows virtual-key code: host hotkeys (Alt+Enter, F11, F2) and
// the modern screens' keys, whose down state must not depend on who owned the input when they were pressed. Letters and
// digits are their ASCII codes.
namespace vk {
constexpr int LeftButton = 0x01, RightButton = 0x02, Backspace = 0x08, Tab = 0x09, Enter = 0x0D, Control = 0x11,
              Alt = 0x12, Escape = 0x1B, Space = 0x20, Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28, Delete = 0x2E,
              F2 = 0x71, F11 = 0x7A;
}
bool keyDown(int vk);
} // namespace drakan_win
