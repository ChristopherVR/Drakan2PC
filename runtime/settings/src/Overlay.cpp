#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "settings/Overlay.h"

#include <windows.h>
#include <commctrl.h>
#include <d3d11.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <mutex>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
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

// ImGui is not thread safe and the overlay is driven from two threads: window messages arrive on the window (UI)
// thread, render() runs on the present thread. Every access to the overlay context (the event queue the Win32 backend
// fills from WM_* messages, NewFrame, the frame itself) is serialised by one recursive mutex; the scope also selects
// the context (SetCurrentContext is process global). Without it a press / release pair arriving while a frame was being
// built could be dropped or reordered, which showed up as "a click needs several tries".
std::recursive_mutex g_imguiM;
struct ImGuiCtxScope {
    ImGuiContext* prev;
    std::lock_guard<std::recursive_mutex> lock;
    explicit ImGuiCtxScope(ImGuiContext* c) : lock(g_imguiM), prev(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(c);
    }
    ~ImGuiCtxScope() { ImGui::SetCurrentContext(prev); }
};

// Blend states for the screen layer's draw-list callbacks (one overlay per process; created with its device).
ID3D11BlendState* g_blendAdditive = nullptr;
ID3D11BlendState* g_blendOpaque = nullptr;
void setBlend(ID3D11BlendState* state, float k = 0) {
    auto* rs = static_cast<ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (rs && rs->DeviceContext && state) {
        const float factor[4] = {k, k, k, k};
        rs->DeviceContext->OMSetBlendState(state, factor, 0xffffffffu);
    }
}
void blendAdditive(const ImDrawList*, const ImDrawCmd*) {
    setBlend(g_blendAdditive);
}
// The frame copy has no usable alpha: "opaque" crops blend by the constant blend factor, the screen's fade (callback
// data, a float; 1 without), so they fade with everything else instead of turning into dark rectangles.
void blendOpaque(const ImDrawList*, const ImDrawCmd* cmd) {
    float k = 1.0f;
    if (cmd && cmd->UserCallbackData && cmd->UserCallbackDataSize == sizeof(float))
        std::memcpy(&k, cmd->UserCallbackData, sizeof k);
    setBlend(g_blendOpaque, std::clamp(k, 0.0f, 1.0f));
}
void createBlendStates(ID3D11Device* dev) {
    if (g_blendAdditive || !dev)
        return;
    D3D11_BLEND_DESC d{};
    d.RenderTarget[0].BlendEnable = TRUE;
    d.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    d.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    d.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    dev->CreateBlendState(&d, &g_blendAdditive);
    d.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
    d.RenderTarget[0].DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
    dev->CreateBlendState(&d, &g_blendOpaque);
}

bool fileExists(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

struct SettingsOverlay::Impl {
    OverlayInit init;
    ImGuiContext* ctx = nullptr;
    std::unique_ptr<SettingsStore> store;
    std::unique_ptr<SettingsMenu> menu;
    UiFonts fonts;
    std::atomic<bool> open{false};
    std::function<void(bool)> openCb;
    HudLayer hud;
    HudLayer cursorLayer;
    HudLayer statsLayer;
    ScreenLayer screenLayer;
    std::atomic<bool> screenInput{false}; // the layer takes window input (render thread writes)
    // GetTickCount() when the layer took the input: key / button presses posted before it belong to whoever had the
    // input then (the key that opened the screen reaches the game through raw input on another thread first, and
    // its WM_KEYDOWN can be dispatched only after the layer took over).
    std::atomic<uint32_t> screenInputSince{0};
    ScreenFontSizes screenSizes; // sizes the screen fonts are baked at
    // Copy of the back buffer before the overlay draws (the screen layer's blur and item icon crops).
    ID3D11Texture2D* frameTex = nullptr;
    ID3D11ShaderResourceView* frameSrv = nullptr;
    D3D11_TEXTURE2D_DESC frameDesc{};
    bool copyFrame(ID3D11RenderTargetView* rtv) {
        ID3D11Resource* res = nullptr;
        rtv->GetResource(&res);
        if (!res)
            return false;
        ID3D11Texture2D* bb = nullptr;
        res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb));
        res->Release();
        if (!bb)
            return false;
        D3D11_TEXTURE2D_DESC d{};
        bb->GetDesc(&d);
        if (!frameTex || frameDesc.Width != d.Width || frameDesc.Height != d.Height || frameDesc.Format != d.Format) {
            if (frameSrv)
                frameSrv->Release();
            if (frameTex)
                frameTex->Release();
            frameSrv = nullptr;
            frameTex = nullptr;
            D3D11_TEXTURE2D_DESC c = d;
            c.MipLevels = 1;
            c.ArraySize = 1;
            c.SampleDesc.Count = 1;
            c.SampleDesc.Quality = 0;
            c.Usage = D3D11_USAGE_DEFAULT;
            c.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            c.CPUAccessFlags = 0;
            c.MiscFlags = 0;
            if (FAILED(init.device->CreateTexture2D(&c, nullptr, &frameTex)) || !frameTex ||
                FAILED(init.device->CreateShaderResourceView(frameTex, nullptr, &frameSrv))) {
                if (frameTex)
                    frameTex->Release();
                frameTex = nullptr;
                bb->Release();
                return false;
            }
            frameDesc = d;
        }
        if (d.SampleDesc.Count > 1)
            init.context->ResolveSubresource(frameTex, 0, bb, 0, d.Format);
        else
            init.context->CopyResource(frameTex, bb);
        bb->Release();
        return true;
    }
    // Rebuilds the font atlas (menu fonts, the game font when the art is loaded, the screen fonts at `sizes`).
    void rebuildFonts(const ScreenFontSizes& sizes) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        ImGui_ImplDX11_InvalidateDeviceObjects();
        GameSkin& sk = menu->skin();
        fonts = buildFonts(io, headingTtf, bodyTtf, sk.fetched() ? sk.fontImage() : nullptr, sizes);
        menu->setFonts(fonts);
        screenSizes = sizes;
    }
    std::function<bool()> openGate;
    std::function<bool()> allowGate; // false = F1 / the gamepad combo are ignored (cut scenes, movies, loading)
    // the game's art (loaded when the menu first opens with the art available)
    std::string headingTtf, bodyTtf;
    bool artDone = false;
    ULONGLONG artRetryAt = 0;
    std::vector<ID3D11ShaderResourceView*> srvs;
    ImTextureID uploadTexture(const GameImage& im) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(im.w);
        td.Height = static_cast<UINT>(im.h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = im.rgba.data();
        sd.SysMemPitch = static_cast<UINT>(im.w) * 4;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(init.device->CreateTexture2D(&td, &sd, &tex)) || !tex)
            return 0;
        ID3D11ShaderResourceView* srv = nullptr;
        const HRESULT hr = init.device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        if (FAILED(hr) || !srv)
            return 0;
        srvs.push_back(srv);
        return reinterpret_cast<ImTextureID>(srv);
    }
    // Fetch the game's UI textures, rebuild the font atlas with the game's font and upload the textures (frame lock
    // held, before NewFrame).
    void tryLoadArt() {
        if (artDone)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < artRetryAt)
            return;
        artRetryAt = now + 1500;
        GameSkin& sk = menu->skin();
        if (!sk.fetch(*init.host))
            return;
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        ImGui_ImplDX11_InvalidateDeviceObjects();
        fonts = buildFonts(io, headingTtf, bodyTtf, sk.fontImage(), screenSizes);
        menu->setFonts(fonts);
        if (!fonts.game || !sk.upload([this](const GameImage& im) { return uploadTexture(im); })) {
            std::fprintf(stderr, "[settings] game art unavailable (%s): plain style\n",
                         fonts.game ? "texture upload failed" : "font build failed");
            sk.reset();
            menu->setFonts(fonts);
        } else {
            std::fprintf(stderr, "[settings] menu style: game art loaded from INTERFCE.TXD\n");
        }
        artDone = true;
    }
    HWND subclassed = nullptr;
    bool backendsUp = false;
    std::atomic<uint32_t> lastW{0},
        lastH{0}; // size of the last rendered back buffer (client -> back buffer mapping of window-message positions)
    // gamepad combo
    HMODULE xinput = nullptr;
    XInputGetStateFn xGet = nullptr;
    ULONGLONG comboSince = 0;
    bool comboFired = false;

    struct Deferred {
        HWND h;
        unsigned msg;
        uintptr_t wp;
        intptr_t lp;
    };
    std::mutex deferM;
    std::vector<Deferred> deferred;
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
            const uint32_t bw = lastW.load(), bh = lastH.load();
            float px = static_cast<float>(static_cast<short>(LOWORD(lp))),
                  py = static_cast<float>(static_cast<short>(HIWORD(lp)));
            if (bw && bh && GetClientRect(init.hwnd, &rc) && rc.right > 0 && rc.bottom > 0 &&
                (static_cast<uint32_t>(rc.right) != bw || static_cast<uint32_t>(rc.bottom) != bh)) {
                px = px * static_cast<float>(bw) / static_cast<float>(rc.right);
                py = py * static_cast<float>(bh) / static_cast<float>(rc.bottom);
            }
            *x = px;
            *y = py;
        };
        if (isButton && hwnd == init.hwnd) {
            float px, py;
            msgPos(&px, &py);
            ImGui::GetIO().AddMousePosEvent(px, py);
        }
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, static_cast<WPARAM>(wp), static_cast<LPARAM>(lp));
        if (msg == WM_MOUSEMOVE &&
            hwnd == init.hwnd) { // the backend queued the unscaled client position: replace it by the mapped one
            float px, py;
            msgPos(&px, &py);
            ImGui::GetIO().AddMousePosEvent(px, py);
        }
    }
    void drainDeferred() { // frame lock held
        std::vector<Deferred> d;
        {
            std::lock_guard<std::mutex> l(deferM);
            d.swap(deferred);
        }
        for (const Deferred& e : d)
            feed(e.h, e.msg, e.wp, e.lp);
    }

    static LRESULT CALLBACK SubclassProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
        auto* self = reinterpret_cast<SettingsOverlay*>(ref);
        intptr_t res = 0;
        if (self->handleMessage(h, m, w, l, &res))
            return static_cast<LRESULT>(res);
        if (m == WM_NCDESTROY) {
            RemoveWindowSubclass(h, SubclassProc, id);
            self->m_->subclassed = nullptr;
        }
        return DefSubclassProc(h, m, w, l);
    }

    void pollGamepadCombo(SettingsOverlay& o) {
        if (!init.enableGamepadCombo)
            return;
        if (!xGet) {
            if (!xinput) {
                const wchar_t* names[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
                for (auto n : names)
                    if ((xinput = LoadLibraryW(n)))
                        break;
                if (!xinput) {
                    init.enableGamepadCombo = false;
                    return;
                }
            }
            xGet = reinterpret_cast<XInputGetStateFn>(GetProcAddress(xinput, "XInputGetState"));
            if (!xGet) {
                init.enableGamepadCombo = false;
                return;
            }
        }
        bool combo = false;
        for (DWORD i = 0; i < 4 && !combo; ++i) {
            XState st{};
            if (xGet(i, &st) == ERROR_SUCCESS) {
                const WORD need = 0x0020 /*BACK*/ | 0x0100 /*LB*/ | 0x0200 /*RB*/;
                combo = (st.pad.buttons & need) == need;
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (!combo) {
            comboSince = 0;
            comboFired = false;
            return;
        }
        if (!comboSince)
            comboSince = now;
        if (!comboFired && now - comboSince >= 500) {
            comboFired = true;
            if (!menu->isOpen() && allowGate && !allowGate())
                return;
            o.toggle();
        }
    }
};

SettingsOverlay::SettingsOverlay() : m_(std::make_unique<Impl>()) {}

std::unique_ptr<SettingsOverlay> SettingsOverlay::create(const OverlayInit& init) {
    if (!init.hwnd || !init.device || !init.context || !init.host)
        return nullptr;
    std::unique_ptr<SettingsOverlay> o(new SettingsOverlay());
    Impl& m = *o->m_;
    m.init = init;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    m.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m.ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigInputTrickleEventQueue = true; // a press and a release that arrive within one frame are delivered on
                                            // consecutive frames (never collapsed)

    // Fonts: the vendored Gelasio (buildFonts) unless the host names its own heading file.
    std::string heading = init.headingFontPath;
    if (!heading.empty() && !fileExists(heading))
        heading.clear();
    m.headingTtf = heading;
    m.fonts = buildFonts(io, heading, m.bodyTtf);
    applyDrakanStyle(1.0f);

    if (!ImGui_ImplWin32_Init(init.hwnd) || !ImGui_ImplDX11_Init(init.device, init.context)) {
        ImGui::DestroyContext(m.ctx);
        ImGui::SetCurrentContext(prev);
        return nullptr;
    }
    m.backendsUp = true;
    m.store = std::make_unique<SettingsStore>(init.paths, FileIO::native(), init.host->defaultInputIni());
    std::string warn;
    m.store->load(&warn);
    m.menu = std::make_unique<SettingsMenu>(*init.host, *m.store);
    m.menu->setFonts(m.fonts);
    Impl* mp = &m;
    m.menu->setStateCallback([mp](bool open) {
        mp->open.store(open);
        ImGui::GetIO().MouseDrawCursor = open;
        if (mp->openCb)
            mp->openCb(open);
    });
    if (init.installSubclass) {
        InitCommonControls();
        if (SetWindowSubclass(init.hwnd, &Impl::SubclassProc, 0x44534554 /*'DSET'*/,
                              reinterpret_cast<DWORD_PTR>(o.get())))
            m.subclassed = init.hwnd;
    }
    ImGui::SetCurrentContext(prev);
    return o;
}

SettingsOverlay::~SettingsOverlay() {
    if (!m_)
        return;
    if (m_->frameSrv)
        m_->frameSrv->Release();
    if (m_->frameTex)
        m_->frameTex->Release();
    for (ID3D11ShaderResourceView* v : m_->srvs)
        if (v)
            v->Release();
    m_->srvs.clear();
    ImGuiContext* prev = ImGui::GetCurrentContext();
    if (m_->subclassed)
        RemoveWindowSubclass(m_->subclassed, &Impl::SubclassProc, 0x44534554);
    if (m_->ctx) {
        ImGui::SetCurrentContext(m_->ctx);
        if (m_->backendsUp) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext(m_->ctx);
    }
    if (m_->xinput)
        FreeLibrary(m_->xinput);
    ImGui::SetCurrentContext(prev == m_->ctx ? nullptr : prev);
}

bool SettingsOverlay::handleMessage(HWND hwnd, unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (result)
        *result = 0;
    const bool isOpen = m_->menu->isOpen();

    // hotkey (open and closed)
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && static_cast<int>(wp) == m_->init.hotkeyVk) {
        if (!(lp & (1 << 30))) { // ignore auto repeat
            if (isOpen || !m_->allowGate || m_->allowGate())
                toggle(); // cut scenes / movies / loading: F1 is ignored
        }
        return true;
    }
    if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && static_cast<int>(wp) == m_->init.hotkeyVk)
        return true;
    if (!isOpen && !m_->screenInput.load())
        return false;
    if (!isOpen && postedBeforeScreenInput(msg, static_cast<uint32_t>(GetMessageTime()), m_->screenInputSince.load())) {
        static int logged = 0;
        if (logged++ < 8)
            std::fprintf(stderr,
                         "[overlay] screen layer: msg 0x%x (key 0x%x) posted before it took the input, dropped\n", msg,
                         static_cast<unsigned>(wp));
        return true; // a press that started before the screen took over (the game had it through raw input)
    }

    { // never block the window thread on the frame lock (a frame can wait on the window thread, e.g. a window resize):
      // defer instead
        std::unique_lock<std::recursive_mutex> lk(g_imguiM, std::try_to_lock);
        if (lk.owns_lock()) {
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(m_->ctx);
            m_->drainDeferred();
            m_->feed(hwnd, msg, wp, lp);
            ImGui::SetCurrentContext(prev);
        } else {
            std::lock_guard<std::mutex> dl(m_->deferM);
            if (m_->deferred.size() < 4096)
                m_->deferred.push_back({hwnd, msg, wp, lp});
        }
    }
    switch (msg) {
        // Only "down" / text / wheel messages are consumed. Releases and mouse moves still reach the game window so
        // that a key or button held while the menu opens does not stay "down" in the host framework (GLFW/raylib)
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
                SetCursor(nullptr);
                return true;
            } // the overlay draws its own cursor
            return false;
        default:
            return false;
    }
}

void SettingsOverlay::render(ID3D11RenderTargetView* rtv, uint32_t w, uint32_t h) {
    if (!rtv || !w || !h)
        return;
    ImGuiCtxScope scope(m_->ctx);
    m_->pollGamepadCombo(*this);
    const bool menuOpen = m_->menu->isOpen();
    const bool hud = m_->hud.draw && (!m_->hud.active || m_->hud.active());
    const bool cur = m_->cursorLayer.draw && (!m_->cursorLayer.active || m_->cursorLayer.active());
    const bool stats = m_->statsLayer.draw && (!m_->statsLayer.active || m_->statsLayer.active());
    const bool screen = m_->screenLayer.draw && m_->screenLayer.active && m_->screenLayer.active();
    const bool screenInput = screen && !menuOpen && m_->screenLayer.wantsInput && m_->screenLayer.wantsInput();
    if (screenInput != m_->screenInput.load()) {
        // Ownership changes: no key or button held before carries over.
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
        m_->screenInputSince.store(static_cast<uint32_t>(GetTickCount()));
        m_->screenInput.store(screenInput);
    }
    if (!menuOpen && !hud && !cur && !stats && !screen)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (menuOpen || cur || screen)
        m_->tryLoadArt();
    SettingsOverlay::ScreenFrame sf;
    if (screen) {
        if (m_->screenLayer.fontSizes) {
            const ScreenFontSizes want = m_->screenLayer.fontSizes(static_cast<float>(w), static_cast<float>(h));
            bool differs = false;
            for (int i = 0; i < UiFonts::kScreenFonts; ++i)
                differs |= std::fabs(want.px[i] - m_->screenSizes.px[i]) > std::max(0.5f, want.px[i] * 0.03f);
            if (differs)
                m_->rebuildFonts(want);
        }
        createBlendStates(m_->init.device);
        const bool frozen = m_->frameSrv && m_->screenLayer.freezeFrame && m_->screenLayer.freezeFrame();
        if (frozen || m_->copyFrame(rtv)) {
            sf.frame = reinterpret_cast<ImTextureID>(m_->frameSrv);
            sf.frameW = static_cast<float>(m_->frameDesc.Width);
            sf.frameH = static_cast<float>(m_->frameDesc.Height);
        }
        sf.w = static_cast<float>(w);
        sf.h = static_cast<float>(h);
        sf.additive = g_blendAdditive ? &blendAdditive : nullptr;
        sf.opaque = g_blendOpaque ? &blendOpaque : nullptr;
        sf.fonts = &m_->fonts;
        sf.skin = m_->menu->skin().ready() ? &m_->menu->skin() : nullptr;
        sf.input = screenInput;
    }
    m_->drainDeferred();
    m_->lastW.store(w);
    m_->lastH.store(h);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
    // the back buffer can differ from the client area (DPI, letterboxed child windows): map the pointer
    RECT rc{};
    if (GetClientRect(m_->init.hwnd, &rc) && rc.right > 0 && rc.bottom > 0 &&
        (static_cast<uint32_t>(rc.right) != w || static_cast<uint32_t>(rc.bottom) != h)) {
        POINT pt{};
        if (GetCursorPos(&pt) && ScreenToClient(m_->init.hwnd, &pt))
            io.AddMousePosEvent(static_cast<float>(pt.x) * static_cast<float>(w) / static_cast<float>(rc.right),
                                static_cast<float>(pt.y) * static_cast<float>(h) / static_cast<float>(rc.bottom));
    }
    ImGui::NewFrame();
    { // DRAKAN_OVERLAY_DEBUG=1: every left click ImGui registers (input path verification, one line per click)
        static const bool dbg = std::getenv("DRAKAN_OVERLAY_DEBUG") != nullptr;
        static int clicks = 0;
        if (dbg && menuOpen && ImGui::IsMouseClicked(0, false))
            std::fprintf(stderr, "[overlay] click #%d at (%.0f, %.0f) hovered=%d\n", ++clicks, io.MousePos.x,
                         io.MousePos.y, ImGui::IsAnyItemHovered() ? 1 : 0);
    }
    if (screen)
        m_->screenLayer.draw(sf);
    if (menuOpen)
        m_->menu->frame();
    if (stats)
        m_->statsLayer.draw(static_cast<float>(w), static_cast<float>(h),
                            std::clamp(static_cast<float>(h) / 1080.0f, 0.62f, 3.0f));
    if (hud)
        m_->hud.draw(static_cast<float>(w), static_cast<float>(h),
                     std::clamp(static_cast<float>(h) / 1080.0f, 0.62f, 3.0f));
    if (cur && !menuOpen)
        m_->cursorLayer.draw(static_cast<float>(w), static_cast<float>(h),
                             std::clamp(static_cast<float>(h) / 1080.0f, 0.62f, 3.0f));
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return;

    ID3D11DeviceContext* ctx = m_->init.context;
    ID3D11RenderTargetView* oldRtv[1] = {};
    ID3D11DepthStencilView* oldDsv = nullptr;
    ctx->OMGetRenderTargets(1, oldRtv, &oldDsv);
    UINT nvp = 1;
    D3D11_VIEWPORT oldVp{};
    ctx->RSGetViewports(&nvp, &oldVp);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    ImGui_ImplDX11_RenderDrawData(dd);
    ctx->OMSetRenderTargets(1, oldRtv, oldDsv);
    if (nvp)
        ctx->RSSetViewports(1, &oldVp);
    if (oldRtv[0])
        oldRtv[0]->Release();
    if (oldDsv)
        oldDsv->Release();
}

void SettingsOverlay::setHudLayer(HudLayer layer) {
    m_->hud = std::move(layer);
}
void SettingsOverlay::setCursorLayer(HudLayer layer) {
    m_->cursorLayer = std::move(layer);
}
void SettingsOverlay::setStatsLayer(HudLayer layer) {
    m_->statsLayer = std::move(layer);
}
void SettingsOverlay::setScreenLayer(ScreenLayer layer) {
    m_->screenLayer = std::move(layer);
}
bool SettingsOverlay::screenInputActive() const {
    return m_->screenInput.load();
}

void SettingsOverlay::drawStatsPanel(const std::vector<std::string>& lines, int corner, float w, float h, float scale) {
    if (lines.empty())
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fs = std::max(13.0f, 17.0f * std::max(0.8f, scale));
    float tw = 0.0f;
    for (const auto& l : lines)
        tw = std::max(tw, font->CalcTextSizeA(fs, 100000.0f, 0.0f, l.c_str()).x);
    const float pad = fs * 0.45f, lh = fs * 1.2f;
    const float pw = tw + 2.0f * pad, ph = static_cast<float>(lines.size()) * lh + 2.0f * pad - (lh - fs);
    const float margin = fs * 0.7f;
    const float x0 = (corner == 1 || corner == 3) ? w - margin - pw : margin;
    const float y0 = (corner == 2 || corner == 3) ? h - margin - ph : margin;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(0, 0, 0, 150), fs * 0.25f);
    float y = y0 + pad;
    for (const auto& l : lines) {
        dl->AddText(font, fs, ImVec2(x0 + pad + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 220), l.c_str());
        dl->AddText(font, fs, ImVec2(x0 + pad, y), IM_COL32(120, 255, 140, 255), l.c_str());
        y += lh;
    }
}
void SettingsOverlay::setAllowGate(std::function<bool()> allow) {
    m_->allowGate = std::move(allow);
}
void SettingsOverlay::setOpenInterceptor(std::function<bool()> gate) {
    m_->openGate = std::move(gate);
}

bool SettingsOverlay::pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const {
    RECT rc{};
    POINT pt{};
    if (!m_->init.hwnd || !GetCursorPos(&pt) || !ScreenToClient(m_->init.hwnd, &pt) ||
        !GetClientRect(m_->init.hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return false;
    if (pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom)
        return false;
    *x = static_cast<float>(pt.x) * static_cast<float>(w) / static_cast<float>(rc.right);
    *y = static_cast<float>(pt.y) * static_cast<float>(h) / static_cast<float>(rc.bottom);
    return true;
}

void SettingsOverlay::drawNativeMenuText(const std::string& text, float x, float y, float height, bool highlighted) {
    ImFont* font = m_->menu->skin().font;
    for (unsigned char ch : text)
        if (ch >= 0x80) {
            font = m_->fonts.body;
            break;
        }
    if (!font)
        font = ImGui::GetFont();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float shadow = std::max(1.0f, height * 0.07f);
    dl->AddText(font, height, ImVec2(x + shadow, y + shadow), IM_COL32(0, 0, 0, 220), text.c_str());
    dl->AddText(font, height, ImVec2(x, y), highlighted ? IM_COL32(192, 240, 0, 255) : IM_COL32(224, 224, 224, 255),
                text.c_str());
}

void SettingsOverlay::drawMenuCursor(float x, float y, float scale) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = std::max(0.8f, scale) * 22.0f; // arrow height in pixels
    // classic arrow, tip at (x, y): gold fill, dark outline, soft shadow (matches the game's brass menu frame)
    const ImVec2 pts[7] = {ImVec2(0.0f, 0.0f),   ImVec2(0.0f, 0.86f),  ImVec2(0.22f, 0.66f), ImVec2(0.38f, 1.0f),
                           ImVec2(0.52f, 0.94f), ImVec2(0.36f, 0.62f), ImVec2(0.64f, 0.62f)};
    ImVec2 p[7], sh[7];
    for (int i = 0; i < 7; ++i) {
        p[i] = ImVec2(x + pts[i].x * s, y + pts[i].y * s);
        sh[i] = ImVec2(p[i].x + s * 0.07f, p[i].y + s * 0.07f);
    }
    dl->AddTriangleFilled(sh[0], sh[1], sh[2], IM_COL32(0, 0, 0, 80));
    dl->AddTriangleFilled(sh[0], sh[2], sh[6], IM_COL32(0, 0, 0, 80));
    const ImU32 gold = IM_COL32(240, 204, 110, 255);
    dl->AddTriangleFilled(p[0], p[1], p[2], gold);
    dl->AddTriangleFilled(p[0], p[2], p[6], gold);
    dl->AddTriangleFilled(p[2], p[5], p[6], gold);
    dl->AddTriangleFilled(p[2], p[3], p[4], gold);
    dl->AddTriangleFilled(p[2], p[4], p[5], gold);
    dl->AddPolyline(p, 7, IM_COL32(20, 14, 8, 255), ImDrawFlags_Closed, std::max(1.2f, s * 0.06f));
}

void SettingsOverlay::drawCrosshair(float x, float y, float scale) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = std::max(0.8f, scale) * 9.0f; // arm length in pixels
    const float gap = s * 0.45f, th = std::max(1.5f, s * 0.2f);
    const ImU32 ink = IM_COL32(240, 204, 110, 230), shadow = IM_COL32(0, 0, 0, 150);
    const auto arms = [&](float ox, float oy, ImU32 c, float t) {
        dl->AddLine(ImVec2(x + ox - s - gap, y + oy), ImVec2(x + ox - gap, y + oy), c, t);
        dl->AddLine(ImVec2(x + ox + gap, y + oy), ImVec2(x + ox + gap + s, y + oy), c, t);
        dl->AddLine(ImVec2(x + ox, y + oy - s - gap), ImVec2(x + ox, y + oy - gap), c, t);
        dl->AddLine(ImVec2(x + ox, y + oy + gap), ImVec2(x + ox, y + oy + gap + s), c, t);
    };
    arms(1.0f, 1.0f, shadow, th + 1.0f);
    arms(0.0f, 0.0f, ink, th);
    dl->AddCircleFilled(ImVec2(x, y), th * 0.75f, ink);
}

void SettingsOverlay::drawSubtitleBand(const std::string& s, float alpha, float w, float h, float sizeFactor) {
    if (s.empty() || alpha <= 0.0f)
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    if (!(w > 0 && h > 0))
        return;
    const float factor = std::isfinite(sizeFactor) && sizeFactor > 0 ? std::clamp(sizeFactor, 0.5f, 3.0f) : 1.0f;
    float fs = std::max(12.0f, h * 0.036f * factor);
    std::vector<std::string> lines;
    auto wrap = [&]() {
        lines.clear();
        const float maxW = std::min(w * 0.80f, fs * 40.0f);
        const char* p = s.c_str();
        const char* end = p + s.size();
        while (p < end) {
            const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
            const char* paragraphEnd = nl ? nl : end;
            if (p == paragraphEnd)
                lines.emplace_back();
            while (p < paragraphEnd) {
                const char* cut = font->CalcWordWrapPositionA(fs / font->FontSize, p, paragraphEnd, maxW);
                if (cut <= p) { // guarantee progress without splitting UTF-8
                    cut = p + 1;
                    while (cut < paragraphEnd && (static_cast<unsigned char>(*cut) & 0xc0) == 0x80)
                        ++cut;
                }
                const char* inkEnd = cut;
                while (inkEnd > p && (inkEnd[-1] == ' ' || inkEnd[-1] == '\r' || inkEnd[-1] == '\t'))
                    --inkEnd;
                lines.emplace_back(p, inkEnd);
                p = cut;
                while (p < paragraphEnd && (*p == ' ' || *p == '\t'))
                    ++p;
            }
            p = nl ? nl + 1 : end;
        }
    };
    wrap();
    // Reflow at a smaller size if a long cue would cover the whole image.
    for (int i = 0; i < 12 && fs > 8 && (lines.size() * 1.25f + 1.1f) * fs > h * 0.90f; ++i) {
        fs = std::max(8.0f, fs * 0.85f);
        wrap();
    }
    const float padY = fs * 0.55f;
    const float bandH = lines.empty() ? 2 * padY : fs + (lines.size() - 1) * fs * 1.25f + 2 * padY;
    const float y1 = h * 0.965f, y0 = y1 - bandH;
    const int a = static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
    dl->AddRectFilled(ImVec2(0.0f, y0), ImVec2(w, y1), IM_COL32(0, 0, 0, a * 165 / 255));
    // centre every line on its own
    float y = y0 + padY;
    dl->PushClipRect(ImVec2(0, std::max(0.0f, y0)), ImVec2(w, y1), true);
    for (const auto& line : lines) {
        const char* p = line.c_str();
        const char* le = p + line.size();
        const ImVec2 ls = font->CalcTextSizeA(fs, 100000.0f, 0.0f, p, le);
        const ImVec2 pos((w - ls.x) * 0.5f, y);
        dl->AddText(font, fs, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, a), p, le);
        dl->AddText(font, fs, pos, IM_COL32(245, 245, 240, a), p, le);
        y += fs * 1.25f;
    }
    dl->PopClipRect();
}

void SettingsOverlay::open() {
    ImGuiCtxScope scope(m_->ctx);
    if (m_->menu->isOpen())
        return;
    ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse();
    m_->menu->open();
}
void SettingsOverlay::close() {
    ImGuiCtxScope scope(m_->ctx);
    m_->menu->close();
}
void SettingsOverlay::toggle() {
    if (m_->menu->isOpen()) {
        close();
        return;
    }
    if (m_->openGate && !m_->openGate())
        return; // the host opens later (e.g. after the game paused itself)
    open();
}
bool SettingsOverlay::isOpen() const {
    return m_->menu->isOpen();
}
bool SettingsOverlay::isOpenAtomic() const {
    return m_->open.load();
}
void SettingsOverlay::setOpenChangedCallback(std::function<void(bool)> cb) {
    m_->openCb = std::move(cb);
}
SettingsMenu& SettingsOverlay::menu() {
    return *m_->menu;
}
SettingsStore& SettingsOverlay::store() {
    return *m_->store;
}
void* SettingsOverlay::imguiContext() const {
    return m_->ctx;
}

} // namespace dsettings
