#include "gameui/TitleScreen.h"

#include <algorithm>

#include "Paint.h"

namespace gameui {

namespace {

constexpr float kColW = 520, kColX = (kDesignW - kColW) * 0.5f;
constexpr float kRowH = 56, kRowStep = 64, kColPad = 24, kColCentreY = 650, kDescH = 64;
constexpr float kMargin = 96, kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36;

PauseConfig optionsConfig(const TitleConfig& c) {
    PauseConfig p;
    p.screen = c.screen;
    p.hostPages = true;
    p.rootPage = PausePage::Options;
    p.statusPanel = false;
    p.centred = true;
    p.rootTitle = "Main Menu";
    p.sliderStep = c.sliderStep;
    p.optionHoldFrames = c.optionHoldFrames;
    p.suspendFrames = c.suspendFrames;
    p.pcSettingsPages = c.pcSettingsPages;
    p.unifiedOptions = c.unifiedOptions;
    return p;
}

std::string rowDescription(TitleRow r, bool enabled, const TitleState& st) {
    switch (r) {
        case TitleRow::Continue: {
            std::string s = "Your newest save";
            if (!st.continueLocation.empty())
                s += ": " + st.continueLocation;
            const std::string d = formatSaveDate(st.continueTime, st.dateOrder);
            if (!d.empty())
                s += ", " + d + " " + formatSaveTime(st.continueTime, st.clock24);
            return s + ".";
        }
        case TitleRow::LoadGame:
            return enabled ? "Load a saved game." : "No saved games on the memory card.";
        case TitleRow::NewGame:
            return "Start a new adventure.";
        case TitleRow::Options:
            return "Controller and audio options.";
        case TitleRow::Credits:
            return "The people who made the game.";
        case TitleRow::PcSettings:
            return "Display, graphics, controls and more.";
        case TitleRow::Quit:
            return "Close the game.";
        case TitleRow::QuitToDesktop:
        case TitleRow::Cancel:
            break;
    }
    return {};
}

} // namespace

const char* titleRowLabel(TitleRow r) {
    switch (r) {
        case TitleRow::Continue:
            return "Continue";
        case TitleRow::LoadGame:
            return "Load Game";
        case TitleRow::NewGame:
            return "New Game";
        case TitleRow::Options:
            return "Options";
        case TitleRow::Credits:
            return "Credits";
        case TitleRow::PcSettings:
            return "PC Settings";
        case TitleRow::Quit:
            return "Quit";
        case TitleRow::QuitToDesktop:
            return "Quit to Desktop";
        case TitleRow::Cancel:
            return "Cancel";
    }
    return "";
}

const char* titleActionName(TitleActionKind k) {
    switch (k) {
        case TitleActionKind::Continue:
            return "Continue";
        case TitleActionKind::LoadGame:
            return "LoadGame";
        case TitleActionKind::NewGame:
            return "NewGame";
        case TitleActionKind::Credits:
            return "Credits";
        case TitleActionKind::OpenPcSettings:
            return "OpenPcSettings";
        case TitleActionKind::QuitToDesktop:
            return "QuitToDesktop";
    }
    return "?";
}

uint32_t TitleActionQueue::push(TitleAction a) {
    std::lock_guard<std::mutex> lock(m_);
    a.id = nextId_++;
    q_.push_back(a);
    return a.id;
}

std::optional<TitleAction> TitleActionQueue::pop() {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.empty())
        return std::nullopt;
    TitleAction a = q_.front();
    q_.pop_front();
    return a;
}

size_t TitleActionQueue::size() const {
    std::lock_guard<std::mutex> lock(m_);
    return q_.size();
}

void TitleActionQueue::clear() {
    std::lock_guard<std::mutex> lock(m_);
    q_.clear();
}

namespace design::title {
Rect menuPanel(int rows) {
    const float h = static_cast<float>(rows) * kRowStep - (kRowStep - kRowH) + 2 * kColPad + kDescH;
    return {kColX, kColCentreY - h * 0.5f, kColW, h};
}
Rect row(int i, int rows) {
    const Rect p = menuPanel(rows);
    return {p.x + kColPad, p.y + kColPad + static_cast<float>(i) * kRowStep, p.w - 2 * kColPad, kRowH};
}
Rect cardPanel() {
    return design::pause::cardPanel(2);
}
Rect cardRow(int i) {
    return design::pause::cardRow(i, 2);
}
} // namespace design::title

TitleScreen::TitleScreen(TitleConfig cfg)
    : cfg_(std::move(cfg)), options_(optionsConfig(cfg_)), pcPages_(SettingsPagesConfig{cfg_.screen}) {}

void TitleScreen::open() {
    open_ = true;
    page_ = TitlePage::Main;
    focus_ = hover_ = mainFocus_ = {};
    suspendLeft_ = 0;
    loading_ = false;
    options_.close();
    pcPages_.close();
}

void TitleScreen::close() {
    open_ = false;
    suspendLeft_ = 0;
    options_.close();
    pcPages_.close();
    targets_.clear();
    hints_.clear();
}

void TitleScreen::reactivate() {
    open_ = true;
    suspendLeft_ = 0;
    loading_ = false;
    if (page_ == TitlePage::Options)
        options_.reactivate();
}

std::vector<TitleRow> TitleScreen::rows(const TitleState& st) const {
    return rowsOf(page_, st);
}

std::vector<TitleRow> TitleScreen::rowsOf(TitlePage page, const TitleState& st) const {
    if (page == TitlePage::QuitConfirm)
        return {TitleRow::QuitToDesktop, TitleRow::Cancel};
    std::vector<TitleRow> r;
    if (cfg_.showContinue && st.continueSlot > 0)
        r.push_back(TitleRow::Continue);
    r.insert(r.end(), {TitleRow::LoadGame, TitleRow::NewGame, TitleRow::Options, TitleRow::Credits});
    if (cfg_.showPcSettings && !cfg_.unifiedOptions) // one Options screen: PC Settings is inside it
        r.push_back(TitleRow::PcSettings);
    if (st.canQuit)
        r.push_back(TitleRow::Quit);
    return r;
}

bool TitleScreen::rowEnabled(TitleRow r, const TitleState& st) const {
    switch (r) {
        case TitleRow::LoadGame:
            return st.canLoad;
        case TitleRow::Continue:
            return st.continueSlot > 0;
        default:
            return true;
    }
}

const HitTarget* TitleScreen::findTarget(const TargetId& id) const {
    for (const HitTarget& t : targets_)
        if (t.id == id)
            return &t;
    return nullptr;
}

Rect TitleScreen::rowRect(int i, int n) const {
    return design::title::row(i, n);
}

void TitleScreen::buildTargets(const TitleState& st) {
    targets_.clear();
    const std::vector<TitleRow> rs = rows(st);
    const int n = static_cast<int>(rs.size());
    for (int i = 0; i < n; ++i) {
        const TitleRow r = rs[static_cast<size_t>(i)];
        const bool en = rowEnabled(r, st);
        const Rect rd = page_ == TitlePage::QuitConfirm ? design::title::cardRow(i) : rowRect(i, n);
        targets_.push_back(
            {{page_ == TitlePage::QuitConfirm ? TargetKind::CardButton : TargetKind::TitleRow, static_cast<int>(r)},
             lf_.map(rd),
             en,
             en});
    }
}

void TitleScreen::ensureFocus(const TitleState& st) {
    if (const HitTarget* t = findTarget(focus_); t && t->navigable)
        return;
    const HitTarget* cur = findTarget(focus_);
    focus_ = {};
    if (cur) // the focused row became disabled: the next enabled one below
        for (auto it = targets_.begin() + (cur - targets_.data()); it != targets_.end(); ++it)
            if (it->navigable && it->id.kind != TargetKind::Hint) {
                focus_ = it->id;
                return;
            }
    // First: Continue when there is one, else the first enabled row (Load Game, or New Game without saves).
    (void)st;
    for (const HitTarget& t : targets_)
        if (t.navigable && t.id.kind != TargetKind::Hint) {
            focus_ = t.id;
            return;
        }
}

void TitleScreen::buildHints(const TitleState& st) {
    hints_.clear();
    const bool card = page_ == TitlePage::QuitConfirm;
    const HitTarget* t = findTarget(focus_);
    hints_.push_back({Prompt::Confirm, "Select", t && t->enabled});
    if (card)
        hints_.push_back({Prompt::Back, "Cancel"});
    (void)st;
    const detail::Paint p(lf_, cfg_.screen);
    const std::vector<Rect> rs =
        p.layoutHints(hints_, kDesignW - kMargin, false, kFooterY, kHintH, kHintGlyph, kHintGap, FontRole::Body);
    for (size_t i = 0; i < hints_.size(); ++i)
        targets_.push_back(
            {{TargetKind::Hint, static_cast<int>(hints_[i].prompt)}, lf_.map(rs[i]), false, hints_[i].enabled});
}

void TitleScreen::activate(TitleRow r, const TitleState& st, TitleFrameOutput& out) {
    if (!rowEnabled(r, st))
        return;
    auto emit = [&](TitleActionKind k, int32_t v = 0) {
        out.actions.push_back({0, k, v});
        out.sound = UiSound::Confirm;      // the host leaves it to the native row press where there is one
        suspendLeft_ = cfg_.suspendFrames; // the menu is left: wait for reactivate() (or the screen closes)
    };
    switch (r) {
        case TitleRow::Continue:
            emit(TitleActionKind::Continue, st.continueSlot);
            loading_ = true; // feedback at once: the load screen, its scan and the load follow
            break;
        case TitleRow::LoadGame:
            emit(TitleActionKind::LoadGame);
            break;
        case TitleRow::NewGame:
            emit(TitleActionKind::NewGame);
            break;
        case TitleRow::Options:
            mainFocus_ = focus_;
            page_ = TitlePage::Options;
            options_.open();
            out.optionActions.push_back({0, PauseActionKind::EnterPage, static_cast<int32_t>(PausePage::Options)});
            out.sound = UiSound::Confirm;
            break;
        case TitleRow::Credits:
            emit(TitleActionKind::Credits);
            break;
        case TitleRow::PcSettings:
            if (cfg_.pcSettingsPages && st.pcSettings) { // the modern pages, inside the title menu
                mainFocus_ = focus_;
                page_ = TitlePage::PcSettings;
                pcPages_.setTrail({"Main Menu"});
                pcPages_.open();
                out.sound = UiSound::Confirm;
            } else
                emit(TitleActionKind::OpenPcSettings);
            break;
        case TitleRow::Quit:
            mainFocus_ = focus_;
            page_ = TitlePage::QuitConfirm;
            focus_ = {TargetKind::CardButton, static_cast<int>(TitleRow::Cancel)};
            out.sound = UiSound::Confirm;
            break;
        case TitleRow::QuitToDesktop:
            emit(TitleActionKind::QuitToDesktop);
            break;
        case TitleRow::Cancel:
            back(out);
            break;
    }
}

void TitleScreen::back(TitleFrameOutput& out) {
    if (page_ == TitlePage::QuitConfirm) {
        page_ = TitlePage::Main;
        focus_ = mainFocus_;
        out.sound = UiSound::Back;
    }
    // Main: the title has no "back" (the native page ignores Esc too).
}

void TitleScreen::applyInput(const TitleState& st, const UiInput& in, TitleFrameOutput& out) {
    hover_ = {};
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
            if (hover_.kind == TargetKind::TitleRow || hover_.kind == TargetKind::CardButton) {
                focus_ = hover_;
                activate(static_cast<TitleRow>(hover_.index), st, out);
            } else if (hover_.kind == TargetKind::Hint) {
                if (static_cast<Prompt>(hover_.index) == Prompt::Back)
                    back(out);
                else if (focus_.kind == TargetKind::TitleRow || focus_.kind == TargetKind::CardButton)
                    activate(static_cast<TitleRow>(focus_.index), st, out);
            }
        }
    }
    std::vector<UiKey> keys = in.keys;
    if (in.wheel != 0)
        keys.push_back(in.wheel > 0 ? UiKey::Up : UiKey::Down);
    for (UiKey k : keys) {
        if (suspendLeft_ > 0 || page_ == TitlePage::Options || page_ == TitlePage::PcSettings)
            break;
        const TitlePage before = page_;
        int dy = 0;
        switch (k) {
            case UiKey::Up:
                dy = -1;
                break;
            case UiKey::Down:
                dy = 1;
                break;
            case UiKey::Confirm:
                if (focus_.kind == TargetKind::TitleRow || focus_.kind == TargetKind::CardButton)
                    activate(static_cast<TitleRow>(focus_.index), st, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                back(out);
                break;
            default:
                break;
        }
        if (page_ != before) {
            buildTargets(st);
            ensureFocus(st);
            continue;
        }
        if (dy == 0)
            continue;
        const HitTarget* cur = findTarget(focus_);
        if (!cur) {
            ensureFocus(st);
            continue;
        }
        const int next = InventoryScreen::pickDirectional(targets_, cur->rect, 0, dy);
        if (next >= 0)
            focus_ = targets_[static_cast<size_t>(next)].id;
    }
}

TitleFrameOutput TitleScreen::frame(const TitleState& st, const UiInput& in, const Viewport& vp) {
    TitleFrameOutput out;
    if (!open_)
        return out;
    lf_ = computeFrame(vp);
    const auto optionsState = [&] {
        PauseState ps;
        ps.options = st.options;
        if (cfg_.pcSettingsPages) {
            ps.pcSettings = st.pcSettings;
            ps.now = st.now;
            ps.capturedKey = st.capturedKey;
        }
        return ps;
    };
    if (page_ == TitlePage::PcSettings && !st.pcSettings) { // the glue stopped passing a schema
        pcPages_.close();
        page_ = TitlePage::Main;
        focus_ = mainFocus_;
    }
    if (page_ == TitlePage::PcSettings) {
        SettingsInput si{in, st.now, st.capturedKey};
        SettingsFrameOutput so = pcPages_.frame(*st.pcSettings, si, vp);
        out.settings = std::move(so.actions);
        out.sound = so.sound;
        if (!so.wantsClose) {
            out.draw = std::move(so.draw);
            return out;
        }
        // Back on the landing page: the main menu again with PC Settings focused, drawn this frame.
        pcPages_.close();
        page_ = TitlePage::Main;
        focus_ = mainFocus_;
        out.sound = UiSound::Back;
        buildTargets(st);
        ensureFocus(st);
        buildHints(st);
        draw(st, out.draw);
        return out;
    }
    if (page_ == TitlePage::Options) {
        static const Snapshot kNoSnapshot;
        PauseFrameOutput o = options_.frame(kNoSnapshot, optionsState(), in, vp);
        out.sound = o.sound;
        out.optionActions = std::move(o.actions);
        out.settings = std::move(o.settings);
        if (!o.wantsBack) {
            out.draw = std::move(o.draw);
            return out;
        }
        // Back on the Options page: close native Options (9) and return to the menu, drawn this frame.
        out.optionActions.push_back({0, PauseActionKind::LeavePage, static_cast<int32_t>(PausePage::Options)});
        page_ = TitlePage::Main;
        focus_ = mainFocus_;
        out.sound = UiSound::Back;
        buildTargets(st);
        ensureFocus(st);
        buildHints(st);
        draw(st, out.draw);
        return out;
    }
    buildTargets(st);
    ensureFocus(st);
    buildHints(st);
    const TitlePage pageBefore = page_;
    const TargetId focusBefore = focus_;
    if (loading_) {
        hover_ = {}; // Continue runs: no input until reactivate()
    } else if (suspendLeft_ > 0) {
        --suspendLeft_;
        hover_ = {};
    } else {
        applyInput(st, in, out);
    }
    if (page_ == TitlePage::Options) {
        // Entered this frame: the Options page draws from the next frame; show it already (no blank frame).
        static const Snapshot kNoSnapshot;
        PauseFrameOutput o = options_.frame(kNoSnapshot, optionsState(), {}, vp);
        out.draw = std::move(o.draw);
        return out;
    }
    if (page_ == TitlePage::PcSettings) { // opened this frame: show the pages at once (no input yet)
        SettingsInput si;
        si.ui.device = in.device;
        si.now = st.now;
        out.draw = pcPages_.frame(*st.pcSettings, si, vp).draw;
        return out;
    }
    if (out.sound == UiSound::None && page_ == pageBefore && !(focus_ == focusBefore) &&
        focusBefore.kind != TargetKind::None)
        out.sound = UiSound::Move;
    buildTargets(st);
    ensureFocus(st);
    buildHints(st);
    draw(st, out.draw);
    return out;
}

void TitleScreen::draw(const TitleState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect view{lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h};
    if (st.backdrop.kind != ImageKind::None)
        d.image(view, st.backdrop);
    // No header band: the art carries the logo. A low gradient under the footer keeps the hints readable.
    Rect fb = lf_.band(960, 120);
    fb.h = std::max(fb.h, lf_.view.y + lf_.view.h - fb.y); // to the viewport bottom (letterboxed 4:3)
    d.gradient(fb, withAlpha(th.panel, 0), withAlpha(th.panel, 220));

    if (loading_) {
        p.confirmCard(d, design::title::cardPanel(), "Loading...",
                      st.continueLocation.empty() ? std::string("Continuing your last game.")
                                                  : "Continuing at " + st.continueLocation + ".");
        return;
    }
    const bool card = page_ == TitlePage::QuitConfirm;
    const std::vector<TitleRow> main = rowsOf(TitlePage::Main, st); // the main column, under the Quit card too
    const int n = static_cast<int>(main.size());
    const Rect panel = design::title::menuPanel(n);
    d.panel(p.map(panel), PanelStyle::Main, withAlpha(th.panel, 214));
    std::optional<TitleRow> focusedRow;
    for (int i = 0; i < n; ++i) {
        const TitleRow r = main[static_cast<size_t>(i)];
        const TargetId id{TargetKind::TitleRow, static_cast<int>(r)};
        const bool en = rowEnabled(r, st);
        const bool foc = !card && focus_ == id, hov = !card && hover_ == id && en;
        const Rect rd = rowRect(i, n);
        p.button(d, rd, titleRowLabel(r), foc, hov, en, TextAlign::Center);
        if (r == TitleRow::Continue && !st.continueLocation.empty())
            p.text(d, {rd.x + 32, rd.y, rd.w - 64, rd.h}, st.continueLocation, FontRole::Small,
                   foc ? th.text : th.textMuted, TextAlign::Right);
        if (foc)
            focusedRow = r;
    }
    if (focusedRow) {
        // The focused row's description in the panel's foot, under a rule (as the pause menu).
        const Rect desc{panel.x + kColPad, panel.bottom() - kColPad - 40, panel.w - 2 * kColPad, 40};
        p.rule(d, desc.x, desc.y - 6, desc.w);
        p.text(d, desc, rowDescription(*focusedRow, rowEnabled(*focusedRow, st), st), FontRole::Small, th.textMuted,
               TextAlign::Center);
    }
    if (!st.footer.empty())
        p.text(d, {kMargin, kFooterY, 700, kHintH}, st.footer, FontRole::Small, th.textMuted);

    if (card) {
        p.confirmCard(d, design::title::cardPanel(), "Quit the game?", "Drakan closes and returns to the desktop.");
        const std::vector<TitleRow> cr = rows(st);
        for (size_t i = 0; i < cr.size(); ++i) {
            const TargetId id{TargetKind::CardButton, static_cast<int>(cr[i])};
            p.button(d, design::title::cardRow(static_cast<int>(i)), titleRowLabel(cr[i]), focus_ == id, hover_ == id,
                     true);
        }
    }

    for (const Hint& h : hints_) {
        const TargetId id{TargetKind::Hint, static_cast<int>(h.prompt)};
        if (const HitTarget* t = findTarget(id))
            p.drawHint(d, h, t->rect, hover_ == id && h.enabled, kHintGlyph, FontRole::Body);
    }
}

} // namespace gameui
