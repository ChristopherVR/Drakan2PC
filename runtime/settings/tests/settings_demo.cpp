// settings_demo: renders the settings overlay over a dummy D3D11 "game" with a fake host.
//   settings_demo.exe                       interactive window (F1 toggles the menu, Esc closes it, Alt+F4 quits)
//   settings_demo.exe --screenshots <dir>   self test + one PNG per page (settings_*.png), exit code 0 when everything
//   passed
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <crtdbg.h>
#include <dbghelp.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "FakeHost.h"
#include "imgui.h"
#include "settings/Bindings.h"
#include "settings/Overlay.h"

using Microsoft::WRL::ComPtr;
using namespace dsettings;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    std::printf("CRASH code=%08lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);
    void* frames[40];
    const USHORT n = CaptureStackBackTrace(0, 40, frames, nullptr);
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    for (USHORT i = 0; i < n; ++i) {
        std::memset(buf, 0, sizeof buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 d = 0;
        if (SymFromAddr(proc, reinterpret_cast<DWORD64>(frames[i]), &d, sym))
            std::printf("  %2u %s +0x%llx\n", i, sym->Name, static_cast<unsigned long long>(d));
        else
            std::printf("  %2u %p\n", i, frames[i]);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

struct Gfx {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain> sc;
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;
    UINT w = 0, h = 0;
    bool offscreen = true;
};

static bool createGfx(Gfx& g, HWND hwnd, UINT w, UINT h, bool swapchain) {
    g.w = w;
    g.h = h;
    g.offscreen = !swapchain;
    const D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr;
    if (swapchain) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = w;
        sd.BufferDesc.Height = h;
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &sd,
                                           &g.sc, &g.dev, nullptr, &g.ctx);
        if (FAILED(hr))
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &sd,
                                               &g.sc, &g.dev, nullptr, &g.ctx);
        if (FAILED(hr))
            return false;
        g.sc->GetBuffer(0, IID_PPV_ARGS(&g.tex));
    } else {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &g.dev, nullptr,
                               &g.ctx);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &g.dev, nullptr,
                                   &g.ctx);
        if (FAILED(hr))
            return false;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(g.dev->CreateTexture2D(&td, nullptr, &g.tex)))
            return false;
    }
    return SUCCEEDED(g.dev->CreateRenderTargetView(g.tex.Get(), nullptr, &g.rtv));
}

// A dusk landscape made of clear rectangles: stands in for the game image.
static void drawFakeScene(Gfx& g) {
    ComPtr<ID3D11DeviceContext1> c1;
    g.ctx.As(&c1);
    const float black[4] = {0, 0, 0, 1};
    g.ctx->ClearRenderTargetView(g.rtv.Get(), black);
    if (!c1)
        return;
    const int H = static_cast<int>(g.h), W = static_cast<int>(g.w);
    const int horizon = H * 62 / 100;
    for (int i = 0; i < 48; ++i) {
        const float t = i / 47.0f;
        const float col[4] = {0.07f + 0.80f * t * t, 0.10f + 0.40f * t, 0.30f - 0.05f * t + 0.12f * (1 - t) * 0.0f,
                              1.0f};
        D3D11_RECT r{0, horizon * i / 48, W, horizon * (i + 1) / 48 + 1};
        c1->ClearView(g.rtv.Get(), col, &r, 1);
    }
    // mountains: stepped silhouettes
    struct Peak {
        float cx, w, h;
        float shade;
    };
    const Peak peaks[] = {{0.12f, 0.30f, 0.22f, 0.10f},
                          {0.38f, 0.38f, 0.30f, 0.07f},
                          {0.70f, 0.34f, 0.26f, 0.09f},
                          {0.92f, 0.26f, 0.18f, 0.12f}};
    for (const Peak& p : peaks) {
        const int steps = 40;
        for (int s = 0; s < steps; ++s) {
            const float f = (s + 0.5f) / steps; // 0 top .. 1 base
            const float halfW = p.w * 0.5f * W * f;
            D3D11_RECT r{static_cast<LONG>(p.cx * W - halfW), static_cast<LONG>(horizon - p.h * H * (1 - f)),
                         static_cast<LONG>(p.cx * W + halfW),
                         static_cast<LONG>(horizon - p.h * H * (1 - (s + 1.0f) / steps) + 1)};
            const float col[4] = {p.shade, p.shade * 1.1f, p.shade * 1.6f, 1.0f};
            r.top = static_cast<LONG>(horizon - p.h * H * (1 - f));
            r.bottom = r.top + static_cast<LONG>(p.h * H / steps) + 1;
            c1->ClearView(g.rtv.Get(), col, &r, 1);
        }
    }
    for (int i = 0; i < 24; ++i) {
        const float t = i / 23.0f;
        const float col[4] = {0.05f + 0.06f * (1 - t), 0.12f + 0.10f * (1 - t), 0.05f, 1.0f};
        D3D11_RECT r{0, horizon + (H - horizon) * i / 24, W, horizon + (H - horizon) * (i + 1) / 24 + 1};
        c1->ClearView(g.rtv.Get(), col, &r, 1);
    }
    // sun
    for (int i = 0; i < 14; ++i) {
        const int rad = 46 * H / 900, y = horizon - rad * 2 + i * rad * 2 / 14;
        const float dy = (i + 0.5f) / 14 * 2 - 1;
        const int hw = static_cast<int>(std::sqrt(std::max(0.0f, 1 - dy * dy)) * rad);
        const float col[4] = {1.0f, 0.78f, 0.35f, 1.0f};
        D3D11_RECT r{W * 55 / 100 - hw, y, W * 55 / 100 + hw, y + rad * 2 / 14 + 1};
        c1->ClearView(g.rtv.Get(), col, &r, 1);
    }
}

static bool readback(Gfx& g, std::vector<uint8_t>& px) {
    D3D11_TEXTURE2D_DESC td{};
    g.tex->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(g.dev->CreateTexture2D(&td, nullptr, &st)))
        return false;
    g.ctx->CopyResource(st.Get(), g.tex.Get());
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(g.ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &ms)))
        return false;
    px.resize(static_cast<size_t>(g.w) * g.h * 4);
    for (UINT y = 0; y < g.h; ++y)
        std::memcpy(px.data() + static_cast<size_t>(y) * g.w * 4,
                    static_cast<const uint8_t*>(ms.pData) + static_cast<size_t>(y) * ms.RowPitch, g.w * 4);
    g.ctx->Unmap(st.Get(), 0);
    return true;
}

static bool savePng(const std::wstring& path, const std::vector<uint8_t>& bgra, UINT w, UINT h) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))))
        return false;
    ComPtr<IWICStream> stream;
    if (FAILED(f->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
        return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return false;
    ComPtr<IWICBitmapFrameEncode> fr;
    if (FAILED(enc->CreateNewFrame(&fr, nullptr)) || FAILED(fr->Initialize(nullptr)))
        return false;
    fr->SetSize(w, h);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    fr->SetPixelFormat(&pf);
    if (FAILED(fr->WritePixels(h, w * 4, w * h * 4, const_cast<BYTE*>(bgra.data()))))
        return false;
    return SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}

static size_t distinctColours(const std::vector<uint8_t>& px) {
    std::set<uint32_t> s;
    const uint32_t* p = reinterpret_cast<const uint32_t*>(px.data());
    for (size_t i = 0; i < px.size() / 4 && s.size() < 5000; i += 97)
        s.insert(p[i]);
    return s.size();
}

static const char* kSeedDrakan =
    "; drakan.ini - Drakan port settings. Environment variables (named in the comments) override these values.\r\n"
    "[general]\r\nskip_memcard_warning = 1\r\n\r\n"
    "[video]\r\nrenderer = d3d11\r\nscale = auto\r\naspect = 4:3\r\nwindow_width = 1280\r\nwindow_height = "
    "960\r\nfullscreen = 0\r\nvsync = 1\r\n"
    "filter = game\r\ndeinterlace = weave\r\nflicker_filter = 0\r\nsharp_present = 1\r\n\r\n"
    "[audio]\r\nenabled = 1\r\nvolume = 100\r\ndevice = default\r\n";

static void writeFile(const fs::path& p, const std::string& t) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << t;
}
static std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}

struct Rig {
    HWND hwnd = nullptr;
    Gfx g;
    FakeHost host;
    std::unique_ptr<SettingsOverlay> ov;
    fs::path dir;

    bool init(const fs::path& d, UINT w, UINT h, bool seed = true) {
        dir = d;
        fs::create_directories(dir);
        if (seed) {
            writeFile(dir / "drakan.ini", kSeedDrakan);
            writeFile(dir / "drakan_input.ini", builtinDefaultInputIni());
        }
        if (!createGfx(g, hwnd, w, h, false))
            return false;
        OverlayInit oi;
        oi.hwnd = hwnd;
        oi.device = g.dev.Get();
        oi.context = g.ctx.Get();
        oi.host = &host;
        oi.paths.drakanIni = (dir / "drakan.ini").string();
        oi.paths.inputIni = (dir / "drakan_input.ini").string();
        oi.installSubclass = false;
        oi.enableGamepadCombo = false;
        ov = SettingsOverlay::create(oi);
        if (ov)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(
                ov->imguiContext())); // the demo drives ImGui directly (injects input, test hooks)
        return ov != nullptr;
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) {
            drawFakeScene(g);
            ov->render(g.rtv.Get(), g.w, g.h);
            g.ctx->Flush();
            Sleep(8);
        }
    }
    bool shot(const fs::path& out) {
        frames(5);
        std::vector<uint8_t> px;
        if (!readback(g, px))
            return false;
        const size_t cols = distinctColours(px);
        CHECK(cols > 200);
        const bool ok = savePng(out.wstring(), px, g.w, g.h);
        std::printf("screenshot %s (%zu colours) %s\n", out.string().c_str(), cols, ok ? "ok" : "FAILED");
        CHECK(ok);
        return ok;
    }
    ImGuiContext* ctx() { return static_cast<ImGuiContext*>(ov->imguiContext()); }
};

static void keyTap(Rig& r, ImGuiKey k) {
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(r.ctx());
    ImGui::GetIO().AddKeyEvent(k, true);
    r.frames(2);
    ImGui::GetIO().AddKeyEvent(k, false);
    r.frames(2);
    ImGui::SetCurrentContext(prev);
}

static void selfTest(const fs::path& outDir) {
    std::printf("---- self test ----\n");
    Rig r;
    r.hwnd = CreateWindowExW(0, L"STATIC", L"settings_demo", WS_OVERLAPPEDWINDOW, 0, 0, 1600, 900, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!r.init(outDir / "selftest_ini", 1600, 900)) {
        std::printf("cannot create device\n");
        ++g_fail;
        return;
    }
    SettingsMenu& m = r.ov->menu();
    CHECK(!r.ov->isOpen() && !r.ov->isOpenAtomic());
    r.frames(2);
    r.ov->open();
    CHECK(r.ov->isOpen() && r.ov->isOpenAtomic() && r.host.opened == 1);
    r.frames(4);

    // 1. key capture end to end: rebind Jump slot 0 to Z
    m.setPage(Page::Controls);
    r.frames(3);
    m.demoState(SettingsMenu::DemoState::Capture);
    r.frames(3);
    keyTap(r, ImGuiKey_Z);
    r.frames(2);
    CHECK((m.pending().controls.game.sourcesFor("TRIANGLE") == std::vector<std::string>{"Z"}));
    // 2. capture with a conflict: bind Jump to E (used by "Call Arokh / Use"), choose "use for both" by calling the
    // same path
    m.demoState(SettingsMenu::DemoState::Capture);
    r.frames(3);
    keyTap(r, ImGuiKey_E);
    r.frames(2);
    CHECK((m.pending().controls.game.sourcesFor("TRIANGLE") ==
           std::vector<std::string>{"Z"})); // conflict popup is waiting, nothing committed yet
    m.demoState(SettingsMenu::DemoState::None);
    r.frames(3);

    // 3. apply: video + audio + bindings
    Settings& p = m.pendingMut();
    p.video.vsync = false;
    p.video.frameLimit = 120;
    p.audio.volume = 70;
    p.controls.invertY = true;
    p.general.language = "german";
    m.applyPending(false);
    r.frames(3);
    CHECK(r.host.videoApplies == 1 && !r.host.lastVideo.vsync);
    CHECK(r.host.audioApplies == 1 && r.host.lastAudio.volume == 70);
    CHECK(r.host.reloads == 1);
    CHECK(r.host.regens == 1); // bindings changed, keyboard prompts on
    CHECK(r.host.generalApplies == 1);
    const std::string di = readFile(r.dir / "drakan.ini"), ii = readFile(r.dir / "drakan_input.ini");
    CHECK(di.find("vsync = 0") != std::string::npos && di.find("frame_limit = 120") != std::string::npos);
    CHECK(di.find("volume = 70") != std::string::npos && di.find("language = german") != std::string::npos);
    CHECK(ii.find("invert_y = 1") != std::string::npos && ii.find("Z = TRIANGLE") != std::string::npos &&
          ii.find("SPACE = TRIANGLE") == std::string::npos);
    CHECK(r.host.restarts == 0); // popup first, the user decides
    // 4. window mode change -> revert prompt, then revert path through the host
    p.video.windowMode = WindowMode::Borderless;
    m.applyPending(false);
    r.frames(3);
    CHECK(r.host.videoApplies == 2 && r.host.lastVideo.windowMode == WindowMode::Borderless);
    // 5. cancel closes without saving anything new
    r.ov->close();
    CHECK(!r.ov->isOpen() && !r.ov->isOpenAtomic() && r.host.closed == 1);
    // 6. re-open reloads the saved files
    r.ov->open();
    CHECK(m.pending().video.windowMode == WindowMode::Borderless && !m.pending().video.vsync);
    CHECK((m.pending().controls.game.sourcesFor("TRIANGLE") == std::vector<std::string>{"Z"}));
    r.ov->close();
    CHECK(r.host.opened == 2 && r.host.closed == 2);
    std::printf("---- self test done ----\n");
}

static void screenshots(const fs::path& outDir) {
    fs::create_directories(outDir);
    Rig r;
    r.hwnd = CreateWindowExW(0, L"STATIC", L"settings_demo", WS_OVERLAPPEDWINDOW, 0, 0, 1600, 900, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!r.init(outDir / "screenshot_ini", 1600, 900)) {
        std::printf("cannot create device\n");
        ++g_fail;
        return;
    }
    SettingsMenu& m = r.ov->menu();
    r.frames(2);
    r.ov->open();
    r.frames(4);
    r.shot(outDir / "settings_display.png");
    m.demoScrollTo(1000.0f);
    r.frames(4);
    r.shot(outDir / "settings_display_textures.png");
    m.demoScrollTo(0.0f);
    r.frames(2);
    m.demoScrollTo(-1.0f);

    m.setPage(Page::Audio);
    r.frames(3);
    r.shot(outDir / "settings_audio.png");

    m.setPage(Page::Controls);
    m.demoControlsTab(0);
    r.frames(4);
    r.shot(outDir / "settings_controls_game.png");
    // show a conflict: give Attack the Circle key as well, and rebind one slot to a reserved key
    m.pendingMut().controls.game.bind("CIRCLE", "MOUSE1");
    m.pendingMut().controls.game.bind("TRIANGLE", "F1");
    m.demoScrollTo(120.0f);
    r.frames(4);
    r.shot(outDir / "settings_controls_conflicts.png");
    m.demoScrollTo(-1.0f);
    m.demoState(SettingsMenu::DemoState::Capture);
    r.frames(4);
    r.shot(outDir / "settings_capture.png");
    m.demoState(SettingsMenu::DemoState::CaptureConflict);
    r.frames(4);
    r.shot(outDir / "settings_capture_conflict.png");
    m.demoState(SettingsMenu::DemoState::None);
    r.frames(3);
    m.demoControlsTab(1);
    r.frames(4);
    r.shot(outDir / "settings_controls_menu.png");
    m.demoControlsTab(2);
    r.frames(4);
    r.shot(outDir / "settings_controls_mouse.png");

    m.setPage(Page::General);
    r.frames(4);
    r.shot(outDir / "settings_general.png");

    m.setPage(Page::Display);
    m.demoState(SettingsMenu::DemoState::RestartPrompt);
    r.frames(5);
    r.shot(outDir / "settings_restart_prompt.png");
    // dismiss with Esc, then the revert prompt
    keyTap(r, ImGuiKey_Escape);
    m.demoState(SettingsMenu::DemoState::RevertPrompt);
    r.frames(5);
    r.shot(outDir / "settings_revert_prompt.png");
    r.ov.reset();

    // a second size: 1280x720 with the compact scale
    Rig s;
    s.hwnd = r.hwnd;
    if (s.init(outDir / "screenshot_ini2", 1280, 720)) {
        s.frames(2);
        s.ov->open();
        s.frames(4);
        s.shot(outDir / "settings_display_720p.png");
        s.ov->menu().setPage(Page::Controls);
        s.ov->menu().demoControlsTab(0);
        s.frames(4);
        s.shot(outDir / "settings_controls_720p.png");
    } else
        ++g_fail;
}

// Styled screenshots (needs DRAKAN_DEMO_TXD = INTERFCE.TXD extracted from the disc): r6_f1_demo_<page>_<size>.png in
// <dir>.
static void styleShots(const fs::path& outDir) {
    fs::create_directories(outDir);
    struct Sz {
        UINT w, h;
        const char* tag;
    };
    const Sz sizes[] = {{1920, 1080, "1080p"}, {3840, 2160, "4k"}, {1280, 720, "720p"}};
    for (const Sz& z : sizes) {
        Rig r;
        r.hwnd = CreateWindowExW(0, L"STATIC", L"settings_demo", WS_OVERLAPPEDWINDOW, 0, 0, z.w, z.h, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        if (!r.init(outDir / (std::string("style_ini_") + z.tag), z.w, z.h)) {
            std::printf("cannot create device\n");
            ++g_fail;
            return;
        }
        SettingsMenu& m = r.ov->menu();
        const std::string t = z.tag;
        auto shot = [&](const char* name) { r.shot(outDir / ("r6_f1_demo_" + std::string(name) + "_" + t + ".png")); };
        r.frames(2);
        r.ov->open();
        r.frames(6);
        CHECK(m.skinned()); // the art was found and uploaded
        shot("display");
        keyTap(r, ImGuiKey_DownArrow);
        keyTap(r, ImGuiKey_DownArrow);
        keyTap(r, ImGuiKey_DownArrow);
        r.frames(4);
        shot("display_focus");
        m.demoScrollTo(1000.0f);
        r.frames(4);
        shot("display_textures");
        m.demoScrollTo(-1.0f);
        m.setPage(Page::Audio);
        r.frames(4);
        shot("audio");
        m.setPage(Page::Controls);
        m.demoControlsTab(0);
        r.frames(4);
        shot("controls_game");
        m.demoControlsTab(1);
        r.frames(4);
        shot("controls_menu");
        m.demoControlsTab(2);
        r.frames(4);
        shot("controls_mouse");
        m.setPage(Page::General);
        r.frames(4);
        shot("general");
        m.setPage(Page::Controls);
        m.demoControlsTab(0);
        m.demoState(SettingsMenu::DemoState::Capture);
        r.frames(5);
        shot("capture");
        m.demoState(SettingsMenu::DemoState::None);
        r.frames(3);
        m.setPage(Page::Display);
        m.demoState(SettingsMenu::DemoState::RestartPrompt);
        r.frames(5);
        shot("restart");
        r.ov.reset();
    }
}

// ---- interactive ----
static LRESULT CALLBACK DemoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) { /* closes the menu only (handled by the overlay) */
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(h, m, w, l);
}

static int interactive() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DemoProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DrakanSettingsDemo";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rc{0, 0, 1280, 720};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd =
        CreateWindowW(wc.lpszClassName, L"Drakan settings demo - F1 opens the menu", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                      100, 80, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    Gfx g;
    if (!createGfx(g, hwnd, 1280, 720, true))
        return 2;
    FakeHost host;
    OverlayInit oi;
    oi.hwnd = hwnd;
    oi.device = g.dev.Get();
    oi.context = g.ctx.Get();
    oi.host = &host;
    fs::create_directories("settings_demo_data");
    oi.paths.drakanIni = "settings_demo_data/drakan.ini";
    oi.paths.inputIni = "settings_demo_data/drakan_input.ini";
    if (!fs::exists(oi.paths.drakanIni))
        writeFile(oi.paths.drakanIni, kSeedDrakan);
    auto ov = SettingsOverlay::create(oi);
    if (!ov)
        return 3;
    MSG msg{};
    bool run = true;
    while (run) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                run = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        RECT c{};
        GetClientRect(hwnd, &c);
        if (c.right > 0 && c.bottom > 0 && (static_cast<UINT>(c.right) != g.w || static_cast<UINT>(c.bottom) != g.h)) {
            g.ctx->OMSetRenderTargets(0, nullptr, nullptr);
            g.rtv.Reset();
            g.tex.Reset();
            g.sc->ResizeBuffers(0, c.right, c.bottom, DXGI_FORMAT_UNKNOWN, 0);
            g.sc->GetBuffer(0, IID_PPV_ARGS(&g.tex));
            g.dev->CreateRenderTargetView(g.tex.Get(), nullptr, &g.rtv);
            g.w = c.right;
            g.h = c.bottom;
        }
        drawFakeScene(g);
        ov->render(g.rtv.Get(), g.w, g.h);
        g.sc->Present(1, 0);
    }
    return 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    SetUnhandledExceptionFilter(CrashHandler);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetProcessDPIAware();
    if (argc >= 3 && std::strcmp(argv[1], "--styleshots") == 0) {
        styleShots(argv[2]);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--screenshots") == 0) {
        const fs::path out = argv[2];
        selfTest(out);
        screenshots(out);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    return interactive();
}
