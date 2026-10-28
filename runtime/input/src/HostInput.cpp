#include "input/HostInput.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace drakan_input {

namespace {
using PFN_XInputGetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

// Cursor hiding for windows owned by another thread (GLFW / D3D child): subclass WM_SETCURSOR.
std::atomic<bool> g_cursorHidden{false};
std::atomic<bool> g_menuCursorHidden{false};
std::mutex g_subM;
std::unordered_map<HWND, WNDPROC> g_orig;

LRESULT CALLBACK subProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    WNDPROC orig = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_subM);
        auto it = g_orig.find(h);
        if (it != g_orig.end())
            orig = it->second;
    }
    if (msg == WM_SETCURSOR && (g_cursorHidden || g_menuCursorHidden) && LOWORD(l) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
    }
    return orig ? CallWindowProcW(orig, h, msg, w, l) : DefWindowProcW(h, msg, w, l);
}

void subclassOne(HWND h) {
    std::lock_guard<std::mutex> lock(g_subM);
    if (g_orig.count(h))
        return;
    WNDPROC old = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&subProc)));
    if (old)
        g_orig[h] = old;
}
BOOL CALLBACK subChild(HWND h, LPARAM) {
    subclassOne(h);
    return TRUE;
}
} // namespace

std::string exeDirectory() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    if (n == 0)
        return "";
    std::wstring w(buf, n);
    size_t pos = w.find_last_of(L"/\\");
    if (pos == std::wstring::npos)
        return "";
    w.resize(pos + 1);
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), 'X');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

struct HostInput::Impl {
    InputAccumulator* acc = nullptr;
    HWND hwnd = nullptr;   // window supplied by the owner (may be null)
    HWND ownWnd = nullptr; // hidden message window when hwnd is null
    HWND capWnd = nullptr; // window to clip / hide the cursor for (may be null: foreground window of the process)
    std::thread thr;
    std::atomic<bool> running{false};
    std::atomic<bool> captured{false};
    bool wasFocused = true;
    bool sink = false;
    int wheelRemainder = 0;
    HMODULE xinput = nullptr;
    PFN_XInputGetState getState = nullptr;
    DWORD xinputUser = 0;
    bool cursorHidden = false;
    std::vector<BYTE> rawBuf;

    bool processFocus() const {
        HWND fg = GetForegroundWindow();
        if (!fg)
            return false;
        if (hwnd)
            return fg == hwnd || GetAncestor(fg, GA_ROOT) == GetAncestor(hwnd, GA_ROOT);
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId();
    }
    HWND target() const { return hwnd ? hwnd : capWnd ? capWnd : (processFocus() ? GetForegroundWindow() : nullptr); }

    void onRaw(HRAWINPUT h) {
        UINT size = 0;
        if (GetRawInputData(h, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
            return;
        if (rawBuf.size() < size)
            rawBuf.resize(size);
        if (GetRawInputData(h, RID_INPUT, rawBuf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
            return;
        const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(rawBuf.data());
        if (sink && !processFocus())
            return; // background sink: ignore other apps' input
        if (ri->header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& m = ri->data.mouse;
            if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE) && (m.lLastX || m.lLastY))
                acc->onMouseMove(m.lLastX, m.lLastY);
            USHORT f = m.usButtonFlags;
            if (f & RI_MOUSE_LEFT_BUTTON_DOWN)
                acc->onMouseButton(MBTN_LEFT, true);
            if (f & RI_MOUSE_LEFT_BUTTON_UP)
                acc->onMouseButton(MBTN_LEFT, false);
            if (f & RI_MOUSE_RIGHT_BUTTON_DOWN)
                acc->onMouseButton(MBTN_RIGHT, true);
            if (f & RI_MOUSE_RIGHT_BUTTON_UP)
                acc->onMouseButton(MBTN_RIGHT, false);
            if (f & RI_MOUSE_MIDDLE_BUTTON_DOWN)
                acc->onMouseButton(MBTN_MIDDLE, true);
            if (f & RI_MOUSE_MIDDLE_BUTTON_UP)
                acc->onMouseButton(MBTN_MIDDLE, false);
            if (f & RI_MOUSE_BUTTON_4_DOWN)
                acc->onMouseButton(MBTN_X1, true);
            if (f & RI_MOUSE_BUTTON_4_UP)
                acc->onMouseButton(MBTN_X1, false);
            if (f & RI_MOUSE_BUTTON_5_DOWN)
                acc->onMouseButton(MBTN_X2, true);
            if (f & RI_MOUSE_BUTTON_5_UP)
                acc->onMouseButton(MBTN_X2, false);
            if (f & RI_MOUSE_WHEEL) {
                wheelRemainder += static_cast<SHORT>(m.usButtonData);
                int n = wheelRemainder / WHEEL_DELTA;
                wheelRemainder -= n * WHEEL_DELTA;
                if (n)
                    acc->onWheel(n);
            }
        } else if (ri->header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& k = ri->data.keyboard;
            if (k.MakeCode == 0 || k.MakeCode == 0xFF)
                return; // fake / overrun keys
            bool e0 = (k.Flags & RI_KEY_E0) != 0;
            if (e0 && (k.MakeCode == 0x2A || k.MakeCode == 0x36))
                return; // fake shift sent with some E0 keys
            int dik = (k.MakeCode & 0x7F) | (e0 ? 0x80 : 0);
            acc->onKey(dik, !(k.Flags & RI_KEY_BREAK));
        }
    }

    void registerDevices(HWND target, DWORD flags) {
        RAWINPUTDEVICE d[2] = {};
        d[0].usUsagePage = 0x01;
        d[0].usUsage = 0x02;
        d[0].dwFlags = flags;
        d[0].hwndTarget = target; // mouse
        d[1].usUsagePage = 0x01;
        d[1].usUsage = 0x06;
        d[1].dwFlags = flags;
        d[1].hwndTarget = target; // keyboard
        RegisterRawInputDevices(d, 2, sizeof(RAWINPUTDEVICE));
    }

    static LRESULT CALLBACK ownProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_INPUT && self)
            self->onRaw(reinterpret_cast<HRAWINPUT>(l));
        return DefWindowProcW(h, msg, w, l);
    }

    void ownThread() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = &Impl::ownProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DrakanInputSink";
        RegisterClassW(&wc);
        ownWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (ownWnd) {
            SetWindowLongPtrW(ownWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
            registerDevices(ownWnd, RIDEV_INPUTSINK);
        }
        running = true;
        MSG msg;
        while (running && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (ownWnd)
            DestroyWindow(ownWnd);
        ownWnd = nullptr;
    }
};

HostInput::HostInput() : p_(std::make_unique<Impl>()) {}
HostInput::~HostInput() {
    shutdown();
}

bool HostInput::init(void* hwnd, InputAccumulator* acc) {
    p_->acc = acc;
    p_->hwnd = static_cast<HWND>(hwnd);
    if (p_->hwnd) {
        p_->registerDevices(p_->hwnd, 0);
    } else {
        p_->sink = true;
        p_->thr = std::thread([this] { p_->ownThread(); });
        for (int i = 0; i < 200 && !p_->running; ++i)
            Sleep(5);
        if (!p_->ownWnd)
            return false;
    }
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll"}) {
        p_->xinput = LoadLibraryW(dll);
        if (p_->xinput)
            break;
    }
    if (p_->xinput)
        p_->getState = reinterpret_cast<PFN_XInputGetState>(GetProcAddress(p_->xinput, "XInputGetState"));
    return true;
}

void HostInput::shutdown() {
    if (!p_)
        return;
    setCapture(false);
    if (p_->thr.joinable()) {
        p_->running = false;
        if (p_->ownWnd)
            PostMessageW(p_->ownWnd, WM_QUIT, 0, 0);
        p_->thr.join();
    }
    if (p_->xinput) {
        FreeLibrary(p_->xinput);
        p_->xinput = nullptr;
        p_->getState = nullptr;
    }
}

bool HostInput::handleMessage(void*, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result) {
    switch (msg) {
        case WM_INPUT:
            p_->onRaw(reinterpret_cast<HRAWINPUT>(lParam));
            return false;
        case WM_KILLFOCUS:
        case WM_CAPTURECHANGED:
            p_->acc->releaseAll();
            return false;
        case WM_ACTIVATEAPP:
            if (!wParam)
                p_->acc->releaseAll();
            return false;
        case WM_SETCURSOR:
            if (p_->captured && LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);
                if (result)
                    *result = TRUE;
                return true;
            }
            return false;
        default:
            return false;
    }
}

namespace {
float radial(float x, float y, float dz, float* ox, float* oy) {
    float m = std::sqrt(x * x + y * y);
    if (m <= dz) {
        *ox = *oy = 0;
        return 0;
    }
    float s = std::min(1.0f, (m - dz) / (1.0f - dz)) / m;
    *ox = x * s;
    *oy = y * s;
    return m;
}
} // namespace

void HostInput::poll(FrameInput& in, bool gamepadEnabled, float dz) {
    bool foc = p_->processFocus();
    if (p_->wasFocused && !foc)
        p_->acc->releaseAll();
    p_->wasFocused = foc;
    in.focused = foc;
    in.clientW = in.clientH = 0;
    if (HWND t = p_->target()) {
        POINT pt;
        RECT rc;
        if (GetCursorPos(&pt) && ScreenToClient(t, &pt) && GetClientRect(t, &rc)) {
            in.cursorX = pt.x;
            in.cursorY = pt.y;
            in.clientW = rc.right - rc.left;
            in.clientH = rc.bottom - rc.top;
        }
    }
    in.gamepad = GamepadState{};
    if (gamepadEnabled && p_->getState) {
        XINPUT_STATE st = {};
        DWORD r = ERROR_DEVICE_NOT_CONNECTED;
        for (DWORD u = 0; u < 4 && r != ERROR_SUCCESS; ++u) {
            DWORD user = (p_->xinputUser + u) & 3;
            r = p_->getState(user, &st);
            if (r == ERROR_SUCCESS)
                p_->xinputUser = user;
        }
        if (r == ERROR_SUCCESS) {
            GamepadState& g = in.gamepad;
            g.connected = true;
            WORD b = st.Gamepad.wButtons;
            auto m = [&](WORD x, uint16_t d) {
                if (b & x)
                    g.buttons |= d;
            };
            m(XINPUT_GAMEPAD_A, Pad::CROSS);
            m(XINPUT_GAMEPAD_B, Pad::CIRCLE);
            m(XINPUT_GAMEPAD_X, Pad::SQUARE);
            m(XINPUT_GAMEPAD_Y, Pad::TRIANGLE);
            m(XINPUT_GAMEPAD_LEFT_SHOULDER, Pad::L1);
            m(XINPUT_GAMEPAD_RIGHT_SHOULDER, Pad::R1);
            m(XINPUT_GAMEPAD_BACK, Pad::SELECT);
            m(XINPUT_GAMEPAD_START, Pad::START);
            m(XINPUT_GAMEPAD_LEFT_THUMB, Pad::L3);
            m(XINPUT_GAMEPAD_RIGHT_THUMB, Pad::R3);
            m(XINPUT_GAMEPAD_DPAD_UP, Pad::UP);
            m(XINPUT_GAMEPAD_DPAD_DOWN, Pad::DOWN);
            m(XINPUT_GAMEPAD_DPAD_LEFT, Pad::LEFT);
            m(XINPUT_GAMEPAD_DPAD_RIGHT, Pad::RIGHT);
            if (st.Gamepad.bLeftTrigger > 30)
                g.buttons |= Pad::L2;
            if (st.Gamepad.bRightTrigger > 30)
                g.buttons |= Pad::R2;
            radial(st.Gamepad.sThumbLX / 32767.0f, st.Gamepad.sThumbLY / 32767.0f, dz, &g.lx, &g.ly);
            radial(st.Gamepad.sThumbRX / 32767.0f, st.Gamepad.sThumbRY / 32767.0f, dz, &g.rx, &g.ry);
        }
    }
}

void HostInput::setCapture(bool want) {
    if (want) {
        if (HWND t = p_->target()) {
            RECT rc;
            POINT tl{0, 0};
            if (GetClientRect(t, &rc) && ClientToScreen(t, &tl)) {
                RECT clip{tl.x, tl.y, tl.x + rc.right, tl.y + rc.bottom};
                ClipCursor(&clip);
            }
        }
        if (!p_->cursorHidden) {
            g_cursorHidden = true;
            p_->cursorHidden = true;
        }
    } else {
        if (p_->captured)
            ClipCursor(nullptr);
        if (p_->cursorHidden) {
            g_cursorHidden = false;
            p_->cursorHidden = false;
        }
    }
    p_->captured = want;
}

bool HostInput::captured() const {
    return p_->captured;
}

void HostInput::setMenuCursorHidden(bool hidden) {
    g_menuCursorHidden = hidden;
}

void HostInput::setCaptureWindow(void* hwnd) {
    p_->capWnd = static_cast<HWND>(hwnd);
    if (p_->capWnd) {
        subclassOne(p_->capWnd);
        EnumChildWindows(p_->capWnd, &subChild, 0);
    }
}

bool screenshotWindowBmp(void* hwndv, const char* path) {
    HWND hwnd = static_cast<HWND>(hwndv);
    RECT rc;
    POINT tl{0, 0};
    if (!hwnd || !GetClientRect(hwnd, &rc) || !ClientToScreen(hwnd, &tl))
        return false;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return false;
    HDC scr = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(scr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h; // bottom-up
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = BitBlt(mem, 0, 0, w, h, scr, tl.x, tl.y, SRCCOPY | CAPTUREBLT) != 0;
        SelectObject(mem, old);
        if (ok) {
            size_t stride = (static_cast<size_t>(w) * 3 + 3) & ~static_cast<size_t>(3);
            BITMAPFILEHEADER fh = {};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
            fh.bfSize = static_cast<DWORD>(fh.bfOffBits + stride * h);
            FILE* f = std::fopen(path, "wb");
            if (f) {
                std::fwrite(&fh, sizeof fh, 1, f);
                std::fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
                std::fwrite(bits, stride * h, 1, f);
                std::fclose(f);
            } else
                ok = false;
        }
    }
    if (bmp)
        DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, scr);
    return ok;
}

bool windowClientSize(void* hwndv, int* width, int* height) {
    RECT rc{};
    if (!hwndv || !width || !height || !GetClientRect(static_cast<HWND>(hwndv), &rc) || rc.right <= rc.left ||
        rc.bottom <= rc.top)
        return false;
    *width = rc.right - rc.left;
    *height = rc.bottom - rc.top;
    return true;
}

void makeWindowTopmost(void* hwndv) {
    HWND hwnd = static_cast<HWND>(hwndv);
    if (hwnd)
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace drakan_input

#else // non-Windows stub

namespace drakan_input {
struct HostInput::Impl {
    bool cap = false;
};
std::string exeDirectory() {
    return "";
}
bool windowClientSize(void*, int*, int*) {
    return false;
}
bool screenshotWindowBmp(void*, const char*) {
    return false;
}
void makeWindowTopmost(void*) {}
HostInput::HostInput() : p_(std::make_unique<Impl>()) {}
HostInput::~HostInput() = default;
bool HostInput::init(void*, InputAccumulator*) {
    return false;
}
void HostInput::shutdown() {}
bool HostInput::handleMessage(void*, unsigned, uintptr_t, intptr_t, intptr_t*) {
    return false;
}
void HostInput::poll(FrameInput&, bool, float) {}
void HostInput::setCapture(bool c) {
    p_->cap = c;
}
void HostInput::setCaptureWindow(void*) {}
void HostInput::setMenuCursorHidden(bool) {}
bool HostInput::captured() const {
    return p_->cap;
}
} // namespace drakan_input

#endif
