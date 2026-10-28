// Drakan: native Win32 top-level game window (replaces the raylib / GLFW OpenGL window when the D3D11 renderer is
// used).
//   * the D3D11 flip-model swapchain is created directly on this top-level HWND (so driver overlays - NVIDIA / AMD /
//   Steam / RTSS /
//     capture - see an ordinary D3D11 game window),
//   * it owns title, icon, DPI placement, the message pump, the close request (X button, Alt+F4, WM_QUIT) and the
//   cursor shape,
//   * keyboard / mouse for the game come from drakan_input (Raw Input), the settings overlay subclasses this window.
// This is the "window / present" platform interface of docs/reference/portability.md: a browser build replaces this
// file with a canvas.
#pragma once
#include <cstdint>

namespace drakan_win {
// Creates the window (client area clientW x clientH, centred on the primary monitor's work area, shrunk to fit). Call
// once, on the thread that will pump messages. Returns false when the window cannot be created. The window starts
// hidden: apply the start-up window mode (borderless fullscreen) first, then show().
bool create(const char* utf8Title, int clientW, int clientH);
// Shows the window and brings it to the foreground (flashes the taskbar button when Windows refuses).
void show();
void* handle(); // HWND, nullptr before create()
void destroy(); // destroys the window and restores the timer resolution
// Dispatches every pending message of this thread (never blocks). Call once per host frame.
void pump();
// True once the user asked to close (X button, Alt+F4, taskbar, WM_QUIT) or requestClose() was called. The window
// itself stays up until destroy().
bool closeRequested();
void requestClose(); // thread safe: ends the host loop as if the X button was pressed
// While Windows runs its modal move / size loop the message pump does not return; this callback (window thread, ~125
// Hz) keeps presenting.
void setModalTick(void (*fn)(void*), void* ctx);
void setHideCursor(bool hide);  // fullscreen: no arrow over the game
void setPosition(int x, int y); // outer window position (test aid DRAKAN_WINDOW_POS)
} // namespace drakan_win
