#include "gameui/PauseScreen.h"

#include <cmath>
#include <cstdlib>

#include "gameui/UnifiedOptions.h"

#include <algorithm>
#include <cmath>

#include "Paint.h"

namespace gameui {

namespace {

using detail::fmtInt;

constexpr float kMargin = 96;
constexpr float kPanelY = 160, kPanelH = 660;
constexpr float kMenuX = kMargin, kMenuW = 560;
constexpr float kStatusW = 560, kStatusX = kDesignW - kMargin - kStatusW;
constexpr float kRowX = kMenuX + 28, kRowW = kMenuW - 56, kRowY = kPanelY + 36, kRowH = 88, kRowStep = 104;
constexpr float kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36;
// Slider rows: the label on the upper line, the track below it.
constexpr float kTrackInset = 32, kTrackY = 58, kTrackH = 14;
// Choice rows: "<  value  >" at the right.
constexpr float kChoiceW = 132, kChoiceArrowW = 36;
// Confirm card.
constexpr float kCardW = 720, kCardHeadH = 176, kCardRowH = 76, kCardRowStep = 88, kCardPad = 44;

const char* schemeDescription(int32_t scheme) {
    switch (scheme) {
        case 0:
            return "Scheme A: left stick turns and moves, right stick orbits the camera.";
        case 1:
            return "Scheme B: the sticks as scheme A, an alternative button layout.";
        case 2:
            return "Scheme C: twin stick. Left stick moves and strafes, right stick turns and looks.";
        default:
            return "";
    }
}

const char* rowDescription(PauseRow r, bool enabled, int32_t scheme) {
    switch (r) {
        case PauseRow::Resume:
            return "Return to the game.";
        case PauseRow::Save:
            return enabled ? "Save your progress to the memory card." : "Saving is not possible right now.";
        case PauseRow::Load:
            return enabled ? "Load a saved game." : "No saved games found.";
        case PauseRow::Options:
            return "Controller, audio and PC settings.";
        case PauseRow::Quit:
            return "Leave the game. Unsaved progress is lost.";
        case PauseRow::Controller:
            return "Button scheme, vibration and pitch.";
        case PauseRow::Audio:
            return "Volume, subtitles and stereo sound.";
        case PauseRow::PcSettings:
            return "Display, graphics, controls and more.";
        case PauseRow::Defaults:
            return "Restore the default game options.";
        case PauseRow::Back:
            return "Back to the previous page.";
        case PauseRow::MasterVolume:
            return enabled ? "Volume of all sound." : "The audio options are not available right now.";
        case PauseRow::MusicVolume:
            return enabled ? "Volume of the music." : "The audio options are not available right now.";
        case PauseRow::Subtitles:
            return enabled ? "Show subtitles in cut scenes." : "The audio options are not available right now.";
        case PauseRow::Stereo:
            return enabled ? "Stereo or mono sound output." : "The audio options are not available right now.";
        case PauseRow::Scheme:
            return enabled ? schemeDescription(scheme) : "The controller options are not available right now.";
        case PauseRow::Vibration:
            return enabled ? "Controller vibration." : "The controller options are not available right now.";
        case PauseRow::ReversePitch:
            return enabled ? "Invert looking up and down." : "The controller options are not available right now.";
        case PauseRow::KeyBindings:
            return "Keyboard, mouse and gamepad bindings (PC Settings).";
        case PauseRow::QuitToTitle:
            return enabled ? "End the game and return to the title screen." : "Not possible right now.";
        case PauseRow::QuitToDesktop:
            return "Close the game.";
        case PauseRow::RestoreDefaults:
            return "Reset the audio and controller options.";
        case PauseRow::Cancel:
            return "";
    }
    return "";
}

bool isSetAction(PauseActionKind k) {
    return pauseActionOption(k).has_value();
}

} // namespace

const char* pauseActionName(PauseActionKind k) {
    switch (k) {
        case PauseActionKind::Resume:
            return "Resume";
        case PauseActionKind::OpenSave:
            return "OpenSave";
        case PauseActionKind::OpenLoad:
            return "OpenLoad";
        case PauseActionKind::OpenOptions:
            return "OpenOptions";
        case PauseActionKind::Quit:
            return "Quit";
        case PauseActionKind::OpenController:
            return "OpenController";
        case PauseActionKind::OpenAudio:
            return "OpenAudio";
        case PauseActionKind::OpenPcSettings:
            return "OpenPcSettings";
        case PauseActionKind::DefaultSettings:
            return "DefaultSettings";
        case PauseActionKind::EnterPage:
            return "EnterPage";
        case PauseActionKind::LeavePage:
            return "LeavePage";
        case PauseActionKind::SetMasterVolume:
            return "SetMasterVolume";
        case PauseActionKind::SetMusicVolume:
            return "SetMusicVolume";
        case PauseActionKind::SetSubtitles:
            return "SetSubtitles";
        case PauseActionKind::SetStereo:
            return "SetStereo";
        case PauseActionKind::SetControllerScheme:
            return "SetControllerScheme";
        case PauseActionKind::SetVibration:
            return "SetVibration";
        case PauseActionKind::SetReversePitch:
            return "SetReversePitch";
        case PauseActionKind::RestoreDefaults:
            return "RestoreDefaults";
        case PauseActionKind::OpenKeyBindings:
            return "OpenKeyBindings";
        case PauseActionKind::QuitToTitle:
            return "QuitToTitle";
        case PauseActionKind::QuitToDesktop:
            return "QuitToDesktop";
    }
    return "?";
}

std::optional<PauseOption> pauseActionOption(PauseActionKind k) {
    switch (k) {
        case PauseActionKind::SetMasterVolume:
            return PauseOption::MasterVolume;
        case PauseActionKind::SetMusicVolume:
            return PauseOption::MusicVolume;
        case PauseActionKind::SetSubtitles:
            return PauseOption::Subtitles;
        case PauseActionKind::SetStereo:
            return PauseOption::Stereo;
        case PauseActionKind::SetControllerScheme:
            return PauseOption::Scheme;
        case PauseActionKind::SetVibration:
            return PauseOption::Vibration;
        case PauseActionKind::SetReversePitch:
            return PauseOption::ReversePitch;
        default:
            return std::nullopt;
    }
}

PauseActionKind pauseSetAction(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
            return PauseActionKind::SetMasterVolume;
        case PauseOption::MusicVolume:
            return PauseActionKind::SetMusicVolume;
        case PauseOption::Subtitles:
            return PauseActionKind::SetSubtitles;
        case PauseOption::Stereo:
            return PauseActionKind::SetStereo;
        case PauseOption::Scheme:
            return PauseActionKind::SetControllerScheme;
        case PauseOption::Vibration:
            return PauseActionKind::SetVibration;
        case PauseOption::ReversePitch:
            return PauseActionKind::SetReversePitch;
    }
    return PauseActionKind::SetMasterVolume;
}

const char* pausePageName(PausePage p) {
    switch (p) {
        case PausePage::Root:
            return "Paused";
        case PausePage::Options:
            return "Options";
        case PausePage::Audio:
            return "Audio";
        case PausePage::Controller:
            return "Controller";
        case PausePage::QuitConfirm:
            return "Quit";
        case PausePage::DefaultsConfirm:
            return "Default Settings";
    }
    return "";
}

const char* pauseRowLabel(PauseRow r) {
    switch (r) {
        case PauseRow::Resume:
            return "Resume";
        case PauseRow::Save:
            return "Save Game";
        case PauseRow::Load:
            return "Load Game";
        case PauseRow::Options:
            return "Options";
        case PauseRow::Quit:
            return "Quit";
        case PauseRow::Controller:
            return "Controller";
        case PauseRow::Audio:
            return "Audio";
        case PauseRow::PcSettings:
            return "PC Settings";
        case PauseRow::Defaults:
            return "Default Settings";
        case PauseRow::Back:
            return "Back";
        case PauseRow::MasterVolume:
            return "Master Volume";
        case PauseRow::MusicVolume:
            return "Music Volume";
        case PauseRow::Subtitles:
            return "Show Subtitles";
        case PauseRow::Stereo:
            return "Stereo Sound";
        case PauseRow::Scheme:
            return "Control Scheme";
        case PauseRow::Vibration:
            return "Vibration";
        case PauseRow::ReversePitch:
            return "Reverse Pitch";
        case PauseRow::KeyBindings:
            return "Key Bindings";
        case PauseRow::QuitToTitle:
            return "Quit to Title";
        case PauseRow::QuitToDesktop:
            return "Quit to Desktop";
        case PauseRow::RestoreDefaults:
            return "Restore Defaults";
        case PauseRow::Cancel:
            return "Cancel";
    }
    return "";
}

PauseRowKind pauseRowKind(PauseRow r) {
    switch (r) {
        case PauseRow::MasterVolume:
        case PauseRow::MusicVolume:
            return PauseRowKind::Slider;
        case PauseRow::Subtitles:
        case PauseRow::Stereo:
        case PauseRow::Vibration:
        case PauseRow::ReversePitch:
            return PauseRowKind::Toggle;
        case PauseRow::Scheme:
            return PauseRowKind::Choice;
        default:
            return PauseRowKind::Button;
    }
}

std::optional<PauseOption> pauseRowOption(PauseRow r) {
    switch (r) {
        case PauseRow::MasterVolume:
            return PauseOption::MasterVolume;
        case PauseRow::MusicVolume:
            return PauseOption::MusicVolume;
        case PauseRow::Subtitles:
            return PauseOption::Subtitles;
        case PauseRow::Stereo:
            return PauseOption::Stereo;
        case PauseRow::Scheme:
            return PauseOption::Scheme;
        case PauseRow::Vibration:
            return PauseOption::Vibration;
        case PauseRow::ReversePitch:
            return PauseOption::ReversePitch;
        default:
            return std::nullopt;
    }
}

const char* pauseSchemeLabel(int32_t scheme) {
    switch (scheme) {
        case 0:
            return "A";
        case 1:
            return "B";
        case 2:
            return "C";
        default:
            return "?";
    }
}

// ---- queue ----

uint32_t PauseActionQueue::push(PauseAction a) {
    std::lock_guard<std::mutex> lock(m_);
    if (isSetAction(a.kind) && !q_.empty() && q_.back().kind == a.kind) {
        q_.back().value = a.value;
        return q_.back().id;
    }
    a.id = nextId_++;
    q_.push_back(a);
    return a.id;
}

std::optional<PauseAction> PauseActionQueue::pop() {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.empty())
        return std::nullopt;
    PauseAction a = q_.front();
    q_.pop_front();
    return a;
}

size_t PauseActionQueue::size() const {
    std::lock_guard<std::mutex> lock(m_);
    return q_.size();
}

void PauseActionQueue::clear() {
    std::lock_guard<std::mutex> lock(m_);
    q_.clear();
}

// ---- layout ----

namespace design::pause {
Rect menuPanel() {
    return {kMenuX, kPanelY, kMenuW, kPanelH};
}
Rect row(int i) {
    return {kRowX, kRowY + static_cast<float>(i) * kRowStep, kRowW, kRowH};
}
Rect statusPanel() {
    return {kStatusX, kPanelY, kStatusW, kPanelH};
}
Rect sliderTrack(int i) {
    const Rect r = row(i);
    return {r.x + kTrackInset, r.y + kTrackY, r.w - 2 * kTrackInset, kTrackH};
}
Rect choicePrev(int i) {
    const Rect r = row(i);
    return {r.right() - 32 - kChoiceW, r.y, kChoiceArrowW, r.h};
}
Rect cardPanel(int rows) {
    const float h = kCardHeadH + static_cast<float>(rows) * kCardRowStep + kCardPad - (kCardRowStep - kCardRowH);
    return {(kDesignW - kCardW) * 0.5f, (kDesignH - h) * 0.5f, kCardW, h};
}
Rect cardRow(int i, int rows) {
    const Rect c = cardPanel(rows);
    return {c.x + kCardPad, c.y + kCardHeadH + static_cast<float>(i) * kCardRowStep, c.w - 2 * kCardPad, kCardRowH};
}
} // namespace design::pause

// ---- state machine ----

PauseScreen::PauseScreen(PauseConfig cfg) : cfg_(std::move(cfg)), pcPages_(SettingsPagesConfig{cfg_.screen}) {}

float PauseScreen::dx() const {
    return cfg_.centred ? (kDesignW - kMenuW) * 0.5f - kMenuX : 0.0f;
}

void PauseScreen::open() {
    open_ = true;
    page_ = cfg_.rootPage;
    focus_ = hover_ = {};
    stack_.clear();
    for (Override& o : overrides_)
        o = {};
    dragRow_.reset();
    suspendLeft_ = 0;
    pcOpen_ = false;
    pcPages_.close();
    unified_ = false;
    nativeSub_ = PausePage::Options;
    // Embedded on the Options page (the title's Options): straight onto the unified pages once a schema arrives.
    unifiedPending_ = cfg_.unifiedOptions && cfg_.pcSettingsPages && cfg_.rootPage == PausePage::Options;
}

void PauseScreen::reactivate() {
    open_ = true;
    suspendLeft_ = 0;
}

void PauseScreen::close() {
    open_ = false;
    suspendLeft_ = 0;
    dragRow_.reset();
    pcOpen_ = false;
    pcPages_.close();
    unified_ = unifiedPending_ = false;
    targets_.clear();
    hints_.clear();
}

void PauseScreen::openUnified(PauseFrameOutput* out) {
    if (out) // from the root page: open the native Options screen under the pages, Back returns to the Options row
        pushPage(PausePage::Options, PauseRow::Controller, *out);
    std::vector<std::string> trail;
    if (!cfg_.rootTitle.empty())
        trail.push_back(cfg_.rootTitle);
    else if (cfg_.rootPage == PausePage::Root)
        trail.push_back(pausePageName(PausePage::Root));
    pcPages_.setTrail(trail);
    pcPages_.setTitle("Options");
    pcPages_.open();
    pcOpen_ = unified_ = true;
    unifiedPending_ = false;
    nativeSub_ = PausePage::Options;
}

void PauseScreen::syncNativePage(PauseFrameOutput& out, bool closing) {
    PausePage want = PausePage::Options;
    if (!closing && pcPages_.view() == SettingsView::Page) {
        const int i = pcPages_.pageIndex();
        if (i >= 0 && i < static_cast<int>(merged_.pages.size())) {
            const std::string& id = merged_.pages[static_cast<size_t>(i)].id;
            want = id == "audio" ? PausePage::Audio : id == "controls" ? PausePage::Controller : PausePage::Options;
        }
    }
    if (want == nativeSub_)
        return;
    if (nativeSub_ != PausePage::Options)
        out.actions.push_back({0, PauseActionKind::LeavePage, static_cast<int32_t>(nativeSub_)});
    if (want != PausePage::Options)
        out.actions.push_back({0, PauseActionKind::EnterPage, static_cast<int32_t>(want)});
    nativeSub_ = want;
}

void PauseScreen::splitUnified(std::vector<SettingsAction>& in, PauseFrameOutput& out) {
    const auto set = [&](PauseOption o, int32_t v) {
        const PauseActionKind k = pauseSetAction(o);
        v = clampPauseOption(o, v);
        // One Set per option and frame (a drag queues its last value), as setOption.
        auto it =
            std::find_if(out.actions.begin(), out.actions.end(), [&](const PauseAction& a) { return a.kind == k; });
        if (it != out.actions.end())
            it->value = v;
        else
            out.actions.push_back({0, k, v});
    };
    for (SettingsAction& a : in) {
        if (a.kind == SettingsActionKind::Change) {
            if (const std::optional<PauseOption> o = gameOptionFromKey(a.key)) {
                char* end = nullptr;
                const double v = std::strtod(a.value.c_str(), &end);
                if (end != a.value.c_str())
                    set(*o, static_cast<int32_t>(std::lround(v)));
                continue;
            }
        }
        if (a.kind == SettingsActionKind::ResetPage) // the page's PC defaults and the game's own defaults on it
            for (int i = 0; i < kPauseOptionCount; ++i) {
                const auto o = static_cast<PauseOption>(i);
                if (a.page == gameOptionPage(o))
                    set(o, gameOptionDefault(o));
            }
        out.settings.push_back(std::move(a));
    }
}

std::vector<PausePage> PauseScreen::pageStack() const {
    std::vector<PausePage> v;
    for (const StackEntry& e : stack_)
        v.push_back(e.page);
    return v;
}

std::vector<PauseRow> PauseScreen::rowsOf(PausePage p) {
    switch (p) {
        case PausePage::Root:
            break;
        case PausePage::Options:
            return {PauseRow::Controller, PauseRow::Audio, PauseRow::PcSettings, PauseRow::Defaults, PauseRow::Back};
        case PausePage::Audio:
            return {PauseRow::MasterVolume, PauseRow::MusicVolume, PauseRow::Subtitles, PauseRow::Stereo,
                    PauseRow::Back};
        case PausePage::Controller:
            return {PauseRow::Scheme, PauseRow::Vibration, PauseRow::ReversePitch, PauseRow::KeyBindings,
                    PauseRow::Back};
        case PausePage::QuitConfirm:
            return {PauseRow::QuitToTitle, PauseRow::QuitToDesktop, PauseRow::Cancel};
        case PausePage::DefaultsConfirm:
            return {PauseRow::RestoreDefaults, PauseRow::Cancel};
    }
    return {PauseRow::Resume, PauseRow::Save, PauseRow::Load, PauseRow::Options, PauseRow::Quit};
}

std::vector<PauseRow> PauseScreen::rows() const {
    return rowsOf(page_);
}

bool PauseScreen::rowEnabled(PauseRow r, const PauseState& st) const {
    switch (r) {
        case PauseRow::Save:
            return st.canSave;
        case PauseRow::Load:
            return st.canLoad;
        case PauseRow::Options:
            return st.canOptions;
        case PauseRow::Quit:
            return st.canQuit;
        case PauseRow::QuitToTitle:
            return st.canQuitToTitle;
        default:
            return pauseRowOption(r) ? st.options.valid : true;
    }
}

int32_t PauseScreen::optionValue(PauseOption o, const PauseState& st) const {
    const Override& ov = overrides_[static_cast<int>(o)];
    return ov.active ? ov.value : st.options.get(o);
}

const HitTarget* PauseScreen::findTarget(const TargetId& id) const {
    for (const HitTarget& t : targets_)
        if (t.id == id)
            return &t;
    return nullptr;
}

Rect PauseScreen::rowRect(PausePage p, int i) const {
    if (isCard(p))
        return design::pause::cardRow(i, static_cast<int>(rowsOf(p).size()));
    return shifted(design::pause::row(i));
}

Rect PauseScreen::sliderTrack(PauseRow r) const {
    if (pauseRowKind(r) != PauseRowKind::Slider)
        return {};
    const std::vector<PauseRow> rs = rows();
    for (size_t i = 0; i < rs.size(); ++i)
        if (rs[i] == r)
            return lf_.map(shifted(design::pause::sliderTrack(static_cast<int>(i))));
    return {};
}

void PauseScreen::buildTargets(const PauseState& st) {
    targets_.clear();
    const std::vector<PauseRow> rs = rows();
    for (size_t i = 0; i < rs.size(); ++i) {
        const bool en = rowEnabled(rs[i], st);
        targets_.push_back(
            {{TargetKind::MenuRow, static_cast<int>(rs[i])}, lf_.map(rowRect(page_, static_cast<int>(i))), en, en});
    }
}

void PauseScreen::ensureFocus() {
    if (const HitTarget* t = findTarget(focus_); t && t->navigable)
        return;
    // The focused row became disabled (or none yet): the nearest enabled row below, else above.
    const HitTarget* cur = findTarget(focus_);
    focus_ = {};
    if (cur) {
        const auto it = std::find_if(targets_.begin(), targets_.end(), [&](const HitTarget& t) { return &t == cur; });
        for (auto j = it; j != targets_.end(); ++j)
            if (j->navigable && j->id.kind == TargetKind::MenuRow) {
                focus_ = j->id;
                return;
            }
    }
    for (const HitTarget& t : targets_)
        if (t.navigable && t.id.kind == TargetKind::MenuRow) {
            focus_ = t.id;
            return;
        }
}

void PauseScreen::buildHints(const PauseState& st) {
    hints_.clear();
    const bool row = focus_.kind == TargetKind::MenuRow;
    const PauseRow fr = row ? static_cast<PauseRow>(focus_.index) : PauseRow::Resume;
    const PauseRowKind kind = row ? pauseRowKind(fr) : PauseRowKind::Button;
    const bool en = row && rowEnabled(fr, st);
    if (kind == PauseRowKind::Slider || kind == PauseRowKind::Choice || kind == PauseRowKind::Toggle)
        hints_.push_back({Prompt::Adjust, "Adjust", en});
    if (kind != PauseRowKind::Slider)
        hints_.push_back({Prompt::Confirm,
                          kind == PauseRowKind::Toggle   ? "Toggle"
                          : kind == PauseRowKind::Choice ? "Change"
                                                         : "Select",
                          en});
    hints_.push_back({Prompt::Back, isCard(page_)
                                        ? "Cancel"
                                        : (stack_.empty() && cfg_.rootPage == PausePage::Root ? "Resume" : "Back")});
    const detail::Paint p(lf_, cfg_.screen);
    const std::vector<Rect> rs =
        p.layoutHints(hints_, kDesignW - kMargin, false, kFooterY, kHintH, kHintGlyph, kHintGap, FontRole::Body);
    for (size_t i = 0; i < hints_.size(); ++i)
        targets_.push_back(
            {{TargetKind::Hint, static_cast<int>(hints_[i].prompt)}, lf_.map(rs[i]), false, hints_[i].enabled});
}

void PauseScreen::pushPage(PausePage p, PauseRow focusRow, PauseFrameOutput& out) {
    stack_.push_back({page_, focus_});
    page_ = p;
    focus_ = {TargetKind::MenuRow, static_cast<int>(focusRow)};
    dragRow_.reset();
    if (cfg_.hostPages && !isCard(p))
        out.actions.push_back({0, PauseActionKind::EnterPage, static_cast<int32_t>(p)});
}

bool PauseScreen::setOption(PauseOption o, int32_t v, const PauseState& st, PauseFrameOutput& out, bool sound) {
    v = clampPauseOption(o, v);
    if (v == optionValue(o, st))
        return false;
    overrides_[static_cast<int>(o)] = {true, v, 0};
    // One Set per option and frame (a drag and a key in the same frame keep the last).
    const PauseActionKind k = pauseSetAction(o);
    auto it = std::find_if(out.actions.begin(), out.actions.end(), [&](const PauseAction& a) { return a.kind == k; });
    if (it != out.actions.end())
        it->value = v;
    else
        out.actions.push_back({0, k, v});
    if (sound && out.sound == UiSound::None)
        out.sound = o == PauseOption::MasterVolume || o == PauseOption::MusicVolume ? UiSound::Slider : UiSound::Toggle;
    return true;
}

void PauseScreen::adjust(PauseRow r, int dir, const PauseState& st, PauseFrameOutput& out) {
    const std::optional<PauseOption> o = pauseRowOption(r);
    if (!o || !rowEnabled(r, st) || dir == 0)
        return;
    const int32_t cur = optionValue(*o, st);
    switch (pauseRowKind(r)) {
        case PauseRowKind::Slider:
            setOption(*o, cur + dir * std::max(1, cfg_.sliderStep), st, out, true);
            break;
        case PauseRowKind::Toggle:
            setOption(*o, cur ? 0 : 1, st, out, true);
            break;
        case PauseRowKind::Choice: {
            const PauseOptionRange rg = pauseOptionRange(*o);
            const int32_t n = rg.hi - rg.lo + 1;
            setOption(*o, rg.lo + ((cur - rg.lo + dir) % n + n) % n, st, out, true);
            break;
        }
        case PauseRowKind::Button:
            break;
    }
}

void PauseScreen::sliderFromPointer(PauseRow r, float x, const PauseState& st, PauseFrameOutput& out) {
    const std::optional<PauseOption> o = pauseRowOption(r);
    const Rect t = sliderTrack(r);
    if (!o || t.empty())
        return;
    const PauseOptionRange rg = pauseOptionRange(*o);
    const float f = std::clamp((x - t.x) / t.w, 0.0f, 1.0f);
    setOption(*o, rg.lo + static_cast<int32_t>(std::lround(f * static_cast<float>(rg.hi - rg.lo))), st, out, false);
}

void PauseScreen::activate(PauseRow r, const PauseState& st, PauseFrameOutput& out) {
    if (!rowEnabled(r, st))
        return;
    auto emit = [&](PauseActionKind k) {
        out.actions.push_back({0, k});
        suspendLeft_ = cfg_.suspendFrames; // a native child opens: wait for it (reactivate) before taking input
    };
    switch (r) {
        case PauseRow::Resume:
            out.actions.push_back({0, PauseActionKind::Resume});
            out.wantsResume = true;
            break;
        case PauseRow::Save:
            emit(PauseActionKind::OpenSave);
            break;
        case PauseRow::Load:
            emit(PauseActionKind::OpenLoad);
            break;
        case PauseRow::Options:
            if (cfg_.unifiedOptions && cfg_.pcSettingsPages && st.pcSettings)
                openUnified(&out);
            else if (hostOptions())
                pushPage(PausePage::Options, PauseRow::Controller, out);
            else
                emit(PauseActionKind::OpenOptions);
            break;
        case PauseRow::Quit:
            if (cfg_.quitCard)
                pushPage(PausePage::QuitConfirm, st.canQuitToTitle ? PauseRow::QuitToTitle : PauseRow::QuitToDesktop,
                         out);
            else
                emit(PauseActionKind::Quit);
            break;
        case PauseRow::Controller:
            if (cfg_.hostPages)
                pushPage(PausePage::Controller, PauseRow::Scheme, out);
            else
                emit(PauseActionKind::OpenController);
            break;
        case PauseRow::Audio:
            if (cfg_.hostPages)
                pushPage(PausePage::Audio, PauseRow::MasterVolume, out);
            else
                emit(PauseActionKind::OpenAudio);
            break;
        case PauseRow::PcSettings:
            if (cfg_.pcSettingsPages && st.pcSettings) { // the modern pages, inside the pause menu
                pcPages_.setTrail({pausePageName(PausePage::Root), pausePageName(PausePage::Options)});
                pcPages_.setTitle("PC Settings");
                pcPages_.open();
                pcOpen_ = true;
            } else
                emit(PauseActionKind::OpenPcSettings);
            break;
        case PauseRow::Defaults:
            if (cfg_.hostPages)
                pushPage(PausePage::DefaultsConfirm, PauseRow::Cancel, out);
            else
                emit(PauseActionKind::DefaultSettings);
            break;
        case PauseRow::KeyBindings:
            if (cfg_.pcSettingsPages && st.pcSettings) { // straight onto the gameplay keys; Back returns here
                pcPages_.setTrail({pausePageName(PausePage::Root), pausePageName(PausePage::Options),
                                   pausePageName(PausePage::Controller)});
                pcPages_.setTitle("PC Settings");
                pcPages_.open("game");
                pcOpen_ = true;
            } else
                emit(PauseActionKind::OpenKeyBindings);
            break;
        case PauseRow::Subtitles:
        case PauseRow::Stereo:
        case PauseRow::Vibration:
        case PauseRow::ReversePitch:
        case PauseRow::Scheme:
            adjust(r, 1, st, out);
            break;
        case PauseRow::MasterVolume:
        case PauseRow::MusicVolume:
            break; // Left / Right, the mouse
        case PauseRow::RestoreDefaults:
            out.actions.push_back({0, PauseActionKind::RestoreDefaults});
            for (Override& o : overrides_)
                o = {}; // the guest's values are the truth again
            back(out);
            break;
        case PauseRow::QuitToTitle:
            emit(PauseActionKind::QuitToTitle);
            break;
        case PauseRow::QuitToDesktop:
            emit(PauseActionKind::QuitToDesktop);
            break;
        case PauseRow::Back:
        case PauseRow::Cancel:
            back(out);
            break;
    }
}

void PauseScreen::back(PauseFrameOutput& out) {
    dragRow_.reset();
    if (stack_.empty() && cfg_.rootPage != PausePage::Root) {
        out.wantsBack = true; // embedded: the host leaves its root page itself
        return;
    }
    if (stack_.empty()) {
        out.actions.push_back({0, PauseActionKind::Resume});
        out.wantsResume = true;
        return;
    }
    if (cfg_.hostPages && !isCard(page_) && page_ != PausePage::Root)
        out.actions.push_back({0, PauseActionKind::LeavePage, static_cast<int32_t>(page_)});
    const StackEntry e = stack_.back();
    stack_.pop_back();
    page_ = e.page;
    focus_ = e.focus;
}

void PauseScreen::ageOverrides(const PauseState& st) {
    for (int i = 0; i < kPauseOptionCount; ++i) {
        Override& ov = overrides_[i];
        if (!ov.active)
            continue;
        const auto o = static_cast<PauseOption>(i);
        const bool dragged = dragRow_ && pauseRowOption(*dragRow_) == o;
        if (st.options.valid && st.options.get(o) == ov.value && !dragged)
            ov = {};
        else if (!dragged && ++ov.age > cfg_.optionHoldFrames)
            ov = {}; // never reported: show the guest's value again
    }
}

void PauseScreen::applyInput(const PauseState& st, const UiInput& in, PauseFrameOutput& out) {
    hover_ = {};
    // A slider drag: follows the pointer while the button is held, wherever it is.
    if (dragRow_) {
        if (in.leftDown && !in.leftClick && in.pointerValid && rowEnabled(*dragRow_, st)) {
            sliderFromPointer(*dragRow_, in.pointerX, st, out);
            hover_ = {TargetKind::MenuRow, static_cast<int>(*dragRow_)};
            return;
        }
        dragRow_.reset();
    }
    if (in.pointerValid) {
        for (size_t i = targets_.size(); i-- > 0;)
            if (targets_[i].rect.contains(in.pointerX, in.pointerY)) {
                hover_ = targets_[i].id;
                if (in.pointerMoved && targets_[i].navigable)
                    focus_ = targets_[i].id;
                break;
            }
    }
    if (in.rightClick) {
        back(out);
        return;
    }
    if (in.leftClick && hover_.kind != TargetKind::None) {
        const HitTarget* t = findTarget(hover_);
        if (t && t->enabled) {
            if (hover_.kind == TargetKind::MenuRow) {
                focus_ = hover_;
                const auto r = static_cast<PauseRow>(hover_.index);
                const std::vector<PauseRow> rs = rows();
                const int idx = static_cast<int>(std::find(rs.begin(), rs.end(), r) - rs.begin());
                switch (pauseRowKind(r)) {
                    case PauseRowKind::Slider: {
                        // Click on (or near) the track: jump there and start a drag. On the label: focus only.
                        const Rect tr = sliderTrack(r);
                        const float pad = lf_.px(18);
                        if (in.pointerX >= tr.x - pad && in.pointerX < tr.right() + pad && in.pointerY >= tr.y - pad &&
                            in.pointerY < tr.bottom() + pad) {
                            sliderFromPointer(r, in.pointerX, st, out);
                            dragRow_ = r;
                        }
                        break;
                    }
                    case PauseRowKind::Choice:
                        if (lf_.map(shifted(design::pause::choicePrev(idx))).contains(in.pointerX, in.pointerY))
                            adjust(r, -1, st, out);
                        else
                            adjust(r, 1, st, out);
                        break;
                    default:
                        activate(r, st, out);
                        break;
                }
            } else if (hover_.kind == TargetKind::Hint) {
                const auto pr = static_cast<Prompt>(hover_.index);
                if (pr == Prompt::Back)
                    back(out);
                else if (pr == Prompt::Adjust && focus_.kind == TargetKind::MenuRow)
                    adjust(static_cast<PauseRow>(focus_.index), 1, st, out);
                else if (focus_.kind == TargetKind::MenuRow)
                    activate(static_cast<PauseRow>(focus_.index), st, out);
            }
        }
    }
    std::vector<UiKey> keys = in.keys;
    if (in.wheel != 0)
        keys.push_back(in.wheel > 0 ? UiKey::Up : UiKey::Down);
    for (UiKey k : keys) {
        if (out.wantsResume || out.wantsBack || suspendLeft_ > 0)
            break;
        const PausePage pageBefore = page_;
        int dy = 0;
        switch (k) {
            case UiKey::Up:
                dy = -1;
                break;
            case UiKey::Down:
                dy = 1;
                break;
            case UiKey::Left:
            case UiKey::Right:
                if (focus_.kind == TargetKind::MenuRow)
                    adjust(static_cast<PauseRow>(focus_.index), k == UiKey::Left ? -1 : 1, st, out);
                break;
            case UiKey::Confirm:
                if (focus_.kind == TargetKind::MenuRow)
                    activate(static_cast<PauseRow>(focus_.index), st, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                back(out);
                break;
            default:
                break;
        }
        if (page_ != pageBefore) {
            buildTargets(st);
            ensureFocus();
            continue;
        }
        if (dy == 0)
            continue;
        const HitTarget* cur = findTarget(focus_);
        if (!cur) {
            ensureFocus();
            continue;
        }
        const int next = InventoryScreen::pickDirectional(targets_, cur->rect, 0, dy);
        if (next >= 0)
            focus_ = targets_[static_cast<size_t>(next)].id;
    }
}

PauseFrameOutput PauseScreen::frame(const Snapshot& s, const PauseState& st, const UiInput& in, const Viewport& vp) {
    PauseFrameOutput out;
    if (!open_)
        return out;
    UiInput input = in;
    if (unifiedPending_ && st.pcSettings)
        openUnified(nullptr);
    if (pcOpen_ && st.pcSettings) {
        // The PC Settings pages replace the pause page (header, panels, footer) until Back leaves them.
        SettingsInput si{in, st.now, st.capturedKey};
        const SettingsSchema* schema = st.pcSettings;
        if (unified_) {
            foldGameOptions(*st.pcSettings, st.options, merged_);
            schema = &merged_;
        }
        SettingsFrameOutput so = pcPages_.frame(*schema, si, vp);
        if (unified_) {
            syncNativePage(out, so.wantsClose);
            splitUnified(so.actions, out);
        } else {
            out.settings = std::move(so.actions);
        }
        out.sound = so.sound;
        if (!so.wantsClose) {
            out.draw = std::move(so.draw);
            return out;
        }
        pcOpen_ = false; // back on the page that opened them, focus kept; this frame's input is used up
        input = UiInput{};
        input.device = in.device;
        if (unified_) { // the pages were the Options screen: leave it (embedded: the host leaves its root itself)
            unified_ = false;
            pcPages_.close();
            back(out);
            if (out.wantsBack) {
                close();
                return out;
            }
        }
    } else if (pcOpen_) {
        pcOpen_ = false; // the glue stopped passing a schema
        pcPages_.close();
        if (unified_) {
            unified_ = false;
            syncNativePage(out, true);
            back(out);
            if (out.wantsBack) {
                close();
                return out;
            }
        }
    }
    lf_ = computeFrame(vp);
    ageOverrides(st);
    buildTargets(st);
    ensureFocus();
    buildHints(st);
    const PausePage pageBefore = page_;
    const size_t depthBefore = stack_.size();
    const bool pcBefore = pcOpen_;
    const TargetId focusBefore = focus_;
    if (suspendLeft_ > 0) {
        --suspendLeft_;
        hover_ = {};
        dragRow_.reset();
    } else {
        applyInput(st, input, out);
    }
    if (pcOpen_ && !pcBefore)
        out.sound = UiSound::Confirm;
    else if (page_ != pageBefore)
        out.sound = stack_.size() > depthBefore ? UiSound::Confirm : UiSound::Back;
    else if (out.wantsResume || out.wantsBack)
        out.sound = UiSound::Back;
    else if (out.sound == UiSound::None && !(focus_ == focusBefore) && focusBefore.kind != TargetKind::None)
        out.sound = UiSound::Move;
    else if (out.sound == UiSound::None && !out.actions.empty())
        out.sound = UiSound::Confirm; // a row that acts (Save, Load, Quit ...): the host skips it when a native tap
                                      // makes the sound
    if (out.wantsResume || out.wantsBack) {
        close();
        return out;
    }
    if (pcOpen_) { // PC Settings opened this frame: show the pages at once (no input yet)
        SettingsInput si;
        si.ui.device = in.device;
        si.now = st.now;
        if (unified_)
            foldGameOptions(*st.pcSettings, st.options, merged_);
        SettingsFrameOutput so = pcPages_.frame(unified_ ? merged_ : *st.pcSettings, si, vp);
        out.draw = std::move(so.draw);
        return out;
    }
    buildTargets(st);
    ensureFocus();
    buildHints(st);
    draw(s, st, out.draw);
    return out;
}

// ---- drawing ----

void PauseScreen::draw(const Snapshot& s, const PauseState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    d.panel({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, PanelStyle::Backdrop, th.backdrop);

    // A confirm card is drawn over the page it was opened from.
    const PausePage menuPage = isCard(page_) && !stack_.empty() ? stack_.back().page : page_;

    // Header band as on the Tab screen: the page title centred, as the Tab screen centres its page row (the top-left
    // corner is where overlays such as the fps counter sit; the gold is in the status strip).
    const Rect band = lf_.band(0, 120);
    d.gradient(band, withAlpha(th.panel, 240), withAlpha(th.panel, 0));
    p.ornateRule(d, band.x, band.right(), band.y + p.px(112), true);
    p.text(d, {kDesignW * 0.5f - 450, 24, 900, 60}, pausePageName(menuPage), FontRole::Title, th.heading,
           TextAlign::Center);
    if ((!stack_.empty() || !cfg_.rootTitle.empty()) && !isCard(page_)) {
        // Breadcrumb: where Back goes.
        std::string trail = cfg_.rootTitle;
        for (const StackEntry& e : stack_)
            trail += std::string(trail.empty() ? "" : "  /  ") + pausePageName(e.page);
        p.text(d, {kDesignW * 0.5f - 450, 80, 900, 24}, trail, FontRole::Small, th.textMuted, TextAlign::Center);
    }

    drawMenu(menuPage, !isCard(page_), st, d);
    if (cfg_.statusPanel)
        drawStatus(s, st, d);
    if (isCard(page_))
        drawCard(st, d);

    // Footer hints.
    const Rect fb = lf_.band(992, 88);
    d.gradient(fb, withAlpha(th.panel, 0), withAlpha(th.panel, 235));
    for (size_t i = 0; i < hints_.size(); ++i) {
        const TargetId id{TargetKind::Hint, static_cast<int>(hints_[i].prompt)};
        const HitTarget* t = findTarget(id);
        if (t)
            p.drawHint(d, hints_[i], t->rect, hover_ == id && hints_[i].enabled, kHintGlyph, FontRole::Body);
    }
}

void PauseScreen::drawMenu(PausePage page, bool live, const PauseState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    d.panel(p.map(shifted(design::pause::menuPanel())), PanelStyle::Main, th.panel);
    const std::vector<PauseRow> rs = rowsOf(page);
    std::optional<PauseRow> focusedRow;
    for (size_t i = 0; i < rs.size(); ++i) {
        drawRow(rs[i], shifted(design::pause::row(static_cast<int>(i))), live, st, d);
        if (live && focus_ == TargetId{TargetKind::MenuRow, static_cast<int>(rs[i])})
            focusedRow = rs[i];
    }
    if (focusedRow) {
        const Rect desc{kRowX + dx() + 4, kRowY + 5 * kRowStep + 4, kRowW - 8, 60};
        p.rule(d, kRowX + dx(), desc.y - 6, kRowW);
        const bool en = rowEnabled(*focusedRow, st);
        const std::vector<std::string> lines =
            p.wrap(rowDescription(*focusedRow, en, optionValue(PauseOption::Scheme, st)), desc.w, FontRole::Small);
        float y = desc.y + 6;
        for (size_t i = 0; i < lines.size() && i < 2; ++i, y += 28)
            p.text(d, {desc.x, y, desc.w, 28}, lines[i], FontRole::Small, th.textMuted);
    }
}

void PauseScreen::drawRow(PauseRow r, const Rect& rd, bool live, const PauseState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect rp = p.map(rd);
    const bool en = rowEnabled(r, st);
    const TargetId id{TargetKind::MenuRow, static_cast<int>(r)};
    const bool foc = live && focus_ == id;
    const bool hov = live && hover_ == id && en;
    d.panel(rp, PanelStyle::Card, !en ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
    if (foc)
        p.focus(d, rp);
    const Color labelC = !en ? th.textDisabled : (foc ? th.heading : th.text);
    const Color valueC = !en ? th.textDisabled : th.heading;
    const PauseRowKind kind = pauseRowKind(r);
    const std::optional<PauseOption> opt = pauseRowOption(r);
    const int32_t v = opt ? optionValue(*opt, st) : 0;
    const Rect inner{rd.x + 32, rd.y, rd.w - 64, rd.h};
    switch (kind) {
        case PauseRowKind::Button:
            p.text(d, inner, pauseRowLabel(r), FontRole::Heading, labelC);
            if (!en)
                p.text(d, inner, "Unavailable", FontRole::Small, th.textDisabled, TextAlign::Right);
            break;
        case PauseRowKind::Slider: {
            const Rect line{inner.x, rd.y + 6, inner.w, 46};
            p.text(d, line, pauseRowLabel(r), FontRole::Heading, labelC);
            p.text(d, line, en ? fmtInt(v) : std::string("--"), FontRole::Numeric, valueC, TextAlign::Right);
            const Rect track{rd.x + kTrackInset, rd.y + kTrackY, rd.w - 2 * kTrackInset, kTrackH};
            const Rect tp = p.map(track);
            d.panel(tp, PanelStyle::Inset, th.inset);
            const PauseOptionRange rg = pauseOptionRange(*opt);
            const float f = std::clamp(static_cast<float>(v - rg.lo) / static_cast<float>(rg.hi - rg.lo), 0.0f, 1.0f);
            if (en) {
                const Rect in = tp.inset(p.px(3));
                d.gradient({in.x, in.y, in.w * f, in.h}, th.brass, withAlpha(th.brass, 170));
                // The knob (none while the value is unknown).
                const float kw = p.px(14), kh = p.px(30);
                const Rect knob{tp.x + tp.w * f - kw * 0.5f, tp.cy() - kh * 0.5f, kw, kh};
                d.panel(knob, PanelStyle::Card, foc ? th.ember : th.brass);
                d.outline(knob, th.heading, p.px(1.5f), p.px(3));
            }
            break;
        }
        case PauseRowKind::Toggle: {
            p.text(d, {inner.x, rd.y, inner.w - 110, rd.h}, pauseRowLabel(r), FontRole::Heading, labelC);
            const Rect box{inner.right() - 30, rd.y + (rd.h - 30) * 0.5f, 30, 30};
            d.outline(p.map(box), en ? th.brass : th.textDisabled, p.px(2), p.px(3));
            if (v && en)
                d.rect(p.map(box.inset(7)), foc ? th.ember : th.brass, p.px(2));
            p.text(d, {inner.x, rd.y, inner.w - 46, rd.h}, en ? (v ? "On" : "Off") : "--", FontRole::Body,
                   v && en ? valueC : th.textMuted, TextAlign::Right);
            break;
        }
        case PauseRowKind::Choice: {
            p.text(d, {inner.x, rd.y, inner.w - kChoiceW - 8, rd.h}, pauseRowLabel(r), FontRole::Heading, labelC);
            const Rect area{inner.right() - kChoiceW, rd.y, kChoiceW, rd.h};
            const Color arrowC = !en ? th.textDisabled : (foc ? th.ember : th.brass);
            p.text(d, {area.x, rd.y, kChoiceArrowW, rd.h}, "<", FontRole::Heading, arrowC, TextAlign::Center);
            p.text(d, {area.right() - kChoiceArrowW, rd.y, kChoiceArrowW, rd.h}, ">", FontRole::Heading, arrowC,
                   TextAlign::Center);
            p.text(d, {area.x + kChoiceArrowW, rd.y, kChoiceW - 2 * kChoiceArrowW, rd.h},
                   en ? pauseSchemeLabel(v) : "--", FontRole::Heading, valueC, TextAlign::Center);
            break;
        }
    }
}

void PauseScreen::drawCard(const PauseState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const std::vector<PauseRow> rs = rows();
    const int n = static_cast<int>(rs.size());
    d.rect({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, withAlpha(th.backdropEdge, 120));
    const Rect c = design::pause::cardPanel(n);
    d.panel(p.map(c), PanelStyle::Main, th.panelRaised);
    const bool quit = page_ == PausePage::QuitConfirm;
    p.text(d, {c.x + kCardPad, c.y + 28, c.w - 2 * kCardPad, 52}, quit ? "Quit the game?" : "Restore default settings?",
           FontRole::Title, th.heading);
    p.rule(d, c.x + kCardPad, c.y + 92, c.w - 2 * kCardPad);
    const std::vector<std::string> body =
        p.wrap(quit ? "Unsaved progress is lost." : "Audio and controller options return to their default values.",
               c.w - 2 * kCardPad, FontRole::Body);
    float y = c.y + 104;
    for (size_t i = 0; i < body.size() && i < 2; ++i, y += 30)
        p.text(d, {c.x + kCardPad, y, c.w - 2 * kCardPad, 30}, body[i], FontRole::Body, th.textMuted);
    for (int i = 0; i < n; ++i)
        drawRow(rs[static_cast<size_t>(i)], design::pause::cardRow(i, n), true, st, d);
}

void PauseScreen::drawStatus(const Snapshot& s, const PauseState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect panel = design::pause::statusPanel();
    d.panel(p.map(panel), PanelStyle::Main, th.panel);
    const float x = panel.x + 32, w = panel.w - 64;
    float y = panel.y + 24;

    const std::string loc = locationName(s, st.location);
    p.label(d, {x, y, w, 28}, "Location", th.brass);
    y += 28;
    p.text(d, {x, y, w, 56}, loc.empty() ? std::string("Unknown") : loc, FontRole::Title,
           loc.empty() ? th.textMuted : th.heading);
    y += 72;
    p.rule(d, x, y, w);
    y += 16;

    p.label(d, {x, y, w, 28}, "Objectives", th.brass);
    y += 40;
    const std::vector<const QuestEntry*> open = orderedObjectives(s, false);
    const float limit = panel.y + 400;
    if (open.empty()) {
        p.text(d, {x, y, w, 32}, "No open objectives.", FontRole::Body, th.textDisabled, TextAlign::Left, 23);
    }
    int shown = 0;
    for (const QuestEntry* q : open) {
        if (shown >= 3 || y + 30 > limit)
            break;
        const Rect box{x + 2, y + 9, 12, 12};
        d.outline(p.map(box), q->optional ? th.brassDim : th.brass, p.px(2), p.px(2));
        std::vector<std::string> lines = p.wrap(objectiveLabel(s, *q, loc), w - 30, FontRole::Body, 23);
        if (lines.size() > 2) {
            lines.resize(2);
            lines.back() += "...";
        }
        for (const std::string& line : lines) {
            if (y + 30 > limit)
                break;
            p.text(d, {x + 30, y, w - 30, 30}, line, FontRole::Body, th.text, TextAlign::Left, 23);
            y += 30;
        }
        y += 10;
        ++shown;
    }
    if (static_cast<int>(open.size()) > shown && y + 26 <= limit + 26)
        p.text(d, {x + 30, y, w - 30, 26},
               "+" + fmtInt(static_cast<int32_t>(open.size()) - shown) + " more in the journal", FontRole::Small,
               th.textMuted);

    y = panel.y + 420;
    p.rule(d, x, y, w);
    y += 16;
    p.label(d, {x, y, w, 28}, "Rynn", th.brass);
    p.text(d, {x, y, w, 28}, "Level " + fmtInt(s.stats.level), FontRole::Small, th.text, TextAlign::Right);
    y += 40;
    p.bar(d, {x, y, w, 34}, s.stats.hp, s.stats.hpMax, th.hp, "Health");
    y += 46;
    p.bar(d, {x, y, w, 34}, s.stats.mana, s.stats.manaMax, th.mana, "Mana");
    y += 52;
    p.text(d, {x, y, w, 36}, "Gold", FontRole::Body, th.textMuted);
    p.text(d, {x, y, w - 44, 36}, fmtInt(s.stats.gold), FontRole::Numeric, th.heading, TextAlign::Right);
    d.imageOr(p.map({x + w - 38, y - 2, 40, 40}), {ImageKind::UiIcon, uint32_t(UiIconId::GoldBag)},
              {ImageKind::SkinOrb, 0}, 0xffffffffu, th.xp);
}

} // namespace gameui
