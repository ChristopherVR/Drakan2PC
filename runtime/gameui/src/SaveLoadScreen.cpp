#include "gameui/SaveLoadScreen.h"

#include <algorithm>

#include "Paint.h"
#include "gameui/PauseScreen.h"

namespace gameui {

namespace {

using detail::fmtInt;

constexpr float kMargin = 96;
constexpr float kPanelY = 150, kPanelH = 820;
constexpr float kListX = kMargin, kListW = 840;
constexpr float kPrevW = 840, kPrevX = kDesignW - kMargin - kPrevW;
constexpr float kCardX = kListX + 28, kCardW = kListW - 56, kCardY = kPanelY + 96, kCardH = 160, kCardStep = 176;
constexpr float kThumbPad = 16, kThumbW = 228, kThumbH = 128;
constexpr float kBarY = kPanelY + 22, kBarH = 56, kArrowW = 56;
constexpr float kImgX = kPrevX + 32, kImgY = kPanelY + 32, kImgW = kPrevW - 64, kImgH = (kPrevW - 64) * 9.0f / 16.0f;
constexpr float kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36;

const char* slotTitle(const SaveSlot& s, SaveLoadMode mode) {
    switch (s.state) {
        case SlotState::Used:
            return nullptr; // the location
        case SlotState::Empty:
            return mode == SaveLoadMode::Save ? "Empty slot" : "Empty";
        case SlotState::NoSpace:
            return "Not enough space";
        case SlotState::Corrupt:
            return "Damaged save";
        case SlotState::Unknown:
            break;
    }
    return "Reading...";
}

std::string dateLine(const SaveSlotsState& st, const SaveSlot& s) {
    const std::string d = formatSaveDate(s.time, st.dateOrder), t = formatSaveTime(s.time, st.clock24);
    if (d.empty())
        return t;
    return t.empty() ? d : d + ", " + t;
}

std::string locationOf(const SaveSlot& s) {
    return s.location.empty() ? std::string("Unknown location") : s.location;
}

const char* confirmLabel(SaveLoadMode mode, SlotState s) {
    if (mode == SaveLoadMode::Load)
        return "Load";
    return s == SlotState::Used || s == SlotState::Corrupt ? "Overwrite" : "Save";
}

} // namespace

const char* saveLoadActionName(SaveLoadActionKind k) {
    switch (k) {
        case SaveLoadActionKind::SelectSlot:
            return "SelectSlot";
        case SaveLoadActionKind::ChangePage:
            return "ChangePage";
        case SaveLoadActionKind::ConfirmSave:
            return "ConfirmSave";
        case SaveLoadActionKind::ConfirmOverwrite:
            return "ConfirmOverwrite";
        case SaveLoadActionKind::ConfirmLoad:
            return "ConfirmLoad";
        case SaveLoadActionKind::Back:
            return "Back";
    }
    return "?";
}

// ---- queue ----

uint32_t SaveLoadActionQueue::push(SaveLoadAction a) {
    std::lock_guard<std::mutex> lock(m_);
    if (a.kind == SaveLoadActionKind::SelectSlot && !q_.empty() && q_.back().kind == SaveLoadActionKind::SelectSlot) {
        q_.back().value = a.value;
        return q_.back().id;
    }
    a.id = nextId_++;
    q_.push_back(a);
    return a.id;
}

std::optional<SaveLoadAction> SaveLoadActionQueue::pop() {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.empty())
        return std::nullopt;
    SaveLoadAction a = q_.front();
    q_.pop_front();
    return a;
}

size_t SaveLoadActionQueue::size() const {
    std::lock_guard<std::mutex> lock(m_);
    return q_.size();
}

void SaveLoadActionQueue::clear() {
    std::lock_guard<std::mutex> lock(m_);
    q_.clear();
}

// ---- layout ----

namespace design::saveload {
Rect listPanel() {
    return {kListX, kPanelY, kListW, kPanelH};
}
Rect slotCard(int row) {
    return {kCardX, kCardY + static_cast<float>(row) * kCardStep, kCardW, kCardH};
}
Rect slotThumb(int row) {
    const Rect c = slotCard(row);
    return {c.x + kThumbPad, c.y + kThumbPad, kThumbW, kThumbH};
}
Rect pageBar() {
    return {kCardX, kBarY, kCardW, kBarH};
}
Rect pageArrow(int dir) {
    const Rect b = pageBar();
    return dir < 0 ? Rect{b.right() - 300, b.y, kArrowW, b.h} : Rect{b.right() - kArrowW, b.y, kArrowW, b.h};
}
Rect previewPanel() {
    return {kPrevX, kPanelY, kPrevW, kPanelH};
}
Rect previewImage() {
    return {kImgX, kImgY, kImgW, kImgH};
}
Rect cardPanel() {
    return design::pause::cardPanel(2);
}
Rect cardButton(int i) {
    return design::pause::cardRow(i, 2);
}
} // namespace design::saveload

// ---- state machine ----

SaveLoadScreen::SaveLoadScreen(SaveLoadConfig cfg) : cfg_(std::move(cfg)) {}

bool SaveLoadScreen::slotActionable(SaveLoadMode mode, SlotState s) {
    if (mode == SaveLoadMode::Load)
        return s == SlotState::Used;
    return s == SlotState::Empty || s == SlotState::Used || s == SlotState::Corrupt;
}

void SaveLoadScreen::open(SaveLoadMode mode, SaveLoadContext ctx, int focusSlot) {
    open_ = true;
    mode_ = ctx == SaveLoadContext::LevelChange ? SaveLoadMode::Save : mode;
    ctx_ = ctx;
    card_ = SaveCard::None;
    cardSlot_ = 0;
    focus_ = hover_ = listFocus_ = {};
    wantFocus_ = focusSlot;
    focusRow_ = 0;
    pendingPage_ = -1;
    pendingAge_ = 0;
    lastSelected_ = 0;
    suspendLeft_ = 0;
    progress_ = SaveProgress::None;
    progressSlot_ = 0;
}

void SaveLoadScreen::close() {
    open_ = false;
    card_ = SaveCard::None;
    progress_ = SaveProgress::None;
    suspendLeft_ = 0;
    pendingPage_ = -1;
    targets_.clear();
    hints_.clear();
}

void SaveLoadScreen::reactivate() {
    suspendLeft_ = 0;
    progress_ = SaveProgress::None;
    if (card_ != SaveCard::None) {
        card_ = SaveCard::None;
        focus_ = listFocus_;
    }
}

int SaveLoadScreen::focusedSlot() const {
    return focus_.kind == TargetKind::SaveSlot ? focus_.index : 0;
}

const HitTarget* SaveLoadScreen::findTarget(const TargetId& id) const {
    for (const HitTarget& t : targets_)
        if (t.id == id)
            return &t;
    return nullptr;
}

bool SaveLoadScreen::listLive(const SaveSlotsState& st) const {
    return (st.card == CardState::Ready || st.card == CardState::Full) && pendingPage_ < 0;
}

void SaveLoadScreen::buildTargets(const SaveSlotsState& st) {
    targets_.clear();
    if (card_ != SaveCard::None) {
        targets_.push_back({{TargetKind::CardButton, static_cast<int>(SaveCardButton::Primary)},
                            lf_.map(design::saveload::cardButton(0))});
        targets_.push_back({{TargetKind::CardButton, static_cast<int>(SaveCardButton::Cancel)},
                            lf_.map(design::saveload::cardButton(1))});
        return;
    }
    if (listLive(st))
        for (size_t i = 0; i < st.slots.size() && i < 4; ++i)
            targets_.push_back(
                {{TargetKind::SaveSlot, st.slots[i].number}, lf_.map(design::saveload::slotCard(static_cast<int>(i)))});
    if (st.pageCount > 1 && st.card != CardState::NoCard && st.card != CardState::Unformatted) {
        const bool en = pendingPage_ < 0;
        targets_.push_back({{TargetKind::PageStep, -1}, lf_.map(design::saveload::pageArrow(-1)), false, en});
        targets_.push_back({{TargetKind::PageStep, 1}, lf_.map(design::saveload::pageArrow(1)), false, en});
    }
}

void SaveLoadScreen::ensureFocus(const SaveSlotsState& st) {
    if (card_ != SaveCard::None) {
        if (focus_.kind != TargetKind::CardButton)
            focus_ = {TargetKind::CardButton,
                      static_cast<int>(card_ == SaveCard::Save ? SaveCardButton::Primary : SaveCardButton::Cancel)};
        return;
    }
    if (!listLive(st)) {
        if (focus_.kind == TargetKind::SaveSlot)
            focusRow_ = std::max(0, (focus_.index - 1) % 4);
        focus_ = {};
        return;
    }
    auto has = [&](int n) { return n > 0 && findTarget({TargetKind::SaveSlot, n}) != nullptr; };
    if (wantFocus_ > 0 && has(wantFocus_)) {
        focus_ = {TargetKind::SaveSlot, wantFocus_};
        wantFocus_ = 0;
        return;
    }
    if (focus_.kind == TargetKind::SaveSlot && has(focus_.index))
        return;
    wantFocus_ = 0;
    if (st.slots.empty())
        return;
    // The native selection, else the kept row, else the first slot this mode can use.
    int pick = 0;
    if (st.nativeRow >= 0 && st.nativeRow < static_cast<int>(st.slots.size()) && focus_.kind == TargetKind::None &&
        lastSelected_ == 0)
        pick = st.slots[static_cast<size_t>(st.nativeRow)].number;
    if (!pick && focusRow_ > 0 && focusRow_ < static_cast<int>(st.slots.size()))
        pick = st.slots[static_cast<size_t>(focusRow_)].number;
    if (!pick)
        for (const SaveSlot& s : st.slots)
            if (slotActionable(mode_, s.state)) {
                pick = s.number;
                break;
            }
    if (!pick)
        pick = st.slots.front().number;
    focus_ = {TargetKind::SaveSlot, pick};
}

void SaveLoadScreen::buildHints(const SaveSlotsState& st) {
    hints_.clear();
    if (card_ != SaveCard::None) {
        hints_.push_back({Prompt::Confirm, "Select"});
        hints_.push_back({Prompt::Back, "Cancel"});
    } else {
        const int n = focusedSlot();
        const SaveSlot* s = n ? st.slot(n) : nullptr;
        const bool en = s && listLive(st) && !st.busy && slotActionable(mode_, s->state);
        hints_.push_back({Prompt::Confirm, confirmLabel(mode_, s ? s->state : SlotState::Empty), en});
        if (st.pageCount > 1)
            hints_.push_back({Prompt::PageNext, "Page", pendingPage_ < 0});
        hints_.push_back({Prompt::Back, ctx_ == SaveLoadContext::LevelChange ? "Skip" : "Back"});
    }
    const detail::Paint p(lf_, cfg_.screen);
    const std::vector<Rect> rs =
        p.layoutHints(hints_, kDesignW - kMargin, false, kFooterY, kHintH, kHintGlyph, kHintGap, FontRole::Body);
    for (size_t i = 0; i < hints_.size(); ++i)
        targets_.push_back(
            {{TargetKind::Hint, static_cast<int>(hints_[i].prompt)}, lf_.map(rs[i]), false, hints_[i].enabled});
}

void SaveLoadScreen::steer(const SaveSlotsState& st, SaveLoadFrameOutput& out) {
    const int n = focusedSlot();
    if (!n || !listLive(st) || st.busy || card_ != SaveCard::None)
        return;
    const int nativeSlot = st.nativeRow >= 0 && st.nativeRow < static_cast<int>(st.slots.size())
                               ? st.slots[static_cast<size_t>(st.nativeRow)].number
                               : 0;
    if (n == nativeSlot) {
        lastSelected_ = n;
        return;
    }
    if (n == lastSelected_)
        return;
    out.actions.push_back({0, SaveLoadActionKind::SelectSlot, n});
    lastSelected_ = n;
}

void SaveLoadScreen::changePage(const SaveSlotsState& st, int delta, SaveLoadFrameOutput& out) {
    if (st.pageCount <= 1 || pendingPage_ >= 0 || card_ != SaveCard::None)
        return;
    const int page = ((st.page + delta) % st.pageCount + st.pageCount) % st.pageCount;
    if (page == st.page)
        return;
    if (focus_.kind == TargetKind::SaveSlot)
        focusRow_ = std::max(0, (focus_.index - 1) % 4);
    out.actions.push_back({0, SaveLoadActionKind::ChangePage, page});
    pendingPage_ = page;
    pendingAge_ = 0;
    wantFocus_ = page * 4 + focusRow_ + 1;
    lastSelected_ = 0; // the rescan may move the native selection
    focus_ = {};
    out.sound = UiSound::Page;
}

void SaveLoadScreen::confirm(SaveLoadActionKind k, int number, SaveLoadFrameOutput& out) {
    if (lastSelected_ != number)
        out.actions.push_back({0, SaveLoadActionKind::SelectSlot, number});
    lastSelected_ = number;
    out.actions.push_back({0, k, number});
    card_ = SaveCard::None;
    focus_ = {TargetKind::SaveSlot, number};
    suspendLeft_ = cfg_.suspendFrames;
    progress_ = k == SaveLoadActionKind::ConfirmLoad ? SaveProgress::Loading : SaveProgress::Saving;
    progressSlot_ = number;
    out.sound = UiSound::Confirm;
}

void SaveLoadScreen::activateSlot(const SaveSlotsState& st, int number, SaveLoadFrameOutput& out) {
    const SaveSlot* s = st.slot(number);
    if (!s || !listLive(st) || st.busy || !slotActionable(mode_, s->state))
        return;
    auto openCard = [&](SaveCard c) {
        listFocus_ = {TargetKind::SaveSlot, number};
        card_ = c;
        cardSlot_ = number;
        focus_ = {};
        out.sound = UiSound::Confirm;
    };
    if (mode_ == SaveLoadMode::Load) {
        if (st.inGame || (ctx_ == SaveLoadContext::Title && cfg_.confirmLoadOnTitle))
            openCard(SaveCard::Load);
        else
            confirm(SaveLoadActionKind::ConfirmLoad, number, out);
        return;
    }
    if (s->state == SlotState::Empty) {
        if (cfg_.confirmSave)
            openCard(SaveCard::Save);
        else
            confirm(SaveLoadActionKind::ConfirmSave, number, out);
        return;
    }
    openCard(SaveCard::Overwrite);
}

void SaveLoadScreen::pressCard(SaveCardButton b, const SaveSlotsState& st, SaveLoadFrameOutput& out) {
    (void)st;
    if (card_ == SaveCard::None)
        return;
    if (b == SaveCardButton::Cancel) {
        card_ = SaveCard::None;
        focus_ = listFocus_;
        out.sound = UiSound::Back;
        return;
    }
    const SaveLoadActionKind k = card_ == SaveCard::Load        ? SaveLoadActionKind::ConfirmLoad
                                 : card_ == SaveCard::Overwrite ? SaveLoadActionKind::ConfirmOverwrite
                                                                : SaveLoadActionKind::ConfirmSave;
    confirm(k, cardSlot_, out);
}

void SaveLoadScreen::back(SaveLoadFrameOutput& out) {
    out.sound = UiSound::Back;
    if (card_ != SaveCard::None) {
        card_ = SaveCard::None;
        focus_ = listFocus_;
        return;
    }
    out.actions.push_back({0, SaveLoadActionKind::Back});
    out.wantsBack = true;
}

void SaveLoadScreen::applyInput(const SaveSlotsState& st, const UiInput& in, SaveLoadFrameOutput& out) {
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
            switch (hover_.kind) {
                case TargetKind::SaveSlot:
                    focus_ = hover_;
                    activateSlot(st, hover_.index, out);
                    break;
                case TargetKind::PageStep:
                    changePage(st, hover_.index, out);
                    break;
                case TargetKind::CardButton:
                    pressCard(static_cast<SaveCardButton>(hover_.index), st, out);
                    break;
                case TargetKind::Hint: {
                    const auto pr = static_cast<Prompt>(hover_.index);
                    if (pr == Prompt::Back)
                        back(out);
                    else if (pr == Prompt::PageNext)
                        changePage(st, 1, out);
                    else if (pr == Prompt::Confirm) {
                        if (focus_.kind == TargetKind::CardButton)
                            pressCard(static_cast<SaveCardButton>(focus_.index), st, out);
                        else if (focus_.kind == TargetKind::SaveSlot)
                            activateSlot(st, focus_.index, out);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    std::vector<UiKey> keys = in.keys;
    if (in.wheel != 0)
        keys.push_back(in.wheel > 0 ? UiKey::Up : UiKey::Down);
    for (UiKey k : keys) {
        if (out.wantsBack || suspendLeft_ > 0)
            break;
        const SaveCard cardBefore = card_;
        int dy = 0;
        switch (k) {
            case UiKey::Up:
                dy = -1;
                break;
            case UiKey::Down:
                dy = 1;
                break;
            case UiKey::Left:
            case UiKey::PagePrev:
                changePage(st, -1, out);
                break;
            case UiKey::Right:
            case UiKey::PageNext:
                changePage(st, 1, out);
                break;
            case UiKey::Confirm:
                if (focus_.kind == TargetKind::CardButton)
                    pressCard(static_cast<SaveCardButton>(focus_.index), st, out);
                else if (focus_.kind == TargetKind::SaveSlot)
                    activateSlot(st, focus_.index, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                back(out);
                break;
            default:
                break;
        }
        if (card_ != cardBefore || pendingPage_ >= 0) {
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

SaveLoadFrameOutput SaveLoadScreen::frame(const SaveSlotsState& st, const UiInput& in, const Viewport& vp) {
    SaveLoadFrameOutput out;
    if (!open_)
        return out;
    lf_ = computeFrame(vp);
    if (pendingPage_ >= 0) {
        if (st.page == pendingPage_ && st.card != CardState::Reading)
            pendingPage_ = -1;
        else if (++pendingAge_ > cfg_.pageWaitFrames)
            pendingPage_ = -1; // the page never came: take the state's page as it is
    }
    buildTargets(st);
    ensureFocus(st);
    buildHints(st);
    const TargetId focusBefore = focus_;
    if (st.loadingSlot > 0 && progress_ == SaveProgress::None) {
        progress_ = SaveProgress::Loading;
        progressSlot_ = st.loadingSlot;
    }
    if (progress_ != SaveProgress::None) {
        hover_ = {}; // the load / save runs: no input until reactivate()
    } else if (suspendLeft_ > 0) {
        --suspendLeft_;
        hover_ = {};
    } else if (st.busy) {
        hover_ = {};
    } else {
        applyInput(st, in, out);
    }
    if (out.sound == UiSound::None && !(focus_ == focusBefore) && focusBefore.kind != TargetKind::None &&
        focus_.kind != TargetKind::None)
        out.sound = UiSound::Move;
    if (out.wantsBack) {
        close();
        return out;
    }
    buildTargets(st);
    ensureFocus(st);
    steer(st, out);
    buildHints(st);
    draw(st, out.draw);
    return out;
}

// ---- drawing ----

void SaveLoadScreen::draw(const SaveSlotsState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    d.panel({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, PanelStyle::Backdrop, th.backdrop);
    if (st.quickOperation) {
        p.confirmCard(d, design::saveload::cardPanel(), st.quickOperation == 1 ? "Quick saving..." : "Quick loading...",
                      "Quicksave");
        return;
    }

    // Header band: the screen's title centred, where it was opened from below it.
    const Rect band = lf_.band(0, 120);
    d.gradient(band, withAlpha(th.panel, 240), withAlpha(th.panel, 0));
    p.ornateRule(d, band.x, band.right(), band.y + p.px(112), true);
    p.text(d, {kDesignW * 0.5f - 450, 24, 900, 60}, mode_ == SaveLoadMode::Save ? "Save Game" : "Load Game",
           FontRole::Title, th.heading, TextAlign::Center);
    const char* from = ctx_ == SaveLoadContext::Title         ? "Main Menu"
                       : ctx_ == SaveLoadContext::LevelChange ? "Level complete"
                                                              : "Paused";
    p.text(d, {kDesignW * 0.5f - 450, 80, 900, 24}, from, FontRole::Small, th.textMuted, TextAlign::Center);

    // Slot list.
    const Rect lp = design::saveload::listPanel();
    d.panel(p.map(lp), PanelStyle::Main, th.panel);
    const Rect bar = design::saveload::pageBar();
    const int first = st.page * 4 + 1;
    p.label(d, {bar.x + 4, bar.y, 400, bar.h}, "Slots " + fmtInt(first) + " - " + fmtInt(first + 3), th.brass);
    if (st.pageCount > 1) {
        const bool en = pendingPage_ < 0;
        const Rect a0 = design::saveload::pageArrow(-1), a1 = design::saveload::pageArrow(1);
        const bool h0 = hover_ == TargetId{TargetKind::PageStep, -1}, h1 = hover_ == TargetId{TargetKind::PageStep, 1};
        d.image(p.map({a0.x + 8, a0.cy() - 18, 36, 36}), {ImageKind::Prompt, uint32_t(Prompt::PagePrev)},
                en ? 0xffffffffu : withAlpha(0xffffffffu, 90));
        d.image(p.map({a1.x + 12, a1.cy() - 18, 36, 36}), {ImageKind::Prompt, uint32_t(Prompt::PageNext)},
                en ? 0xffffffffu : withAlpha(0xffffffffu, 90));
        const int shown = pendingPage_ >= 0 ? pendingPage_ : st.page;
        p.text(d, {a0.right(), bar.y, a1.x - a0.right(), bar.h},
               "Page " + fmtInt(shown + 1) + " of " + fmtInt(st.pageCount), FontRole::Body,
               h0 || h1 ? th.heading : (en ? th.text : th.textMuted), TextAlign::Center);
    }
    p.rule(d, bar.x, bar.bottom() + 6, bar.w);

    auto message = [&](const char* title, const std::string& body) {
        const Rect m{lp.x + 56, lp.y + 300, lp.w - 112, 200};
        p.crest(d, p.map({m.cx(), m.y, 0, 0}).x, p.map({0, m.y, 0, 0}).y, 34);
        p.text(d, {m.x, m.y + 40, m.w, 52}, title, FontRole::Title, th.heading, TextAlign::Center);
        const std::vector<std::string> lines = p.wrap(body, m.w, FontRole::Body);
        float y = m.y + 104;
        for (size_t i = 0; i < lines.size() && i < 3; ++i, y += 32)
            p.text(d, {m.x, y, m.w, 32}, lines[i], FontRole::Body, th.textMuted, TextAlign::Center);
    };
    switch (st.card) {
        case CardState::NoCard:
            message("No memory card", "The memory card could not be read. Check the memory card folder (mc0) of "
                                      "the game and try again.");
            break;
        case CardState::Unformatted:
            message("Memory card not formatted", "The memory card has no file system. Format it before saving.");
            break;
        case CardState::Error:
            message("Memory card error",
                    st.message.empty() ? std::string("The memory card could not be read.") : st.message);
            break;
        case CardState::Reading:
        case CardState::Ready:
        case CardState::Full:
            for (int i = 0; i < 4; ++i) {
                const bool live = listLive(st) && i < static_cast<int>(st.slots.size());
                if (live) {
                    drawSlot(st, st.slots[static_cast<size_t>(i)], design::saveload::slotCard(i), d);
                } else {
                    // Reading (or a page change rescanning): skeleton cards.
                    const Rect c = design::saveload::slotCard(i);
                    d.panel(p.map(c), PanelStyle::Card, withAlpha(th.panel, 140));
                    d.panel(p.map(design::saveload::slotThumb(i)), PanelStyle::Inset, withAlpha(th.inset, 150));
                    p.text(d, {c.x + kThumbPad + kThumbW + 28, c.y + 44, 400, 40},
                           i == 0 ? "Reading the memory card..." : "", FontRole::Body, th.textMuted);
                }
            }
            break;
    }

    drawPreview(st, d);
    if (progress_ != SaveProgress::None)
        drawProgress(st, d);
    else if (card_ != SaveCard::None)
        drawCard(st, d);

    // Footer hints.
    Rect fb = lf_.band(992, 88);
    fb.h = std::max(fb.h, lf_.view.y + lf_.view.h - fb.y); // to the viewport bottom (letterboxed 4:3)
    d.gradient(fb, withAlpha(th.panel, 0), withAlpha(th.panel, 235));
    for (const Hint& h : hints_) {
        const TargetId id{TargetKind::Hint, static_cast<int>(h.prompt)};
        if (const HitTarget* t = findTarget(id))
            p.drawHint(d, h, t->rect, hover_ == id && h.enabled, kHintGlyph, FontRole::Body);
    }
}

void SaveLoadScreen::drawSlot(const SaveSlotsState& st, const SaveSlot& s, const Rect& rd, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const TargetId id{TargetKind::SaveSlot, s.number};
    const bool live = card_ == SaveCard::None;
    const bool foc = live && focus_ == id, hov = live && hover_ == id;
    const bool usable = slotActionable(mode_, s.state);
    const Rect rp = p.map(rd);
    d.panel(rp, PanelStyle::Card, !usable ? withAlpha(th.panel, 150) : (hov ? th.panelRaised : th.panel));
    if (foc)
        p.focus(d, rp);

    // Thumbnail: the crest under the picture (a renderer without it shows the crest), text for the empty states.
    const Rect tb{rd.x + kThumbPad, rd.y + kThumbPad, kThumbW, kThumbH};
    d.panel(p.map(tb), PanelStyle::Inset, th.inset);
    if (s.state == SlotState::Used || s.state == SlotState::Corrupt) {
        p.crest(d, p.map(tb).cx(), p.map(tb).cy(), 22);
        if (s.preview.kind != ImageKind::None)
            d.image(p.map(tb.inset(2)), s.preview,
                    s.state == SlotState::Corrupt ? withAlpha(0xffffffffu, 110) : 0xffffffffu);
    } else {
        p.text(d, tb, s.state == SlotState::NoSpace ? "Full" : "Empty", FontRole::Small, th.textDisabled,
               TextAlign::Center);
    }
    d.outline(p.map(tb), foc ? th.gilt : withAlpha(th.brassDim, 200), p.px(1.25f));

    const float tx = tb.right() + 28, tw = rd.right() - 24 - tx;
    const Color headC = !usable ? th.textMuted : (foc ? th.heading : th.text);
    p.label(d, {tx, rd.y + 14, tw, 26}, "Slot " + fmtInt(s.number), foc ? th.brass : th.brassDim);
    const char* t = slotTitle(s, mode_);
    p.text(d, {tx, rd.y + 42, tw, 44}, t ? std::string(t) : locationOf(s), FontRole::Heading, headC);
    std::string sub;
    if (s.state == SlotState::Used || s.state == SlotState::Corrupt)
        sub = dateLine(st, s);
    else if (s.state == SlotState::Empty)
        sub = mode_ == SaveLoadMode::Save ? "Save your progress here." : "No saved game.";
    else if (s.state == SlotState::NoSpace)
        sub = "No room for a new save.";
    p.text(d, {tx, rd.y + 92, tw, 30}, sub, FontRole::Body, th.textMuted);
    if (s.playSeconds >= 0 && (s.state == SlotState::Used))
        p.text(d, {tx, rd.y + 92, tw, 30}, formatPlayTime(s.playSeconds), FontRole::Small, th.textMuted,
               TextAlign::Right);
    if (!usable && s.state != SlotState::Unknown)
        p.text(d, {tx, rd.y + 14, tw, 26},
               mode_ == SaveLoadMode::Load && s.state == SlotState::Empty ? "" : "Unavailable", FontRole::Small,
               th.textDisabled, TextAlign::Right);
}

void SaveLoadScreen::drawPreview(const SaveSlotsState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect pp = design::saveload::previewPanel();
    d.panel(p.map(pp), PanelStyle::Main, th.panel);
    const Rect img = design::saveload::previewImage();
    d.panel(p.map(img), PanelStyle::Inset, th.inset);
    const Rect ip = p.map(img);
    const int n = card_ != SaveCard::None ? cardSlot_ : focusedSlot();
    const SaveSlot* s = n && listLive(st) ? st.slot(n) : nullptr;
    const float x = pp.x + 32, w = pp.w - 64;
    float y = img.bottom() + 24;
    if (!s) {
        p.crest(d, ip.cx(), ip.cy(), 40);
        d.outline(ip, withAlpha(th.brassDim, 200), p.px(1.5f));
        if (!st.message.empty())
            p.text(d, {x, y, w, 30}, st.message, FontRole::Body, th.bad);
        return;
    }
    const bool hasPicture = s->state == SlotState::Used || s->state == SlotState::Corrupt;
    p.crest(d, ip.cx(), ip.cy(), 40);
    if (hasPicture && s->preview.kind != ImageKind::None)
        d.image(ip.inset(p.px(3)), s->preview,
                s->state == SlotState::Corrupt ? withAlpha(0xffffffffu, 110) : 0xffffffffu);
    if (!hasPicture) {
        const char* big = s->state == SlotState::NoSpace                                ? "No space"
                          : mode_ == SaveLoadMode::Save && s->state == SlotState::Empty ? "Save here"
                                                                                        : "Empty slot";
        p.text(d, {img.x, img.cy() + 30, img.w, 52}, big, FontRole::Title, th.textMuted, TextAlign::Center);
    }
    d.outline(ip, th.gilt, p.px(1.5f));

    p.label(d, {x, y, w, 26}, "Slot " + fmtInt(s->number), th.brass);
    if (s->state == SlotState::Used && !s->file.empty())
        p.text(d, {x, y, w, 26}, s->file, FontRole::Small, th.textMuted, TextAlign::Right);
    y += 30;
    const char* t = slotTitle(*s, mode_);
    p.text(d, {x, y, w, 56}, t ? std::string(t) : locationOf(*s), FontRole::Title,
           s->state == SlotState::Used ? th.heading : th.textMuted);
    y += 64;
    if (hasPicture) {
        const std::string date = formatSaveDate(s->time, st.dateOrder), time = formatSaveTime(s->time, st.clock24);
        p.text(d, {x, y, w * 0.5f, 32}, date.empty() ? std::string("Date unknown") : date, FontRole::Body, th.text);
        p.text(d, {x, y, w, 32}, time, FontRole::Numeric, th.text, TextAlign::Right);
        y += 36;
        if (s->playSeconds >= 0) {
            p.text(d, {x, y, w, 30}, "Play time", FontRole::Body, th.textMuted);
            p.text(d, {x, y, w, 30}, formatPlayTime(s->playSeconds), FontRole::Body, th.text, TextAlign::Right);
            y += 34;
        }
    }
    p.rule(d, x, y + 6, w);
    y += 20;
    std::string desc;
    switch (s->state) {
        case SlotState::Used:
            desc = mode_ == SaveLoadMode::Load
                       ? (st.inGame ? "Load this game. Progress since your last save is lost." : "Load this game.")
                       : "Overwrite this save with your current progress.";
            break;
        case SlotState::Empty:
            desc = mode_ == SaveLoadMode::Save ? "Save your current progress in this slot."
                                               : "This slot holds no saved game.";
            break;
        case SlotState::NoSpace:
            desc = "There is not enough free space on the memory card for a new save. Overwrite an existing save "
                   "instead.";
            break;
        case SlotState::Corrupt:
            desc = mode_ == SaveLoadMode::Save ? "This save is damaged. Saving here replaces it."
                                               : "This save is damaged and cannot be loaded.";
            break;
        case SlotState::Unknown:
            break;
    }
    const std::vector<std::string> lines = p.wrap(desc, w, FontRole::Body);
    for (size_t i = 0; i < lines.size() && i < 2; ++i, y += 30)
        p.text(d, {x, y, w, 30}, lines[i], FontRole::Body, th.textMuted);
    if (!st.message.empty())
        p.text(d, {x, pp.bottom() - 56, w, 30}, st.message, FontRole::Body, th.bad);
}

void SaveLoadScreen::drawProgress(const SaveSlotsState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const SaveSlot* s = st.slot(progressSlot_);
    std::string body = "Slot " + fmtInt(progressSlot_);
    if (s && s->state == SlotState::Used && !locationOf(*s).empty())
        body += ": " + locationOf(*s);
    p.confirmCard(d, design::saveload::cardPanel(), progress_ == SaveProgress::Loading ? "Loading..." : "Saving...",
                  body + ".");
}

void SaveLoadScreen::drawCard(const SaveSlotsState& st, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const SaveSlot* s = st.slot(cardSlot_);
    std::string title, body, primary;
    const std::string what = s ? locationOf(*s) : std::string();
    const std::string when = s ? dateLine(st, *s) : std::string();
    switch (card_) {
        case SaveCard::Overwrite:
            title = "Overwrite this save?";
            body = "Slot " + fmtInt(cardSlot_) + " (" + what + (when.empty() ? "" : ", " + when) +
                   ") is replaced by your current progress.";
            primary = "Overwrite";
            break;
        case SaveCard::Load:
            title = "Load this game?";
            body = "Progress since your last save is lost. Slot " + fmtInt(cardSlot_) + ": " + what + ".";
            primary = "Load";
            break;
        case SaveCard::Save:
            title = "Save in slot " + fmtInt(cardSlot_) + "?";
            body = "Your current progress is saved to the memory card.";
            primary = "Save";
            break;
        case SaveCard::None:
            return;
    }
    p.confirmCard(d, design::saveload::cardPanel(), title, body);
    for (int i = 0; i < 2; ++i) {
        const TargetId id{TargetKind::CardButton, i};
        p.button(d, design::saveload::cardButton(i), i == 0 ? primary : std::string("Cancel"), focus_ == id,
                 hover_ == id, true);
    }
}

} // namespace gameui
