// Drakan: native Win32 top-level game window, see drakan_window.h.
#include "drakan_window.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace drakan_win {
namespace {
HWND g_hwnd = nullptr;
std::atomic<bool> g_close{false};
std::atomic<bool> g_hideCursor{false};
void (*g_modalTick)(void*) = nullptr;
void* g_modalCtx = nullptr;
bool g_timerRes = false;
constexpr UINT_PTR kModalTimer = 0x5053;
const wchar_t* kClass = L"DrakanWindow";

std::wstring widen(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CLOSE:
            g_close.store(true); // the host loop shuts everything down in order, then destroy()
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(h, nullptr); // the swapchain owns the pixels
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) {
                SetCursor(g_hideCursor.load() ? nullptr : LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
                return TRUE;
            }
            break;
        case WM_ENTERSIZEMOVE:
            SetTimer(h, kModalTimer, 8, nullptr);
            break;
        case WM_EXITSIZEMOVE:
            KillTimer(h, kModalTimer);
            break;
        case WM_TIMER:
            if (w == kModalTimer) {
                if (g_modalTick)
                    g_modalTick(g_modalCtx);
                return 0;
            }
            break;
        case WM_SYSCOMMAND:
            if ((w & 0xFFF0) == SC_KEYMENU)
                return 0; // a tap on Alt must not freeze the game in the window-menu loop (Alt+Enter is polled by the
                          // host loop)
            if ((w & 0xFFF0) == SC_SCREENSAVE || (w & 0xFFF0) == SC_MONITORPOWER)
                return 0;
            break;
        case WM_SYSKEYDOWN:
        case WM_SYSCHAR:
            if (w == VK_RETURN || m == WM_SYSCHAR)
                return 0; // Alt+Enter: handled by the host loop, no "ding"
            break;
        case WM_QUIT:
            g_close.store(true);
            break;
        default:
            break;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace

bool create(const char* utf8Title, int clientW, int clientH) {
    if (g_hwnd)
        return true;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT r{0, 0, clientW, clientH};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    const int maxW = work.right - work.left, maxH = work.bottom - work.top;
    if (maxW > 0 && ww > maxW)
        ww = maxW;
    if (maxH > 0 && wh > maxH)
        wh = maxH;
    const int x = work.left + std::max(0, (maxW - ww) / 2), y = work.top + std::max(0, (maxH - wh) / 2);
    const std::wstring title = widen(utf8Title ? utf8Title : "Drakan");
    g_close.store(false);
    g_hwnd = CreateWindowExW(0, kClass, title.c_str(), style, x, y, ww, wh, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd)
        return false;
    if (timeBeginPeriod(1) == TIMERR_NOERROR) // 1 ms sleeps: the host loop's pacing and the frame limiter depend on it
        g_timerRes = true;
    // Stays hidden until show(): the renderer start-up does not pump messages for seconds, and a window shown during
    // that time first appears windowed, can be ghosted ("Not responding") and loses the foreground to any click.
    return true;
}

void show() {
    if (!g_hwnd)
        return;
    // Scripted test runs (scripts/harness/play_run.py) set DRAKAN_NO_ACTIVATE=1: shown behind the other windows, never
    // activated, so a run does not take the foreground from what the user is doing. Rendering and the scripted input
    // do not need the foreground.
    if (const char* e = std::getenv("DRAKAN_NO_ACTIVATE"); e && *e == '1') {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    ShowWindow(g_hwnd, SW_SHOW); // borderless fullscreen start-up has already made it visible at the monitor size
    if (SetForegroundWindow(g_hwnd) && GetForegroundWindow() == g_hwnd)
        return;
    // Windows denies SetForegroundWindow when another window got input since the launch (or the launcher had no
    // foreground rights). A borderless window that is not in the foreground stays under the taskbar and the active
    // window, so retry with the input queue of the current foreground thread attached.
    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, self = GetCurrentThreadId();
    if (fgThread && fgThread != self && AttachThreadInput(self, fgThread, TRUE)) {
        BringWindowToTop(g_hwnd);
        SetForegroundWindow(g_hwnd);
        AttachThreadInput(self, fgThread, FALSE);
    }
    if (GetForegroundWindow() != g_hwnd) {
        FLASHWINFO fi{sizeof fi, g_hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0}; // flash the taskbar button instead
        FlashWindowEx(&fi);
    }
}

void* handle() {
    return g_hwnd;
}

void destroy() {
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    if (g_timerRes) {
        timeEndPeriod(1);
        g_timerRes = false;
    }
}

void pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_close.store(true);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool closeRequested() {
    return g_close.load();
}

void requestClose() {
    g_close.store(true);
    if (g_hwnd)
        PostMessageW(g_hwnd, WM_NULL, 0, 0); // wake a blocked pump
}

void setModalTick(void (*fn)(void*), void* ctx) {
    g_modalCtx = ctx;
    g_modalTick = fn;
}

void setHideCursor(bool hide) {
    g_hideCursor.store(hide);
}

void setPosition(int x, int y) {
    if (g_hwnd)
        SetWindowPos(g_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Surface surface() {
    Surface s;
    if (g_hwnd) {
        s.kind = Surface::Kind::Win32;
        s.window = g_hwnd;
    }
    return s;
}

bool clientSize(int& w, int& h) {
    RECT rc{};
    if (!g_hwnd || !GetClientRect(g_hwnd, &rc))
        return false;
    w = std::max<LONG>(rc.right - rc.left, 0);
    h = std::max<LONG>(rc.bottom - rc.top, 0);
    return true;
}

bool minimized() {
    return g_hwnd && IsIconic(g_hwnd);
}

bool focused() {
    if (!g_hwnd)
        return false;
    const HWND fg = GetForegroundWindow();
    return fg && (fg == g_hwnd || GetAncestor(fg, GA_ROOT) == g_hwnd);
}

namespace {
bool g_fullscreen = false;
LONG_PTR g_savedStyle = 0;
RECT g_savedRect{};
} // namespace

void setFullscreen(bool on) {
    if (on == g_fullscreen || !g_hwnd)
        return;
    if (on) {
        g_savedStyle = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
        GetWindowRect(g_hwnd, &g_savedRect);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, (g_savedStyle & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, g_savedStyle | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, g_savedRect.left, g_savedRect.top, g_savedRect.right - g_savedRect.left,
                     g_savedRect.bottom - g_savedRect.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }
    g_fullscreen = on;
    setHideCursor(on);
    if (on)
        while (ShowCursor(FALSE) >= 0) {
        }
    else
        while (ShowCursor(TRUE) < 0) {
        }
    SetCursor(on ? nullptr : LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
}

bool fullscreen() {
    return g_fullscreen;
}

bool monitorSize(int& w, int& h) {
    MONITORINFO mi{sizeof(mi)};
    if (!g_hwnd || !GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    w = std::max<LONG>(mi.rcMonitor.right - mi.rcMonitor.left, 0);
    h = std::max<LONG>(mi.rcMonitor.bottom - mi.rcMonitor.top, 0);
    return true;
}

void setClientSize(int w, int h) {
    if (!g_hwnd || g_fullscreen || w <= 0 || h <= 0)
        return;
    RECT r{0, 0, w, h};
    const LONG_PTR st = GetWindowLongPtrW(g_hwnd, GWL_STYLE), ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&r, (DWORD)st, FALSE, (DWORD)ex);
    SetWindowPos(g_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// The rational rate from the display configuration, not the rounded DEVMODE frequency: "59 Hz" panels run at e.g.
// 59.9956 Hz.
double refreshSeconds() {
    if (!g_hwnd)
        return 0.0;
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof monitor;
    if (!GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return 0.0;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return 0.0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS)
        return 0.0;
    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            std::wcscmp(source.viewGdiDeviceName, monitor.szDevice) != 0)
            continue;
        const DISPLAYCONFIG_RATIONAL rate = paths[i].targetInfo.refreshRate;
        if (!rate.Numerator || !rate.Denominator)
            return 0.0;
        const double seconds = double(rate.Denominator) / double(rate.Numerator);
        return seconds > 1.0 / 500 && seconds < 1.0 / 20 ? seconds : 0.0;
    }
    return 0.0;
}

bool keyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
} // namespace drakan_win
