// Included by Menu.cpp inside its anonymous namespace (after the palette): the widgets drawn with the game's own art.
// ---- game skin layer ---------------------------------------------------------------------------------------------------
// Set by SettingsMenu::frame() while the game's art is available (null = plain style). Single ImGui context, one frame at a time.
const GameSkin* g_sk = nullptr;
float g_scale = 1.0f;
std::function<void(UiSound)> g_sound;
void Snd(UiSound s) { if (g_sound) g_sound(s); }

const ImU32 kBrassNormal = IM_COL32(176, 156, 126, 255), kBrassHover = IM_COL32(222, 200, 160, 255), kBrassFocus = IM_COL32(255, 238, 200, 255), kBrassDim = IM_COL32(96, 88, 76, 200);

bool IsAscii(const char* s, const char* e) { for (; s < e; ++s) if (static_cast<unsigned char>(*s) > 0x7e || static_cast<unsigned char>(*s) < 0x20) return false; return true; }
const char* LabelEnd(const char* l) { return ImGui::FindRenderedTextEnd(l); }

// Width of `text` in the game's font at `size` (falls back to the current font for characters the game font does not have).
float GameTextW(float size, const char* s, const char* e = nullptr) {
    if (!e) e = s + std::strlen(s);
    ImFont* f = (g_sk && g_sk->font && IsAscii(s, e)) ? g_sk->font : ImGui::GetFont();
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, s, e).x;
}
float GameTextH(float size) { return (g_sk && g_sk->font) ? size * (60.0f / 64.0f) : size; }
// Game font text with a dark outline, top-left at `pos`.
void GameText(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* s, const char* e = nullptr, bool shadow = true) {
    if (!e) e = s + std::strlen(s);
    ImFont* f = (g_sk && g_sk->font && IsAscii(s, e)) ? g_sk->font : ImGui::GetFont();
    if (shadow) {
        const float o = std::max(1.0f, size * 0.06f);
        const ImU32 sc = IM_COL32(12, 6, 2, (col >> 24) * 220 / 255);
        dl->AddText(f, size, ImVec2(pos.x + o, pos.y + o), sc, s, e);
        dl->AddText(f, size, ImVec2(pos.x - o * 0.5f, pos.y + o), sc, s, e);
    }
    dl->AddText(f, size, pos, col, s, e);
}
void GameTextCentered(ImDrawList* dl, ImVec2 a, ImVec2 b, float size, ImU32 col, const char* s, const char* e = nullptr) {
    if (!e) e = s + std::strlen(s);
    const float w = GameTextW(size, s, e), h = GameTextH(size);
    GameText(dl, size, ImVec2(std::floor(a.x + (b.x - a.x - w) * 0.5f), std::floor(a.y + (b.y - a.y - h) * 0.5f)), col, s, e);
}

// A brass plate with the label in its slot (buttons, tabs). Returns true when pressed. `selected` paints the plate bright.
bool PlateWidget(const char* label, ImVec2 size, bool selected, const char* badge = nullptr) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiID id = win->GetID(label);
    const char* le = LabelEnd(label);
    const float fh = ImGui::GetFrameHeight();
    const float h = size.y > 0 ? size.y : fh * 1.3f;
    const float k = h / 31.0f, cap = 30.0f * k;
    const float fsz = std::clamp(h * 0.50f, 10.0f, 80.0f);
    const float need = GameTextW(fsz, label, le) + cap * 2.0f + h * 0.2f;
    ImVec2 sz(size.x > 0 ? size.x : need, h);
    if (size.x < 0) sz.x = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImRect bb(p, ImVec2(p.x + sz.x, p.y + sz.y));
    ImGui::ItemSize(sz, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hov = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
    const bool dis = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    const bool foc = !dis && g.NavId == id && g.NavCursorVisible;
    ImDrawList* dl = win->DrawList;
    ImU32 tint = dis ? kBrassDim : (foc || selected) ? kBrassFocus : hov ? kBrassHover : kBrassNormal;
    if (held && !dis) tint = IM_COL32(150, 130, 100, 255);
    const float oy = held && !dis ? k : 0.0f;
    const ImVec2 a(bb.Min.x, bb.Min.y + oy), b(bb.Max.x, bb.Max.y + oy);
    g_sk->plate(dl, a, b, tint);
    ImVec2 sa, sb;
    GameSkin::plateSlot(a, b, sa, sb);
    if (!dis && (foc || hov || selected)) g_sk->fire(dl, sa, sb, foc ? 0.95f : selected ? 0.8f : 0.55f, ImGui::GetTime());
    const ImU32 tc = dis ? IM_COL32(140, 126, 100, 255) : (foc || hov || selected) ? IM_COL32(255, 232, 160, 255) : IM_COL32(226, 190, 100, 255);
    GameTextCentered(dl, ImVec2(sa.x, a.y), ImVec2(sb.x, b.y), fsz, tc, label, le);
    if (badge && *badge) {
        const float bw = GameTextW(fsz * 0.9f, badge);
        GameText(dl, fsz * 0.9f, ImVec2(sb.x - bw - cap * 0.1f, a.y + (h - GameTextH(fsz * 0.9f)) * 0.5f), IM_COL32(242, 158, 38, 255), badge);
    }
    if (pressed) Snd(UiSound::Confirm);
    return pressed;
}
bool PlateButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
    if (!g_sk) return ImGui::Button(label, size.y > 0 ? size : ImVec2(size.x, 0));
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight() * 1.45f;
    return PlateWidget(label, size, false);
}

// Brass groove + orb knob slider: ImGui does the interaction (invisible frame and grab), the art is drawn over the item rectangle.
template <class T>
bool SkinSlider(const char* id, T* v, T mn, T mx, const char* fmt, ImGuiSliderFlags flags, bool isFloat) {
    ImGuiContext& g = *GImGui;
    const float fh = ImGui::GetFrameHeight();
    char a[48], b[48];
    if (isFloat) { std::snprintf(a, sizeof a, fmt, static_cast<double>(mn)); std::snprintf(b, sizeof b, fmt, static_cast<double>(mx)); }
    else { std::snprintf(a, sizeof a, fmt, static_cast<int>(mn)); std::snprintf(b, sizeof b, fmt, static_cast<int>(mx)); }
    const float tw = std::max(ImGui::CalcTextSize(a).x, ImGui::CalcTextSize(b).x) + ImGui::CalcTextSize("0").x * 1.5f;
    const float total = ImGui::CalcItemWidth();
    const float rail = std::max(total - tw - g.Style.ItemInnerSpacing.x, fh * 3.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p.y + fh * 0.5f, gh = std::max(5.0f, fh * 0.2f);
    dl->AddRectFilled(ImVec2(p.x, cy - gh), ImVec2(p.x + rail, cy + gh), IM_COL32(14, 9, 5, 235), gh);
    dl->AddRect(ImVec2(p.x, cy - gh), ImVec2(p.x + rail, cy + gh), IM_COL32(150, 120, 64, 255), gh, 0, std::max(1.0f, g_scale * 1.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::SetNextItemWidth(rail);
    bool ch;
    if (isFloat) ch = ImGui::SliderFloat(id, reinterpret_cast<float*>(v), static_cast<float>(mn), static_cast<float>(mx), "", flags);
    else ch = ImGui::SliderInt(id, reinterpret_cast<int*>(v), static_cast<int>(mn), static_cast<int>(mx), "", flags);
    ImGui::PopStyleColor(6);
    const bool hov = ImGui::IsItemHovered(), foc = ImGui::IsItemFocused() && g.NavCursorVisible, act = ImGui::IsItemActive();
    const ImVec2 mi = ImGui::GetItemRectMin(), ma = ImGui::GetItemRectMax();
    const double dv = static_cast<double>(*v), dmn = static_cast<double>(mn), dmx = static_cast<double>(mx);
    double t = dmx > dmn ? (dv - dmn) / (dmx - dmn) : 0.0;
    if ((flags & ImGuiSliderFlags_Logarithmic) && dmn > 0.0 && dv > 0.0) t = (std::log(dv) - std::log(dmn)) / (std::log(dmx) - std::log(dmn));
    t = std::clamp(t, 0.0, 1.0);
    const float gs = g.Style.GrabMinSize, r = fh * 0.46f;
    const float cx = mi.x + gs * 0.5f + static_cast<float>(t) * (ma.x - mi.x - gs);
    dl->AddRectFilled(ImVec2(p.x + 2.0f, cy - gh + 2.0f), ImVec2(std::max(cx, p.x + 4.0f), cy + gh - 2.0f), IM_COL32(200, 120, 36, 230), gh);
    if (foc) g_sk->fire(dl, ImVec2(p.x, cy - gh), ImVec2(p.x + rail, cy + gh), 0.7f, ImGui::GetTime());
    g_sk->orb(dl, ImVec2(cx + r * 0.08f, cy + r * 0.14f), r * 1.06f, IM_COL32(0, 0, 0, 120));   // shadow
    g_sk->orb(dl, ImVec2(cx, cy), r, act ? IM_COL32(255, 232, 150, 255) : (hov || foc) ? IM_COL32(255, 206, 96, 255) : IM_COL32(226, 168, 62, 255));
    ImGui::SameLine(0.0f, g.Style.ItemInnerSpacing.x);
    ImGui::AlignTextToFramePadding();
    char buf[64];
    if (isFloat) std::snprintf(buf, sizeof buf, fmt, static_cast<double>(*v)); else std::snprintf(buf, sizeof buf, fmt, static_cast<int>(*v));
    ImGui::TextColored(kGoldHi, "%s", buf);
    return ch;
}
bool SliderFloat(const char* id, float* v, float mn, float mx, const char* fmt = "%.2f", ImGuiSliderFlags flags = 0) {
    if (!g_sk) return ImGui::SliderFloat(id, v, mn, mx, fmt, flags);
    return SkinSlider<float>(id, v, mn, mx, fmt, flags, true);
}
bool SliderInt(const char* id, int* v, int mn, int mx, const char* fmt = "%d") {
    if (!g_sk) return ImGui::SliderInt(id, v, mn, mx, fmt);
    return SkinSlider<int>(id, v, mn, mx, fmt, 0, false);
}

// Combo with a brass plate instead of the frame; the popup itself is the normal ImGui popup in brass colours.
bool ComboBegin(const char* id, const char* preview) {
    if (!g_sk) return ImGui::BeginCombo(id, preview);
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    if (win->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const float fh = ImGui::GetFrameHeight();
    const float w = ImGui::CalcItemWidth();
    const float h = fh * 1.18f, k = h / 31.0f, cap = std::min(30.0f * k, w * 0.45f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID wid = win->GetID(id);
    const bool dis = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    const bool hov = !dis && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h)) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool foc = !dis && g.NavId == wid && g.NavCursorVisible;
    ImDrawList* dl = win->DrawList;
    g_sk->plate(dl, p, ImVec2(p.x + w, p.y + h), dis ? kBrassDim : (foc ? kBrassFocus : hov ? kBrassHover : kBrassNormal));
    ImVec2 sa, sb;
    GameSkin::plateSlot(p, ImVec2(p.x + w, p.y + h), sa, sb);
    if (foc) g_sk->fire(dl, sa, sb, 0.8f, ImGui::GetTime());
    else if (hov) g_sk->fire(dl, sa, sb, 0.4f, ImGui::GetTime());
    // ImGui draws the (transparent) frame and the preview text; the preview starts after the left cap
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(cap * 0.95f, g.Style.FramePadding.y + (h - fh) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.93f, 0.72f, dis ? 0.5f : 1.0f));
    ImGui::SetNextItemWidth(w);
    const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    g_sk->arrow(dl, ImVec2(p.x + w - cap * 0.95f, p.y + h * 0.5f), h * 0.50f, 3, dis ? kBrassDim : IM_COL32(255, 220, 140, 255));
    if (open) Snd(UiSound::Tick);
    return open;
}

// A modal popup in the game's look (stone + brass frame, title in the game font). Falls back to the plain ImGui popup without the art.
bool BeginSkinModal(const char* name, ImGuiWindowFlags fl, const char* title) {
    if (!g_sk) return ImGui::BeginPopupModal(name, nullptr, fl);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40.0f * g_scale, 34.0f * g_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    const bool open = ImGui::BeginPopupModal(name, nullptr, fl | ImGuiWindowFlags_NoTitleBar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize(), b(a.x + sz.x, a.y + sz.y);
    const float inset = 6.0f * g_scale;
    g_sk->stone(dl, ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), 420.0f * g_scale, IM_COL32(110, 96, 86, 255));
    g_sk->frame(dl, ImVec2(a.x - 2.0f * g_scale, a.y - 2.0f * g_scale), ImVec2(b.x + 2.0f * g_scale, b.y + 2.0f * g_scale), 36.0f * g_scale);
    if (title) {
        const float fs = 34.0f * g_scale;
        GameText(dl, fs, ImGui::GetCursorScreenPos(), IM_COL32(255, 220, 130, 255), title);
        ImGui::Dummy(ImVec2(GameTextW(fs, title), GameTextH(fs) + 12.0f * g_scale));
    }
    return true;
}
