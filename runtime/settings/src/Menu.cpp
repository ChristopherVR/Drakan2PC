#include "settings/Menu.h"
#include "settings/Apply.h"
#include "settings/Bindings.h"
#include "settings/Schema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <cstring>
#include <functional>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"

namespace dsettings {

#include "font_roboto_medium.inc"
#include "gelasio/gelasio_bold.inc"
#include "gelasio/gelasio_regular.inc"

namespace {

// ---- palette
// ---------------------------------------------------------------------------------------------------------
const ImVec4 kGold = ImVec4(0.89f, 0.72f, 0.31f, 1.0f);
const ImVec4 kGoldDim = ImVec4(0.62f, 0.49f, 0.20f, 1.0f);
const ImVec4 kGoldHi = ImVec4(1.00f, 0.86f, 0.46f, 1.0f);
const ImVec4 kCream = ImVec4(0.93f, 0.87f, 0.73f, 1.0f);
const ImVec4 kMuted = ImVec4(0.60f, 0.54f, 0.43f, 1.0f);
const ImVec4 kBgDeep = ImVec4(0.075f, 0.058f, 0.042f, 0.98f);
const ImVec4 kPanel = ImVec4(0.125f, 0.095f, 0.068f, 1.0f);
const ImVec4 kPanelHi = ImVec4(0.185f, 0.140f, 0.098f, 1.0f);
const ImVec4 kPanelAct = ImVec4(0.28f, 0.205f, 0.115f, 1.0f);
const ImVec4 kAmber = ImVec4(0.95f, 0.62f, 0.15f, 1.0f);
const ImVec4 kRed = ImVec4(0.90f, 0.32f, 0.26f, 1.0f);
const ImVec4 kGreen = ImVec4(0.45f, 0.78f, 0.38f, 1.0f);

#include "skin_widgets.inc"

// ---- ImGuiKey -> drakan_input.ini source name
// -----------------------------------------------------------------------------
const char* iniNameFromImGuiKey(ImGuiKey k) {
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) {
        static char b[2][2] = {};
        static int i = 0;
        i ^= 1;
        b[i][0] = static_cast<char>('A' + (k - ImGuiKey_A));
        b[i][1] = 0;
        return b[i];
    }
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) {
        static char b[2][2] = {};
        static int i = 0;
        i ^= 1;
        b[i][0] = static_cast<char>('0' + (k - ImGuiKey_0));
        b[i][1] = 0;
        return b[i];
    }
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) {
        static char b[2][4] = {};
        static int i = 0;
        i ^= 1;
        std::snprintf(b[i], 4, "F%d", 1 + (k - ImGuiKey_F1));
        return b[i];
    }
    if (k >= ImGuiKey_Keypad0 && k <= ImGuiKey_Keypad9) {
        static char b[2][12] = {};
        static int i = 0;
        i ^= 1;
        std::snprintf(b[i], 12, "NUMPAD%d", k - ImGuiKey_Keypad0);
        return b[i];
    }
    switch (k) {
        case ImGuiKey_Escape:
            return "ESC";
        case ImGuiKey_Tab:
            return "TAB";
        case ImGuiKey_Space:
            return "SPACE";
        case ImGuiKey_Enter:
            return "ENTER";
        case ImGuiKey_Backspace:
            return "BACKSPACE";
        case ImGuiKey_Insert:
            return "INSERT";
        case ImGuiKey_Delete:
            return "DELETE";
        case ImGuiKey_Home:
            return "HOME";
        case ImGuiKey_End:
            return "END";
        case ImGuiKey_PageUp:
            return "PGUP";
        case ImGuiKey_PageDown:
            return "PGDN";
        case ImGuiKey_UpArrow:
            return "UP";
        case ImGuiKey_DownArrow:
            return "DOWN";
        case ImGuiKey_LeftArrow:
            return "LEFT";
        case ImGuiKey_RightArrow:
            return "RIGHT";
        case ImGuiKey_LeftShift:
            return "LSHIFT";
        case ImGuiKey_RightShift:
            return "RSHIFT";
        case ImGuiKey_LeftCtrl:
            return "LCONTROL";
        case ImGuiKey_RightCtrl:
            return "RCONTROL";
        case ImGuiKey_LeftAlt:
            return "LALT";
        case ImGuiKey_RightAlt:
            return "RALT";
        case ImGuiKey_CapsLock:
            return "CAPSLOCK";
        case ImGuiKey_Minus:
            return "MINUS";
        case ImGuiKey_Equal:
            return "EQUALS";
        case ImGuiKey_LeftBracket:
            return "LBRACKET";
        case ImGuiKey_RightBracket:
            return "RBRACKET";
        case ImGuiKey_Semicolon:
            return "SEMICOLON";
        case ImGuiKey_Apostrophe:
            return "APOSTROPHE";
        case ImGuiKey_GraveAccent:
            return "GRAVE";
        case ImGuiKey_Backslash:
            return "BACKSLASH";
        case ImGuiKey_Comma:
            return "COMMA";
        case ImGuiKey_Period:
            return "PERIOD";
        case ImGuiKey_Slash:
            return "SLASH";
        case ImGuiKey_KeypadDecimal:
            return "NUMPADDOT";
        case ImGuiKey_KeypadDivide:
            return "NUMPADDIV";
        case ImGuiKey_KeypadMultiply:
            return "NUMPADMUL";
        case ImGuiKey_KeypadSubtract:
            return "NUMPADMINUS";
        case ImGuiKey_KeypadAdd:
            return "NUMPADPLUS";
        case ImGuiKey_KeypadEnter:
            return "NUMPADENTER";
        default:
            return nullptr;
    }
}

// ---- small widgets
// -------------------------------------------------------------------------------------------------------
bool Toggle(const char* id, bool* v) {
    ImGuiStyle& st = ImGui::GetStyle();
    if (g_sk) { // game look: square brass socket with a lit gem
        const float side = ImGui::GetFrameHeight() * 0.92f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(id);
        const float tw = std::max(ImGui::CalcTextSize("On").x, ImGui::CalcTextSize("Off").x);
        ImGui::InvisibleButton("##t", ImVec2(side + st.ItemInnerSpacing.x * 1.5f + tw, ImGui::GetFrameHeight()));
        bool changed = false;
        if (ImGui::IsItemClicked() ||
            (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space) ||
                                        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)))) {
            *v = !*v;
            changed = true;
            Snd(*v ? UiSound::On : UiSound::Off);
        }
        const bool hov = ImGui::IsItemHovered(), foc = ImGui::IsItemFocused() && GImGui->NavCursorVisible;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a(p.x, p.y + (ImGui::GetFrameHeight() - side) * 0.5f), b(a.x + side, a.y + side);
        dl->AddRectFilled(ImVec2(a.x + side * 0.12f, a.y + side * 0.12f),
                          ImVec2(b.x - side * 0.12f, b.y - side * 0.12f), IM_COL32(16, 10, 6, 240), side * 0.12f);
        if (foc)
            g_sk->fire(dl, ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), 0.75f, ImGui::GetTime());
        g_sk->box(dl, a, b, (hov || foc) ? kBrassFocus : kBrassNormal);
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        if (*v) {
            g_sk->orb(dl, ImVec2(c.x, c.y + 1.0f), side * 0.30f, IM_COL32(255, 176, 52, 255));
        } else
            g_sk->orb(dl, c, side * 0.20f, IM_COL32(64, 48, 36, 255));
        ImGui::SameLine(0, st.ItemInnerSpacing.x * 1.5f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(*v ? kGoldHi : kMuted, *v ? "On" : "Off");
        ImGui::PopID();
        return changed;
    }
    const float h = ImGui::GetFrameHeight() * 0.80f, w = h * 1.9f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += (ImGui::GetFrameHeight() - h) * 0.5f;
    ImGui::PushID(id);
    ImGui::InvisibleButton("##t", ImVec2(w, ImGui::GetFrameHeight()));
    bool changed = false;
    if (ImGui::IsItemClicked() ||
        (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Space) ||
                                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)))) {
        *v = !*v;
        changed = true;
    }
    const bool hov = ImGui::IsItemHovered(), foc = ImGui::IsItemFocused();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg =
        *v ? ImGui::GetColorU32(ImVec4(0.55f, 0.40f, 0.12f, 1)) : ImGui::GetColorU32(ImVec4(0.20f, 0.15f, 0.11f, 1));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32((hov || foc) ? kGoldHi : kGoldDim), h * 0.5f, 0,
                foc ? 2.5f : 1.5f);
    const float r = h * 0.5f - 3.0f;
    dl->AddCircleFilled(ImVec2(*v ? p.x + w - h * 0.5f : p.x + h * 0.5f, p.y + h * 0.5f), r,
                        ImGui::GetColorU32(*v ? kGoldHi : kMuted));
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(*v ? kGoldHi : kMuted, *v ? "On" : "Off");
    ImGui::PopID();
    return changed;
}

void Hint(const char* tip) {
    if (tip && *tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void SectionTitle(const char* text) {
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
    if (g_sk) {
        const float fs = ImGui::GetFontSize() * 1.12f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        GameText(ImGui::GetWindowDrawList(), fs, p, IM_COL32(236, 190, 92, 255), text);
        ImGui::Dummy(ImVec2(GameTextW(fs, text), GameTextH(fs) + 2.0f));
    } else
        ImGui::TextColored(kGold, "%s", text);
    ImVec2 a = ImGui::GetCursorScreenPos();
    const float x1 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImGui::GetWindowDrawList()->AddLine(a, ImVec2(std::max(a.x + 10.0f, x1), a.y), ImGui::GetColorU32(kGoldDim), 1.0f);
    ImGui::Dummy(ImVec2(0, 3));
}

// A settings row: label (left), control (right). Returns false when the control must not be drawn (never).
struct Row {
    bool supported;
    Row(float labelW, const char* label, const char* tip, bool restart, bool sup = true,
        const char* unavailable = "not available in this build")
        : supported(sup) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(sup ? kCream : kMuted, "%s", label);
        if (restart) {
            ImGui::SameLine();
            ImGui::TextColored(kAmber, "*");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Takes effect after a restart");
            }
        }
        ImGui::TableSetColumnIndex(1);
        if (!sup)
            ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(labelW);
        (void)tip;
        (void)unavailable;
        tip_ = tip;
        un_ = unavailable;
    }
    ~Row() {
        if (!supported) {
            ImGui::EndDisabled();
        }
    }
    void hint() const { Hint(supported ? tip_ : un_); }
    const char* tip_;
    const char* un_;
};

template <class T>
bool EnumCombo(const char* id, T* v, std::initializer_list<std::pair<T, const char*>> items) {
    const char* cur = "?";
    for (auto& i : items)
        if (i.first == *v)
            cur = i.second;
    bool ch = false;
    if (ComboBegin(id, cur)) {
        for (auto& i : items) {
            const bool sel = i.first == *v;
            if (ImGui::Selectable(i.second, sel)) {
                *v = i.first;
                ch = true;
            }
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return ch;
}

bool ToggleRow(float w, const char* label, bool* v, const char* tip, bool restart = false, bool sup = true) {
    Row r(w, label, tip, restart, sup);
    bool c = Toggle(label, v);
    r.hint();
    return c;
}

} // namespace

const char* iniNameOfImGuiKey(int imguiKey) {
    return iniNameFromImGuiKey(static_cast<ImGuiKey>(imguiKey));
}

// ---- style and fonts
// --------------------------------------------------------------------------------------------------------
void applyDrakanStyle(float scale) {
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 3.0f;
    st.ChildRounding = 3.0f;
    st.FrameRounding = 3.0f;
    st.PopupRounding = 3.0f;
    st.GrabRounding = 3.0f;
    st.ScrollbarRounding = 4.0f;
    st.TabRounding = 3.0f;
    st.WindowPadding = ImVec2(16, 14);
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(12, 9);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.ScrollbarSize = 16.0f;
    st.GrabMinSize = 18.0f;
    st.WindowBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 1.0f;
    st.PopupBorderSize = 1.0f;
    st.SeparatorTextBorderSize = 1.0f;
    st.CellPadding = ImVec2(10, 5);
    st.ScaleAllSizes(scale);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = kCream;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = kBgDeep;
    c[ImGuiCol_ChildBg] = ImVec4(0.095f, 0.072f, 0.052f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.072f, 0.045f, 0.99f);
    c[ImGuiCol_Border] = kGoldDim;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = kPanel;
    c[ImGuiCol_FrameBgHovered] = kPanelHi;
    c[ImGuiCol_FrameBgActive] = kPanelAct;
    c[ImGuiCol_TitleBg] = kPanel;
    c[ImGuiCol_TitleBgActive] = kPanelHi;
    c[ImGuiCol_TitleBgCollapsed] = kPanel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.045f, 0.032f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.42f, 0.32f, 0.14f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = kGoldDim;
    c[ImGuiCol_ScrollbarGrabActive] = kGold;
    c[ImGuiCol_CheckMark] = kGoldHi;
    c[ImGuiCol_SliderGrab] = kGold;
    c[ImGuiCol_SliderGrabActive] = kGoldHi;
    c[ImGuiCol_Button] = ImVec4(0.24f, 0.17f, 0.095f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.27f, 0.12f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.52f, 0.37f, 0.14f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.52f, 0.27f, 0.07f, 0.80f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.72f, 0.38f, 0.09f, 0.90f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.90f, 0.52f, 0.12f, 1.0f);
    c[ImGuiCol_Separator] = kGoldDim;
    c[ImGuiCol_SeparatorHovered] = kGold;
    c[ImGuiCol_SeparatorActive] = kGoldHi;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = kGoldDim;
    c[ImGuiCol_ResizeGripActive] = kGold;
    c[ImGuiCol_Tab] = kPanel;
    c[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.32f, 0.13f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.40f, 0.29f, 0.12f, 1.0f);
    c[ImGuiCol_TabDimmed] = kPanel;
    c[ImGuiCol_TabDimmedSelected] = kPanelHi;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.40f, 0.12f, 0.50f);
    c[ImGuiCol_NavCursor] = kGoldHi;
    c[ImGuiCol_NavWindowingHighlight] = kGold;
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
    c[ImGuiCol_TableHeaderBg] = kPanelHi;
    c[ImGuiCol_TableBorderStrong] = kGoldDim;
    c[ImGuiCol_TableBorderLight] = ImVec4(0.30f, 0.23f, 0.12f, 0.6f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 0.9f, 0.7f, 0.035f);
    ImGui::GetStyle() = st;
}

UiFonts buildFonts(ImGuiIO& io, const std::string& headingTtf, const std::string& bodyTtf, const GameImage* gameFont,
                   const ScreenFontSizes& screen) {
    UiFonts f;
    f.bakedPx = 28.0f;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    // Latin-1 supplement + Latin Extended-A: covers English, French, German, Italian and Spanish language names.
    static const ImWchar ranges[] = {0x0020, 0x017F, 0x2013, 0x2014, 0x2018, 0x201D, 0x2022, 0x2022, 0x2026, 0x2026, 0};
    // Text: the vendored Gelasio (a serif metric-compatible with Georgia, OFL; third_party/fonts) unless the host
    // passes its own files. Body, numeric and small text use the regular face, headings and titles the bold one.
    const auto add = [&](const std::string& path, bool bold, float px, ImFontConfig* c, const ImWchar* r) -> ImFont* {
        ImFont* font = path.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(path.c_str(), px, c, r);
        if (!font)
            font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
                bold ? GelasioBold_compressed_data_base85 : GelasioRegular_compressed_data_base85, px, c, r);
        return font;
    };
    f.body = add(bodyTtf, false, f.bakedPx, &cfg, ranges);
    if (!f.body)
        f.body = io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, f.bakedPx, &cfg,
                                                                ranges);
    f.heading = f.body;
    {
        ImFontConfig hc;
        hc.OversampleH = 2;
        hc.OversampleV = 2;
        if (ImFont* h = add(headingTtf, true, f.bakedPx * 1.45f, &hc, ranges))
            f.heading = h;
    }
    io.FontDefault = f.body;
    if (screen.any()) {
        // Baked once per output size bucket at the size they are drawn at (no FontGlobalScale blur at 4K). Body,
        // numeric and small text use the body face, headings and titles the heading face.
        static const ImWchar sr[] = {0x0020, 0x00FF, 0x2013, 0x2014, 0x2018, 0x201D, 0x2022, 0x2022, 0x2026, 0x2026, 0};
        for (int i = 0; i < UiFonts::kScreenFonts; ++i) {
            const float px = std::clamp(screen.px[i], 8.0f, 160.0f);
            if (!(screen.px[i] > 0))
                continue;
            ImFontConfig sc;
            sc.OversampleH = 2;
            sc.OversampleV = 1;
            sc.PixelSnapH = true;
            const bool headingFace = i == 1 || i == 2;
            ImFont* font = add(headingFace ? headingTtf : bodyTtf, headingFace, px, &sc, sr);
            if (!font)
                font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, px, &sc, sr);
            f.screen[i] = font;
            f.screenPx[i] = font ? px : 0.0f;
        }
    }
    if (gameFont) {
        // The game's decorative font for ASCII; the TTF keeps everything else (accents) so non ASCII text still shows.
        ImFontConfig gc;
        gc.OversampleH = 1;
        gc.OversampleV = 1;
        static const ImWchar gr[] = {0x00A1, 0x017F, 0};
        ImFont* g =
            io.Fonts->AddFontFromMemoryCompressedBase85TTF(RobotoMedium_compressed_data_base85, f.gameBakedPx, &gc, gr);
        GameFontJob job;
        if (g && prepareGameGlyphs(io, g, *gameFont, f.gameBakedPx, job)) {
            io.Fonts->Build();
            finishGameGlyphs(io, job);
            f.game = g;
        }
    }
    return f;
}

// ---- menu
// ----------------------------------------------------------------------------------------------------------------------
SettingsMenu::SettingsMenu(ISettingsHost& host, SettingsStore& store) : host_(host), store_(store) {
    p_ = a_ = store_.loaded();
}

void SettingsMenu::refreshEnvironment() {
    caps_ = host_.capabilities();
    mon_ = host_.monitor();
    audioDevices_ = host_.audioDevices();
}

void SettingsMenu::open() {
    if (open_)
        return;
    std::string w;
    store_.load(&w);
    p_ = a_ = store_.loaded();
    refreshEnvironment();
    linkSens_ = std::fabs(p_.controls.sensX - p_.controls.sensY) < 1e-6f;
    open_ = true;
    wantPageFocus_ = true;
    cap_ = Capture();
    host_.onOverlayOpened();
    if (stateCb_)
        stateCb_(true);
}

void SettingsMenu::close() {
    if (!open_)
        return;
    open_ = false;
    cap_ = Capture();
    host_.onOverlayClosed();
    if (stateCb_)
        stateCb_(false);
}

bool SettingsMenu::pageDirty(Page pg) const {
    switch (pg) {
        case Page::Display:
            return !(p_.video == a_.video);
        case Page::Audio:
            return !(p_.audio == a_.audio);
        case Page::Controls:
            return !(p_.controls == a_.controls);
        case Page::General:
            return !(p_.general == a_.general);
    }
    return false;
}

void SettingsMenu::resetBindings(bool gameLayer) {
    const Settings& d = store_.defaults();
    resetLayer(gameLayer ? p_.controls.game : p_.controls.menu, gameLayer ? d.controls.game : d.controls.menu,
               gameLayer ? a_.controls.game : a_.controls.menu);
}

void SettingsMenu::resetPage() {
    static const char* const ids[] = {"display", "audio", "controls", "gameplay"};
    resetPageDefaults(p_, store_.defaults(), ids[static_cast<int>(page_)]);
}

// ---- apply
// ---------------------------------------------------------------------------------------------------------------------
void SettingsMenu::apply(bool closeAfter) {
    if (changedKeys(a_, p_).empty()) {
        if (closeAfter)
            close();
        return;
    }
    // The shared apply path (settings/Apply.h): live apply, save, bindings reload; `a_` becomes `p_`.
    ApplyOutcome o = applySettings(host_, store_, caps_, a_, p_);
    restartReasons_ = o.restartReasons;
    if (!o.error.empty()) {
        errorText_ = o.error;
        openError_ = true;
    }
    if (o.displayRisky && !(o.before.video == p_.video)) {
        revertTo_ = o.before;
        revertDeadline_ = ImGui::GetTime() + 12.0;
        openRevert_ = true;
    } else if (o.needsRestart) {
        openRestart_ = true;
    }
    if (closeAfter && !openRevert_ && !openRestart_ && !openError_)
        close();
}

// ---- key capture
// -----------------------------------------------------------------------------------------------------------------
void SettingsMenu::demoState(DemoState s) {
    switch (s) {
        case DemoState::Capture:
            cap_ = Capture();
            cap_.active = true;
            cap_.game = true;
            cap_.action = "TRIANGLE";
            cap_.slot = 0;
            cap_.startTime = ImGui::GetTime();
            cap_.mouseArmed = true;
            break;
        case DemoState::CaptureConflict:
            cap_ = Capture();
            cap_.active = true;
            cap_.game = true;
            cap_.action = "TRIANGLE";
            cap_.slot = 1;
            cap_.startTime = ImGui::GetTime();
            cap_.conflictAsk = true;
            cap_.pendingSource = "E";
            break;
        case DemoState::RestartPrompt:
            restartReasons_ = {"Internal render scale", "Language"};
            openRestart_ = true;
            break;
        case DemoState::RevertPrompt:
            revertTo_ = a_;
            revertDeadline_ = ImGui::GetTime() + 11.2;
            openRevert_ = true;
            break;
        case DemoState::ErrorPrompt:
            errorText_ = "cannot write drakan.ini (access denied)\n";
            openError_ = true;
            break;
        default:
            cap_ = Capture();
            break;
    }
}

void SettingsMenu::commitCapture(const std::string& source, bool replaceOthers) {
    bindSlot(cap_.game ? p_.controls.game : p_.controls.menu, cap_.action, cap_.slot, source, replaceOthers);
    cap_ = Capture();
}

void SettingsMenu::updateCapture() {
    if (!cap_.active)
        return;
    ImGuiIO& io = ImGui::GetIO();
    if (cap_.conflictAsk)
        return; // waiting for the user to choose in the popup
    // Mouse buttons only count once all of them have been released since the capture started (the click that opened
    // it).
    if (!cap_.mouseArmed) {
        bool any = false;
        for (int b = 0; b < 5; ++b)
            any = any || ImGui::IsMouseDown(b);
        if (!any)
            cap_.mouseArmed = true;
    }
    std::string src;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && src.empty(); ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        if (key == ImGuiKey_Escape)
            continue; // Esc cancels (the popup has an explicit "Bind Esc" button)
        if (key == ImGuiKey_Enter && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown))
            continue;
        if (ImGui::IsKeyPressed(key, false)) {
            if (const char* n = iniNameFromImGuiKey(key))
                src = n;
        }
    }
    if (src.empty() && io.MouseWheel > 0.0f)
        src = "WHEELUP";
    if (src.empty() && io.MouseWheel < 0.0f)
        src = "WHEELDOWN";
    if (src.empty() && ImGui::GetTime() - cap_.startTime > 12.0) {
        cap_ = Capture();
        return;
    } // gamepad-only users: do not get stuck
    if (!src.empty()) {
        BindingLayer& layer = cap_.game ? p_.controls.game : p_.controls.menu;
        auto others = layer.actionsFor(src);
        others.erase(std::remove(others.begin(), others.end(), cap_.action), others.end());
        if (!others.empty()) {
            cap_.conflictAsk = true;
            cap_.pendingSource = src;
        } else
            commitCapture(src, false);
    }
}

// ---- drawing
// ---------------------------------------------------------------------------------------------------------------------
void SettingsMenu::drawHeader(float w) {
    if (skinned()) {
        const float h = S(60), pw = S(470);
        const ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + pw, p.y + h);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        g_sk->plate(dl, p, q, IM_COL32(255, 238, 205, 255));
        ImVec2 sa, sb;
        GameSkin::plateSlot(p, q, sa, sb);
        GameTextCentered(dl, ImVec2(sa.x, p.y), ImVec2(sb.x, q.y), h * 0.50f, IM_COL32(255, 220, 130, 255),
                         "PC Settings");
        const char* sub = "F1 closes this menu";
        const float fs = ImGui::GetFontSize();
        dl->AddText(ImVec2(p.x + w - ImGui::CalcTextSize(sub).x - S(8), p.y + (h - fs) * 0.5f),
                    ImGui::GetColorU32(kMuted), sub);
        ImGui::Dummy(ImVec2(w, h));
        return;
    }
    if (fonts_.heading)
        ImGui::PushFont(fonts_.heading);
    ImGui::TextColored(kGoldHi, "DRAKAN");
    ImGui::SameLine(0, S(14));
    ImGui::TextColored(kCream, "PC Settings");
    if (fonts_.heading)
        ImGui::PopFont();
    const char* sub = "F1 closes this menu";
    ImGui::SameLine(w - ImGui::CalcTextSize(sub).x - S(8));
    ImGui::TextColored(kMuted, "%s", sub);
}

void SettingsMenu::drawSidebar(float h) {
    ImGui::BeginChild("##side", ImVec2(S(260), h), ImGuiChildFlags_Borders);
    struct Item {
        Page p;
        const char* name;
    };
    const Item items[] = {
        {Page::Display, "Display"}, {Page::Audio, "Audio"}, {Page::Controls, "Controls"}, {Page::General, "Gameplay"}};
    for (const Item& it : items) {
        const bool sel = page_ == it.p;
        const bool dirty = pageDirty(it.p);
        if (skinned()) {
            char lb[64];
            std::snprintf(lb, sizeof lb, "%s###nav%d", it.name, static_cast<int>(it.p));
            if (PlateWidget(lb, ImVec2(-1.0f, S(46)), sel, dirty ? "*" : nullptr))
                page_ = it.p;
            if (sel && wantPageFocus_) {
                ImGui::SetKeyboardFocusHere(-1);
                wantPageFocus_ = false;
            }
            ImGui::Dummy(ImVec2(0, S(2)));
            continue;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? kGoldHi : kCream);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        char label[64];
        std::snprintf(label, sizeof label, "%s%s###nav%d", it.name, dirty ? "  *" : "", static_cast<int>(it.p));
        if (ImGui::Selectable(label, sel, 0, ImVec2(0, S(46))))
            page_ = it.p;
        if (sel && wantPageFocus_) {
            ImGui::SetKeyboardFocusHere(-1);
            wantPageFocus_ = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kMuted, "Pg Up / Pg Dn or L2 / R2 switch pages.");
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextColored(kAmber, "*");
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "unsaved changes, or a setting that needs a restart.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

void SettingsMenu::drawFooter() {
    const bool anyDirty = !(p_ == a_);
    if (PlateButton("Defaults for this page", ImVec2(0, S(46))))
        resetPage();
    ImGui::SameLine();
    if (PlateButton("Quit to Desktop", ImVec2(0, S(46))))
        openQuit_ = true;
    const float bw = S(170), spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttonsX = ImGui::GetContentRegionMax().x - bw * 3 - spacing * 2;
    if (anyDirty) {
        // Only in the gap before Cancel; when it is too narrow the * on the page tabs already says so.
        ImGui::SameLine();
        const float room = buttonsX - (ImGui::GetCursorPosX() + spacing);
        for (const char* note : {"Unsaved changes", "Unsaved"})
            if (ImGui::CalcTextSize(note).x <= room) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kMuted, "%s", note);
                break;
            }
    }
    ImGui::SameLine(buttonsX);
    if (PlateButton("Cancel", ImVec2(bw, S(46))))
        close();
    ImGui::SameLine();
    if (!anyDirty)
        ImGui::BeginDisabled();
    if (PlateButton("Apply", ImVec2(bw, S(46))))
        apply(false);
    if (!anyDirty)
        ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.32f, 0.10f, 1));
    if (PlateButton("OK", ImVec2(bw, S(46))))
        apply(true);
    ImGui::PopStyleColor();
}

// ---- schema rows (settings/Schema.h: the same rows the modern pages show) ----
SchemaEnv SettingsMenu::schemaEnv() const {
    SchemaEnv env;
    env.caps = caps_;
    env.monitor = mon_;
    env.audioDevices = audioDevices_;
    env.linkSensitivity = linkSens_;
    return env;
}

void SettingsMenu::drawSchemaPage(const char* pageId, bool controllerView) {
    const gameui::SettingsSchema sc = buildSettingsSchema(p_, schemaEnv());
    const gameui::SettingPage* pg = nullptr;
    for (const gameui::SettingPage& x : sc.pages)
        if (x.id == pageId)
            pg = &x;
    if (!pg)
        return;
    if (ImGui::BeginTable(controllerView ? "##pad" : (std::string("##") + pageId).c_str(), 2,
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 0.40f);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 0.60f);
        for (const gameui::SettingSection& sec : pg->sections) {
            if (controllerView && sec.title != "Gamepad") // the controller view shows the pad options only
                continue;
            bool any = false;
            for (const gameui::SettingRow& r : sec.rows)
                any = any || r.shownIn(false);
            if (!any)
                continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            SectionTitle(sec.title.c_str());
            for (const gameui::SettingRow& r : sec.rows)
                if (r.shownIn(false))
                    drawSchemaRow(r, controllerView);
        }
        ImGui::EndTable();
    }
    if (!controllerView && !pg->note.empty()) {
        ImGui::Dummy(ImVec2(0, S(14)));
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(kMuted, "%s", pg->note.c_str());
        ImGui::PopTextWrapPos();
    }
}

void SettingsMenu::drawSchemaRow(const gameui::SettingRow& r, bool controllerView) {
    using gameui::SettingKind;
    const float cw = -FLT_MIN;
    auto set = [&](const std::string& key, const std::string& value) { applySettingChange(p_, linkSens_, key, value); };
    auto num = [](float v) {
        char b[48];
        std::snprintf(b, sizeof b, "%.9g", static_cast<double>(v));
        return std::string(b);
    };
    // The controller view words the pad switch for a pad-only player.
    const char* help = controllerView && r.key == "input.gamepad.enabled" ? "Read an XInput pad." : r.help.c_str();
    switch (r.kind) {
        case SettingKind::Toggle: {
            Row row(cw, r.label.c_str(), help, r.restart, r.enabled, r.unavailable.c_str());
            bool v = r.on;
            if (Toggle(r.label.c_str(), &v))
                set(r.key, v ? r.onValue : r.offValue);
            row.hint();
            break;
        }
        case SettingKind::Choice: {
            Row row(cw, r.label.c_str(), help, r.restart, r.enabled, r.unavailable.c_str());
            ImGui::PushID(r.key.c_str());
            if (ComboBegin("##c", r.valueText().c_str())) {
                if (r.choices.empty() && !r.emptyText.empty())
                    ImGui::TextDisabled("%s", r.emptyText.c_str());
                for (size_t i = 0; i < r.choices.size(); ++i) {
                    const gameui::SettingChoice& ch = r.choices[i];
                    const bool sel = static_cast<int>(i) == r.current;
                    if (!ch.enabled)
                        ImGui::BeginDisabled();
                    if (ImGui::Selectable(ch.label.c_str(), sel))
                        set(r.key, ch.value);
                    if (sel)
                        ImGui::SetItemDefaultFocus();
                    if (!ch.enabled)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            row.hint();
            break;
        }
        case SettingKind::Slider: {
            Row row(cw, r.label.c_str(), help, r.restart, r.enabled, r.unavailable.c_str());
            ImGui::PushID(r.key.c_str());
            bool linked = r.linked;
            if (!r.linkLabel.empty()) {
                if (ImGui::Checkbox(r.linkLabel.c_str(), &linked))
                    set(r.linkKey, linked ? "1" : "0");
                ImGui::SetNextItemWidth(-FLT_MIN);
            }
            if (r.linkLabel.empty() || !linked) {
                if (r.integer) {
                    int v = static_cast<int>(std::lround(r.value));
                    if (SliderInt("##s", &v, static_cast<int>(r.min), static_cast<int>(r.max), r.format.c_str()))
                        set(r.key, std::to_string(std::clamp(v, static_cast<int>(r.min), static_cast<int>(r.max))));
                } else {
                    float v = r.value;
                    const char* fmt = !r.zeroLabel.empty() && v <= 0.0f ? r.zeroLabel.c_str() : r.format.c_str();
                    if (SliderFloat("##s", &v, r.min, r.max, fmt,
                                    r.logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None))
                        set(r.key, num(std::clamp(v, r.min, r.max)));
                }
            }
            ImGui::PopID();
            row.hint();
            break;
        }
        case SettingKind::Size: {
            Row row(cw, r.label.c_str(), help, r.restart, r.enabled, r.unavailable.c_str());
            int wh[2] = {r.width, r.height};
            ImGui::PushID(r.key.c_str());
            if (ImGui::InputInt2("##c", wh)) {
                const int w = std::clamp(wh[0], r.minWidth, std::max(r.minWidth, r.maxWidth));
                const int h = std::clamp(wh[1], r.minHeight, std::max(r.minHeight, r.maxHeight));
                set(r.key, std::to_string(w) + "x" + std::to_string(h));
            }
            ImGui::PopID();
            row.hint();
            break;
        }
        case SettingKind::Info:
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", r.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", r.text.c_str());
            ImGui::PopTextWrapPos();
            break;
    }
}

void SettingsMenu::drawDisplay() {
    drawSchemaPage("display");
}

void SettingsMenu::drawAudio() {
    drawSchemaPage("audio");
}

void SettingsMenu::drawBindingTable(bool gameLayer) {
    BindingLayer& layer = gameLayer ? p_.controls.game : p_.controls.menu;
    const auto& defs = gameLayer ? gameActions() : menuActions();
    const int kSlots = 3;
    // conflict map: source -> actions bound to it
    std::map<std::string, std::vector<std::string>> bySource;
    for (const auto& l : layer.lines)
        bySource[l.source] = l.actions;

    if (ImGui::BeginTable(gameLayer ? "##bg" : "##bm", 1 + kSlots,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        for (int i = 0; i < kSlots; ++i)
            ImGui::TableSetupColumn(i == 0   ? "Primary"
                                    : i == 1 ? "Secondary"
                                             : "Extra",
                                    ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        const char* lastGroup = "";
        for (const ActionDef& d : defs) {
            if (std::string(lastGroup) != d.group) {
                lastGroup = d.group;
                ImGui::TableNextRow(ImGuiTableRowFlags_None);
                ImGui::TableSetColumnIndex(0);
                ImGui::Dummy(ImVec2(0, S(2)));
                ImGui::TextColored(kGold, "%s", d.group);
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(d.label);
            if (ImGui::IsItemHovered() && d.hint && *d.hint)
                ImGui::SetTooltip("%s", d.hint);
            const auto srcs = layer.sourcesFor(d.id);
            for (int s = 0; s < kSlots; ++s) {
                ImGui::TableSetColumnIndex(1 + s);
                ImGui::PushID(d.id);
                ImGui::PushID(s);
                const bool has = s < static_cast<int>(srcs.size());
                std::string label = has ? sourceDisplayName(srcs[static_cast<size_t>(s)]) : std::string("-");
                bool conflict = false, reserved = false;
                std::string tip;
                if (has) {
                    const auto& acts = bySource[srcs[static_cast<size_t>(s)]];
                    if (acts.size() > 1) {
                        conflict = true;
                        tip = "Also bound to:";
                        for (const auto& a : acts) {
                            if (a == d.id)
                                continue;
                            const char* nm = a.c_str();
                            for (const auto& dd : defs)
                                if (a == dd.id)
                                    nm = dd.label;
                            tip += std::string("\n  ") + nm;
                        }
                    }
                    const std::string rr = reservedReason(srcs[static_cast<size_t>(s)], p_.controls.toggleKey);
                    if (!rr.empty()) {
                        reserved = true;
                        tip += (tip.empty() ? "" : "\n") + std::string("Note: ") + rr;
                    }
                }
                if (conflict) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.27f, 0.06f, 1));
                    ImGui::PushStyleColor(ImGuiCol_Border, kAmber);
                    ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
                } else if (reserved) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.12f, 0.10f, 1));
                    ImGui::PushStyleColor(ImGuiCol_Border, kRed);
                    ImGui::PushStyleColor(ImGuiCol_Text, kCream);
                } else if (!has) {
                    ImGui::PushStyleColor(ImGuiCol_Button, kPanel);
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.23f, 0.12f, 1));
                    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.17f, 0.095f, 1));
                    ImGui::PushStyleColor(ImGuiCol_Border, kGoldDim);
                    ImGui::PushStyleColor(ImGuiCol_Text, kCream);
                }
                const bool pressed = ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0));
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::BeginTooltip();
                    if (!tip.empty()) {
                        ImGui::TextColored(conflict ? kAmber : kRed, "%s", tip.c_str());
                        ImGui::Separator();
                    }
                    ImGui::TextDisabled("Click to rebind, right click to clear");
                    ImGui::EndTooltip();
                }
                if (pressed) {
                    cap_ = Capture();
                    cap_.active = true;
                    cap_.game = gameLayer;
                    cap_.action = d.id;
                    cap_.slot = s;
                    cap_.startTime = ImGui::GetTime();
                    cap_.mouseArmed = false;
                }
                if (has && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    layer.unbind(d.id, srcs[static_cast<size_t>(s)]);
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    // advanced: tokens we do not know (CMD:, other ACT:)
    std::vector<const BindingLine*> unknown;
    for (const auto& l : layer.lines) {
        bool all = true;
        for (const auto& a : l.actions) {
            bool known = false;
            for (const auto& d : defs)
                if (a == d.id)
                    known = true;
            if (!known)
                all = false;
        }
        if (!all)
            unknown.push_back(&l);
    }
    if (!unknown.empty()) {
        ImGui::Dummy(ImVec2(0, S(8)));
        if (ImGui::CollapsingHeader("Other bindings in the file")) {
            for (const BindingLine* l : unknown) {
                std::string acts;
                for (const auto& a : l->actions)
                    acts += (acts.empty() ? "" : " | ") + a;
                ImGui::TextColored(kCream, "%s", sourceDisplayName(l->source).c_str());
                ImGui::SameLine(S(200));
                ImGui::TextColored(kMuted, "%s", acts.c_str());
                ImGui::SameLine(S(520));
                ImGui::PushID(l->source.c_str());
                if (ImGui::SmallButton("Remove")) {
                    layer.clearSource(l->source);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    if (ImGui::Button(gameLayer ? "Reset gameplay keys to defaults" : "Reset menu keys to defaults"))
        resetBindings(gameLayer);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "Amber = key used for several actions. Red = key reserved by the port.");
}

void SettingsMenu::drawMouseGamepad() {
    drawSchemaPage("controls");
}

// The PS2 pad button an action id stands for; empty for actions with no pad equivalent (ACT:..).
static const char* padButtonName(const std::string& id) {
    static const struct {
        const char* id;
        const char* name;
    } map[] = {{"LSTICK_UP", "Left stick up"},
               {"LSTICK_DOWN", "Left stick down"},
               {"LSTICK_LEFT", "Left stick left"},
               {"LSTICK_RIGHT", "Left stick right"},
               {"RSTICK_UP", "Right stick up"},
               {"RSTICK_DOWN", "Right stick down"},
               {"RSTICK_LEFT", "Right stick left"},
               {"RSTICK_RIGHT", "Right stick right"},
               {"DPAD_UP", "D-pad up"},
               {"DPAD_DOWN", "D-pad down"},
               {"DPAD_LEFT", "D-pad left"},
               {"DPAD_RIGHT", "D-pad right"},
               {"TRIANGLE", "Triangle"},
               {"CROSS", "Cross"},
               {"SQUARE", "Square"},
               {"CIRCLE", "Circle"},
               {"L1", "L1"},
               {"L2", "L2"},
               {"L3", "L3"},
               {"R1", "R1"},
               {"R2", "R2"},
               {"R3", "R3"},
               {"SELECT", "Select"},
               {"START", "Start"}};
    for (const auto& m : map)
        if (id == m.id)
            return m.name;
    return "";
}

// Controller view of the Controls page: the game's own pad layout (read-only, the pad is not rebindable) and the
// gamepad options.
void SettingsMenu::drawControllerView() {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kMuted, "The original PlayStation 2 controls. Button prompts in the game show the pad icons.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(4)));
    drawSchemaPage("controls", true);
    ImGui::Dummy(ImVec2(0, S(6)));
    for (int layer = 0; layer < 2; ++layer) {
        const bool gameLayer = layer == 0;
        SectionTitle(gameLayer ? "In gameplay" : "In menus, inventory and cutscenes");
        if (ImGui::BeginTable(gameLayer ? "##pg" : "##pm", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Pad", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            for (const ActionDef& d : gameLayer ? gameActions() : menuActions()) {
                const char* btn = padButtonName(d.id);
                if (!*btn)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(d.label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kCream, "%s", btn);
            }
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, S(6)));
    }
}

void SettingsMenu::drawControls() {
    // View switch: keyboard and mouse (the port's bindings) or the controller (what the game shipped with). It drives
    // the existing "Button prompts" setting, so the two can never disagree.
    PromptStyle& view = p_.general.prompts;
    {
        const bool kb = view == PromptStyle::Keyboard;
        if (skinned()) {
            if (PlateWidget("Keyboard and mouse###cv0", ImVec2(S(340), S(46)), kb))
                view = PromptStyle::Keyboard;
            ImGui::SameLine();
            if (PlateWidget("Controller###cv1", ImVec2(S(250), S(46)), !kb))
                view = PromptStyle::Ps2;
        } else {
            if (ImGui::RadioButton("Keyboard and mouse", kb))
                view = PromptStyle::Keyboard;
            ImGui::SameLine();
            if (ImGui::RadioButton("Controller", !kb))
                view = PromptStyle::Ps2;
        }
        if (caps_.promptStyle) {
            ImGui::SameLine();
            ImGui::TextColored(kAmber, "*");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The button prompts in the game change after a restart");
        }
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    if (view == PromptStyle::Ps2) {
        drawControllerView();
        return;
    }
    if (skinned()) { // the game's brass plates as tabs
        if (forceTab_ >= 0)
            controlsTab_ = forceTab_;
        forceTab_ = -1;
        static const char* names[3] = {"Gameplay keys###ct0", "Menu keys###ct1", "Mouse and gamepad###ct2"};
        for (int i = 0; i < 3; ++i) {
            if (i)
                ImGui::SameLine();
            if (PlateWidget(names[i], ImVec2(S(i == 0 ? 285 : i == 1 ? 250 : 340), S(46)), controlsTab_ == i))
                controlsTab_ = i;
        }
        ImGui::Dummy(ImVec2(0, S(4)));
        if (controlsTab_ == 0)
            drawBindingTable(true);
        else if (controlsTab_ == 1) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kMuted,
                               "Active whenever the game is not in gameplay: title, pause, inventory, cutscenes. Esc "
                               "normally gives Circle and Start together (back, and resume from the pause menu).");
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, S(4)));
            drawBindingTable(false);
        } else
            drawMouseGamepad();
        return;
    }
    if (ImGui::BeginTabBar("##ctab")) {
        if (ImGui::BeginTabItem("Gameplay keys", nullptr, forceTab_ == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            controlsTab_ = 0;
            drawBindingTable(true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Menu keys", nullptr, forceTab_ == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            controlsTab_ = 1;
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kMuted,
                               "Active whenever the game is not in gameplay: title, pause, inventory, cutscenes. Esc "
                               "normally gives Circle and Start together (back, and resume from the pause menu).");
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, S(4)));
            drawBindingTable(false);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mouse and gamepad", nullptr, forceTab_ == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            controlsTab_ = 2;
            drawMouseGamepad();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    forceTab_ = -1;
}

void SettingsMenu::drawGeneral() {
    drawSchemaPage("gameplay");
    ImGui::Dummy(ImVec2(0, S(14)));
    SectionTitle("Hotkeys");
    ImGui::TextColored(kCream, "F1");
    ImGui::SameLine(S(200));
    ImGui::TextColored(kMuted, "Open / close this menu (also Back + LB + RB on a gamepad)");
    ImGui::TextColored(kCream, "Alt+Enter / F11");
    ImGui::SameLine(S(200));
    ImGui::TextColored(kMuted, "Toggle fullscreen");
    ImGui::TextColored(kCream, "%s",
                       p_.controls.toggleKey.empty() ? "F10" : sourceDisplayName(p_.controls.toggleKey).c_str());
    ImGui::SameLine(S(200));
    ImGui::TextColored(kMuted, "Free / capture the mouse");
    ImGui::Dummy(ImVec2(0, S(10)));
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(kMuted, "Settings are stored in drakan.ini and drakan_input.ini next to the game. Comments and "
                               "keys this menu does not know are kept.");
    ImGui::PopTextWrapPos();
}

void SettingsMenu::drawPopups() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    // ---- key capture ----
    if (cap_.active && !ImGui::IsPopupOpen("##capture"))
        ImGui::OpenPopup("##capture");
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(560), 0));
    if (BeginSkinModal("##capture",
                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav,
                       nullptr)) {
        if (!cap_.active)
            ImGui::CloseCurrentPopup();
        const auto& defs = cap_.game ? gameActions() : menuActions();
        const char* label = cap_.action.c_str();
        for (const auto& d : defs)
            if (cap_.action == d.id)
                label = d.label;
        if (fonts_.heading)
            ImGui::PushFont(fonts_.heading);
        ImGui::TextColored(kGoldHi, "%s", label);
        if (fonts_.heading)
            ImGui::PopFont();
        if (!cap_.conflictAsk) {
            ImGui::TextColored(kCream, "Press a key, scroll the mouse wheel,");
            ImGui::TextColored(kCream, "or click inside the box with a mouse button.");
            ImGui::Dummy(ImVec2(0, S(6)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 sz(ImGui::GetContentRegionAvail().x, S(86));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f);
            dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(ImVec4(0.16f, 0.12f, 0.07f, 1)),
                              4.0f);
            dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y),
                        ImGui::GetColorU32(ImVec4(kGold.x, kGold.y, kGold.z, 0.5f + 0.5f * pulse)), 4.0f, 0, 2.0f);
            const char* t = "Waiting for input...";
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f), ImGui::GetColorU32(kGoldHi), t);
            ImGui::InvisibleButton("##box", sz,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                       ImGuiButtonFlags_MouseButtonMiddle);
            if (cap_.mouseArmed && ImGui::IsItemHovered()) {
                for (int b = 0; b < 5; ++b)
                    if (ImGui::IsMouseClicked(b)) {
                        const char* n[] = {"MOUSE1", "MOUSE2", "MOUSE3", "MOUSE4", "MOUSE5"};
                        BindingLayer& layer = cap_.game ? p_.controls.game : p_.controls.menu;
                        auto others = layer.actionsFor(n[b]);
                        others.erase(std::remove(others.begin(), others.end(), cap_.action), others.end());
                        if (!others.empty()) {
                            cap_.conflictAsk = true;
                            cap_.pendingSource = n[b];
                        } else {
                            commitCapture(n[b], false);
                            ImGui::CloseCurrentPopup();
                        }
                        break;
                    }
            }
            ImGui::Dummy(ImVec2(0, S(6)));
            // Three buttons share the content width; fixed 200 px plates overflowed the 560 px popup.
            const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
            if (PlateButton("Bind Esc", ImVec2(bw, S(46)))) {
                BindingLayer& layer = cap_.game ? p_.controls.game : p_.controls.menu;
                auto others = layer.actionsFor("ESC");
                others.erase(std::remove(others.begin(), others.end(), cap_.action), others.end());
                if (!others.empty()) {
                    cap_.conflictAsk = true;
                    cap_.pendingSource = "ESC";
                } else {
                    commitCapture("ESC", false);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (PlateButton("Clear", ImVec2(bw, S(46)))) {
                clearSlot(cap_.game ? p_.controls.game : p_.controls.menu, cap_.action, cap_.slot);
                cap_ = Capture();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (PlateButton("Cancel", ImVec2(bw, S(46))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                cap_ = Capture();
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextColored(kMuted, "Esc cancels.");
        } else {
            BindingLayer& layer = cap_.game ? p_.controls.game : p_.controls.menu;
            ImGui::TextColored(kCream, "%s is already used for:", sourceDisplayName(cap_.pendingSource).c_str());
            for (const auto& a : layer.actionsFor(cap_.pendingSource)) {
                if (a == cap_.action)
                    continue;
                const char* nm = a.c_str();
                for (const auto& d : defs)
                    if (a == d.id)
                        nm = d.label;
                ImGui::TextColored(kAmber, "   %s", nm);
            }
            ImGui::Dummy(ImVec2(0, S(8)));
            if (PlateButton("Replace", ImVec2(S(200), S(46)))) {
                commitCapture(cap_.pendingSource, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (PlateButton("Use for both", ImVec2(S(200), S(46)))) {
                commitCapture(cap_.pendingSource, false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (PlateButton("Cancel", ImVec2(S(200), S(46)))) {
                cap_ = Capture();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    } else if (cap_.active && !ImGui::IsPopupOpen("##capture")) {
        // popup got dismissed by other means
    }

    // ---- quit to desktop ----
    if (openQuit_) {
        ImGui::OpenPopup("Quit to Desktop");
        openQuit_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (BeginSkinModal("Quit to Desktop", ImGuiWindowFlags_AlwaysAutoResize, "Quit to Desktop")) {
        ImGui::TextColored(kCream, "Close the game and return to the desktop?");
        ImGui::TextColored(kMuted, "Progress since your last save is lost.");
        ImGui::Dummy(ImVec2(0, S(10)));
        if (PlateButton("Quit", ImVec2(S(200), S(46)))) {
            ImGui::CloseCurrentPopup();
            host_.requestQuit();
            close();
        }
        ImGui::SameLine();
        if (PlateButton("Cancel", ImVec2(S(200), S(46))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- restart ----
    if (openRestart_) {
        ImGui::OpenPopup("Restart required");
        openRestart_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (BeginSkinModal("Restart required", ImGuiWindowFlags_AlwaysAutoResize, "Restart required")) {
        ImGui::TextColored(kCream, "These settings take effect after a restart:");
        for (const auto& r : restartReasons_)
            ImGui::TextColored(kAmber, "   %s", r.c_str());
        ImGui::Dummy(ImVec2(0, S(10)));
        if (PlateButton("Restart now", ImVec2(S(200), S(46)))) {
            ImGui::CloseCurrentPopup();
            host_.requestRestart(restartReasons_);
            close();
        }
        ImGui::SameLine();
        if (PlateButton("Later", ImVec2(S(200), S(46))))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- display change confirmation ----
    if (openRevert_) {
        ImGui::OpenPopup("Keep display settings?");
        openRevert_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (BeginSkinModal("Keep display settings?", ImGuiWindowFlags_AlwaysAutoResize, "Keep display settings?")) {
        const double left = revertDeadline_ - ImGui::GetTime();
        ImGui::TextColored(kCream, "The display settings were applied.");
        ImGui::TextColored(kCream, "If you can read this, keep them.");
        ImGui::TextColored(kAmber, "Reverting in %d seconds...", std::max(0, static_cast<int>(std::ceil(left))));
        ImGui::Dummy(ImVec2(0, S(10)));
        bool revert = left <= 0.0;
        if (PlateButton("Keep", ImVec2(S(200), S(46)))) {
            ImGui::CloseCurrentPopup();
            if (!restartReasons_.empty() && !openRestart_)
                openRestart_ = true;
        }
        ImGui::SameLine();
        if (PlateButton("Revert", ImVec2(S(200), S(46))))
            revert = true;
        if (revert) {
            ImGui::CloseCurrentPopup();
            revertVideo(host_, store_, a_, p_, revertTo_.video);
        }
        ImGui::EndPopup();
    }

    // ---- errors ----
    if (openError_) {
        ImGui::OpenPopup("Problem");
        openError_ = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (BeginSkinModal("Problem", ImGuiWindowFlags_AlwaysAutoResize, "Problem")) {
        ImGui::TextColored(kRed, "Something did not work:");
        ImGui::PushTextWrapPos(S(520));
        ImGui::TextUnformatted(errorText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(10)));
        if (PlateButton("OK", ImVec2(S(200), S(46))))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool SettingsMenu::frame() {
    if (!open_)
        return false;
    // Live setters are queued to the next renderer boundary. Refresh status
    // after that boundary instead of retaining the capability snapshot from open.
    if (ImGui::GetTime() >= capsRefreshAt_) {
        caps_ = host_.capabilities();
        capsRefreshAt_ = ImGui::GetTime() + .5;
    }
    ImGuiIO& io = ImGui::GetIO();
    // scale
    {
        int w = 0, h = 0;
        host_.windowSize(w, h);
        const float dh = io.DisplaySize.y > 0 ? io.DisplaySize.y : static_cast<float>(h);
        float sc = p_.general.uiScale > 0.0f ? p_.general.uiScale * (dh / 1080.0f > 0.9f ? 1.0f : 1.0f)
                                             : std::clamp(dh / 1080.0f, 0.62f, 3.0f);
        if (p_.general.uiScale > 0.0f)
            sc = p_.general.uiScale * std::clamp(dh / 1080.0f, 0.62f, 3.0f);
        scale_ = sc;
        const float fontScale = (24.0f / fonts_.bakedPx) * sc;
        if (std::fabs(sc - lastStyleScale_) > 0.001f) {
            applyDrakanStyle(sc);
            lastStyleScale_ = sc;
        }
        io.FontGlobalScale = fontScale;
    }
    if (fonts_.body)
        ImGui::PushFont(fonts_.body);
    g_sk = skinned() ? &skin_ : nullptr;
    g_scale = scale_;
    g_sound = [this](UiSound sd) { host_.playUiSound(sd); };
    { // the game's menu sounds: a tick when the highlight moves (keyboard / gamepad focus, or the mouse moving onto
      // another control)
        ImGuiContext& gc = *GImGui;
        const double now = ImGui::GetTime();
        static double lastTick = -1.0;
        bool tick = false;
        if (gc.NavId != lastNav_ && gc.NavId != 0 && lastNav_ != 0 && gc.NavCursorVisible && !cap_.active)
            tick = true;
        if (gc.HoveredId != lastHover_ && gc.HoveredId != 0 && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) &&
            gc.ActiveId == 0)
            tick = true;
        lastNav_ = gc.NavId;
        lastHover_ = gc.HoveredId;
        if (tick && now - lastTick > 0.05) {
            lastTick = now;
            host_.playUiSound(UiSound::Tick);
        }
    }
    io.MouseDrawCursor = g_sk == nullptr; // the game's own cursor is drawn below when the art is available

    // keep keyboard nav off while capturing so arrows etc. can be bound
    if (cap_.active) {
        if (!navFlagsSaved_) {
            savedNavFlags_ = io.ConfigFlags;
            navFlagsSaved_ = true;
        }
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    } else if (navFlagsSaved_) {
        io.ConfigFlags = savedNavFlags_;
        navFlagsSaved_ = false;
    }

    updateCapture();

    // global shortcuts (only when no capture / popup)
    const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    // dim the game
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(6, 4, 2, 190));

    const float pw = std::min(io.DisplaySize.x * 0.94f, S(1360));
    const float ph = std::min(io.DisplaySize.y * 0.92f, S(960));
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(pw, ph));
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleColor(ImGuiCol_Border, kGold);
    if (g_sk) {
        wf |= ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(40), S(34)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    }
    const bool began = ImGui::Begin("##drakan_settings", nullptr, wf);
    if (g_sk)
        ImGui::PopStyleVar(2);
    if (began) {
        if (g_sk) { // stone backdrop, soft vignette and the game's brass frame
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetWindowPos(), b(a.x + pw, a.y + ph);
            const float inset = S(6);
            g_sk->stone(dl, ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), S(420),
                        IM_COL32(120, 104, 94, 255));
            const ImU32 dk = IM_COL32(8, 5, 3, 150), cl = IM_COL32(8, 5, 3, 0);
            const float vw = S(120);
            dl->AddRectFilledMultiColor(ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, a.y + inset + vw), dk, dk,
                                        cl, cl);
            dl->AddRectFilledMultiColor(ImVec2(a.x + inset, b.y - inset - vw), ImVec2(b.x - inset, b.y - inset), cl, cl,
                                        dk, dk);
            g_sk->frame(dl, ImVec2(a.x - S(2), a.y - S(2)), ImVec2(b.x + S(2), b.y + S(2)), S(36));
        } else
        // double gold frame + corner ornaments
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 a = ImGui::GetWindowPos(), b = ImVec2(a.x + pw, a.y + ph);
            const float in = S(5);
            dl->AddRect(ImVec2(a.x + in, a.y + in), ImVec2(b.x - in, b.y - in), ImGui::GetColorU32(kGoldDim), 2.0f);
            const float cs = S(18);
            const ImU32 col = ImGui::GetColorU32(kGoldHi);
            const ImVec2 cr[4] = {
                {a.x + in, a.y + in}, {b.x - in, a.y + in}, {a.x + in, b.y - in}, {b.x - in, b.y - in}};
            const float sx[4] = {1, -1, 1, -1}, sy[4] = {1, 1, -1, -1};
            for (int i = 0; i < 4; ++i) {
                dl->AddLine(cr[i], ImVec2(cr[i].x + sx[i] * cs, cr[i].y), col, 3.0f);
                dl->AddLine(cr[i], ImVec2(cr[i].x, cr[i].y + sy[i] * cs), col, 3.0f);
                dl->AddCircleFilled(ImVec2(cr[i].x + sx[i] * S(4), cr[i].y + sy[i] * S(4)), S(3), col);
            }
        }
        drawHeader(ImGui::GetContentRegionAvail().x);
        ImGui::Separator();
        const float footerH = S(62);
        const float bodyH = ImGui::GetContentRegionAvail().y - footerH;
        drawSidebar(bodyH);
        ImGui::SameLine();
        ImGui::BeginChild("##content", ImVec2(0, bodyH), ImGuiChildFlags_Borders);
        if (demoScroll_ >= 0.0f)
            ImGui::SetScrollY(demoScroll_);
        switch (page_) {
            case Page::Display:
                drawDisplay();
                break;
            case Page::Audio:
                drawAudio();
                break;
            case Page::Controls:
                drawControls();
                break;
            case Page::General:
                drawGeneral();
                break;
        }
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, S(2)));
        drawFooter();

        drawPopups();
    }
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::End();
    ImGui::PopStyleColor();
    if (g_sk)
        g_sk->cursor(ImGui::GetForegroundDrawList(), io.MousePos, S(34));

    // keyboard shortcuts for page switching / closing
    if (!cap_.active && !popupOpen && open_) {
        const int n = 4;
        int pi = static_cast<int>(page_);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_GamepadR2)) {
            page_ = static_cast<Page>((pi + 1) % n);
            host_.playUiSound(UiSound::Tick);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_GamepadL2)) {
            page_ = static_cast<Page>((pi + n - 1) % n);
            host_.playUiSound(UiSound::Tick);
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) &&
            !ImGui::IsAnyItemActive() && windowFocused) {
            host_.playUiSound(UiSound::Back);
            close();
        }
    }
    if (fonts_.body)
        ImGui::PopFont();
    g_sk = nullptr;
    return open_;
}

} // namespace dsettings
