#include "gameui/SettingsPages.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Paint.h"

namespace gameui {

namespace {

constexpr float kMargin = 96;
constexpr float kPanelY = 160, kPanelH = 660;
constexpr float kMenuX = kMargin, kMenuW = 560;
constexpr float kHelpW = 560, kHelpX = kDesignW - kMargin - kHelpW;
constexpr float kRowX = kMenuX + 28, kRowW = kMenuW - 56, kRowY = kPanelY + 36, kRowH = 88, kRowStep = 104;
// Category / binding pages: a wide panel left of the help panel.
constexpr float kPageW = kHelpX - 48 - kMargin;
constexpr float kInX = kMargin + 24, kInW = kPageW - 48;
constexpr float kTabY = kPanelY + 20, kTabH = 52;
constexpr float kListY = kTabY + kTabH + 18, kListBottom = kPanelY + kPanelH - 20;
constexpr float kBarW = 8;
constexpr float kLineW = kInW - kBarW - 16;
constexpr float kLineH = 58, kLineStep = 64, kHeadH = 52;
constexpr float kValueW = 440, kValuePad = 24, kArrowW = 44;
constexpr float kChipW = 190, kChipGap = 12;
constexpr float kFooterY = 1012, kHintH = 44, kHintGlyph = 40, kHintGap = 36;
// Cards.
constexpr float kCardW = 780, kCardPad = 44, kCardHead = 104, kCardLine = 30;
constexpr float kCardRowH = 68, kCardRowStep = 78;
constexpr float kChoiceH = 52, kChoiceStep = 56;
constexpr int kChoiceVisible = 8;
constexpr float kCaptureBoxH = 92;
constexpr int kCardBase = 1000, kChoiceBase = 2000;

const char* const kSlotNames[] = {"Primary", "Secondary", "Extra"};

std::string fmtNum(float v, bool integer) {
    char b[48];
    if (integer)
        std::snprintf(b, sizeof b, "%d", static_cast<int>(std::lround(v)));
    else
        std::snprintf(b, sizeof b, "%.6g", static_cast<double>(v));
    return b;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : sep) + x;
    return s;
}

// Slider value <-> track fraction (log scale when the row asks for it).
float toFraction(const SettingRow& r, float v) {
    if (r.max <= r.min)
        return 0;
    if (r.logarithmic && r.min > 0) {
        const float a = std::log(r.min), b = std::log(r.max);
        return std::clamp((std::log(std::max(v, r.min)) - a) / (b - a), 0.0f, 1.0f);
    }
    return std::clamp((v - r.min) / (r.max - r.min), 0.0f, 1.0f);
}
float fromFraction(const SettingRow& r, float f) {
    f = std::clamp(f, 0.0f, 1.0f);
    float v = r.logarithmic && r.min > 0 ? std::exp(std::log(r.min) + f * (std::log(r.max) - std::log(r.min)))
                                         : r.min + f * (r.max - r.min);
    if (r.integer)
        v = std::round(v);
    else if (r.step > 0 && !r.logarithmic)
        v = r.min + std::round((v - r.min) / r.step) * r.step;
    return std::clamp(v, r.min, r.max);
}

} // namespace

const char* settingsActionName(SettingsActionKind k) {
    switch (k) {
        case SettingsActionKind::Change:
            return "Change";
        case SettingsActionKind::Bind:
            return "Bind";
        case SettingsActionKind::ClearBinding:
            return "ClearBinding";
        case SettingsActionKind::ResetBindings:
            return "ResetBindings";
        case SettingsActionKind::ResetPage:
            return "ResetPage";
        case SettingsActionKind::Restart:
            return "Restart";
    }
    return "?";
}

// ---- layout ----

Rect SettingsPages::landingRow(int i) {
    return {kRowX, kRowY + static_cast<float>(i) * kRowStep, kRowW, kRowH};
}
Rect SettingsPages::pagePanel() {
    return {kMargin, kPanelY, kPageW, kPanelH};
}
Rect SettingsPages::helpPanel() {
    return {kHelpX, kPanelY, kHelpW, kPanelH};
}
Rect SettingsPages::tabRect(int i, int n) {
    const float gap = 8, w = (kInW - gap * static_cast<float>(std::max(0, n - 1))) / static_cast<float>(std::max(1, n));
    return {kInX + static_cast<float>(i) * (w + gap), kTabY, w, kTabH};
}
Rect SettingsPages::listArea() {
    return {kInX, kListY, kInW, kListBottom - kListY};
}

// ---- state ----

SettingsPages::SettingsPages(SettingsPagesConfig cfg) : cfg_(std::move(cfg)) {}

void SettingsPages::open(const std::string& pageId) {
    open_ = true;
    view_ = SettingsView::Landing;
    card_ = SettingsCard::None;
    tab_ = 0;
    focus_ = hover_ = pageFocus_ = {};
    scroll_ = 0;
    drag_.reset();
    held_.clear();
    restartReasons_.clear();
    restartNeeded_.clear();
    restartPending_ = false;
    problem_.clear();
    pendingOpen_ = pageId;
    returnOnBack_ = !pageId.empty();
}

void SettingsPages::close() {
    open_ = false;
    card_ = SettingsCard::None;
    drag_.reset();
    targets_.clear();
    hints_.clear();
}

int SettingsPages::tabCount() const {
    return schema_ ? static_cast<int>(schema_->pages.size() + schema_->bindings.size()) : 0;
}

const SettingPage* SettingsPages::currentPage() const {
    if (!schema_ || view_ != SettingsView::Page || tab_ < 0 || tab_ >= static_cast<int>(schema_->pages.size()))
        return nullptr;
    return &schema_->pages[static_cast<size_t>(tab_)];
}

const BindingLayerView* SettingsPages::currentLayer() const {
    if (!schema_ || view_ != SettingsView::Bindings)
        return nullptr;
    const int i = tab_ - static_cast<int>(schema_->pages.size());
    if (i < 0 || i >= static_cast<int>(schema_->bindings.size()))
        return nullptr;
    return &schema_->bindings[static_cast<size_t>(i)];
}

int SettingsPages::pageIndex() const {
    if (!schema_)
        return -1;
    return view_ == SettingsView::Bindings ? tab_ - static_cast<int>(schema_->pages.size()) : tab_;
}

const SettingRow* SettingsPages::rowAt(int section, int row) const {
    const SettingPage* p = currentPage();
    if (!p || section < 0 || section >= static_cast<int>(p->sections.size()))
        return nullptr;
    const SettingSection& s = p->sections[static_cast<size_t>(section)];
    if (row < 0 || row >= static_cast<int>(s.rows.size()))
        return nullptr;
    return &s.rows[static_cast<size_t>(row)];
}

const BindingAction* SettingsPages::actionAt(int group, int action) const {
    const BindingLayerView* l = currentLayer();
    if (!l || group < 0 || group >= static_cast<int>(l->groups.size()))
        return nullptr;
    const BindingGroup& g = l->groups[static_cast<size_t>(group)];
    if (action < 0 || action >= static_cast<int>(g.actions.size()))
        return nullptr;
    return &g.actions[static_cast<size_t>(action)];
}

const SettingsPages::Item* SettingsPages::itemFor(const TargetId& id) const {
    if (id.kind != TargetKind::MenuRow)
        return nullptr;
    if (id.index >= kChoiceBase) {
        for (const Item& it : cardItems_)
            if (it.kind == Item::Choice && it.a == id.index - kChoiceBase)
                return &it;
        return nullptr;
    }
    if (id.index >= kCardBase) {
        const int i = id.index - kCardBase;
        for (const Item& it : cardItems_)
            if (it.kind == Item::CardRow && it.a == i)
                return &it;
        return nullptr;
    }
    if (id.index < 0 || id.index >= static_cast<int>(items_.size()))
        return nullptr;
    return &items_[static_cast<size_t>(id.index)];
}

const SettingRow* SettingsPages::focusedRow() const {
    const Item* it = card_ == SettingsCard::None ? itemFor(focus_) : itemFor(pageFocus_);
    if (!it || it->kind != Item::Row)
        return nullptr;
    return rowAt(it->a, it->b);
}

std::string SettingsPages::focusedKey() const {
    const SettingRow* r = focusedRow();
    return r ? r->key : std::string();
}

std::string SettingsPages::shownValue(const SettingRow& r) const {
    if (drag_ && drag_->key == r.key)
        return fmtNum(drag_->value, r.integer);
    for (const Held& h : held_)
        if (h.key == r.key)
            return h.value;
    return r.currentValue;
}

bool SettingsPages::shownOn(const SettingRow& r) const {
    return shownValue(r) == r.onValue;
}

int SettingsPages::shownChoice(const SettingRow& r) const {
    const std::string v = shownValue(r);
    if (v == r.currentValue)
        return r.current;
    for (size_t i = 0; i < r.choices.size(); ++i)
        if (r.choices[i].value == v)
            return static_cast<int>(i);
    return r.current;
}

float SettingsPages::shownSlider(const SettingRow& r) const {
    if (drag_ && drag_->key == r.key)
        return drag_->value;
    const std::string v = shownValue(r);
    if (v == r.currentValue)
        return r.value;
    char* e = nullptr;
    const float f = std::strtof(v.c_str(), &e);
    return e && e != v.c_str() ? f : r.value;
}

// The rows of a page that the modern pages show (OverlayOnly rows are skipped).
void SettingsPages::build() {
    items_.clear();
    headings_.clear();
    notes_.clear();
    contentH_ = 0;
    if (!schema_)
        return;
    const int np = static_cast<int>(schema_->pages.size());
    if (view_ == SettingsView::Landing) {
        int i = 0;
        for (; i < np; ++i)
            items_.push_back({Item::Category, i, 0, 0, landingRow(i)});
        if (!schema_->bindings.empty())
            items_.push_back({Item::BindLayer, 0, 0, 0, landingRow(i++)});
        items_.push_back({Item::Back, 0, 0, 0, landingRow(i)});
        return;
    }
    const int nt = tabCount();
    for (int t = 0; t < nt; ++t)
        items_.push_back({Item::Tab, t, 0, 0, tabRect(t, nt)});
    float y = 0;
    const detail::Paint p(lf_, cfg_.screen);
    if (const SettingPage* pg = currentPage()) {
        for (size_t s = 0; s < pg->sections.size(); ++s) {
            const SettingSection& sec = pg->sections[s];
            bool any = false;
            for (const SettingRow& r : sec.rows)
                any = any || r.shownIn(true);
            if (!any)
                continue;
            headings_.push_back({sec.title, y});
            y += kHeadH;
            for (size_t i = 0; i < sec.rows.size(); ++i) {
                const SettingRow& r = sec.rows[i];
                if (!r.shownIn(true))
                    continue;
                Item it{Item::Row, static_cast<int>(s), static_cast<int>(i), 0, {kInX, kListY + y, kLineW, kLineH}};
                it.enabled = r.enabled;
                it.navigable = r.kind != SettingKind::Info;
                it.scrolls = true;
                items_.push_back(it);
                y += kLineStep;
            }
            y += 8;
        }
        if (!pg->note.empty()) {
            const std::vector<std::string> lines = p.wrap(pg->note, kLineW - 24, FontRole::Small);
            for (const std::string& l : lines) {
                notes_.push_back({l, y});
                y += 28;
            }
            y += 8;
        }
    } else if (const BindingLayerView* l = currentLayer()) {
        if (!l->description.empty()) {
            for (const std::string& line : p.wrap(l->description, kLineW - 24, FontRole::Small)) {
                notes_.push_back({line, y});
                y += 28;
            }
            y += 10;
        }
        const int slots = std::max(1, std::min(3, schema_->bindingSlots));
        for (size_t g = 0; g < l->groups.size(); ++g) {
            headings_.push_back({l->groups[g].title, y});
            y += kHeadH;
            for (size_t a = 0; a < l->groups[g].actions.size(); ++a) {
                const float right = kInX + kLineW - 16;
                for (int s = 0; s < slots; ++s) {
                    const float x =
                        right - static_cast<float>(slots - s) * kChipW - static_cast<float>(slots - 1 - s) * kChipGap;
                    Item it{Item::Chip,
                            static_cast<int>(g),
                            static_cast<int>(a),
                            s,
                            {x, kListY + y + 7, kChipW, kLineH - 14}};
                    it.scrolls = true;
                    items_.push_back(it);
                }
                y += kLineStep;
            }
            y += 8;
        }
    }
    contentH_ = y;
}

float SettingsPages::cardHeight(int bodyLines, int rows, bool box) const {
    return kCardHead + static_cast<float>(bodyLines) * kCardLine + 18 + (box ? kCaptureBoxH + 18 : 0) +
           static_cast<float>(rows) * kCardRowStep - (kCardRowStep - kCardRowH) + kCardPad;
}

std::vector<std::string> SettingsPages::cardRows() const {
    switch (card_) {
        case SettingsCard::KeepDisplay:
            return {"Keep", "Revert"};
        case SettingsCard::Restart:
            return {"Restart Now", "Later"};
        case SettingsCard::Problem:
            return {"OK"};
        case SettingsCard::Defaults:
            return {"Restore Defaults", "Cancel"};
        case SettingsCard::Capture:
            return {"Bind Esc", "Clear", "Cancel"};
        case SettingsCard::Conflict:
            return {"Replace", "Use for Both", "Cancel"};
        default:
            return {};
    }
}

std::vector<std::string> SettingsPages::cardBody() const {
    const detail::Paint p(lf_, cfg_.screen);
    std::string body;
    switch (card_) {
        case SettingsCard::KeepDisplay: {
            const int left = std::max(0, static_cast<int>(std::ceil(keepDeadline_ - now_)));
            body = "The display settings were applied. Reverting in " + std::to_string(left) +
                   (left == 1 ? " second." : " seconds.");
            break;
        }
        case SettingsCard::Restart:
            body = "These settings take effect after a restart: " + join(restartReasons_, ", ") + ".";
            break;
        case SettingsCard::Problem:
            body = problem_;
            break;
        case SettingsCard::Defaults:
            if (view_ == SettingsView::Bindings) {
                const BindingLayerView* l = currentLayer();
                body = "Every " + std::string(l ? l->title : "key") + " binding returns to its default.";
            } else {
                const SettingPage* pg = currentPage();
                body =
                    "Every setting on the " + std::string(pg ? pg->title : "") + " page returns to its default value.";
            }
            break;
        case SettingsCard::Capture:
            body = "Press a key, scroll the wheel, or click inside the box. Esc cancels.";
            break;
        case SettingsCard::Conflict: {
            const BindingLayerView* l = currentLayer();
            std::vector<std::string> names;
            if (l)
                for (const std::string& a : l->actionsOn(cap_.pendingSource))
                    if (a != cap_.action)
                        names.push_back(l->actionLabel(a));
            body = "Also used for: " + join(names, ", ") + ".";
            break;
        }
        default:
            break;
    }
    std::vector<std::string> lines =
        body.empty() ? std::vector<std::string>{} : p.wrap(body, kCardW - 2 * kCardPad, FontRole::Body);
    if (lines.size() > 4) {
        lines.resize(4);
        lines.back() += "...";
    }
    return lines;
}

Rect SettingsPages::cardPanel() const {
    if (card_ == SettingsCard::Choices) {
        const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
        const int n = r ? std::max(1, std::min(kChoiceVisible, static_cast<int>(r->choices.size()))) : 1;
        const float h = kCardHead + static_cast<float>(n) * kChoiceStep - (kChoiceStep - kChoiceH) + kCardPad;
        return {(kDesignW - kCardW) * 0.5f, (kDesignH - h) * 0.5f, kCardW, h};
    }
    const float h = cardHeight(static_cast<int>(cardBody().size()), static_cast<int>(cardRows().size()),
                               card_ == SettingsCard::Capture);
    return {(kDesignW - kCardW) * 0.5f, (kDesignH - h) * 0.5f, kCardW, h};
}

Rect SettingsPages::captureBox() const {
    const Rect c = cardPanel();
    const float y = c.y + kCardHead + static_cast<float>(cardBody().size()) * kCardLine + 18;
    return {c.x + kCardPad, y, c.w - 2 * kCardPad, kCaptureBoxH};
}

void SettingsPages::buildCard() {
    cardItems_.clear();
    if (card_ == SettingsCard::None)
        return;
    const Rect c = cardPanel();
    if (card_ == SettingsCard::Choices) {
        const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
        if (!r)
            return;
        const int n = static_cast<int>(r->choices.size());
        choiceScroll_ = std::clamp(choiceScroll_, 0, std::max(0, n - kChoiceVisible));
        for (int i = choiceScroll_; i < n && i < choiceScroll_ + kChoiceVisible; ++i) {
            Item it{Item::Choice,
                    i,
                    0,
                    0,
                    {c.x + kCardPad, c.y + kCardHead + static_cast<float>(i - choiceScroll_) * kChoiceStep,
                     c.w - 2 * kCardPad, kChoiceH}};
            it.enabled = r->choices[static_cast<size_t>(i)].enabled;
            cardItems_.push_back(it);
        }
        return;
    }
    const std::vector<std::string> rows = cardRows();
    float y = c.y + kCardHead + static_cast<float>(cardBody().size()) * kCardLine + 18 +
              (card_ == SettingsCard::Capture ? kCaptureBoxH + 18 : 0);
    for (size_t i = 0; i < rows.size(); ++i, y += kCardRowStep) {
        Item it{Item::CardRow, static_cast<int>(i), 0, 0, {c.x + kCardPad, y, c.w - 2 * kCardPad, kCardRowH}};
        if (card_ == SettingsCard::Capture && i == 1) {
            const BindingAction* a = actionAt(cap_.group, cap_.index);
            it.enabled = a && cap_.slot < static_cast<int>(a->slots.size());
        }
        cardItems_.push_back(it);
    }
}

Rect SettingsPages::itemPx(const Item& it) const {
    Rect r = it.rect;
    if (it.scrolls)
        r.y -= scroll_;
    return lf_.map(r);
}

void SettingsPages::buildTargets() {
    targets_.clear();
    if (card_ != SettingsCard::None) {
        for (const Item& it : cardItems_) {
            const int id = it.kind == Item::Choice ? kChoiceBase + it.a : kCardBase + it.a;
            targets_.push_back({{TargetKind::MenuRow, id}, lf_.map(it.rect), it.enabled, it.enabled});
        }
        return;
    }
    // Rows scrolled out of the list keep their real place (directional picking reaches them); pointerOver() clips
    // hover and clicks to the list.
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        targets_.push_back({{TargetKind::MenuRow, static_cast<int>(i)}, itemPx(it), it.navigable, it.enabled});
    }
}

bool SettingsPages::pointerOver(const HitTarget& t, float x, float y) const {
    if (!t.rect.contains(x, y))
        return false;
    if (t.id.kind == TargetKind::MenuRow && card_ == SettingsCard::None) {
        const Item* it = itemFor(t.id);
        if (it && it->scrolls && !lf_.map(listArea()).contains(x, y))
            return false;
    }
    return true;
}

void SettingsPages::ensureFocus() {
    if (card_ != SettingsCard::None) {
        if (const Item* it = itemFor(focus_); it && it->enabled)
            return;
        for (const Item& it : cardItems_)
            if (it.enabled) {
                focus_ = {TargetKind::MenuRow, it.kind == Item::Choice ? kChoiceBase + it.a : kCardBase + it.a};
                return;
            }
        return;
    }
    if (const Item* it = itemFor(focus_); it && it->navigable)
        return;
    focus_ = {};
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].navigable && items_[i].kind != Item::Tab) {
            focus_ = {TargetKind::MenuRow, static_cast<int>(i)};
            scrollToFocus_ = true;
            return;
        }
}

void SettingsPages::keepFocusVisible() {
    const float maxScroll = std::max(0.0f, contentH_ - (kListBottom - kListY));
    if (scrollToFocus_ && card_ == SettingsCard::None) {
        if (const Item* it = itemFor(focus_); it && it->scrolls) {
            float top = it->rect.y - kListY, bottom = top + it->rect.h;
            // Keep the section heading above the first row of a section in view.
            for (const Heading& h : headings_)
                if (h.y + kHeadH >= top - 1 && h.y < top)
                    top = h.y;
            if (top < scroll_)
                scroll_ = top;
            if (bottom > scroll_ + (kListBottom - kListY))
                scroll_ = bottom - (kListBottom - kListY) + 6;
        } else if (it && it->kind == Item::Tab) {
            // tabs do not scroll
        }
    }
    scrollToFocus_ = false;
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
}

void SettingsPages::buildHints() {
    hints_.clear();
    switch (card_) {
        case SettingsCard::None:
            break;
        case SettingsCard::Capture:
            hints_.push_back({Prompt::Back, "Cancel"});
            break;
        case SettingsCard::KeepDisplay:
            hints_.push_back({Prompt::Confirm, "Select"});
            hints_.push_back({Prompt::Back, "Revert"});
            break;
        default:
            hints_.push_back({Prompt::Confirm, "Select"});
            hints_.push_back({Prompt::Back, "Cancel"});
            break;
    }
    if (card_ == SettingsCard::None) {
        const Item* it = itemFor(focus_);
        if (view_ == SettingsView::Landing) {
            hints_.push_back({Prompt::Confirm, "Select"});
        } else if (it && it->kind == Item::Row) {
            const SettingRow* r = rowAt(it->a, it->b);
            const bool en = r && r->enabled;
            if (r && r->kind == SettingKind::Slider)
                hints_.push_back({Prompt::Adjust, "Adjust", en});
            else if (r && r->kind == SettingKind::Choice) {
                hints_.push_back({Prompt::Adjust, "Change", en});
                hints_.push_back({Prompt::Confirm, "List", en});
            } else if (r && r->kind == SettingKind::Toggle)
                hints_.push_back({Prompt::Confirm, "Toggle", en});
        } else if (it && it->kind == Item::Chip) {
            const BindingAction* a = actionAt(it->a, it->b);
            hints_.push_back({Prompt::Confirm, "Rebind"});
            hints_.push_back({Prompt::Secondary, "Clear", a && it->c < static_cast<int>(a->slots.size())});
        } else if (it && it->kind == Item::Tab) {
            hints_.push_back({Prompt::Adjust, "Page"});
        }
        if (view_ == SettingsView::Page)
            hints_.push_back({Prompt::Tertiary, "Defaults"});
        else if (view_ == SettingsView::Bindings)
            hints_.push_back({Prompt::Tertiary, "Defaults"});
        if (view_ != SettingsView::Landing)
            hints_.push_back({Prompt::PageNext, "Next Page"});
        hints_.push_back({Prompt::Back, "Back"});
    }
    const detail::Paint p(lf_, cfg_.screen);
    const std::vector<Rect> rs =
        p.layoutHints(hints_, kDesignW - kMargin, false, kFooterY, kHintH, kHintGlyph, kHintGap, FontRole::Body);
    for (size_t i = 0; i < hints_.size(); ++i)
        targets_.push_back(
            {{TargetKind::Hint, static_cast<int>(hints_[i].prompt)}, lf_.map(rs[i]), false, hints_[i].enabled});
}

Rect SettingsPages::valueRect(const std::string& key) const {
    if (view_ != SettingsView::Page)
        return {};
    for (const Item& it : items_) {
        if (it.kind != Item::Row)
            continue;
        const SettingRow* r = rowAt(it.a, it.b);
        if (!r || r->key != key)
            continue;
        Rect d = it.rect;
        d.y -= scroll_;
        const Rect area{d.right() - kValuePad - kValueW, d.y, kValueW, d.h};
        if (r->kind == SettingKind::Slider)
            return lf_.map(sliderTrack(area));
        return lf_.map(area);
    }
    return {};
}

Rect SettingsPages::sliderTrack(const Rect& area) {
    return {area.x, area.cy() - 7, area.w - 200, 14}; // the value text right of it (up to "0.0020   (x1.00)")
}

// ---- input ----

void SettingsPages::enterPage(int tab, SettingsFrameOutput& out) {
    if (!schema_ || tab < 0 || tab >= tabCount())
        return;
    const int np = static_cast<int>(schema_->pages.size());
    view_ = tab < np ? SettingsView::Page : SettingsView::Bindings;
    tab_ = tab;
    scroll_ = 0;
    drag_.reset();
    focus_ = {};
    build();
    ensureFocus();
    out.sound = UiSound::Confirm;
}

void SettingsPages::switchTab(int dir, SettingsFrameOutput& out) {
    const int n = tabCount();
    if (n <= 1 || view_ == SettingsView::Landing)
        return;
    const bool onTab = [&] {
        const Item* it = itemFor(focus_);
        return it && it->kind == Item::Tab;
    }();
    enterPage((tab_ + dir + n) % n, out);
    if (onTab) // stay on the tab strip while stepping through the tabs
        focusCurrentTab();
}

void SettingsPages::focusCurrentTab() {
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].kind == Item::Tab && items_[i].a == tab_)
            focus_ = {TargetKind::MenuRow, static_cast<int>(i)};
}

void SettingsPages::change(const SettingRow& r, const std::string& value, SettingsFrameOutput& out, bool final,
                           bool revert) {
    if (!r.enabled)
        return;
    const std::string before = shownValue(r);
    if (value == before && final && !drag_)
        return;
    if (value != before || final) {
        SettingsAction a;
        a.kind = SettingsActionKind::Change;
        a.key = r.key;
        a.value = value;
        a.final = final;
        a.revert = revert;
        // One change per key and frame (a drag and a key in the same frame keep the last).
        auto it = std::find_if(out.actions.begin(), out.actions.end(), [&](const SettingsAction& x) {
            return x.kind == SettingsActionKind::Change && x.key == r.key;
        });
        if (it != out.actions.end())
            *it = a;
        else
            out.actions.push_back(a);
    }
    held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Held& h) { return h.key == r.key; }), held_.end());
    if (value != r.currentValue)
        held_.push_back({r.key, value, 0});
    if (out.sound == UiSound::None)
        out.sound = r.kind == SettingKind::Slider ? UiSound::Slider : UiSound::Toggle;
    if (r.confirm && final && !revert && card_ == SettingsCard::None) {
        keepKey_ = r.key;
        keepPrevious_ = r.currentValue;
        keepDeadline_ = now_ + cfg_.revertSeconds;
        openCard(SettingsCard::KeepDisplay);
    }
}

void SettingsPages::adjust(const SettingRow& r, int dir, SettingsFrameOutput& out) {
    if (!r.enabled || dir == 0)
        return;
    switch (r.kind) {
        case SettingKind::Toggle:
            change(r, shownOn(r) ? r.offValue : r.onValue, out);
            break;
        case SettingKind::Choice: {
            const int n = static_cast<int>(r.choices.size());
            int i = shownChoice(r);
            if (i < 0)
                i = dir > 0 ? -1 : n;
            for (int j = i + dir; j >= 0 && j < n; j += dir)
                if (r.choices[static_cast<size_t>(j)].enabled) {
                    change(r, r.choices[static_cast<size_t>(j)].value, out);
                    break;
                }
            break;
        }
        case SettingKind::Slider: {
            const float cur = shownSlider(r);
            float v;
            if (r.logarithmic || r.step <= 0) {
                const float steps = r.logarithmic ? 40.0f : 20.0f;
                v = fromFraction(r, toFraction(r, cur) + static_cast<float>(dir) / steps);
                if (!r.integer && !r.logarithmic && r.step <= 0) {
                    // keep tidy numbers on an unstepped linear slider
                    const float unit = (r.max - r.min) / 20.0f;
                    v = std::clamp(r.min + std::round((v - r.min) / unit) * unit, r.min, r.max);
                }
            } else {
                v = std::clamp(cur + static_cast<float>(dir) * r.step, r.min, r.max);
                if (r.integer)
                    v = std::round(v);
            }
            change(r, fmtNum(v, r.integer), out);
            break;
        }
        default:
            break;
    }
}

void SettingsPages::sliderFromPointer(const SettingRow& r, float x, SettingsFrameOutput& out, bool final) {
    const Rect t = valueRect(r.key);
    if (t.empty())
        return;
    const float v = fromFraction(r, (x - t.x) / t.w);
    change(r, fmtNum(v, r.integer), out, final);
    if (!final)
        drag_ = Drag{r.key, v};
}

void SettingsPages::openCard(SettingsCard c) {
    if (card_ == SettingsCard::None)
        pageFocus_ = focus_;
    card_ = c;
    focus_ = {};
    buildCard();
    int f = 0;
    if (c == SettingsCard::Restart || c == SettingsCard::Defaults)
        f = 1; // "Later" / "Cancel": nothing drastic on a stray Confirm
    else if (c == SettingsCard::Capture)
        f = 2;
    if (c == SettingsCard::Choices) {
        const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
        const int cur = r ? std::max(0, shownChoice(*r)) : 0;
        choiceScroll_ = std::max(0, cur - kChoiceVisible / 2);
        buildCard();
        focus_ = {TargetKind::MenuRow, kChoiceBase + cur};
    } else {
        focus_ = {TargetKind::MenuRow, kCardBase + f};
    }
    ensureFocus();
}

void SettingsPages::closeCard() {
    card_ = SettingsCard::None;
    cardItems_.clear();
    focus_ = pageFocus_;
    // Due cards: a problem first, then the restart question.
    if (!problem_.empty()) {
        openCard(SettingsCard::Problem);
        return;
    }
    if (restartPending_ && !restartReasons_.empty()) {
        restartPending_ = false;
        openCard(SettingsCard::Restart);
    }
}

void SettingsPages::cardBack(SettingsFrameOutput& out) {
    if (card_ == SettingsCard::KeepDisplay) {
        revertKeep(out);
        return;
    }
    if (card_ == SettingsCard::Restart)
        restartReasons_.clear();
    if (card_ == SettingsCard::Problem)
        problem_.clear();
    closeCard();
    out.sound = UiSound::Back;
}

void SettingsPages::revertKeep(SettingsFrameOutput& out) {
    if (schema_)
        if (const SettingRow* r = schema_->find(keepKey_)) {
            card_ = SettingsCard::None; // change() must not reopen the card
            change(*r, keepPrevious_, out, true, true);
        }
    restartPending_ = false; // the overlay only asks for the restart after Keep
    closeCard();
}

void SettingsPages::back(SettingsFrameOutput& out) {
    drag_.reset();
    if (view_ == SettingsView::Landing || returnOnBack_) {
        out.wantsClose = true;
        return;
    }
    const int np = schema_ ? static_cast<int>(schema_->pages.size()) : 0;
    const int landing = view_ == SettingsView::Bindings ? np : tab_;
    view_ = SettingsView::Landing;
    build();
    focus_ = {TargetKind::MenuRow, std::clamp(landing, 0, static_cast<int>(items_.size()) - 1)};
    out.sound = UiSound::Back;
}

void SettingsPages::startCapture(const Item& chip, bool fromPointer) {
    cap_ = Capture();
    cap_.group = chip.a;
    cap_.index = chip.b;
    cap_.slot = chip.c;
    if (const BindingAction* a = actionAt(chip.a, chip.b))
        cap_.action = a->id;
    cap_.started = now_;
    cap_.armed = !fromPointer;
    openCard(SettingsCard::Capture);
}

void SettingsPages::finishCapture(const std::string& source, SettingsFrameOutput& out) {
    const BindingLayerView* l = currentLayer();
    if (!l || source.empty())
        return;
    std::vector<std::string> others = l->actionsOn(source);
    others.erase(std::remove(others.begin(), others.end(), cap_.action), others.end());
    if (!others.empty()) {
        cap_.pendingSource = source;
        openCard(SettingsCard::Conflict); // card to card: the page focus stays saved
        return;
    }
    emitBind(source, false, out);
    closeCard();
}

void SettingsPages::emitBind(const std::string& source, bool replace, SettingsFrameOutput& out) {
    const BindingLayerView* l = currentLayer();
    if (!l)
        return;
    SettingsAction a;
    a.kind = SettingsActionKind::Bind;
    a.layer = l->id;
    a.action = cap_.action;
    a.slot = cap_.slot;
    a.source = source;
    a.replace = replace;
    out.actions.push_back(a);
    out.sound = UiSound::Confirm;
}

void SettingsPages::activateCard(const Item& it, SettingsFrameOutput& out) {
    if (!it.enabled)
        return;
    if (it.kind == Item::Choice) {
        const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
        closeCard();
        if (r && it.a < static_cast<int>(r->choices.size()))
            change(*r, r->choices[static_cast<size_t>(it.a)].value, out);
        return;
    }
    const int i = it.a;
    switch (card_) {
        case SettingsCard::KeepDisplay:
            if (i == 0) {
                closeCard(); // kept; a pending restart question follows
            } else
                revertKeep(out);
            break;
        case SettingsCard::Restart:
            if (i == 0) {
                SettingsAction a;
                a.kind = SettingsActionKind::Restart;
                a.value = join(restartReasons_, "\n");
                out.actions.push_back(a);
            }
            restartReasons_.clear();
            closeCard();
            break;
        case SettingsCard::Problem:
            problem_.clear();
            closeCard();
            break;
        case SettingsCard::Defaults:
            if (i == 0) {
                SettingsAction a;
                if (const BindingLayerView* l = currentLayer()) {
                    a.kind = SettingsActionKind::ResetBindings;
                    a.layer = l->id;
                } else if (const SettingPage* pg = currentPage()) {
                    a.kind = SettingsActionKind::ResetPage;
                    a.page = pg->id;
                }
                held_.clear();
                out.actions.push_back(a);
            }
            closeCard();
            break;
        case SettingsCard::Capture:
            if (i == 0)
                finishCapture("ESC", out);
            else if (i == 1) {
                SettingsAction a;
                a.kind = SettingsActionKind::ClearBinding;
                if (const BindingLayerView* l = currentLayer())
                    a.layer = l->id;
                a.action = cap_.action;
                a.slot = cap_.slot;
                out.actions.push_back(a);
                closeCard();
            } else
                closeCard();
            break;
        case SettingsCard::Conflict:
            if (i < 2)
                emitBind(cap_.pendingSource, i == 0, out);
            closeCard();
            break;
        default:
            closeCard();
            break;
    }
    if (out.sound == UiSound::None)
        out.sound = UiSound::Confirm;
}

void SettingsPages::activate(const Item& it, SettingsFrameOutput& out, bool fromPointer, float px) {
    switch (it.kind) {
        case Item::Category:
            enterPage(it.a, out);
            return;
        case Item::BindLayer:
            enterPage(schema_ ? static_cast<int>(schema_->pages.size()) + it.a : 0, out);
            return;
        case Item::Back:
            back(out);
            return;
        case Item::Tab:
            // Focus stays on the chosen tab: enterPage() would put it on the first row of the new page, which then
            // showed as focused under a clicked tab until the pointer moved.
            if (it.a != tab_) {
                enterPage(it.a, out);
                focusCurrentTab();
            }
            return;
        case Item::Chip:
            startCapture(it, fromPointer);
            return;
        case Item::Row:
            break;
        default:
            return;
    }
    const SettingRow* r = rowAt(it.a, it.b);
    if (!r || !r->enabled)
        return;
    switch (r->kind) {
        case SettingKind::Toggle:
            change(*r, shownOn(*r) ? r->offValue : r->onValue, out);
            break;
        case SettingKind::Choice: {
            if (fromPointer) {
                const Rect area = valueRect(r->key);
                const float aw = lf_.px(kArrowW);
                if (px < area.x)
                    break; // the label: focus only
                if (px < area.x + aw) {
                    adjust(*r, -1, out);
                    break;
                }
                if (px >= area.right() - aw) {
                    adjust(*r, 1, out);
                    break;
                }
            }
            if (r->choices.empty() && r->emptyText.empty())
                break;
            choiceKey_ = r->key;
            openCard(SettingsCard::Choices);
            out.sound = UiSound::Confirm;
            break;
        }
        case SettingKind::Slider:
            if (fromPointer) {
                const Rect t = valueRect(r->key);
                const float pad = lf_.px(18);
                if (px >= t.x - pad && px < t.right() + pad)
                    sliderFromPointer(*r, px, out, false);
            }
            break;
        default:
            break;
    }
}

void SettingsPages::press(Prompt pr, SettingsFrameOutput& out) {
    const Item* it = itemFor(focus_);
    switch (pr) {
        case Prompt::Back:
            if (card_ != SettingsCard::None)
                cardBack(out);
            else
                back(out);
            break;
        case Prompt::Confirm:
            if (card_ != SettingsCard::None) {
                if (it)
                    activateCard(*it, out);
            } else if (it)
                activate(*it, out, false, 0);
            break;
        case Prompt::Adjust:
            if (it && it->kind == Item::Row) {
                if (const SettingRow* r = rowAt(it->a, it->b))
                    adjust(*r, 1, out);
            } else if (it && it->kind == Item::Tab)
                switchTab(1, out);
            break;
        case Prompt::Secondary:
            if (it && it->kind == Item::Chip) {
                const BindingAction* a = actionAt(it->a, it->b);
                const BindingLayerView* l = currentLayer();
                if (a && l && it->c < static_cast<int>(a->slots.size())) {
                    SettingsAction x;
                    x.kind = SettingsActionKind::ClearBinding;
                    x.layer = l->id;
                    x.action = a->id;
                    x.slot = it->c;
                    out.actions.push_back(x);
                    out.sound = UiSound::Confirm;
                }
            }
            break;
        case Prompt::Tertiary:
            if (view_ != SettingsView::Landing)
                openCard(SettingsCard::Defaults);
            break;
        case Prompt::PageNext:
            switchTab(1, out);
            break;
        case Prompt::PagePrev:
            switchTab(-1, out);
            break;
        default:
            break;
    }
}

void SettingsPages::moveFocus(int dx, int dy) {
    const HitTarget* cur = nullptr;
    for (const HitTarget& t : targets_)
        if (t.id == focus_)
            cur = &t;
    if (!cur) {
        ensureFocus();
        return;
    }
    std::vector<HitTarget> nav;
    for (const HitTarget& t : targets_)
        if (t.id.kind == TargetKind::MenuRow)
            nav.push_back(t);
    const int next = InventoryScreen::pickDirectional(nav, cur->rect, dx, dy);
    if (next >= 0) {
        focus_ = nav[static_cast<size_t>(next)].id;
        scrollToFocus_ = true;
    }
}

void SettingsPages::applyCardInput(const SettingsInput& in, SettingsFrameOutput& out) {
    const UiInput& ui = in.ui;
    hover_ = {};
    if (card_ == SettingsCard::KeepDisplay && now_ >= keepDeadline_) {
        revertKeep(out);
        return;
    }
    if (card_ == SettingsCard::Capture) {
        if (!ui.leftDown && !ui.leftClick)
            cap_.armed = true;
        if (!in.capturedKey.empty()) {
            finishCapture(in.capturedKey, out);
            return;
        }
        const Rect box = lf_.map(captureBox());
        const bool inBox = ui.pointerValid && box.contains(ui.pointerX, ui.pointerY);
        if (inBox && cap_.armed && (ui.leftClick || ui.rightClick)) {
            finishCapture(ui.leftClick ? "MOUSE1" : "MOUSE2", out);
            return;
        }
        if (inBox && ui.wheel != 0) {
            finishCapture(ui.wheel > 0 ? "WHEELUP" : "WHEELDOWN", out);
            return;
        }
        if (now_ - cap_.started > cfg_.captureSeconds) {
            closeCard(); // gamepad-only users: do not get stuck
            return;
        }
    }
    if (ui.pointerValid) {
        for (size_t i = targets_.size(); i-- > 0;)
            if (targets_[i].rect.contains(ui.pointerX, ui.pointerY)) {
                hover_ = targets_[i].id;
                if (ui.pointerMoved && targets_[i].navigable)
                    focus_ = targets_[i].id;
                break;
            }
    }
    if (ui.rightClick) {
        cardBack(out);
        return;
    }
    if (ui.leftClick && hover_.kind != TargetKind::None) {
        if (hover_.kind == TargetKind::Hint)
            press(static_cast<Prompt>(hover_.index), out);
        else if (const Item* it = itemFor(hover_)) {
            focus_ = hover_;
            activateCard(*it, out);
        }
        return;
    }
    if (card_ == SettingsCard::Choices && ui.wheel != 0) {
        choiceScroll_ -= ui.wheel > 0 ? 1 : -1;
        buildCard();
    }
    for (UiKey k : ui.keys) {
        if (card_ == SettingsCard::None)
            break;
        const SettingsCard before = card_;
        switch (k) {
            case UiKey::Up:
            case UiKey::Down: {
                const int dy = k == UiKey::Up ? -1 : 1;
                if (card_ == SettingsCard::Choices) {
                    const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
                    if (!r)
                        break;
                    int i = focus_.index - kChoiceBase;
                    for (int j = i + dy; j >= 0 && j < static_cast<int>(r->choices.size()); j += dy)
                        if (r->choices[static_cast<size_t>(j)].enabled) {
                            i = j;
                            break;
                        }
                    if (i < choiceScroll_)
                        choiceScroll_ = i;
                    if (i >= choiceScroll_ + kChoiceVisible)
                        choiceScroll_ = i - kChoiceVisible + 1;
                    focus_ = {TargetKind::MenuRow, kChoiceBase + i};
                    buildCard();
                } else {
                    moveFocus(0, dy);
                }
                break;
            }
            case UiKey::Confirm:
                if (const Item* it = itemFor(focus_))
                    activateCard(*it, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                cardBack(out);
                break;
            default:
                break;
        }
        if (card_ != before)
            break;
        buildTargets();
    }
}

void SettingsPages::applyInput(const SettingsInput& in, SettingsFrameOutput& out) {
    const UiInput& ui = in.ui;
    hover_ = {};
    if (drag_) {
        const SettingRow* r = schema_ ? schema_->find(drag_->key) : nullptr;
        if (r && ui.leftDown && !ui.leftClick && ui.pointerValid) {
            sliderFromPointer(*r, ui.pointerX, out, false);
            for (size_t i = 0; i < items_.size(); ++i)
                if (items_[i].kind == Item::Row && rowAt(items_[i].a, items_[i].b) == r)
                    hover_ = {TargetKind::MenuRow, static_cast<int>(i)};
            return;
        }
        // Released: commit the dragged value once.
        const float v = drag_->value;
        drag_.reset();
        if (r) {
            SettingsAction a;
            a.kind = SettingsActionKind::Change;
            a.key = r->key;
            a.value = fmtNum(v, r->integer);
            out.actions.push_back(a);
        }
    }
    if (ui.pointerValid) {
        for (size_t i = targets_.size(); i-- > 0;)
            if (pointerOver(targets_[i], ui.pointerX, ui.pointerY)) {
                hover_ = targets_[i].id;
                if (ui.pointerMoved && targets_[i].navigable && targets_[i].id.kind == TargetKind::MenuRow)
                    focus_ = targets_[i].id;
                break;
            }
    }
    if (ui.rightClick) {
        back(out);
        return;
    }
    if (ui.leftClick && hover_.kind != TargetKind::None) {
        if (hover_.kind == TargetKind::Hint) {
            press(static_cast<Prompt>(hover_.index), out);
            return;
        }
        if (const Item* it = itemFor(hover_)) {
            if (it->navigable)
                focus_ = hover_;
            const Item copy = *it;
            activate(copy, out, true, ui.pointerX);
        }
        return;
    }
    if (ui.wheel != 0 && view_ != SettingsView::Landing) {
        scroll_ -= ui.wheel * kLineStep * 2;
    }
    std::vector<UiKey> keys = ui.keys;
    if (ui.wheel != 0 && view_ == SettingsView::Landing)
        keys.push_back(ui.wheel > 0 ? UiKey::Up : UiKey::Down);
    for (UiKey k : keys) {
        if (out.wantsClose || card_ != SettingsCard::None)
            break;
        const SettingsView viewBefore = view_;
        const int tabBefore = tab_;
        const Item* it = itemFor(focus_);
        switch (k) {
            case UiKey::Up:
                moveFocus(0, -1);
                break;
            case UiKey::Down:
                moveFocus(0, 1);
                break;
            case UiKey::Left:
            case UiKey::Right: {
                const int dir = k == UiKey::Left ? -1 : 1;
                if (it && it->kind == Item::Row) {
                    if (const SettingRow* r = rowAt(it->a, it->b))
                        adjust(*r, dir, out);
                } else if (it && it->kind == Item::Tab)
                    switchTab(dir, out);
                else
                    moveFocus(dir, 0);
                break;
            }
            case UiKey::Confirm:
                if (it) {
                    const Item copy = *it;
                    activate(copy, out, false, 0);
                }
                break;
            case UiKey::Back:
            case UiKey::Close:
                back(out);
                break;
            case UiKey::Secondary:
                press(Prompt::Secondary, out);
                break;
            case UiKey::Tertiary:
                press(Prompt::Tertiary, out);
                break;
            case UiKey::PagePrev:
                switchTab(-1, out);
                break;
            case UiKey::PageNext:
                switchTab(1, out);
                break;
            default:
                break;
        }
        if (view_ != viewBefore || tab_ != tabBefore) {
            build();
            ensureFocus();
        }
        buildTargets();
    }
}

void SettingsPages::onApplied(const SettingsFeedback& f) {
    for (const std::string& r : f.restartReasons) {
        if (std::find(restartReasons_.begin(), restartReasons_.end(), r) == restartReasons_.end())
            restartReasons_.push_back(r);
        if (std::find(restartNeeded_.begin(), restartNeeded_.end(), r) == restartNeeded_.end())
            restartNeeded_.push_back(r);
    }
    if (!f.error.empty())
        problem_ = f.error;
    if (!f.restartReasons.empty())
        restartPending_ = true;
    if (!f.message.empty()) {
        status_ = f.message;
        statusUntil_ = now_ + 3.0;
    }
    if (card_ == SettingsCard::None || card_ == SettingsCard::Choices) {
        if (card_ == SettingsCard::Choices)
            closeCard();
        else if (!problem_.empty())
            openCard(SettingsCard::Problem);
        else if (restartPending_ && !restartReasons_.empty()) {
            restartPending_ = false;
            openCard(SettingsCard::Restart);
        }
    }
}

void SettingsPages::ageHeld() {
    if (!schema_)
        return;
    held_.erase(std::remove_if(held_.begin(), held_.end(),
                               [&](Held& h) {
                                   const SettingRow* r = schema_->find(h.key);
                                   return !r || r->currentValue == h.value || ++h.frames > 90;
                               }),
                held_.end());
}

SettingsFrameOutput SettingsPages::frame(const SettingsSchema& schema, const SettingsInput& in, const Viewport& vp) {
    SettingsFrameOutput out;
    if (!open_)
        return out;
    schema_ = &schema;
    now_ = in.now;
    lf_ = computeFrame(vp);
    if (!pendingOpen_.empty()) {
        const std::string id = pendingOpen_;
        pendingOpen_.clear();
        for (int t = 0; t < tabCount(); ++t) {
            const int np = static_cast<int>(schema.pages.size());
            const std::string& tid =
                t < np ? schema.pages[static_cast<size_t>(t)].id : schema.bindings[static_cast<size_t>(t - np)].id;
            if (tid == id) {
                SettingsFrameOutput ignore;
                enterPage(t, ignore);
            }
        }
    }
    ageHeld();
    build();
    buildCard();
    buildTargets();
    ensureFocus();
    buildHints();
    const SettingsView viewBefore = view_;
    const int tabBefore = tab_;
    const TargetId focusBefore = focus_;
    const SettingsCard cardBefore = card_;
    if (card_ != SettingsCard::None)
        applyCardInput(in, out);
    else
        applyInput(in, out);
    if ((view_ != viewBefore || tab_ != tabBefore) && (out.sound == UiSound::None || out.sound == UiSound::Move))
        out.sound = UiSound::Page; // a tab flip (entering / leaving a page sets Confirm / Back itself)
    else if (out.sound == UiSound::None && card_ != cardBefore && card_ != SettingsCard::None)
        out.sound = UiSound::Confirm; // a card opened (capture, defaults, ...)
    else if (out.sound == UiSound::None && card_ == cardBefore && !(focus_ == focusBefore) &&
             focusBefore.kind != TargetKind::None)
        out.sound = UiSound::Move;
    if (out.wantsClose) {
        close();
        return out;
    }
    build();
    buildCard();
    keepFocusVisible();
    buildTargets();
    ensureFocus();
    keepFocusVisible();
    buildTargets();
    buildHints();
    draw(out.draw);
    return out;
}

// ---- drawing ----

void SettingsPages::draw(DrawList& d) const {
    const Theme& th = cfg_.screen.theme;
    if (cfg_.drawBackdrop)
        d.panel({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, PanelStyle::Backdrop, th.backdrop);
    drawHeader(d);
    if (view_ == SettingsView::Landing)
        drawLanding(d);
    else
        drawPage(d);
    drawHelp(d);
    if (card_ != SettingsCard::None)
        drawCard(d);
    drawFooter(d);
}

void SettingsPages::drawHeader(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect band = lf_.band(0, 120);
    d.gradient(band, withAlpha(th.panel, 240), withAlpha(th.panel, 0));
    p.ornateRule(d, band.x, band.right(), band.y + p.px(112), true);
    const SettingPage* pg = currentPage();
    const BindingLayerView* l = currentLayer();
    const std::string title = pg ? pg->title : l ? l->title : cfg_.title;
    p.text(d, {kDesignW * 0.5f - 450, 24, 900, 60}, title, FontRole::Title, th.heading, TextAlign::Center);
    std::vector<std::string> trail = cfg_.trail;
    if (view_ != SettingsView::Landing)
        trail.push_back(cfg_.title);
    if (!trail.empty())
        p.text(d, {kDesignW * 0.5f - 450, 80, 900, 24}, join(trail, "  /  "), FontRole::Small, th.textMuted,
               TextAlign::Center);
}

void SettingsPages::drawLanding(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    d.panel(p.map({kMenuX, kPanelY, kMenuW, kPanelH}), PanelStyle::Main, th.panel);
    const bool live = card_ == SettingsCard::None;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        const TargetId id{TargetKind::MenuRow, static_cast<int>(i)};
        const bool foc = live && focus_ == id, hov = live && hover_ == id;
        const Rect rp = p.map(it.rect);
        d.panel(rp, PanelStyle::Card, hov ? th.panelRaised : th.panel);
        if (foc)
            p.focus(d, rp);
        std::string label = "Back";
        if (it.kind == Item::Category && schema_)
            label = schema_->pages[static_cast<size_t>(it.a)].title;
        else if (it.kind == Item::BindLayer)
            label = "Key Bindings";
        const Rect inner{it.rect.x + 32, it.rect.y, it.rect.w - 64, it.rect.h};
        p.text(d, inner, label, FontRole::Heading, foc ? th.heading : th.text);
        if (it.kind != Item::Back)
            p.text(d, inner, ">", FontRole::Heading, foc ? th.ember : th.brassDim, TextAlign::Right);
    }
}

void SettingsPages::drawTabs(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const bool live = card_ == SettingsCard::None;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (it.kind != Item::Tab)
            continue;
        const TargetId id{TargetKind::MenuRow, static_cast<int>(i)};
        const bool cur = it.a == tab_, foc = live && focus_ == id, hov = live && hover_ == id;
        const Rect rp = p.map(it.rect);
        d.panel(rp, PanelStyle::Inset, cur ? th.panelRaised : (hov ? withAlpha(th.panelRaised, 200) : th.inset));
        if (foc)
            p.focus(d, rp);
        if (cur)
            d.rect({rp.x + p.px(10), rp.bottom() - p.px(4), rp.w - p.px(20), p.px(3)}, th.gilt);
        const int np = schema_ ? static_cast<int>(schema_->pages.size()) : 0;
        const std::string& label = it.a < np ? schema_->pages[static_cast<size_t>(it.a)].title
                                             : schema_->bindings[static_cast<size_t>(it.a - np)].title;
        p.text(d, it.rect.inset(6), label, FontRole::Body, cur || foc ? th.heading : th.textMuted, TextAlign::Center,
               21);
    }
}

void SettingsPages::drawPage(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    d.panel(p.map(pagePanel()), PanelStyle::Main, th.panel);
    drawTabs(d);
    const Rect list = listArea();
    d.pushClip(p.map(list));
    for (const Heading& h : headings_) {
        const float y = kListY + h.y - scroll_;
        p.label(d, {kInX + 8, y + 8, kLineW - 16, 28}, h.title, th.brass);
        p.rule(d, kInX + 8, y + 40, kLineW - 16);
    }
    for (const Heading& n : notes_)
        p.text(d, {kInX + 12, kListY + n.y - scroll_, kLineW - 24, 28}, n.title, FontRole::Small, th.textMuted);
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (it.kind == Item::Row)
            drawRow(it, static_cast<int>(i), d);
        else if (it.kind == Item::Chip && it.c == 0)
            drawChipRow(static_cast<int>(i), d);
    }
    d.popClip();
    // Scroll bar.
    const float viewH = list.h;
    if (contentH_ > viewH + 1) {
        const Rect track{kInX + kInW - kBarW, list.y + 4, kBarW, list.h - 8};
        d.panel(p.map(track), PanelStyle::Inset, th.inset);
        const float f = viewH / contentH_;
        const float maxScroll = contentH_ - viewH;
        const float th0 = std::max(40.0f, track.h * f);
        const float y = track.y + (track.h - th0) * (maxScroll > 0 ? scroll_ / maxScroll : 0);
        d.rect(p.map({track.x + 1, y, track.w - 2, th0}), th.brassDim, p.px(3));
    }
}

void SettingsPages::drawRow(const Item& it, int index, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const SettingRow* r = rowAt(it.a, it.b);
    if (!r)
        return;
    Rect rd = it.rect;
    rd.y -= scroll_;
    if (rd.bottom() < kListY - 2 || rd.y > kListBottom + 2)
        return;
    const Rect rp = p.map(rd);
    const TargetId id{TargetKind::MenuRow, index};
    const bool live = card_ == SettingsCard::None;
    const bool foc = (live && focus_ == id) || (!live && pageFocus_ == id);
    const bool hov = live && hover_ == id && r->enabled;
    const bool en = r->enabled;
    if (r->kind != SettingKind::Info) {
        d.panel(rp, PanelStyle::Card, !en ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
        if (foc)
            p.focus(d, rp);
    }
    const Color labelC = !en ? th.textDisabled : (foc ? th.heading : th.text);
    const Color valueC = !en ? th.textDisabled : th.heading;
    const Rect area{rd.right() - kValuePad - kValueW, rd.y, kValueW, rd.h};
    const float labelW = area.x - rd.x - 48;
    p.text(d, {rd.x + 24, rd.y, labelW, rd.h}, r->label, FontRole::Body, labelC);
    if (r->restart) {
        const float w = std::min(labelW, p.textWidth(r->label, FontRole::Body));
        p.text(d, {rd.x + 24 + w + 6, rd.y, 20, rd.h}, "*", FontRole::Body, en ? th.ember : th.textDisabled);
    }
    switch (r->kind) {
        case SettingKind::Toggle: {
            const bool on = shownOn(*r);
            const Rect box{area.right() - 28, rd.y + (rd.h - 28) * 0.5f, 28, 28};
            d.outline(p.map(box), en ? th.brass : th.textDisabled, p.px(2), p.px(3));
            if (on)
                d.rect(p.map(box.inset(7)), !en ? th.textDisabled : (foc ? th.ember : th.brass), p.px(2));
            p.text(d, {area.x, rd.y, area.w - 44, rd.h}, on ? "On" : "Off", FontRole::Body,
                   on && en ? valueC : th.textMuted, TextAlign::Right);
            break;
        }
        case SettingKind::Choice: {
            const int ci = shownChoice(*r);
            std::string v;
            if (ci == r->current || ci < 0)
                v = r->valueText();
            else
                v = r->choices[static_cast<size_t>(ci)].label;
            const Color arrowC = !en ? th.textDisabled : (foc ? th.ember : th.brass);
            const bool canPrev = en && ci > 0, canNext = en && ci + 1 < static_cast<int>(r->choices.size());
            p.text(d, {area.x, rd.y, kArrowW, rd.h}, "<", FontRole::Heading, canPrev ? arrowC : th.textDisabled,
                   TextAlign::Center);
            p.text(d, {area.right() - kArrowW, rd.y, kArrowW, rd.h}, ">", FontRole::Heading,
                   canNext ? arrowC : th.textDisabled, TextAlign::Center);
            p.text(d, {area.x + kArrowW, rd.y, area.w - 2 * kArrowW, rd.h}, v, FontRole::Body, valueC,
                   TextAlign::Center);
            break;
        }
        case SettingKind::Slider: {
            const float v = shownSlider(*r);
            const Rect track = sliderTrack(area);
            const Rect tp = p.map(track);
            d.panel(tp, PanelStyle::Inset, th.inset);
            const float f = toFraction(*r, v);
            const Rect in = tp.inset(p.px(3));
            if (en)
                d.gradient({in.x, in.y, in.w * f, in.h}, th.brass, withAlpha(th.brass, 170));
            const float kw = p.px(14), kh = p.px(30);
            const Rect knob{tp.x + tp.w * f - kw * 0.5f, tp.cy() - kh * 0.5f, kw, kh};
            d.panel(knob, PanelStyle::Card, !en ? th.textDisabled : (foc ? th.ember : th.brass));
            d.outline(knob, en ? th.heading : th.textDisabled, p.px(1.5f), p.px(3));
            p.text(d, {track.right() + 12, rd.y, area.right() - track.right() - 12, rd.h}, r->formatValue(v),
                   FontRole::Body, valueC, TextAlign::Right);
            break;
        }
        case SettingKind::Info:
            p.text(d, {area.x - 120, rd.y, area.w + 120, rd.h}, r->text, FontRole::Small, th.textMuted,
                   TextAlign::Right);
            break;
        case SettingKind::Size:
            p.text(d, area, r->valueText(), FontRole::Body, valueC, TextAlign::Right);
            break;
    }
}

void SettingsPages::drawChipRow(int first, DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Item& f = items_[static_cast<size_t>(first)];
    const BindingAction* a = actionAt(f.a, f.b);
    if (!a)
        return;
    const float y = f.rect.y - 7 - scroll_;
    if (y + kLineH < kListY - 2 || y > kListBottom + 2)
        return;
    const Rect rd{kInX, y, kLineW, kLineH};
    const bool live = card_ == SettingsCard::None;
    bool rowFocus = false;
    for (size_t i = static_cast<size_t>(first);
         i < items_.size() && items_[i].kind == Item::Chip && items_[i].a == f.a && items_[i].b == f.b; ++i) {
        const TargetId id{TargetKind::MenuRow, static_cast<int>(i)};
        rowFocus = rowFocus || (live ? focus_ == id : pageFocus_ == id);
    }
    d.panel(p.map(rd), PanelStyle::Card, th.panel);
    const float chipsX = items_[static_cast<size_t>(first)].rect.x;
    p.text(d, {rd.x + 24, rd.y, chipsX - rd.x - 40, rd.h}, a->label, FontRole::Body, rowFocus ? th.heading : th.text);
    for (size_t i = static_cast<size_t>(first);
         i < items_.size() && items_[i].kind == Item::Chip && items_[i].a == f.a && items_[i].b == f.b; ++i) {
        const Item& c = items_[i];
        const TargetId id{TargetKind::MenuRow, static_cast<int>(i)};
        const bool foc = live ? focus_ == id : pageFocus_ == id;
        const bool hov = live && hover_ == id;
        Rect cd = c.rect;
        cd.y -= scroll_;
        const Rect cp = p.map(cd);
        const bool has = c.c < static_cast<int>(a->slots.size());
        const BindingSlot* s = has ? &a->slots[static_cast<size_t>(c.c)] : nullptr;
        const bool conflict = s && !s->alsoBoundTo.empty(), reserved = s && !s->reserved.empty();
        d.panel(cp, PanelStyle::Inset, hov ? th.panelRaised : th.inset);
        if (foc)
            p.selectCell(d, cp);
        else if (conflict)
            d.outline(cp, th.ember, p.px(2), p.px(3));
        else if (reserved)
            d.outline(cp, th.bad, p.px(2), p.px(3));
        const Color tc = !s ? th.textDisabled : conflict ? th.ember : reserved ? th.bad : (foc ? th.heading : th.text);
        p.text(d, cd.inset(8), s ? s->display : std::string("-"), FontRole::Body, tc, TextAlign::Center, 21);
    }
}

void SettingsPages::drawHelp(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect panel = helpPanel();
    d.panel(p.map(panel), PanelStyle::Main, th.panel);
    const float x = panel.x + 32, w = panel.w - 64;
    float y = panel.y + 24;
    const float bottom = panel.bottom() - 24;
    const Item* it = itemFor(card_ == SettingsCard::None ? focus_ : pageFocus_);

    std::string kicker, title, body;
    std::vector<std::pair<std::string, Color>> notes;
    const int np = schema_ ? static_cast<int>(schema_->pages.size()) : 0;
    if (it && (it->kind == Item::Category || it->kind == Item::Tab) && schema_ && it->a < np) {
        const SettingPage& pg = schema_->pages[static_cast<size_t>(it->a)];
        kicker = cfg_.title;
        title = pg.title;
        body = pg.description;
    } else if (it && (it->kind == Item::BindLayer || it->kind == Item::Tab) && schema_ && !schema_->bindings.empty()) {
        const int li = it->kind == Item::BindLayer ? 0 : it->a - np;
        const BindingLayerView& l =
            schema_->bindings[static_cast<size_t>(std::clamp(li, 0, static_cast<int>(schema_->bindings.size()) - 1))];
        kicker = cfg_.title;
        title = it->kind == Item::BindLayer ? "Key Bindings" : l.title;
        body = it->kind == Item::BindLayer
                   ? "Rebind the keyboard and mouse for gameplay and for the menus. Conflicts are marked; each "
                     "layer can return to its defaults."
                   : l.description;
    } else if (it && it->kind == Item::Back) {
        kicker = cfg_.title;
        title = "Back";
        body = cfg_.trail.empty() ? "Leave " + cfg_.title + "." : "Back to " + cfg_.trail.back() + ".";
    } else if (it && it->kind == Item::Row) {
        const SettingRow* r = rowAt(it->a, it->b);
        const SettingPage* pg = currentPage();
        if (r && pg) {
            kicker = pg->sections[static_cast<size_t>(it->a)].title;
            title = r->label;
            body = r->help;
            if (!r->enabled)
                notes.push_back({"Unavailable: " + (r->needs.empty() ? r->unavailable : r->needs), th.textMuted});
            if (r->restart)
                notes.push_back({"* Takes effect after a restart.", th.ember});
            if (r->confirm && r->enabled)
                notes.push_back({"You can keep or revert the change.", th.textMuted});
        }
    } else if (it && it->kind == Item::Chip) {
        const BindingAction* a = actionAt(it->a, it->b);
        const BindingLayerView* l = currentLayer();
        if (a && l) {
            kicker = l->groups[static_cast<size_t>(it->a)].title;
            title = a->label;
            body = a->hint;
            const bool has = it->c < static_cast<int>(a->slots.size());
            const char* slot = kSlotNames[std::clamp(it->c, 0, 2)];
            notes.push_back({std::string(slot) + " key: " +
                                 (has ? a->slots[static_cast<size_t>(it->c)].display : std::string("not bound")),
                             th.text});
            if (has) {
                const BindingSlot& s = a->slots[static_cast<size_t>(it->c)];
                if (!s.alsoBoundTo.empty())
                    notes.push_back({"Also bound to: " + join(s.alsoBoundTo, ", "), th.ember});
                if (!s.reserved.empty())
                    notes.push_back({"Note: " + s.reserved, th.bad});
            }
        }
    }
    if (!kicker.empty()) {
        p.label(d, {x, y, w, 28}, kicker, th.brass);
        y += 32;
    }
    if (!title.empty()) {
        std::vector<std::string> tl = p.wrap(title, w, FontRole::Title);
        if (tl.size() > 2)
            tl.resize(2);
        for (const std::string& l : tl) {
            p.text(d, {x, y, w, 52}, l, FontRole::Title, th.heading);
            y += 52;
        }
        y += 12;
        p.rule(d, x, y, w);
        y += 18;
    }
    // The footer of the panel: settings that wait for a restart, else how changes are kept.
    std::vector<std::string> foot;
    Color footC = th.textMuted;
    if (!restartNeeded_.empty()) {
        foot = p.wrap("Restart to apply: " + join(restartNeeded_, ", ") + ".", w, FontRole::Small);
        footC = th.ember;
    } else if (now_ < statusUntil_ && !status_.empty()) {
        foot = p.wrap(status_, w, FontRole::Small);
    } else {
        foot = p.wrap(view_ == SettingsView::Bindings ? "Changes apply at once and are saved to drakan_input.ini."
                                                      : "Changes apply at once and are saved.",
                      w, FontRole::Small);
    }
    if (foot.size() > 3)
        foot.resize(3);
    const float footY = bottom - static_cast<float>(foot.size()) * 26;
    for (const std::string& l : p.wrap(body, w, FontRole::Body, 23)) {
        if (y + 30 > footY - 80)
            break;
        p.text(d, {x, y, w, 30}, l, FontRole::Body, th.text, TextAlign::Left, 23);
        y += 30;
    }
    y += 10;
    for (const auto& n : notes) {
        for (const std::string& l : p.wrap(n.first, w, FontRole::Small)) {
            if (y + 28 > footY - 8)
                break;
            p.text(d, {x, y, w, 28}, l, FontRole::Small, n.second);
            y += 28;
        }
        y += 4;
    }
    p.rule(d, x, footY - 10, w);
    float fy = footY;
    for (const std::string& l : foot) {
        p.text(d, {x, fy, w, 26}, l, FontRole::Small, footC);
        fy += 26;
    }
}

void SettingsPages::drawCard(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    d.rect({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, withAlpha(th.backdropEdge, 120));
    const Rect c = cardPanel();
    d.panel(p.map(c), PanelStyle::Main, th.panelRaised);
    std::string title;
    switch (card_) {
        case SettingsCard::Choices: {
            const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
            title = r ? r->label : "";
            break;
        }
        case SettingsCard::KeepDisplay:
            title = "Keep these display settings?";
            break;
        case SettingsCard::Restart:
            title = "Restart required";
            break;
        case SettingsCard::Problem:
            title = "Something did not work";
            break;
        case SettingsCard::Defaults:
            title = "Restore defaults?";
            break;
        case SettingsCard::Capture: {
            const BindingAction* a = actionAt(cap_.group, cap_.index);
            title = a ? a->label : cap_.action;
            break;
        }
        case SettingsCard::Conflict:
            title = sourceTitle(cap_.pendingSource) + " is already used";
            break;
        default:
            break;
    }
    p.text(d, {c.x + kCardPad, c.y + 28, c.w - 2 * kCardPad, 52}, title, FontRole::Title, th.heading);
    p.rule(d, c.x + kCardPad, c.y + 88, c.w - 2 * kCardPad);
    if (card_ == SettingsCard::Choices) {
        const SettingRow* r = schema_ ? schema_->find(choiceKey_) : nullptr;
        if (!r)
            return;
        const int cur = shownChoice(*r);
        if (r->choices.empty())
            p.text(d, {c.x + kCardPad, c.y + kCardHead, c.w - 2 * kCardPad, kChoiceH}, r->emptyText, FontRole::Body,
                   th.textMuted);
        for (const Item& it : cardItems_) {
            const TargetId id{TargetKind::MenuRow, kChoiceBase + it.a};
            const bool foc = focus_ == id, hov = hover_ == id && it.enabled;
            const Rect rp = p.map(it.rect);
            d.panel(rp, PanelStyle::Card, !it.enabled ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
            if (foc)
                p.focus(d, rp);
            const SettingChoice& ch = r->choices[static_cast<size_t>(it.a)];
            if (it.a == cur)
                d.rect(p.map({it.rect.x + 20, it.rect.cy() - 6, 12, 12}), th.gilt, p.px(2));
            p.text(d, {it.rect.x + 48, it.rect.y, it.rect.w - 72, it.rect.h}, ch.label, FontRole::Body,
                   !it.enabled ? th.textDisabled : (foc || it.a == cur ? th.heading : th.text));
            if (!it.enabled)
                p.text(d, {it.rect.x + 48, it.rect.y, it.rect.w - 72, it.rect.h}, "Unavailable", FontRole::Small,
                       th.textDisabled, TextAlign::Right);
        }
        const int n = static_cast<int>(r->choices.size());
        if (choiceScroll_ > 0)
            p.text(d, {c.right() - kCardPad - 200, c.y + 92, 200, 12}, "more above", FontRole::Small, th.textMuted,
                   TextAlign::Right, 14);
        if (choiceScroll_ + kChoiceVisible < n)
            p.text(d, {c.right() - kCardPad - 200, c.bottom() - kCardPad + 10, 200, 20}, "more below", FontRole::Small,
                   th.textMuted, TextAlign::Right, 14);
        return;
    }
    float y = c.y + kCardHead;
    for (const std::string& l : cardBody()) {
        p.text(d, {c.x + kCardPad, y, c.w - 2 * kCardPad, kCardLine}, l, FontRole::Body,
               card_ == SettingsCard::KeepDisplay || card_ == SettingsCard::Conflict ? th.text : th.textMuted);
        y += kCardLine;
    }
    if (card_ == SettingsCard::Capture) {
        const Rect box = captureBox();
        const Rect bp = p.map(box);
        d.panel(bp, PanelStyle::Inset, th.inset);
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now_) * 5.0f);
        d.outline(bp, withAlpha(th.gilt, static_cast<uint8_t>(120 + 135 * pulse)), p.px(2), p.px(4));
        p.text(d, box, "Waiting for input...", FontRole::Heading, th.heading, TextAlign::Center);
        const int left = std::max(0, static_cast<int>(std::ceil(cfg_.captureSeconds - (now_ - cap_.started))));
        p.text(d, {box.x, box.bottom() - 28, box.w - 12, 24}, "Cancels in " + std::to_string(left) + " s",
               FontRole::Small, th.textMuted, TextAlign::Right, 16);
    }
    const std::vector<std::string> rows = cardRows();
    for (const Item& it : cardItems_) {
        const TargetId id{TargetKind::MenuRow, kCardBase + it.a};
        const bool foc = focus_ == id, hov = hover_ == id && it.enabled;
        const Rect rp = p.map(it.rect);
        d.panel(rp, PanelStyle::Card, !it.enabled ? withAlpha(th.panel, 120) : (hov ? th.panelRaised : th.panel));
        if (foc)
            p.focus(d, rp);
        p.text(d, {it.rect.x + 32, it.rect.y, it.rect.w - 64, it.rect.h}, rows[static_cast<size_t>(it.a)],
               FontRole::Heading, !it.enabled ? th.textDisabled : (foc ? th.heading : th.text));
    }
}

void SettingsPages::drawFooter(DrawList& d) const {
    const detail::Paint p(lf_, cfg_.screen);
    const Theme& th = cfg_.screen.theme;
    const Rect fb = lf_.band(992, 88);
    d.gradient(fb, withAlpha(th.panel, 0), withAlpha(th.panel, 235));
    for (size_t i = 0; i < hints_.size(); ++i) {
        const TargetId id{TargetKind::Hint, static_cast<int>(hints_[i].prompt)};
        for (const HitTarget& t : targets_)
            if (t.id == id) {
                p.drawHint(d, hints_[i], t.rect, hover_ == id && hints_[i].enabled, kHintGlyph, FontRole::Body);
                break;
            }
    }
}

std::string SettingsPages::sourceTitle(const std::string& source) const {
    if (const BindingLayerView* l = currentLayer())
        for (const BindingGroup& g : l->groups)
            for (const BindingAction& a : g.actions)
                for (const BindingSlot& s : a.slots)
                    if (s.source == source && !s.display.empty())
                        return s.display;
    return source;
}

} // namespace gameui
