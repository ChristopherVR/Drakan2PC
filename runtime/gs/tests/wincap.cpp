// wincap: captures the composed client area of the game window (what the user sees, via the screen DC) as PNG(s) and
// prints the mean luma of every capture, to spot flashes / flicker.
//   wincap.exe <title-substring> <out-prefix> [count=1] [interval-ms=0] [delay-ms=0]
// Writes <out-prefix>.png (count == 1) or <out-prefix>_NNN.png.
#include "gsr/gsr.h"

#include <windows.h>
#include <shellscalingapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Find {
    std::string needle;
    HWND hwnd = nullptr;
};
static BOOL CALLBACK enumProc(HWND h, LPARAM l) {
    Find* f = (Find*)l;
    char t[256] = {};
    GetWindowTextA(h, t, sizeof t);
    if (IsWindowVisible(h) && std::strstr(t, f->needle.c_str()) && !f->hwnd) {
        // optional WINCAP_PATH=<substring of the exe path> picks the window of one specific build
        const char* want = std::getenv("WINCAP_PATH");
        if (want && *want) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            char path[MAX_PATH] = {};
            DWORD n = MAX_PATH;
            if (ph) {
                QueryFullProcessImageNameA(ph, 0, path, &n);
                CloseHandle(ph);
            }
            if (!std::strstr(path, want))
                return TRUE;
        }
        f->hwnd = h;
    }
    return TRUE;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: wincap <title-substring> <out-prefix> [count] [interval-ms] [delay-ms]\n");
        return 1;
    }
    using Fn = BOOL(WINAPI*)(HANDLE);
    if (auto fn = (Fn)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)(intptr_t)-4);
    const int count = argc > 3 ? std::atoi(argv[3]) : 1;
    const int interval = argc > 4 ? std::atoi(argv[4]) : 0;
    const int delay = argc > 5 ? std::atoi(argv[5]) : 0;
    Sleep(delay);
    Find f{argv[1]};
    EnumWindows(enumProc, (LPARAM)&f);
    if (!f.hwnd) {
        std::printf("window '%s' not found\n", argv[1]);
        return 2;
    }
    for (int n = 0; n < count; ++n) {
        RECT rc{};
        GetClientRect(f.hwnd, &rc);
        POINT p{0, 0};
        ClientToScreen(f.hwnd, &p);
        const int w = rc.right, h = rc.bottom;
        HDC sdc = GetDC(nullptr);
        HDC mdc = CreateCompatibleDC(sdc);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mdc, bmp);
        BitBlt(mdc, 0, 0, w, h, sdc, p.x, p.y, SRCCOPY | CAPTUREBLT);
        gsr::Image img;
        img.width = w;
        img.height = h;
        img.rgba.resize((size_t)w * h * 4);
        const uint8_t* src = (const uint8_t*)bits;
        double luma = 0;
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            img.rgba[i * 4 + 0] = src[i * 4 + 2];
            img.rgba[i * 4 + 1] = src[i * 4 + 1];
            img.rgba[i * 4 + 2] = src[i * 4 + 0];
            img.rgba[i * 4 + 3] = 255;
            luma += 0.299 * src[i * 4 + 2] + 0.587 * src[i * 4 + 1] + 0.114 * src[i * 4];
        }
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        char name[512];
        if (count == 1)
            std::snprintf(name, sizeof name, "%s.png", argv[2]);
        else
            std::snprintf(name, sizeof name, "%s_%03d.png", argv[2], n);
        gsr::writePng(name, img);
        std::printf("%03d %dx%d meanLuma=%.2f %s\n", n, w, h, luma / ((double)w * h), name);
        if (n + 1 < count && interval > 0)
            Sleep(interval);
    }
    return 0;
}
