#include "gameui/TitleScreen.h"

#include <algorithm>

#include "Paint.h"
#include "gameui/MenuText.h"
#include "gameui/UiSkin.h"

namespace gameui {

namespace {

constexpr float kColPad = 24, kDescH = 64;
// The menu column's geometry is skin-driven ([layout] title_*; defaults 520 / 56 / 64 / 650).
float colW() {
    return skinNumber("title_column_w", 520);
}
float rowH() {
    return skinNumber("title_row_h", 56);
}
float rowStep() {
    return skinNumber("title_row_step", 64);
}
float colCentreY() {
    return skinNumber("title_centre_y", 650);
}
constexpr float kMargin = 96, kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36;

PauseConfig optionsConfig(const TitleConfig& c) {
    PauseConfig p;
    p.screen = c.screen;
    p.hostPages = true;
    p.rootPage = PausePage::Options;
    p.statusPanel = false;
    p.centred = true;
    p.rootTitle = menuText("Main Menu");
    p.sliderStep = c.sliderStep;
    p.optionHoldFrames = c.optionHoldFrames;
    p.suspendFrames = c.suspendFrames;
    p.pcSettingsPages = c.pcSettingsPages;
    p.unifiedOptions = c.unifiedOptions;
    return p;
}

std::string rowDescription(TitleRow r, bool enabled, const TitleState& st) {
    if (r != TitleRow::Continue && !(r == TitleRow::LoadGame && !enabled))
        if (const char* d = titleRowDefaultDescription(r))
            return skinText(std::string("title.desc.") + titleRowId(r), d);
    switch (r) {
        case TitleRow::Continue: {
            std::string s = menuText("Your newest save");
            if (!st.continueLocation.empty())
                s += ": " + st.continueLocation;
            const std::string d = formatSaveDate(st.continueTime, st.dateOrder);
            if (!d.empty())
                s += ", " + d + " " + formatSaveTime(st.continueTime, st.clock24);
            return s + ".";
        }
        case TitleRow::LoadGame:
            return enabled ? menuText("Load a saved game.") : menuText("No saved games on the memory card.");
        case TitleRow::NewGame:
            return menuText("Start a new adventure.");
        case TitleRow::Options:
            return menuText("Controller and audio options.");
        case TitleRow::Credits:
            return menuText("The people who made the game.");
        case TitleRow::PcSettings:
            return menuText("Display, graphics, controls and more.");
        case TitleRow::Quit:
            return menuText("Close the game.");
        case TitleRow::Multiplayer:
            return menuText("Host or join a match over the network.");
        case TitleRow::QuitToDesktop:
        case TitleRow::Cancel:
        case TitleRow::NewGameSolo:
        case TitleRow::NewGameCoop:
        case TitleRow::GameDrakan:
        case TitleRow::GameOotf:
        case TitleRow::ModsEnable:
        case TitleRow::ModsOpen:
        case TitleRow::ModsKeepOff:
            break;
    }
    return {};
}

} // namespace

const char* titleRowId(TitleRow r) {
    switch (r) {
        case TitleRow::Continue:
            return "continue";
        case TitleRow::LoadGame:
            return "load_game";
        case TitleRow::NewGame:
            return "new_game";
        case TitleRow::Options:
            return "options";
        case TitleRow::Credits:
            return "credits";
        case TitleRow::PcSettings:
            return "pc_settings";
        case TitleRow::Quit:
            return "quit";
        case TitleRow::Multiplayer:
            return "multiplayer";
        case TitleRow::QuitToDesktop:
            return "quit_to_desktop";
        case TitleRow::Cancel:
            return "cancel";
        case TitleRow::NewGameSolo:
            return "new_game_solo";
        case TitleRow::NewGameCoop:
            return "new_game_coop";
        case TitleRow::ModsEnable:
            return "mods_enable";
        case TitleRow::ModsOpen:
            return "mods_open";
        case TitleRow::ModsKeepOff:
            return "mods_keep_off";
        default: // game rows
            break;
    }
    return "";
}

const char* titleRowDefaultDescription(TitleRow r) {
    switch (r) {
        case TitleRow::LoadGame:
            return menuText("Load a saved game.");
        case TitleRow::NewGame:
            return menuText("Start a new adventure.");
        case TitleRow::Options:
            return menuText("Controller and audio options.");
        case TitleRow::Credits:
            return menuText("The people who made the game.");
        case TitleRow::PcSettings:
            return menuText("Display, graphics, controls and more.");
        case TitleRow::Quit:
            return menuText("Close the game.");
        case TitleRow::Multiplayer:
            return menuText("Host or join a match over the network.");
        default:
            return nullptr;
    }
}

const char* titleRowLabel(TitleRow r) {
    return skinText(std::string("title.row.") + titleRowId(r), titleRowDefaultLabel(r));
}

const char* titleRowDefaultLabel(TitleRow r) {
    switch (r) {
        case TitleRow::Continue:
            return menuText("Continue");
        case TitleRow::LoadGame:
            return menuText("Load Game");
        case TitleRow::NewGame:
            return menuText("New Game");
        case TitleRow::Options:
            return menuText("Options");
        case TitleRow::Credits:
            return menuText("Credits");
        case TitleRow::PcSettings:
            return menuText("PC Settings");
        case TitleRow::Quit:
            return menuText("Quit");
        case TitleRow::Multiplayer:
            return menuText("Multiplayer");
        case TitleRow::QuitToDesktop:
            return menuText("Quit to Desktop");
        case TitleRow::Cancel:
            return menuText("Cancel");
        case TitleRow::NewGameSolo:
            return menuText("Solo");
        case TitleRow::NewGameCoop:
            return menuText("Co-op");
        case TitleRow::GameDrakan:
            return "Drakan";
        case TitleRow::GameOotf:
            return "Order of the Flame";
        case TitleRow::ModsEnable:
            return menuText("Enable and restart");
        case TitleRow::ModsOpen:
            return menuText("Open Mods menu");
        case TitleRow::ModsKeepOff:
            return menuText("Keep them off");
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
        case TitleActionKind::SelectGame:
            return "SelectGame";
        case TitleActionKind::ModsEnable:
            return "ModsEnable";
        case TitleActionKind::ModsKeepOff:
            return "ModsKeepOff";
        case TitleActionKind::OpenMods:
            return "OpenMods";
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
    const float h = static_cast<float>(rows) * rowStep() - (rowStep() - rowH()) + 2 * kColPad + kDescH;
    return {(kDesignW - colW()) * 0.5f, colCentreY() - h * 0.5f, colW(), h};
}
Rect row(int i, int rows) {
    const Rect p = menuPanel(rows);
    return {p.x + kColPad, p.y + kColPad + static_cast<float>(i) * rowStep(), p.w - 2 * kColPad, rowH()};
}
Rect cardPanel() {
    return design::pause::cardPanel(2);
}
Rect cardRow(int i) {
    return design::pause::cardRow(i, 2);
}
namespace {
constexpr float kGamePad = 44, kGameHeadH = 120, kGameListW = 600, kGameRowH = 64, kGameRowStep = 76;
}
Rect gameCard() {
    return {200, 160, 1520, 760};
}
Rect gameRow(int i) {
    const Rect c = gameCard();
    return {c.x + kGamePad, c.y + kGameHeadH + static_cast<float>(i) * kGameRowStep, kGameListW, kGameRowH};
}
Rect gameCover() {
    const Rect c = gameCard();
    const float x = c.x + kGamePad + kGameListW + 40, w = c.right() - kGamePad - x;
    return {x, c.y + kGameHeadH, w, w * 9.0f / 16.0f};
}
Rect gameDescription() {
    const Rect cv = gameCover();
    return {cv.x, cv.bottom() + 20, cv.w, gameCard().bottom() - kGamePad + 14 - (cv.bottom() + 20)};
}
namespace {
constexpr float kModsW = 1000, kModsHeadH = 290, kModsRowH = 72, kModsRowStep = 84, kModsPad = 44;
}
Rect modsCard() {
    const float h = kModsHeadH + 3 * kModsRowStep - (kModsRowStep - kModsRowH) + kModsPad;
    return {(kDesignW - kModsW) * 0.5f, (kDesignH - h) * 0.5f, kModsW, h};
}
Rect modsRow(int i) {
    const Rect c = modsCard();
    return {c.x + kModsPad + 120, c.y + kModsHeadH + static_cast<float>(i) * kModsRowStep, c.w - 2 * kModsPad - 240,
            kModsRowH};
}
// Left of the logo (design x >= 530) and of the campaign plaque (x >= 460), above Rynn's head on the art.
// The plate's frame is about 30 units thick at the sides: the text keeps clear of it.
constexpr float kNoteX = 24, kNoteY = 32, kNoteW = 432, kNotePadX = 34, kNotePadY = 22, kNoteLineH = 30;
Rect notePlate(int lines) {
    lines = std::clamp(lines, 1, kNoteLinesMax);
    return {kNoteX, kNoteY, kNoteW, kNoteLineH * static_cast<float>(lines) + 2 * kNotePadY};
}
Rect noteLine(int i) {
    return {kNoteX + kNotePadX, kNoteY + kNotePadY + kNoteLineH * static_cast<float>(i), noteTextWidth(), kNoteLineH};
}
float noteTextWidth() {
    return kNoteW - 2 * kNotePadX;
}
} // namespace design::title

namespace {
bool isGameRow(TitleRow r) {
    return static_cast<int>(r) >= static_cast<int>(TitleRow::GameDrakan) &&
           static_cast<int>(r) < static_cast<int>(TitleRow::ModsEnable);
}
int gameOf(TitleRow r) {
    return static_cast<int>(r) - static_cast<int>(TitleRow::GameDrakan);
}
TitleRow gameRowOf(int i) {
    return static_cast<TitleRow>(static_cast<int>(TitleRow::GameDrakan) + i);
}
int gameCount(const TitleState& st) {
    return std::clamp(static_cast<int>(st.games.size()), 1, 100);
}
// First game shown in the list: the focused one stays visible.
int gameScroll(int focused, int n) {
    return std::clamp(focused - (design::title::kGameRowsShown - 1), 0, std::max(0, n - design::title::kGameRowsShown));
}
} // namespace

TitleScreen::TitleScreen(TitleConfig cfg)
    : cfg_(std::move(cfg)), options_(optionsConfig(cfg_)), pcPages_(SettingsPagesConfig{cfg_.screen}),
      mpScreen_(mp::MpConfig{cfg_.screen}) {}

void TitleScreen::open() {
    open_ = true;
    page_ = TitlePage::Main;
    focus_ = hover_ = mainFocus_ = {};
    suspendLeft_ = 0;
    loading_ = false;
    options_.close();
    pcPages_.close();
    mpScreen_.close();
    modsAsked_ = false; // the host stops passing TitleState::newMods once the player answered
    gameAsked_ = false; // every visit to the title asks which game again (the last choice has the focus)
}

void TitleScreen::close() {
    open_ = false;
    suspendLeft_ = 0;
    options_.close();
    pcPages_.close();
    mpScreen_.close();
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

static bool isCard(TitlePage p) {
    return p == TitlePage::QuitConfirm || p == TitlePage::NewGameChoice || p == TitlePage::GameChoice ||
           p == TitlePage::ModsNotice;
}

std::vector<TitleRow> TitleScreen::rows(const TitleState& st) const {
    return rowsOf(page_, st);
}

std::vector<TitleRow> TitleScreen::rowsOf(TitlePage page, const TitleState& st) const {
    if (page == TitlePage::QuitConfirm)
        return {TitleRow::QuitToDesktop, TitleRow::Cancel};
    if (page == TitlePage::NewGameChoice)
        return {TitleRow::NewGameSolo, TitleRow::NewGameCoop};
    if (page == TitlePage::ModsNotice)
        return {TitleRow::ModsEnable, TitleRow::ModsOpen, TitleRow::ModsKeepOff};
    if (page == TitlePage::GameChoice) {
        std::vector<TitleRow> g;
        for (int i = 0; i < gameCount(st); ++i)
            g.push_back(gameRowOf(i));
        return g;
    }
    std::vector<TitleRow> r;
    if (cfg_.showContinue && st.continueSlot > 0)
        r.push_back(TitleRow::Continue);
    r.insert(r.end(), {TitleRow::LoadGame, TitleRow::NewGame});
    if (cfg_.showMultiplayer)
        r.push_back(TitleRow::Multiplayer);
    r.insert(r.end(), {TitleRow::Options, TitleRow::Credits});
    if (cfg_.showPcSettings && !cfg_.unifiedOptions) // one Options screen: PC Settings is inside it
        r.push_back(TitleRow::PcSettings);
    if (st.canQuit)
        r.push_back(TitleRow::Quit);
    return applyRowOrder("title.main", std::move(r), [](TitleRow x) { return titleRowId(x); }); // skin order / hidden
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
    if (page_ == TitlePage::GameChoice) { // the visible part of the game list
        const int focused =
            focus_.kind == TargetKind::CardButton ? gameOf(static_cast<TitleRow>(focus_.index)) : st.game;
        const int first = gameScroll(focused, n);
        for (int i = first; i < n && i < first + design::title::kGameRowsShown; ++i)
            targets_.push_back({{TargetKind::CardButton, static_cast<int>(gameRowOf(i))},
                                lf_.map(design::title::gameRow(i - first)),
                                true,
                                true});
        return;
    }
    for (int i = 0; i < n; ++i) {
        const TitleRow r = rs[static_cast<size_t>(i)];
        const bool en = rowEnabled(r, st);
        const Rect rd = page_ == TitlePage::ModsNotice ? design::title::modsRow(i)
                        : isCard(page_)                ? design::title::cardRow(i)
                                                       : rowRect(i, n);
        targets_.push_back({{isCard(page_) ? TargetKind::CardButton : TargetKind::TitleRow, static_cast<int>(r)},
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
    const bool card = isCard(page_);
    const HitTarget* t = findTarget(focus_);
    hints_.push_back({Prompt::Confirm, menuText("Select"), t && t->enabled});
    if (card)
        hints_.push_back({Prompt::Back, page_ == TitlePage::ModsNotice ? menuText("Later") : menuText("Cancel")});
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
    if (isGameRow(r)) { // the game card: the menu runs for that game from now on (the host switches at once)
        out.actions.push_back({0, TitleActionKind::SelectGame, gameOf(r)});
        page_ = TitlePage::Main;
        focus_ = {};
        out.sound = UiSound::Confirm;
        return;
    }
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
            if (cfg_.showCoop) { // Solo or Co-op first
                mainFocus_ = focus_;
                page_ = TitlePage::NewGameChoice;
                focus_ = {TargetKind::CardButton, static_cast<int>(TitleRow::NewGameSolo)};
                out.sound = UiSound::Confirm;
            } else
                emit(TitleActionKind::NewGame);
            break;
        case TitleRow::NewGameSolo:
            page_ = TitlePage::Main;
            focus_ = mainFocus_;
            emit(TitleActionKind::NewGame);
            break;
        case TitleRow::NewGameCoop: // the party is hosted or joined on the campaign (the Multiplayer pages, Co-op
                                    // scope)
            page_ = TitlePage::Multiplayer;
            mpScreen_.open(st.multiplayer, mp::MpScope::Coop);
            out.sound = UiSound::Confirm;
            break;
        case TitleRow::GameDrakan:
        case TitleRow::GameOotf:
            break;                 // handled above
        case TitleRow::ModsEnable: // the host turns the packs on and restarts the game
            page_ = TitlePage::Main;
            focus_ = mainFocus_;
            emit(TitleActionKind::ModsEnable);
            break;
        case TitleRow::ModsKeepOff:
            out.actions.push_back({0, TitleActionKind::ModsKeepOff, 0});
            page_ = TitlePage::Main;
            focus_ = mainFocus_;
            out.sound = UiSound::Confirm;
            break;
        case TitleRow::ModsOpen:
            if (cfg_.pcSettingsPages) { // the modern pages, on the Mods page; Back returns to the main menu
                page_ = TitlePage::PcSettings;
                focus_ = mainFocus_;
                pcPages_.setTrail({menuText("Main Menu")});
                pcPages_.open("mods");
                out.actions.push_back({0, TitleActionKind::OpenMods, 1});
                out.sound = UiSound::Confirm;
            } else { // the host opens the F1 overlay on its Mods page
                page_ = TitlePage::Main;
                focus_ = mainFocus_;
                emit(TitleActionKind::OpenMods, 0);
            }
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
                pcPages_.setTrail({menuText("Main Menu")});
                pcPages_.open();
                out.sound = UiSound::Confirm;
            } else
                emit(TitleActionKind::OpenPcSettings);
            break;
        case TitleRow::Multiplayer:
            mainFocus_ = focus_;
            page_ = TitlePage::Multiplayer;
            mpScreen_.open(st.multiplayer, mp::MpScope::Versus);
            out.sound = UiSound::Confirm;
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
    if (isCard(page_)) {
        page_ = TitlePage::Main;
        focus_ = mainFocus_;
        out.sound = UiSound::Back;
    }
    // Main: the title has no "back" (the native page ignores Esc too), except to the game choice.
    else if (page_ == TitlePage::Main && offerGames_) {
        mainFocus_ = focus_;
        page_ = TitlePage::GameChoice;
        focus_ = {};
        out.sound = UiSound::Back;
    }
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
        if (suspendLeft_ > 0 || page_ == TitlePage::Options || page_ == TitlePage::PcSettings ||
            page_ == TitlePage::Multiplayer)
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
        if (page_ == TitlePage::GameChoice) { // by index: the list scrolls past its visible rows
            const int n = gameCount(st);
            const int i = focus_.kind == TargetKind::CardButton ? gameOf(static_cast<TitleRow>(focus_.index)) : st.game;
            focus_ = {TargetKind::CardButton, static_cast<int>(gameRowOf(std::clamp(i + dy, 0, n - 1)))};
            buildTargets(st);
            continue;
        }
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

void TitleScreen::resumeMultiplayer(const mp::MpState& st) {
    mainFocus_ = focus_;
    page_ = TitlePage::Multiplayer;
    mpScreen_.openLobby(st);
}

TitleFrameOutput TitleScreen::frame(const TitleState& st, const UiInput& in, const Viewport& vp) {
    TitleFrameOutput out;
    if (!open_)
        return out;
    lf_ = computeFrame(vp);
    offerGames_ = st.offerGames;
    if (!st.newMods.empty() && !modsAsked_ && page_ == TitlePage::Main && !loading_) {
        modsAsked_ = true; // first: enabling restarts the game, so the game card would be asked for nothing
        mainFocus_ = focus_;
        page_ = TitlePage::ModsNotice;
        focus_ = {TargetKind::CardButton, static_cast<int>(TitleRow::ModsEnable)};
    }
    if (st.offerGames && !gameAsked_ && page_ == TitlePage::Main && !loading_) {
        gameAsked_ = true;
        page_ = TitlePage::GameChoice;
        focus_ = {TargetKind::CardButton, static_cast<int>(gameRowOf(std::clamp(st.game, 0, gameCount(st) - 1)))};
    }
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
    if (page_ == TitlePage::Multiplayer) {
        mp::MpFrameOutput mo = mpScreen_.frame(st.multiplayer, in, vp);
        out.sound = mo.sound;
        out.mpActions = std::move(mo.actions);
        if (!mo.wantsClose) {
            out.draw = std::move(mo.draw);
            return out;
        }
        // Back on the Multiplayer page: the main menu again with Multiplayer focused, drawn this frame.
        mpScreen_.close();
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
    if (page_ == TitlePage::Multiplayer) { // opened this frame: show the page at once (no input yet)
        UiInput none;
        none.device = in.device;
        out.draw = mpScreen_.frame(st.multiplayer, none, vp).draw;
        return out;
    }
    if (page_ == TitlePage::PcSettings && st.pcSettings) { // opened this frame: show the pages at once
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

void TitleScreen::drawGameCard(const TitleState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect c = design::title::gameCard();
    d.rect({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, withAlpha(th.backdropEdge, 120));
    d.panel(p.map(c), PanelStyle::Main, th.panelRaised);
    constexpr float pad = 44;
    p.text(d, {c.x + pad, c.y + 28, c.w - 2 * pad, 52}, menuText("Choose your game"), FontRole::Title, th.heading);
    p.text(d, {c.x + pad, c.y + 40, c.w - 2 * pad, 40}, menuText("Each game keeps its own saves."), FontRole::Small,
           th.textMuted, TextAlign::Right);
    p.rule(d, c.x + pad, c.y + 92, c.w - 2 * pad);
    const int n = gameCount(st);
    const int focused =
        focus_.kind == TargetKind::CardButton ? std::clamp(gameOf(static_cast<TitleRow>(focus_.index)), 0, n - 1) : 0;
    const int first = gameScroll(focused, n);
    const float listW = design::title::gameRow(0).w;
    for (int i = first; i < n && i < first + design::title::kGameRowsShown; ++i) {
        const TargetId id{TargetKind::CardButton, static_cast<int>(gameRowOf(i))};
        const std::string name = i < static_cast<int>(st.games.size()) ? st.games[static_cast<size_t>(i)].name : "?";
        const Rect r = design::title::gameRow(i - first);
        p.button(d, r, name, focus_ == id, hover_ == id, true);
        if (i == st.game) // the game the menu runs for now
            p.text(d, {r.x + 24, r.y, r.w - 48, r.h}, menuText("Playing"), FontRole::Small, th.textMuted,
                   TextAlign::Right);
    }
    if (first > 0)
        p.text(d, {c.x + pad, c.y + 96, listW, 22}, menuText("More above"), FontRole::Small, th.textMuted,
               TextAlign::Center);
    if (first + design::title::kGameRowsShown < n)
        p.text(d, {c.x + pad, c.bottom() - pad + 6, listW, 30}, menuText("More below"), FontRole::Small, th.textMuted,
               TextAlign::Center);
    if (focused >= static_cast<int>(st.games.size()))
        return;
    const TitleState::Game& g = st.games[static_cast<size_t>(focused)];
    const Rect cv = design::title::gameCover();
    d.panel(p.map(cv), PanelStyle::Main, th.panel);
    if (g.cover.kind != ImageKind::None)
        d.image(p.map(cv), g.cover);
    else // no picture: the title on the plain panel
        p.text(d, {cv.x + 24, cv.y + cv.h * 0.5f - 40, cv.w - 48, 80}, g.name, FontRole::Title, th.text,
               TextAlign::Center);
    d.outline(p.map(cv), withAlpha(th.brass, 200), p.px(2));
    const Rect dr = design::title::gameDescription();
    p.text(d, {dr.x, dr.y, dr.w, 40}, g.name, FontRole::Heading, th.text);
    const std::vector<std::string> lines =
        p.wrap(g.description.empty() ? std::string(menuText("A campaign from an installed mod pack.")) : g.description,
               dr.w, FontRole::Body);
    float y = dr.y + 46;
    for (size_t i = 0; i < lines.size() && y + 30 <= dr.bottom() + 1; ++i, y += 30)
        p.text(d, {dr.x, y, dr.w, 30}, lines[i], FontRole::Body, th.textMuted);
}

void TitleScreen::drawModsNotice(const TitleState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect c = design::title::modsCard();
    d.rect({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, withAlpha(th.backdropEdge, 120));
    d.panel(p.map(c), PanelStyle::Main, th.panelRaised);
    constexpr float pad = 44;
    p.fittedText(d, {c.x + pad, c.y + 28, c.w - 2 * pad, 52},
                 st.newMods.size() == 1 ? menuText("New mod pack found") : menuText("New mod packs found"),
                 FontRole::Title, th.heading);
    p.rule(d, c.x + pad, c.y + 92, c.w - 2 * pad);
    std::string names;
    for (const std::string& n : st.newMods)
        names += (names.empty() ? "" : ", ") + n;
    std::vector<std::string> body;
    auto add = [&](const std::string& text, size_t maxLines) {
        const std::vector<std::string> l = p.wrap(text, c.w - 2 * pad, FontRole::Body);
        for (size_t i = 0; i < l.size() && i < maxLines; ++i)
            body.push_back(i + 1 == maxLines && i + 1 < l.size() ? l[i] + "..." : l[i]);
    };
    add(st.newMods.size() == 1
            ? menuFormat("In the mods folder: {0}.", {names})
            : menuFormat("{0} new packs in the mods folder: {1}.", {std::to_string(st.newMods.size()), names}),
        3);
    add(menuText("New packs stay off until you enable them. They take effect after a restart."), 2);
    if (st.newModCampaigns > 0)
        add(menuText("A campaign pack adds its game to the Choose your game screen."), 2);
    const float bottom = design::title::modsRow(0).y - 12;
    float y = c.y + 108;
    for (size_t i = 0; i < body.size() && y + 30 <= bottom; ++i, y += 30)
        p.text(d, {c.x + pad, y, c.w - 2 * pad, 30}, body[i], FontRole::Body, th.textMuted);
    const std::vector<TitleRow> cr = rowsOf(TitlePage::ModsNotice, st);
    for (size_t i = 0; i < cr.size(); ++i) {
        const TargetId id{TargetKind::CardButton, static_cast<int>(cr[i])};
        p.button(d, design::title::modsRow(static_cast<int>(i)), titleRowLabel(cr[i]), focus_ == id, hover_ == id, true,
                 TextAlign::Center);
    }
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

    if (!st.brandTitle.empty()) { // a text plaque over the native logo
        const Rect plaque{460, 30, 1000, 300};
        d.panel(p.map(plaque), PanelStyle::Main,
                withAlpha(th.panel, 255)); // opaque: the native logo must not ghost through
        p.text(d, {plaque.x, plaque.y + 50, plaque.w, 120}, st.brandTitle, FontRole::Title, th.text, TextAlign::Center);
        if (!st.brandSubtitle.empty())
            p.text(d, {plaque.x, plaque.y + 175, plaque.w, 70}, st.brandSubtitle, FontRole::Heading, th.textMuted,
                   TextAlign::Center);
    }
    if (loading_) {
        p.confirmCard(d, design::title::cardPanel(), menuText("Loading..."),
                      st.continueLocation.empty() ? std::string(menuText("Continuing your last game."))
                                                  : menuFormat("Continuing at {0}.", {st.continueLocation}));
        return;
    }
    const bool card = isCard(page_);
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
    if (!st.footer.empty() || st.offerGames) {
        const std::string game = st.offerGames && st.game >= 0 && st.game < static_cast<int>(st.games.size())
                                     ? st.games[static_cast<size_t>(st.game)].name
                                     : std::string();
        const std::string line = game.empty() ? st.footer : st.footer.empty() ? game : game + "   " + st.footer;
        p.text(d, {kMargin, kFooterY, 700, kHintH}, line, FontRole::Small, th.textMuted);
    }

    if (!card) { // why an enabled campaign is not on the game card, packs that could not be read
        std::vector<std::string> lines;
        for (const std::string* note : {&st.gameChoiceNote, &st.modsNote}) {
            if (note->empty())
                continue;
            const std::vector<std::string> l = p.wrap(*note, design::title::noteTextWidth(), FontRole::Small);
            const size_t keep = std::min<size_t>(l.size(), design::title::kNoteLinesMax / 2);
            for (size_t i = 0; i < keep; ++i)
                lines.push_back(i + 1 == keep && keep < l.size() ? l[i] + "..." : l[i]);
        }
        if (!lines.empty()) {
            // A plate under the lines: the title art behind them is bright.
            d.panel(p.map(design::title::notePlate(static_cast<int>(lines.size()))), PanelStyle::Main,
                    withAlpha(th.panel, 225));
            for (size_t i = 0; i < lines.size(); ++i)
                p.text(d, design::title::noteLine(static_cast<int>(i)), lines[i], FontRole::Small, th.text);
        }
    }

    if (page_ == TitlePage::GameChoice) {
        drawGameCard(st, d);
    } else if (page_ == TitlePage::ModsNotice) {
        drawModsNotice(st, d);
    } else if (card) {
        if (page_ == TitlePage::NewGameChoice)
            p.confirmCard(d, design::title::cardPanel(), menuText("New Game"),
                          menuText("Play the campaign alone or with friends."));
        else
            p.confirmCard(d, design::title::cardPanel(), skinText("title.quit.title", menuText("Quit the game?")),
                          skinText("title.quit.body", menuText("Drakan closes and returns to the desktop.")));
        const std::vector<TitleRow> cr = rows(st);
        for (size_t i = 0; i < cr.size(); ++i) {
            const TargetId id{TargetKind::CardButton, static_cast<int>(cr[i])};
            const std::string label = titleRowLabel(cr[i]);
            p.button(d, design::title::cardRow(static_cast<int>(i)), label, focus_ == id, hover_ == id, true);
        }
    }

    for (const Hint& h : hints_) {
        const TargetId id{TargetKind::Hint, static_cast<int>(h.prompt)};
        if (const HitTarget* t = findTarget(id))
            p.drawHint(d, h, t->rect, hover_ == id && h.enabled, kHintGlyph, FontRole::Body);
    }
}

} // namespace gameui
