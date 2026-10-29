// SettingsOverlay: the platform-free core. The window half is an OverlayPlatform (OverlayWin32.cpp / OverlayGlfw.cpp),
// the graphics half an OverlayRenderer (OverlayD3D11.cpp / OverlayWebGpu.cpp).
#include "OverlayImpl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include "settings/OverlayD3D11.h"
#endif

namespace dsettings {

std::recursive_mutex g_overlayImguiMutex;

namespace {
bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::u8path(p), ec);
}
uint64_t steadyMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace

void SettingsOverlay::Impl::rebuildFonts(const ScreenFontSizes& sizes) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    init.renderer->invalidateFonts();
    GameSkin& sk = menu->skin();
    fonts = buildFonts(io, headingTtf, bodyTtf, sk.fetched() ? sk.fontImage() : nullptr, sizes);
    menu->setFonts(fonts);
    screenSizes = sizes;
}

ImTextureID SettingsOverlay::Impl::uploadTexture(const GameImage& im) {
    const ImTextureID t =
        init.renderer->createTexture(im.rgba.data(), static_cast<uint32_t>(im.w), static_cast<uint32_t>(im.h));
    if (t)
        images.push_back(t);
    return t;
}

void SettingsOverlay::Impl::tryLoadArt() {
    if (artDone)
        return;
    const uint64_t now = steadyMs();
    if (now < artRetryAtMs)
        return;
    artRetryAtMs = now + 1500;
    GameSkin& sk = menu->skin();
    if (!sk.fetch(*init.host))
        return;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    init.renderer->invalidateFonts();
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

void SettingsOverlay::Impl::hotkey(SettingsOverlay& overlay) {
    if (menu->isOpen() || !allowGate || allowGate())
        overlay.toggle(); // cut scenes / movies / loading: opening is ignored
}

SettingsOverlay::SettingsOverlay() : m_(std::make_unique<Impl>()) {}

std::unique_ptr<SettingsOverlay> SettingsOverlay::create(const OverlayInit& init) {
    if (!init.hwnd || !init.host)
        return nullptr;
    std::unique_ptr<SettingsOverlay> o(new SettingsOverlay());
    Impl& m = *o->m_;
    m.init = init;
#if defined(_WIN32)
    if (!m.init.renderer && init.device && init.context)
        m.init.renderer = createD3D11OverlayRenderer(init.device, init.context);
#endif
    if (!m.init.renderer)
        return nullptr;
    m.platform = createOverlayPlatform();
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
    if (!m.init.renderer->init() || !m.platform->init(*o, m)) {
        m.platform->shutdown();
        m.init.renderer->shutdown();
        m.platform.reset();
        ImGui::DestroyContext(m.ctx);
        m.ctx = nullptr;
        ImGui::SetCurrentContext(prev);
        return nullptr;
    }
    m.backendsUp = true;
    ImGui::SetCurrentContext(prev);
    return o;
}

SettingsOverlay::~SettingsOverlay() {
    if (!m_)
        return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    if (m_->ctx) {
        ImGui::SetCurrentContext(m_->ctx);
        if (m_->backendsUp) {
            m_->platform->shutdown();
            for (ImTextureID t : m_->images)
                m_->init.renderer->releaseTexture(t);
            m_->images.clear();
            m_->init.renderer->shutdown();
        }
        m_->platform.reset();
        ImGui::DestroyContext(m_->ctx);
    }
    ImGui::SetCurrentContext(prev == m_->ctx ? nullptr : prev);
}

bool SettingsOverlay::handleMessage(HWND hwnd, unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    return m_->platform && m_->platform->handleMessage(hwnd, msg, wp, lp, result);
}

void SettingsOverlay::render(void* target, uint32_t w, uint32_t h, const HudView& image) {
    if (!target || !w || !h)
        return;
    ImGuiCtxScope scope(m_->ctx);
    m_->platform->poll();
    const bool menuOpen = m_->menu->isOpen();
    const bool hud = hudLayers().anyActive(menuOpen);
    const bool screen = m_->screenLayer.draw && m_->screenLayer.active && m_->screenLayer.active();
    const bool screenInput = screen && !menuOpen && m_->screenLayer.wantsInput && m_->screenLayer.wantsInput();
    if (screenInput != m_->screenInput.load()) {
        // Ownership changes: no key or button held before carries over.
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
        m_->screenInputSince.store(m_->platform->nowMs());
        m_->screenInput.store(screenInput);
    }
    if (!menuOpen && !hud && !screen)
        return;

    ImGuiIO& io = ImGui::GetIO();
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
        const bool frozen = m_->frameTex && m_->screenLayer.freezeFrame && m_->screenLayer.freezeFrame();
        if (!frozen)
            m_->frameTex = m_->init.renderer->copyTarget(target, &m_->frameW, &m_->frameH);
        if (m_->frameTex) {
            sf.frame = m_->frameTex;
            sf.frameW = static_cast<float>(m_->frameW);
            sf.frameH = static_cast<float>(m_->frameH);
        }
        sf.w = static_cast<float>(w);
        sf.h = static_cast<float>(h);
        sf.additive = m_->init.renderer->additiveCallback();
        sf.opaque = m_->init.renderer->opaqueCallback();
        sf.fonts = &m_->fonts;
        sf.skin = m_->menu->skin().ready() ? &m_->menu->skin() : nullptr;
        sf.input = screenInput;
    }
    m_->lastW.store(w);
    m_->lastH.store(h);
    m_->init.renderer->newFrame();
    m_->platform->newFrame(w, h);
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
    if (hud) {
        HudView view = image;
        view.w = static_cast<float>(w);
        view.h = static_cast<float>(h);
        view.uiScale = hudScaleFor(view.h);
        hudLayers().draw(view, menuOpen);
    }
    ImGui::Render();
    if (ImDrawData* dd = ImGui::GetDrawData())
        m_->init.renderer->render(target, w, h, dd);
}

void SettingsOverlay::setScreenLayer(ScreenLayer layer) {
    m_->screenLayer = std::move(layer);
}
bool SettingsOverlay::screenInputActive() const {
    return m_->screenInput.load();
}

void SettingsOverlay::setAllowGate(std::function<bool()> allow) {
    m_->allowGate = std::move(allow);
}
void SettingsOverlay::setOpenInterceptor(std::function<bool()> gate) {
    m_->openGate = std::move(gate);
}

bool SettingsOverlay::pointerInBackbuffer(uint32_t w, uint32_t h, float* x, float* y) const {
    return m_->platform && m_->platform->pointerInBackbuffer(w, h, x, y);
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
OverlayRenderer& SettingsOverlay::renderer() {
    return *m_->init.renderer;
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
