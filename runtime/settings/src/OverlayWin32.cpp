// SettingsOverlay's Win32 window platform: the window subclass, imgui_impl_win32, F1 and the XInput gamepad combo.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "OverlayImpl.h"

#include <windows.h>
#include <commctrl.h>

#include <cstdio>

#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#pragma comment(lib, "comctl32.lib")

namespace dsettings {

namespace {

// ---- minimal dynamic XInput (so there is no link dependency and the module also works on systems without it) ----
struct XPad {
    WORD buttons;
    BYTE lt, rt;
    SHORT lx, ly, rx, ry;
};
struct XState {
    DWORD packet;
    XPad pad;
};
typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XState*);

// A key / button press message stamped (GetMessageTime, the GetTickCount clock) at or before the moment the screen
// layer took the input. Equal ticks count as before: the clock steps in ~16 ms and the opening key's raw input reaches
// the game within a frame or two, so a press stamped in the same tick as the take-over is the opening press.
bool postedBeforeScreenInput(UINT msg, uint32_t posted, uint32_t since) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            return static_cast<int32_t>(posted - since) <= 0;
        default:
            return false;
    }
}

constexpr UINT_PTR kSubclassId = 0x44534554; // 'DSET'

class Win32Platform final : public OverlayPlatform {
public:
    ~Win32Platform() override {
        if (subclassed_)
            RemoveWindowSubclass(subclassed_, &SubclassProc, kSubclassId);
        if (xinput_)
            FreeLibrary(xinput_);
    }

    bool init(SettingsOverlay& overlay, SettingsOverlay::Impl& impl) override {
        overlay_ = &overlay;
        m_ = &impl;
        hwnd_ = static_cast<HWND>(impl.init.hwnd);
        if (!ImGui_ImplWin32_Init(hwnd_))
            return false;
        up_ = true;
        if (impl.init.installSubclass) {
            InitCommonControls();
            if (SetWindowSubclass(hwnd_, &SubclassProc, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
                subclassed_ = hwnd_;
        }
        return true;
    }
    void shutdown() override {
        if (subclassed_)
            RemoveWindowSubclass(subclassed_, &SubclassProc, kSubclassId);
        subclassed_ = nullptr;
        if (up_)
            ImGui_ImplWin32_Shutdown();
        up_ = false;
    }

    void poll() override { pollGamepadCombo(); }

    void newFrame(uint32_t w, uint32_t h) override {
        drainDeferred();
        ImGui_ImplWin32_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
        // the back buffer can differ from the client area (DPI, letterboxed child windows): map the pointer
        RECT rc{};
        if (GetClientRect(hwnd_, &rc) && rc.right > 0 && rc.bottom > 0 &&
            (static_cast<uint32_t>(rc.right) != w || static_cast<uint32_t>(rc.bottom) != h)) {
            POINT pt{};
            if (GetCursorPos(&pt) && ScreenToClient(hwnd_, &pt))
                io.AddMousePosEvent(static_cast<float>(pt.x) * static_cast<float>(w) / static_cast<float>(rc.right),
                                    static_cast<float>(pt.y) * static_cast<float>(h) / static_cast<float>(rc.bottom));
        }
    }

    bool handleMessage(void* window, unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result) override {
        HWND hwnd = static_cast<HWND>(window);
        if (result)
            *result = 0;
        const bool isOpen = m_->menu->isOpen();

        // hotkey (open and closed)
        if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && static_cast<int>(wp) == m_->init.hotkeyVk) {
            if (!(lp & (1 << 30))) // ignore auto repeat
                m_->hotkey(*overlay_);
            return true;
        }
        if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && static_cast<int>(wp) == m_->init.hotkeyVk)
            return true;
        if (!isOpen && !m_->screenInput.load())
            return false;
        if (!isOpen &&
            postedBeforeScreenInput(msg, static_cast<uint32_t>(GetMessageTime()), m_->screenInputSince.load())) {
            static int logged = 0;
            if (logged++ < 8)
                std::fprintf(stderr,
                             "[overlay] screen layer: msg 0x%x (key 0x%x) posted before it took the input, dropped\n",
                             msg, static_cast<unsigned>(wp));
            return true; // a press that started before the screen took over (the game had it through raw input)
        }

        { // never block the window thread on the frame lock (a frame can wait on the window thread, e.g. a window
          // resize): defer instead
            std::unique_lock<std::recursive_mutex> lk(g_overlayImguiMutex, std::try_to_lock);
            if (lk.owns_lock()) {
                ImGuiContext* prev = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(m_->ctx);
                drainDeferred();
                feed(hwnd, msg, wp, lp);
                ImGui::SetCurrentContext(prev);
            } else {
                std::lock_guard<std::mutex> dl(deferM_);
                if (deferred_.size() < 4096)
                    deferred_.push_back({hwnd, msg, wp, lp});
            }
        }
        switch (msg) {
            // Only "down" / text / wheel messages are consumed. Releases and mouse moves still reach the game window
            // so that a key or button held while the menu opens does not stay "down" in the host framework
            // afterwards.
            case WM_SYSKEYDOWN:
                if (static_cast<int>(wp) == VK_F4)
                    return false; // Alt+F4 still closes the game
                return true;
            case WM_KEYDOWN:
            case WM_CHAR:
            case WM_SYSCHAR:
            case WM_UNICHAR:
            case WM_DEADCHAR:
            case WM_SYSDEADCHAR:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                return true;
            case WM_SETCURSOR:
                if (LOWORD(lp) == HTCLIENT) {
                    SetCursor(nullptr); // the overlay draws its own cursor
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    bool pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const override {
        RECT rc{};
        POINT pt{};
        if (!hwnd_ || !GetCursorPos(&pt) || !ScreenToClient(hwnd_, &pt) || !GetClientRect(hwnd_, &rc) ||
            rc.right <= 0 || rc.bottom <= 0)
            return false;
        if (pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom)
            return false;
        *x = static_cast<float>(pt.x) * static_cast<float>(w) / static_cast<float>(rc.right);
        *y = static_cast<float>(pt.y) * static_cast<float>(h) / static_cast<float>(rc.bottom);
        return true;
    }

    uint32_t nowMs() const override { return static_cast<uint32_t>(GetTickCount()); }

private:
    struct Deferred {
        HWND h;
        unsigned msg;
        uintptr_t wp;
        intptr_t lp;
    };

    static LRESULT CALLBACK SubclassProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
        auto* self = reinterpret_cast<Win32Platform*>(ref);
        intptr_t res = 0;
        if (self->overlay_->handleMessage(h, m, w, l, &res))
            return static_cast<LRESULT>(res);
        if (m == WM_NCDESTROY) {
            RemoveWindowSubclass(h, SubclassProc, id);
            self->subclassed_ = nullptr;
        }
        return DefSubclassProc(h, m, w, l);
    }

    // Feeds one window message to the Win32 backend (frame lock held).
    void feed(HWND hwnd, unsigned msg, uintptr_t wp, intptr_t lp) {
        // Mouse messages carry their own position (client pixels). Queue it ahead of the button so the click lands
        // where the cursor WAS when the button went down, not where the per-frame polling happens to read it (stale
        // hover), mapped to the back buffer like the render path.
        const bool isButton = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
                              msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_RBUTTONDBLCLK ||
                              msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_MBUTTONDBLCLK ||
                              msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP || msg == WM_XBUTTONDBLCLK;
        auto msgPos = [&](float* x, float* y) {
            RECT rc{};
            const uint32_t bw = m_->lastW.load(), bh = m_->lastH.load();
            float px = static_cast<float>(static_cast<short>(LOWORD(lp))),
                  py = static_cast<float>(static_cast<short>(HIWORD(lp)));
            if (bw && bh && GetClientRect(hwnd_, &rc) && rc.right > 0 && rc.bottom > 0 &&
                (static_cast<uint32_t>(rc.right) != bw || static_cast<uint32_t>(rc.bottom) != bh)) {
                px = px * static_cast<float>(bw) / static_cast<float>(rc.right);
                py = py * static_cast<float>(bh) / static_cast<float>(rc.bottom);
            }
            *x = px;
            *y = py;
        };
        if (isButton && hwnd == hwnd_) {
            float px, py;
            msgPos(&px, &py);
            ImGui::GetIO().AddMousePosEvent(px, py);
        }
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, static_cast<WPARAM>(wp), static_cast<LPARAM>(lp));
        if (msg == WM_MOUSEMOVE && hwnd == hwnd_) { // the backend queued the unscaled client position: replace it
            float px, py;
            msgPos(&px, &py);
            ImGui::GetIO().AddMousePosEvent(px, py);
        }
    }
    void drainDeferred() { // frame lock held
        std::vector<Deferred> d;
        {
            std::lock_guard<std::mutex> l(deferM_);
            d.swap(deferred_);
        }
        for (const Deferred& e : d)
            feed(e.h, e.msg, e.wp, e.lp);
    }

    void pollGamepadCombo() {
        if (!m_->init.enableGamepadCombo)
            return;
        if (!xGet_) {
            if (!xinput_) {
                const wchar_t* names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
                for (auto n : names)
                    if ((xinput_ = LoadLibraryW(n)))
                        break;
                if (!xinput_) {
                    m_->init.enableGamepadCombo = false;
                    return;
                }
            }
            xGet_ = reinterpret_cast<XInputGetStateFn>(GetProcAddress(xinput_, "XInputGetState"));
            if (!xGet_) {
                m_->init.enableGamepadCombo = false;
                return;
            }
        }
        bool combo = false;
        for (DWORD i = 0; i < 4 && !combo; ++i) {
            XState st{};
            if (xGet_(i, &st) == ERROR_SUCCESS) {
                const WORD need = 0x0020 /*BACK*/ | 0x0100 /*LB*/ | 0x0200 /*RB*/;
                combo = (st.pad.buttons & need) == need;
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (!combo) {
            comboSince_ = 0;
            comboFired_ = false;
            return;
        }
        if (!comboSince_)
            comboSince_ = now;
        if (!comboFired_ && now - comboSince_ >= 500) {
            comboFired_ = true;
            m_->hotkey(*overlay_);
        }
    }

    SettingsOverlay* overlay_ = nullptr;
    SettingsOverlay::Impl* m_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND subclassed_ = nullptr;
    bool up_ = false;
    std::mutex deferM_;
    std::vector<Deferred> deferred_;
    HMODULE xinput_ = nullptr;
    XInputGetStateFn xGet_ = nullptr;
    ULONGLONG comboSince_ = 0;
    bool comboFired_ = false;
};

} // namespace

std::unique_ptr<OverlayPlatform> createOverlayPlatform() {
    return std::make_unique<Win32Platform>();
}

} // namespace dsettings
