// Win32 host side: Raw Input (mouse + keyboard), XInput, cursor capture. No game knowledge here.
#pragma once
#include "input/FrameInput.h"
#include <cstdint>
#include <memory>
#include <string>

namespace drakan_input {

class HostInput {
public:
    HostInput();
    ~HostInput();
    // hwnd == nullptr: own hidden message-only window on its own thread (raw input with RIDEV_INPUTSINK, events dropped
    // while another process is in the foreground). hwnd != nullptr: registers raw input for that window; the owner must
    // forward messages to handleMessage().
    bool init(void* hwnd, InputAccumulator* acc);
    void shutdown();

    // Feed window messages. Handles WM_INPUT, WM_SETCURSOR (while captured), focus loss. Returns true only when the
    // message was fully handled and *result must be returned (WM_SETCURSOR); WM_INPUT returns false so the caller still
    // runs DefWindowProc as the Raw Input docs require.
    bool handleMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);

    // Per frame: focus, OS cursor, XInput. Fills in.focused / cursor / gamepad.
    void poll(FrameInput& in, bool gamepadEnabled, float deadzone);
    // Hide + clip (true) or release (false) the OS cursor. Safe to call every frame.
    void setCapture(bool captured);
    // Window (and its child windows) whose client area is clipped and whose cursor is hidden while captured. Needed
    // when raw input comes from the private message-only window (init(nullptr, ...)). Subclasses the windows for
    // WM_SETCURSOR.
    void setCaptureWindow(void* hwnd);
    bool captured() const;
    // Hide the OS cursor over the window although the mouse is not captured (the menu cursor is drawn by the overlay).
    void setMenuCursorHidden(bool hidden);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Test aids (Windows only): copy the client area of `hwnd` from the screen into a 24-bit BMP / make the window topmost.
// Read-only client extent, also usable by scripted/headless input (no Raw Input instance).
bool windowClientSize(void* hwnd, int* width, int* height);
bool screenshotWindowBmp(void* hwnd, const char* path);
void makeWindowTopmost(void* hwnd);

} // namespace drakan_input
