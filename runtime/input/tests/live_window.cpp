// Interactive test: opens a window, captures mouse (Esc / F10 toggles), prints the live pad sample every frame.
// Not run in CI.  Usage: input_live [look_mode=stick]
#include "input/DrakanInput.h"
#include <windows.h>
#include <cstdio>
#include <string>

using namespace drakan_input;

static InputModule* g_mod = nullptr;
static std::string g_line1, g_line2, g_line3;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_mod) {
        intptr_t res = 0;
        if (g_mod->onWindowMessage(h, m, w, l, &res))
            return res;
    }
    switch (m) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            SetBkMode(dc, TRANSPARENT);
            TextOutA(dc, 10, 10, g_line1.c_str(), (int)g_line1.size());
            TextOutA(dc, 10, 30, g_line2.c_str(), (int)g_line2.size());
            TextOutA(dc, 10, 50, g_line3.c_str(), (int)g_line3.size());
            TextOutA(dc, 10, 90, "Esc / F10: toggle capture.  WASD, mouse, wheel.  Close window to quit.", 70);
            EndPaint(h, &ps);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

int main(int argc, char** argv) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DrakanInputLive";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND h = CreateWindowW(wc.lpszClassName, L"Drakan input live test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 700,
                           200, nullptr, nullptr, wc.hInstance, nullptr);

    InputModule mod;
    g_mod = &mod;
    std::string ini = "drakan_input_live.ini";
    if (!mod.init(h, ini))
        std::printf("raw input init failed\n");
    if (argc > 1 && std::string(argv[1]) == "look_mode=stick")
        mod.mutableConfig().lookMode = LookMode::Stick;

    MSG msg;
    bool run = true;
    while (run) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                run = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        mod.beginFrame();
        uint8_t buf[32];
        mod.fillPadSample(0, 0, buf);
        const FrameInput& f = mod.frame();
        char a[256], b[256], c[256];
        std::snprintf(a, sizeof a, "pad: %02x %02x btn(low)=%02x%02x  rx=%02x ry=%02x lx=%02x ly=%02x", buf[0], buf[1],
                      buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        std::snprintf(b, sizeof b, "mouse: dx=%d dy=%d wheel=%d btn=%02x captured=%d focused=%d", f.dx, f.dy, f.wheel,
                      f.mouseDown, f.captured, f.focused);
        std::snprintf(c, sizeof c, "xinput: %s buttons=%04x L=(%.2f,%.2f) R=(%.2f,%.2f)",
                      f.gamepad.connected ? "yes" : "no", f.gamepad.buttons, f.gamepad.lx, f.gamepad.ly, f.gamepad.rx,
                      f.gamepad.ry);
        g_line1 = a;
        g_line2 = b;
        g_line3 = c;
        static int n = 0;
        if (++n % 15 == 0) {
            InvalidateRect(h, nullptr, TRUE);
        }
        if (f.dx || f.dy || f.wheel)
            std::printf("%s | %s\n", a, b);
        Sleep(16);
    }
    mod.shutdown();
    return 0;
}
