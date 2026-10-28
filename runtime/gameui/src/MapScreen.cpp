#include "gameui/MapScreen.h"

#include <algorithm>

#include "Paint.h"

namespace gameui {

namespace {

// Design units of a 1080-high canvas anchored at the viewport's right edge.
constexpr float kPanelW = 540, kPanelMargin = 40;
constexpr float kPanelX = kDesignW - kPanelMargin - kPanelW;
constexpr float kPanelY = 48, kPanelH = kDesignH - 2 * kPanelY;
constexpr float kRowsTop = kPanelY + 168;
constexpr float kRowsBottom = kPanelY + kPanelH - 84;
constexpr float kRowX = kPanelX + 16, kRowW = kPanelW - 32;
constexpr float kTextX = 40, kPinW = 44; // inside a row: status glyph column, marker pin column
constexpr float kLineH = 30, kTextSize = 23, kRowPad = 18;
constexpr int kMaxLines = 3;
constexpr float kHeadingH = 46, kPlaceH = 48;
constexpr float kHintY = kPanelY + kPanelH - 62, kHintH = 40, kHintGlyph = 32, kHintGap = 26, kHintSize = 22;
constexpr float kRing = 24; // marker ring radius
constexpr float kMinDesignW = 1440;

Rect panelDesign() {
    return {kPanelX, kPanelY, kPanelW, kPanelH};
}

LayoutFrame frameFor(const Viewport& vp) {
    return detail::anchoredFrame(vp, kMinDesignW, detail::AnchorX::Right, false);
}

} // namespace

MapScreen::MapScreen(ScreenConfig cfg) : cfg_(std::move(cfg)) {}

Rect MapScreen::panelRectFor(const Viewport& vp) {
    return frameFor(vp).map(panelDesign());
}

Vec2 MapScreen::markerToScreen(const QuestEntry& q, const Rect& image, float imageW, float imageH) {
    if (imageW <= 0 || imageH <= 0)
        return {image.cx(), image.cy()};
    return {image.x + static_cast<float>(q.mapX) / imageW * image.w,
            image.y + static_cast<float>(q.mapY) / imageH * image.h};
}

void MapScreen::open(const Snapshot& s, int quest) {
    (void)s;
    open_ = true;
    initialQuest_ = quest;
    focus_ = hover_ = {};
    scroll_ = 0;
    lastMarkerSteer_ = -1;
}

void MapScreen::close() {
    open_ = false;
    targets_.clear();
    hints_.clear();
    hintRects_.clear();
}

const HitTarget* MapScreen::findTarget(const TargetId& id) const {
    for (const HitTarget& t : targets_)
        if (t.id == id)
            return &t;
    return nullptr;
}

const MapMarker* MapScreen::markerFor(int quest) const {
    for (const MapMarker& m : markers_)
        if (m.quest == quest)
            return &m;
    return nullptr;
}

const MapScreen::Row* MapScreen::rowFor(int quest) const {
    for (const Row& r : rows_)
        if (r.kind != Row::Heading && r.quest == quest)
            return &r;
    return nullptr;
}

void MapScreen::buildRows(const Snapshot& s) {
    rows_.clear();
    const detail::Paint p(lf_, cfg_);
    const float textW = kRowW - kTextX - kPinW;
    float y = 0;
    auto heading = [&](const char* label) {
        Row r;
        r.kind = Row::Heading;
        r.label = label;
        r.y = y;
        r.h = kHeadingH;
        y += r.h;
        rows_.push_back(std::move(r));
    };
    const std::vector<const QuestEntry*> objectives = orderedObjectives(s, true);
    for (int optional = 0; optional < 2; ++optional) {
        bool first = true;
        for (const QuestEntry* q : objectives) {
            if (q->optional != (optional == 1))
                continue;
            if (first) {
                heading(optional ? "Optional Objectives" : "Main Objectives");
                first = false;
            }
            Row r;
            r.kind = Row::Objective;
            r.quest = q->index;
            r.done = q->done();
            r.optional = q->optional;
            r.label = objectiveLabel(s, *q, location_);
            r.lines = p.wrap(r.label, textW, FontRole::Body, kTextSize);
            if (r.lines.size() > kMaxLines) {
                r.lines.resize(kMaxLines);
                r.lines.back() += "...";
            }
            r.y = y;
            r.h = static_cast<float>(std::max<size_t>(r.lines.size(), 1)) * kLineH + kRowPad;
            y += r.h + 6;
            rows_.push_back(std::move(r));
        }
    }
    // Places: enabled markers that are not journal entries (their caption names them).
    bool first = true;
    int n = 0;
    for (const QuestEntry& q : s.quests) {
        if (!q.marker() || !q.enabled || q.listed())
            continue;
        ++n;
        if (first) {
            heading("Places");
            first = false;
        }
        Row r;
        r.kind = Row::Place;
        r.quest = q.index;
        r.label = q.caption.empty() ? "Location " + detail::fmtInt(n) : q.caption;
        r.y = y;
        r.h = kPlaceH;
        y += r.h + 6;
        rows_.push_back(std::move(r));
    }
}

void MapScreen::buildTargets() {
    targets_.clear();
    for (const Row& r : rows_) {
        if (r.kind == Row::Heading)
            continue;
        const Rect d{kRowX, kRowsTop + r.y - scroll_, kRowW, r.h};
        targets_.push_back({{TargetKind::Quest, r.quest}, lf_.map(d), true, true});
    }
    const float rad = lf_.px(kRing);
    for (const MapMarker& m : markers_)
        targets_.push_back({{TargetKind::Marker, m.quest}, {m.x - rad, m.y - rad, 2 * rad, 2 * rad}, true, true});
}

void MapScreen::ensureFocus() {
    if (focus_.kind != TargetKind::None && findTarget(focus_))
        return;
    focus_ = {};
    if (initialQuest_ >= 0) {
        if (findTarget({TargetKind::Quest, initialQuest_}))
            focus_ = {TargetKind::Quest, initialQuest_};
        else if (findTarget({TargetKind::Marker, initialQuest_}))
            focus_ = {TargetKind::Marker, initialQuest_};
        initialQuest_ = -1;
        if (focus_.kind != TargetKind::None)
            return;
    }
    if (!targets_.empty())
        focus_ = targets_.front().id; // rows first (open main objectives lead), then markers
}

void MapScreen::buildHints() {
    hints_.clear();
    const bool onRow = focus_.kind == TargetKind::Quest;
    const bool linked = onRow ? markerFor(focus_.index) != nullptr
                              : (focus_.kind == TargetKind::Marker && rowFor(focus_.index) != nullptr);
    hints_.push_back({Prompt::Confirm, focus_.kind == TargetKind::Marker ? "Show in List" : "Show on Map", linked});
    hints_.push_back({Prompt::Back, "Close"});
    const detail::Paint p(lf_, cfg_);
    hintRects_ = p.layoutHints(hints_, kPanelX + kPanelW - 28, false, kHintY, kHintH, kHintGlyph, kHintGap,
                               FontRole::Body, kHintSize);
    for (size_t i = 0; i < hints_.size(); ++i)
        targets_.push_back(
            {{TargetKind::Hint, static_cast<int>(hints_[i].prompt)}, lf_.map(hintRects_[i]), false, hints_[i].enabled});
}

void MapScreen::press(Prompt pr, FrameOutput& out) {
    switch (pr) {
        case Prompt::Confirm:
            // Jump between an objective and its marker.
            if (focus_.kind == TargetKind::Quest && markerFor(focus_.index))
                focus_ = {TargetKind::Marker, focus_.index};
            else if (focus_.kind == TargetKind::Marker && rowFor(focus_.index))
                focus_ = {TargetKind::Quest, focus_.index};
            break;
        case Prompt::Back:
            out.wantsClose = true;
            break;
        default:
            break;
    }
}

void MapScreen::applyInput(const UiInput& in, FrameOutput& out) {
    hover_ = {};
    if (in.pointerValid) {
        const Rect rows = lf_.map({kRowX, kRowsTop, kRowW, kRowsBottom - kRowsTop});
        for (size_t i = targets_.size(); i-- > 0;) {
            const HitTarget& t = targets_[i];
            if (!t.rect.contains(in.pointerX, in.pointerY))
                continue;
            if (t.id.kind == TargetKind::Quest && !rows.contains(in.pointerX, in.pointerY))
                continue; // scrolled out of the list
            hover_ = t.id;
            if (in.pointerMoved && t.navigable)
                focus_ = t.id;
            break;
        }
    }
    if (in.rightClick) {
        press(Prompt::Back, out);
        return;
    }
    if (in.leftClick && hover_.kind != TargetKind::None) {
        const HitTarget* t = findTarget(hover_);
        if (t && t->enabled) {
            if (hover_.kind == TargetKind::Hint)
                press(static_cast<Prompt>(hover_.index), out);
            else
                focus_ = hover_;
        }
    }
    std::vector<UiKey> keys = in.keys;
    if (in.wheel != 0)
        keys.push_back(in.wheel > 0 ? UiKey::Up : UiKey::Down);
    for (UiKey k : keys) {
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
                press(Prompt::Confirm, out);
                break;
            case UiKey::Back:
            case UiKey::Close:
                press(Prompt::Back, out);
                break;
            default:
                break;
        }
        if (dx == 0 && dy == 0)
            continue;
        const HitTarget* cur = findTarget(focus_);
        if (!cur) {
            ensureFocus();
            continue;
        }
        // One spatial model: rows and markers are all targets, so Left from the list reaches the map's markers and
        // Right from the rightmost marker comes back to the list.
        const int next = InventoryScreen::pickDirectional(targets_, cur->rect, dx, dy);
        if (next >= 0)
            focus_ = targets_[static_cast<size_t>(next)].id;
    }
}

FrameOutput MapScreen::frame(const Snapshot& s, const MapInput& m, const UiInput& in, const Viewport& vp) {
    FrameOutput out;
    if (!open_)
        return out;
    lf_ = frameFor(vp);
    panelPx_ = lf_.map(panelDesign());
    location_ = locationName(s, m.location);
    markers_.clear();
    for (const MapMarker& mk : m.markers) {
        const bool known =
            std::any_of(s.quests.begin(), s.quests.end(), [&](const QuestEntry& q) { return q.index == mk.quest; });
        if (known)
            markers_.push_back(mk);
    }
    buildRows(s);
    buildTargets();
    ensureFocus();
    buildHints();
    const TargetId before = focus_;
    applyInput(in, out);
    if (!(focus_ == before) && before.kind != TargetKind::None)
        out.sound = UiSound::Move;
    if (out.wantsClose) {
        close();
        return out;
    }
    // Keep the focused row (or the row linked to the focused marker) inside the list.
    if (const Row* r = rowFor(focus_.index); r && focus_.kind != TargetKind::None) {
        const float area = kRowsBottom - kRowsTop;
        float top = r->y;
        // show the section heading above the first row of a section
        for (size_t i = 0; i < rows_.size(); ++i)
            if (&rows_[i] == r && i > 0 && rows_[i - 1].kind == Row::Heading)
                top = rows_[i - 1].y;
        if (top < scroll_)
            scroll_ = top;
        if (r->y + r->h > scroll_ + area)
            scroll_ = r->y + r->h - area;
    }
    const float content = rows_.empty() ? 0 : rows_.back().y + rows_.back().h;
    scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content - (kRowsBottom - kRowsTop)));
    buildTargets();
    ensureFocus();
    buildHints();
    // The game's map selection follows the focused record (row or marker) when it has a marker on this map: the
    // native selection state (and caption) then name the same place as the panel. One action per focus change; the
    // queue keeps only the newest.
    const int focused = focus_.kind == TargetKind::Quest || focus_.kind == TargetKind::Marker ? focus_.index : -1;
    if (focused >= 0 && markerFor(focused) && focused != lastMarkerSteer_ && s.native.section == 3 &&
        s.native.mapSelection >= 0) {
        lastMarkerSteer_ = focused;
        if (focused != s.native.mapSelection)
            out.actions.push_back({0, ActionKind::SelectMarker, focused, 0});
    }
    draw(s, out.draw);
    return out;
}

// ---- drawing ----

void MapScreen::draw(const Snapshot& s, DrawList& d) const {
    (void)s;
    const detail::Paint p(lf_, cfg_);
    const Theme& th = cfg_.theme;
    const int sel = selectedQuest();

    // On the map: a ring on the selected record's marker (with its name), a faint one under the pointer.
    for (const MapMarker& m : markers_) {
        const float r = p.px(kRing);
        const bool selected = m.quest == sel;
        const bool hovered = hover_.kind == TargetKind::Marker && hover_.index == m.quest;
        if (selected) {
            const float g = p.px(8);
            d.rect({m.x - r - g, m.y - r - g, 2 * (r + g), 2 * (r + g)}, th.emberGlow, r + g);
            d.outline({m.x - r - p.px(2), m.y - r - p.px(2), 2 * r + p.px(4), 2 * r + p.px(4)},
                      withAlpha(th.ivory, 200), p.px(1.5f), r + p.px(2));
            d.outline({m.x - r, m.y - r, 2 * r, 2 * r}, th.select, p.px(2.5f), r);
            d.outline({m.x - r - p.px(5), m.y - r - p.px(5), 2 * r + p.px(10), 2 * r + p.px(10)},
                      withAlpha(th.heading, 150), p.px(1.5f), r + p.px(5));
            const Row* row = rowFor(m.quest);
            const std::string label = row ? row->label : std::string();
            if (!label.empty()) {
                const float w = std::min(420.0f, p.textWidth(label, FontRole::Small) + 28);
                const float h = 38;
                Rect tag{m.x + r + p.px(12), m.y - p.px(h * 0.5f), p.px(w), p.px(h)};
                if (tag.right() > panelPx_.x - p.px(8))
                    tag.x = m.x - r - p.px(12) - tag.w;
                tag.x = std::max(tag.x, lf_.view.x + p.px(8));
                d.panel(tag, PanelStyle::Card, withAlpha(th.panelRaised, 240));
                d.rect({tag.x, tag.y + p.px(6), p.px(3), tag.h - p.px(12)}, th.select);
                d.text({tag.x + p.px(14), tag.y, tag.w - p.px(20), tag.h},
                       ellipsize(label, tag.w - p.px(20), FontRole::Small, p.fontPx(FontRole::Small), cfg_.measure),
                       FontRole::Small, p.fontPx(FontRole::Small), th.text);
            }
        } else if (hovered) {
            d.outline({m.x - r, m.y - r, 2 * r, 2 * r}, th.brass, p.px(2), r);
        }
    }

    // The panel: slightly more opaque than the Tab screen's, it sits on bright parchment.
    const Rect pd = panelDesign();
    d.panel(p.map(pd), PanelStyle::Main, withAlpha(th.panel, 248));
    const float x = pd.x + 32, w = pd.w - 64;
    p.crest(d, p.map(pd).cx(), p.map(pd).y, 30);
    p.label(d, {x, pd.y + 22, w, 28}, "Location", th.brass);
    p.text(d, {x, pd.y + 50, w, 56}, location_.empty() ? std::string("Map") : location_, FontRole::Title, th.heading);
    p.rule(d, x, pd.y + 128, w);

    d.pushClip(p.map({kRowX - 8, kRowsTop - 4, kRowW + 16, kRowsBottom - kRowsTop + 8}));
    if (rows_.empty())
        p.text(d, {x, kRowsTop + 8, w, 34}, "No objectives yet.", FontRole::Body, th.textDisabled);
    for (const Row& r : rows_) {
        const float y = kRowsTop + r.y - scroll_;
        if (y + r.h < kRowsTop - 4 || y > kRowsBottom + 4)
            continue;
        if (r.kind == Row::Heading) {
            p.label(d, {kRowX + 8, y + 12, kRowW - 16, r.h - 14}, r.label, th.brass);
            continue;
        }
        const Rect rd{kRowX, y, kRowW, r.h};
        const Rect rp = p.map(rd);
        const bool focused = focus_.kind == TargetKind::Quest && focus_.index == r.quest;
        const bool linked = !focused && r.quest == sel; // its marker has the focus
        const bool hovered = hover_.kind == TargetKind::Quest && hover_.index == r.quest;
        d.panel(rp, PanelStyle::Card, linked || hovered ? th.panelRaised : withAlpha(th.panel, 190));
        if (focused)
            p.focus(d, rp);
        if (linked)
            d.rect(p.map({rd.x, rd.y + 8, 3, rd.h - 16}), th.select);

        const Color tc = r.done ? th.textDisabled : (focused ? th.heading : th.text);
        if (r.kind == Row::Objective) {
            // Status box: hollow while open, filled once completed.
            const Rect box{rd.x + 14, y + kRowPad * 0.5f + (kLineH - 14) * 0.5f, 14, 14};
            if (r.done)
                d.rect(p.map(box), th.brassDim, p.px(2));
            else
                d.outline(p.map(box), r.optional ? th.brassDim : th.brass, p.px(2), p.px(2));
            float ly = y + kRowPad * 0.5f;
            for (const std::string& line : r.lines) {
                const Rect lr{rd.x + kTextX, ly, kRowW - kTextX - kPinW, kLineH};
                p.text(d, lr, line, FontRole::Body, tc, TextAlign::Left, kTextSize);
                if (r.done)
                    p.strike(d, lr, line, FontRole::Body, withAlpha(th.textMuted, 220), TextAlign::Left, kTextSize);
                ly += kLineH;
            }
        } else {
            p.text(d, {rd.x + kTextX, y, kRowW - kTextX - kPinW, r.h}, r.label, FontRole::Body,
                   focused ? th.heading : th.text, TextAlign::Left, kTextSize);
        }
        // Map pin: the record has a marker on the visible map.
        if (markerFor(r.quest)) {
            const float pr = 9;
            const Rect pin{rd.right() - kPinW * 0.5f - pr, y + kRowPad * 0.5f + kLineH * 0.5f - pr, 2 * pr, 2 * pr};
            const bool lit = r.quest == sel;
            d.outline(p.map(pin), lit ? th.ember : th.brassDim, p.px(2), p.px(pr));
            d.rect(p.map(pin.inset(5)), lit ? th.ember : th.brassDim, p.px(pr - 5));
        }
    }
    d.popClip();
    // Scroll cues.
    const float content = rows_.empty() ? 0 : rows_.back().y + rows_.back().h;
    if (scroll_ > 0)
        d.gradient(p.map({kRowX, kRowsTop - 4, kRowW, 24}), withAlpha(th.panel, 240), withAlpha(th.panel, 0));
    if (content - scroll_ > kRowsBottom - kRowsTop)
        d.gradient(p.map({kRowX, kRowsBottom - 20, kRowW, 24}), withAlpha(th.panel, 0), withAlpha(th.panel, 240));

    for (size_t i = 0; i < hints_.size() && i < hintRects_.size(); ++i) {
        const TargetId id{TargetKind::Hint, static_cast<int>(hints_[i].prompt)};
        p.drawHint(d, hints_[i], p.map(hintRects_[i]), hover_ == id && hints_[i].enabled, kHintGlyph, FontRole::Body,
                   kHintSize);
    }
}

} // namespace gameui
