#include "gameui/StripScreen.h"

#include <algorithm>

#include "Paint.h"
#include "gameui/Addresses.h"

namespace gameui {

namespace {

using detail::fmtInt;

constexpr int kCols = addr::ctl::kGridColumns;
constexpr int kCells = addr::ctl::kSlotCount;
// Design units of a 1080-high canvas: the cell is 0.056 H, the bar sits 12 units above the bottom edge.
constexpr float kCell = 60, kGap = 4;
constexpr float kGridW = 8 * kCell + 7 * kGap; // 508
constexpr float kGridH = 3 * kCell + 2 * kGap; // 188
constexpr float kPad = 14;
constexpr float kBottomMargin = 12;
constexpr float kBarW = kGridW + 2 * kPad, kBarH = kGridH + 2 * kPad;
constexpr float kCardGap = 16, kCardW = 352;
// Order of the Flame's placement: the whole plate (grid + item card) is centred at the bottom. Centring the grid alone
// put the plate's centre 184 units right of the screen's.
constexpr float kBarX = (kDesignW - (kBarW + kCardGap + kCardW)) * 0.5f;
constexpr float kBarY = kDesignH - kBottomMargin - kBarH;
constexpr float kGridX = kBarX + kPad, kGridY = kBarY + kPad;
constexpr float kCardX = kBarX + kBarW + kCardGap;
// One opaque plate holds the prompts and the grid (left) and the item card (right). Opaque because the native grid
// the icons are cropped from lies under it at a larger scale than the host cells (see design::strip::nativeGrid).
constexpr float kPlateH = 292;
constexpr float kPlateX = kBarX, kPlateY = kDesignH - kBottomMargin - kPlateH;
constexpr float kPlateW = kCardX + kCardW - kPlateX;
constexpr float kCardY = kPlateY, kCardH = kPlateH;
constexpr float kHintY = kPlateY + 24, kHintH = 36, kHintGlyph = 30, kHintGap = 22, kHintSize = 22;
// The widest thing the strip draws, centred: the canvas narrows below 4:3 so the card stays on screen.
constexpr float kMinDesignW = 2 * (kCardX + kCardW - kDesignW * 0.5f) + 2 * 40;
constexpr float kMarkSize = 15; // cell marks (E, quick slot number, stack count)

const char* primaryLabel(const Snapshot& s, const ItemEntry& it) {
    if (const char* hand = handUseLabel(handUse(s, it)))
        return hand;
    switch (it.category) {
        case ItemCategory::Melee:
        case ItemCategory::Bow:
        case ItemCategory::Armour:
            return it.equipped ? "Equipped" : "Equip";
        case ItemCategory::SpellScroll:
            return "Read";
        default:
            return "Use";
    }
}

bool primaryEnabled(const Snapshot& s, const ItemEntry& it) {
    // The left-hand weapon is equipped, yet using it again puts it away.
    return !(isEquippable(it.category) && it.equipped) || handUse(s, it) == dualwield::Use::UnequipLeft;
}

const char* refusalText(ActionKind k) {
    switch (k) {
        case ActionKind::UseOrEquip:
            return "That can't be used right now.";
        case ActionKind::Drop:
            return "That can't be dropped.";
        case ActionKind::ToggleHotSlot:
            return "That item is used up, or its requirement is not met.";
        default:
            return "That is not possible right now.";
    }
}

} // namespace

namespace design::strip {
Rect cell(int c) {
    const int col = c % kCols, row = c / kCols;
    return {kGridX + static_cast<float>(col) * (kCell + kGap), kGridY + static_cast<float>(row) * (kCell + kGap), kCell,
            kCell};
}
Rect item(int origin, int w, int h) {
    const Rect c = cell(origin);
    return {c.x, c.y, static_cast<float>(w) * kCell + static_cast<float>(w - 1) * kGap,
            static_cast<float>(h) * kCell + static_cast<float>(h - 1) * kGap};
}
Rect bar() {
    return {kBarX, kBarY, kBarW, kBarH};
}
Rect card() {
    return {kCardX, kCardY, kCardW, kCardH};
}
Rect hintRow() {
    return {kBarX, kHintY, kBarW, kHintH};
}
Rect plate() {
    return {kPlateX, kPlateY, kPlateW, kPlateH};
}
// The Tab screen's grid proportions fitted into the plate. The native item icons stop drawing when they are placed
// small (an item whose texture is not resident at that size is skipped by the mesh draw 189b38 / 17c208: the Health
// Elixir at the strip's cell size never appeared), so the native grid goes here, near the Tab screen's scale, and the
// host fits each crop into its own cell.
Rect nativeGrid() {
    const Rect a = design::gridCell(0), b = design::gridCell(addr::ctl::kSlotCount - 1);
    const float gw = b.right() - a.x, gh = b.bottom() - a.y;
    const Rect in = plate().inset(6);
    const float k = std::min(in.w / gw, in.h / gh);
    return {in.cx() - gw * k * 0.5f, in.cy() - gh * k * 0.5f, gw * k, gh * k};
}
} // namespace design::strip

Rect StripScreen::Layout::cell(int c) const {
    return frame.map(design::strip::cell(c));
}
Rect StripScreen::Layout::item(int origin, int w, int h) const {
    return frame.map(design::strip::item(origin, w, h));
}
Rect StripScreen::Layout::bounds() const {
    const float x0 = std::min({plate.x, bar.x, card.x, hints.x}), y0 = std::min({plate.y, bar.y, card.y, hints.y});
    const float x1 = std::max({plate.right(), bar.right(), card.right(), hints.right()});
    const float y1 = std::max({plate.bottom(), bar.bottom(), card.bottom(), hints.bottom()});
    return {x0, y0, x1 - x0, y1 - y0};
}

StripScreen::Layout StripScreen::layoutFor(const Viewport& vp) {
    Layout l;
    l.frame = detail::anchoredFrame(vp, kMinDesignW, detail::AnchorX::Centre, true);
    l.bar = l.frame.map(design::strip::bar());
    l.card = l.frame.map(design::strip::card());
    l.hints = l.frame.map(design::strip::hintRow());
    l.plate = l.frame.map(design::strip::plate());
    l.nativeGrid = l.frame.map(design::strip::nativeGrid());
    return l;
}

StripScreen::StripScreen(ScreenConfig cfg) : cfg_(std::move(cfg)) {}

void StripScreen::open(const Snapshot& s) {
    (void)s;
    open_ = true;
    closePending_ = false;
    focus_ = hover_ = {};
    lastSteer_ = steerWait_ = 0;
    pendingSelect_ = false;
    pendingAge_ = 0;
    prefetched_.clear();
    toast_.clear();
    toastLeft_ = 0;
}

void StripScreen::close() {
    open_ = false;
    targets_.clear();
    hints_.clear();
    hintRects_.clear();
}

void StripScreen::onResult(const ActionResult& r) {
    if (r.kind == ActionKind::Close) {
        if (closePending_ && !open_ && r.outcome != Outcome::Confirmed)
            open_ = true;
        closePending_ = false;
        return;
    }
    if (r.kind == ActionKind::SelectItem)
        pendingSelect_ = false;
    if (r.outcome != Outcome::Refused || r.kind == ActionKind::SelectItem || r.kind == ActionKind::SwitchSection)
        return;
    toast_ = refusalText(r.kind);
    toastLeft_ = cfg_.toastFrames;
}

const HitTarget* StripScreen::findTarget(const TargetId& id) const {
    for (const HitTarget& t : targets_)
        if (t.id == id)
            return &t;
    return nullptr;
}

const ItemEntry* StripScreen::focusedItem(const Snapshot& s) const {
    return focus_.kind == TargetKind::GridCell ? s.itemAtCell(focus_.index) : nullptr;
}

void StripScreen::buildTargets(const Snapshot& s) {
    targets_.clear();
    if (!s.native.valid)
        return;
    for (int c = 0; c < kCells; ++c) {
        const ItemEntry* it = s.itemAtCell(c);
        if (!it)
            targets_.push_back({{TargetKind::GridCell, c}, layout_.cell(c), true, true});
        else if (it->originCell == c)
            targets_.push_back({{TargetKind::GridCell, c}, layout_.item(c, it->width, it->height), true, true});
    }
}

void StripScreen::ensureFocus(const Snapshot& s) {
    if (focus_.kind != TargetKind::None) {
        const HitTarget* t = findTarget(focus_);
        if (t && t->navigable)
            return;
    }
    focus_ = {};
    if (!s.items.empty()) {
        focus_ = {TargetKind::GridCell, s.items.front().originCell};
        if (findTarget(focus_))
            return;
    }
    for (const HitTarget& t : targets_)
        if (t.navigable) {
            focus_ = t.id;
            return;
        }
    focus_ = {};
}

void StripScreen::buildHints(const Snapshot& s) {
    hints_.clear();
    const ItemEntry* it = focusedItem(s);
    if (s.native.valid) {
        hints_.push_back({Prompt::Confirm, it ? primaryLabel(s, *it) : "Equip", it && primaryEnabled(s, *it)});
        hints_.push_back({Prompt::Secondary, "Drop", it && it->dropAllowed});
        hints_.push_back({Prompt::Tertiary, it && it->hotSlot ? "Unslot" : "Quick Slot", it && !s.shop.active});
    }
    hints_.push_back({Prompt::Back, "Close"});
    const detail::Paint p(layout_.frame, cfg_);
    const Rect row = design::strip::hintRow();
    hintRects_ = p.layoutHints(hints_, row.cx(), true, row.y, row.h, kHintGlyph, kHintGap, FontRole::Body, kHintSize);
    for (size_t i = 0; i < hints_.size(); ++i)
        targets_.push_back({{TargetKind::Hint, static_cast<int>(hints_[i].prompt)},
                            layout_.frame.map(hintRects_[i]),
                            false,
                            hints_[i].enabled});
}

void StripScreen::press(Prompt pr, const Snapshot& s, FrameOutput& out) {
    const ItemEntry* it = focusedItem(s);
    auto push = [&](ActionKind k, int target, uint32_t handle) { out.actions.push_back({0, k, target, handle}); };
    switch (pr) {
        case Prompt::Confirm:
            if (it && primaryEnabled(s, *it))
                push(ActionKind::UseOrEquip, it->slot, it->handle);
            break;
        case Prompt::Back:
            out.actions.push_back({0, ActionKind::Close, -1, 0});
            out.wantsClose = true;
            break;
        case Prompt::Secondary:
            if (it && it->dropAllowed)
                push(ActionKind::Drop, it->slot, it->handle);
            break;
        case Prompt::Tertiary:
            if (it && !s.shop.active)
                push(ActionKind::ToggleHotSlot, it->slot, it->handle);
            break;
        default:
            break;
    }
}

void StripScreen::applyInput(const Snapshot& s, const UiInput& in, FrameOutput& out) {
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
        press(Prompt::Back, s, out);
        return;
    }
    if (in.leftClick && hover_.kind != TargetKind::None) {
        const HitTarget* t = findTarget(hover_);
        if (t && t->enabled) {
            if (hover_.kind == TargetKind::GridCell) {
                focus_ = hover_;
                if (in.doubleClick)
                    press(Prompt::Confirm, s, out);
            } else if (hover_.kind == TargetKind::Hint) {
                press(static_cast<Prompt>(hover_.index), s, out);
            }
        }
    }
    for (UiKey k : in.keys) {
        if (out.wantsClose)
            break;
        int dx = 0, dy = 0;
        switch (k) {
            case UiKey::Up:
                dy = -1;
                break;
            case UiKey::Down:
                dy = 1;
                break;
            case UiKey::Left:
                dx = -1;
                break;
            case UiKey::Right:
                dx = 1;
                break;
            case UiKey::Confirm:
                press(Prompt::Confirm, s, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                press(Prompt::Back, s, out);
                break;
            case UiKey::Secondary:
                press(Prompt::Secondary, s, out);
                break;
            case UiKey::Tertiary:
                press(Prompt::Tertiary, s, out);
                break;
            default:
                break; // no pages or filters on the strip
        }
        if (dx == 0 && dy == 0)
            continue;
        const HitTarget* cur = findTarget(focus_);
        if (!cur) {
            ensureFocus(s);
            continue;
        }
        const int next = InventoryScreen::pickDirectional(targets_, cur->rect, dx, dy);
        if (next >= 0)
            focus_ = targets_[static_cast<size_t>(next)].id;
    }
}

// As InventoryScreen::steerSelection for the Inventory page: the native details describe the native selection, so
// the focused item is selected natively once per handle; equipped gear is fetched once for the card's marks.
void StripScreen::steerSelection(const Snapshot& s, FrameOutput& out) {
    if (pendingSelect_ && ++pendingAge_ < 120)
        return;
    pendingSelect_ = false;
    auto send = [&](int slot, uint32_t handle) {
        out.actions.push_back({0, ActionKind::SelectItem, slot, handle});
        pendingSelect_ = true;
        pendingAge_ = 0;
        lastSteer_ = handle;
        steerWait_ = 0;
    };
    const ItemEntry* it = focusedItem(s);
    if (it && it->slot >= 0 && it->slot < kCells && !it->detailsCurrent) {
        const bool again = it->handle == lastSteer_ && it->details.size() <= 1 && ++steerWait_ >= 30;
        if (it->handle != lastSteer_ || again) {
            send(it->slot, it->handle);
            return;
        }
    }
    for (const ItemEntry& e : s.items) {
        if (!e.equipped || e.details.size() > 1 || e.detailsCurrent || e.slot < 0 || e.slot >= kCells ||
            std::find(prefetched_.begin(), prefetched_.end(), e.handle) != prefetched_.end())
            continue;
        prefetched_.push_back(e.handle);
        send(e.slot, e.handle);
        return;
    }
}

FrameOutput StripScreen::frame(const Snapshot& s, const UiInput& in, const Viewport& vp) {
    FrameOutput out;
    if (!open_)
        return out;
    layout_ = layoutFor(vp);
    buildTargets(s);
    ensureFocus(s);
    buildHints(s);
    const TargetId before = focus_;
    applyInput(s, in, out);
    if (!(focus_ == before) && before.kind != TargetKind::None)
        out.sound = UiSound::Move;
    if (out.wantsClose) {
        close();
        closePending_ = true;
        return out;
    }
    buildTargets(s);
    ensureFocus(s);
    buildHints(s);
    if (s.native.valid)
        steerSelection(s, out);
    if (toastLeft_ > 0 && --toastLeft_ == 0)
        toast_.clear();
    draw(s, out.draw);
    return out;
}

// ---- drawing ----

void StripScreen::draw(const Snapshot& s, DrawList& d) const {
    const detail::Paint p(layout_.frame, cfg_);
    const Theme& th = cfg_.theme;
    const LayoutFrame& lf = layout_.frame;

    // No backdrop: the world stays visible. A low gradient grounds the bar and keeps the prompts readable.
    d.gradient(lf.band(kPlateY - 120, kDesignH - (kPlateY - 120)), withAlpha(th.backdrop, 0),
               withAlpha(th.backdropEdge, 170));

    // The plate is opaque: the native grid (black fill and 3D icons) lies under it.
    const Rect plate = p.map(design::strip::plate());
    d.rect(plate, rgba(14, 10, 7));
    d.panel(plate, PanelStyle::Main, th.panel);
    const Rect bar = design::strip::bar();
    // The crest on the plate's top edge above the grid, as the gem on Order of the Flame's inventory frame.
    p.crest(d, p.map(bar).cx(), plate.y, 30);
    if (!s.native.valid) {
        const char* msg = s.native.onDragon ? "The inventory can't be opened while riding Arokh."
                                            : "The inventory is not available right now.";
        const std::vector<std::string> lines = p.wrap(msg, bar.w - 64, FontRole::Body);
        float y = bar.cy() - static_cast<float>(lines.size()) * 17;
        for (const std::string& l : lines) {
            p.text(d, {bar.x + 32, y, bar.w - 64, 34}, l, FontRole::Body, th.textMuted, TextAlign::Center);
            y += 34;
        }
    } else {
        p.lattice(d, p.map({kGridX, kGridY, kGridW, kGridH}), p.px(kGap));
        for (int c = 0; c < kCells; ++c)
            d.panel(p.map(design::strip::cell(c)), PanelStyle::Inset, th.inset);
        const HitTarget* focusCell = focus_.kind == TargetKind::GridCell ? findTarget(focus_) : nullptr;
        if (focusCell) {
            const Rect& r = focusCell->rect;
            const float g = p.px(4);
            d.rect({r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g}, th.emberGlow, p.px(6));
            d.panel(r, PanelStyle::Card, th.panelRaised);
            d.panel(r, PanelStyle::Focus, th.select);
        }
        const float mark = p.fontPx(FontRole::Small, kMarkSize);
        for (const ItemEntry& it : s.items) {
            const Rect r = layout_.item(it.originCell, it.width, it.height);
            const bool focused = focusCell && focusCell->id.index == it.originCell;
            const bool hovered = hover_.kind == TargetKind::GridCell && hover_.index == it.originCell;
            if (!focused)
                d.panel(r, PanelStyle::Card, hovered ? th.panelRaised : withAlpha(th.panelRaised, 236));
            d.imageOr(r.inset(p.px(3)), {ImageKind::ItemIcon, uint32_t(it.slot)},
                      {ImageKind::CategoryIcon, uint32_t(it.category)}, 0xffffffffu, withAlpha(0xffffffffu, 90));
            if (it.equipped) {
                d.outline(r.inset(p.px(1.5f)), th.brass, p.px(1.5f), p.px(3));
                d.text({r.x + p.px(5), r.y + p.px(2), r.w, p.px(18)}, "E", FontRole::Small, mark, th.heading);
            }
            if (const int q = quickSlotNumber(s, it)) {
                const Rect pill{r.right() - p.px(19), r.y + p.px(3), p.px(16), p.px(16)};
                d.rect(pill, withAlpha(th.ember, 220), p.px(8));
                d.text(pill, fmtInt(q), FontRole::Small, p.fontPx(FontRole::Small, 12), th.text, TextAlign::Center);
            }
            const QuantityKind qk = quantityKindFor(it.category);
            if (it.quantityMax > 0 && qk == QuantityKind::Durability) {
                const float f =
                    std::clamp(static_cast<float>(it.quantity) / static_cast<float>(it.quantityMax), 0.f, 1.f);
                const Rect b{r.x + p.px(6), r.bottom() - p.px(8), r.w - p.px(12), p.px(3)};
                d.rect(b, th.inset);
                d.rect({b.x, b.y, b.w * f, b.h}, f < 0.25f ? th.bad : th.good);
            } else if (it.quantityMax > 0 && qk != QuantityKind::None) {
                d.text({r.x, r.bottom() - p.px(20), r.w - p.px(5), p.px(18)}, fmtInt(it.quantity), FontRole::Small,
                       mark, th.text, TextAlign::Right);
            }
        }
        if (focusCell)
            d.outline(focusCell->rect, th.select, p.px(1.75f), p.px(2));
    }

    drawCard(s, d);

    for (size_t i = 0; i < hints_.size() && i < hintRects_.size(); ++i) {
        const TargetId id{TargetKind::Hint, static_cast<int>(hints_[i].prompt)};
        p.drawHint(d, hints_[i], p.map(hintRects_[i]), hover_ == id && hints_[i].enabled, kHintGlyph, FontRole::Body,
                   kHintSize);
    }

    if (!toast_.empty()) {
        const float w = std::min(900.0f, p.textWidth(toast_, FontRole::Body) + 64);
        const Rect r{(kDesignW - w) * 0.5f, kPlateY - 56, w, 44};
        d.panel(p.map(r), PanelStyle::Card, th.panelRaised);
        d.outline(p.map(r), th.bad, p.px(2), p.px(6));
        p.text(d, r, toast_, FontRole::Body, th.text, TextAlign::Center);
    }
}

void StripScreen::drawCard(const Snapshot& s, DrawList& d) const {
    const detail::Paint p(layout_.frame, cfg_);
    const Theme& th = cfg_.theme;
    const Rect card = design::strip::card();
    // The card shares the plate: a hairline divides it from the grid.
    {
        const Rect div = p.map({card.x - kCardGap * 0.5f, card.y + 22, 0, card.h - 44});
        d.line({div.x + p.px(1), div.y}, {div.x + p.px(1), div.bottom()}, rgba(0, 0, 0, 150), p.px(1.5f));
        d.line({div.x, div.y}, {div.x, div.bottom()}, withAlpha(th.brassDim, 220), p.px(1.25f));
    }
    const float x = card.x + 18, w = card.w - 50; // the right margin clears the plate's frame
    const ItemEntry* it = s.native.valid ? focusedItem(s) : nullptr;
    if (!it) {
        p.text(d, {x, card.y + 18, w, 32}, s.native.valid ? "Empty slot" : "", FontRole::Body, th.textDisabled);
        return;
    }
    // Icon left of the name and kind, as the Tab screen's wide detail card.
    const Rect icon{x, card.y + 18, 64, 64};
    d.panel(p.map(icon), PanelStyle::Inset, th.inset);
    d.imageOr(p.map(icon.inset(3)), {ImageKind::ItemIcon, uint32_t(it->slot)},
              {ImageKind::CategoryIcon, uint32_t(it->category)}, 0xffffffffu, withAlpha(0xffffffffu, 90));
    const float tx = x + 78, tw = w - 78;
    p.text(d, {tx, card.y + 18, tw, 34}, it->name.empty() ? std::string("...") : it->name, FontRole::Heading,
           th.heading, TextAlign::Left, 26);
    std::string sub = categoryLabel(it->category);
    if (it->equipped)
        sub += "  \xC2\xB7  Equipped";
    if (it->hotSlot)
        sub += "  \xC2\xB7  Quick Slot";
    p.text(d, {tx, card.y + 54, tw, 26}, sub, FontRole::Small, it->equipped ? th.ember : th.textMuted, TextAlign::Left,
           17);
    float y = card.y + 96;
    p.rule(d, x, y, w);
    y += 10;

    const QuantityKind qk = quantityKindFor(it->category);
    const bool hasBar = it->quantityMax > 0 && qk != QuantityKind::None;
    const float barY = card.bottom() - 18 - 26;
    const float linesEnd = hasBar ? barY - 6 : card.bottom() - 14;
    constexpr float kLineH = 27;
    if (it->details.size() <= 1 && !it->detailsCurrent) {
        p.text(d, {x, y, w, kLineH}, "...", FontRole::Small, th.textMuted);
        y += kLineH;
    }
    for (size_t i = 1; i < it->details.size() && y + kLineH <= linesEnd; ++i) {
        std::string label, value;
        const std::string& line = it->details[i];
        const bool price = line.find("Price") != std::string::npos || line.find("Sell") != std::string::npos ||
                           line.find("Repair") != std::string::npos;
        if (price && !s.shop.active)
            continue;
        const bool split = splitStatLine(line, label, value);
        if (hasBar && split && label == detail::quantityLabelFor(qk))
            continue; // the bar below
        if (split) {
            p.text(d, {x, y, w, kLineH}, label, FontRole::Small, th.textMuted, TextAlign::Left, 18);
            p.text(d, {x, y, w, kLineH}, value, FontRole::Small, th.text, TextAlign::Right, 18);
        } else {
            p.text(d, {x, y, w, kLineH}, line, FontRole::Small, th.brass, TextAlign::Left, 18);
        }
        y += kLineH;
    }
    if (it->detailsCurrent && !it->requirementMet && y + kLineH <= linesEnd)
        p.text(d, {x, y, w, kLineH}, "Requirement not met", FontRole::Small, th.bad, TextAlign::Left, 18);
    if (hasBar)
        p.bar(d, {x, barY, w, 26}, it->quantity, it->quantityMax,
              qk == QuantityKind::Durability ? th.good : (qk == QuantityKind::Ammo ? th.xp : th.mana),
              detail::quantityLabelFor(qk), 16);
}

} // namespace gameui
