// Unit tests for the strip ("I"), map ("M") and pause (Esc) screens of drakan::gameui, plus the reader / model
// additions they use. Synthetic guest only (ScreenFixture.h), no window.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "ScreenFixture.h"
#include "gameui/MapScreen.h"
#include "gameui/PauseScreen.h"
#include "gameui/StripScreen.h"

using namespace gameui;
using gameui::test::ScreenWorld;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

namespace {

const Viewport kAspects[] = {{0, 0, 1440, 1080}, {0, 0, 1920, 1080}, {0, 0, 2560, 1080},
                             {0, 0, 3840, 2160}, {0, 0, 1024, 768},  {100, 50, 1600, 900}};

UiInput keys(std::initializer_list<UiKey> k) {
    UiInput in;
    in.keys = k;
    return in;
}

UiInput pointerAt(const Rect& r, bool click = false, bool dbl = false) {
    UiInput in;
    in.pointerValid = true;
    in.pointerMoved = true;
    in.pointerX = r.cx();
    in.pointerY = r.cy();
    in.leftClick = click;
    in.doubleClick = dbl;
    return in;
}

bool near(float a, float b, float eps = 0.75f) {
    return std::fabs(a - b) <= eps;
}

bool inside(const Rect& inner, const Viewport& vp) {
    return inner.x >= vp.x - 0.5f && inner.y >= vp.y - 0.5f && inner.right() <= vp.x + vp.w + 0.5f &&
           inner.bottom() <= vp.y + vp.h + 0.5f;
}

bool inside(const Rect& inner, const Rect& outer) {
    return inside(inner, Viewport{outer.x, outer.y, outer.w, outer.h});
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool hasText(const DrawList& d, const std::string& s) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Text && c.text == s)
            return true;
    return false;
}

int count(const DrawList& d, DrawKind k) {
    int n = 0;
    for (const DrawCmd& c : d.commands())
        n += c.kind == k;
    return n;
}

const UiAction* findAction(const FrameOutput& o, ActionKind k) {
    for (const UiAction& a : o.actions)
        if (a.kind == k)
            return &a;
    return nullptr;
}

const HitTarget* target(const std::vector<HitTarget>& ts, TargetKind k, int index) {
    for (const HitTarget& t : ts)
        if (t.id.kind == k && t.id.index == index)
            return &t;
    return nullptr;
}

const Hint* hint(const std::vector<Hint>& hs, Prompt p) {
    for (const Hint& h : hs)
        if (h.prompt == p)
            return &h;
    return nullptr;
}

// ---- reader / model additions ----

void testReaderQuests() {
    ScreenWorld w;
    Snapshot s = w.read();
    CHECK(s.quests.size() == 7);
    CHECK(s.quests[0].textResident && s.quests[0].captionResident && s.quests[0].caption == "The Palace");
    CHECK(s.quests[1].done() && s.quests[1].listed() && !s.quests[1].captionResident);
    CHECK(!s.quests[2].textResident && s.quests[2].text.empty() && s.quests[2].optional);
    CHECK(s.mapRegion == -1); // no map section in the fixture
    CHECK(currentRegion(s) == 0 && locationName(s) == "Surdana" && locationName(s, "Level 1A") == "Level 1A");

    // Display order: main open, main done, optional open...
    const std::vector<const QuestEntry*> o = orderedObjectives(s, true);
    CHECK(o.size() == 4 && o[0]->index == 0 && o[1]->index == 1 && o[2]->index == 2 && o[3]->index == 3);
    const std::vector<const QuestEntry*> open = orderedObjectives(s, false);
    CHECK(open.size() == 3 && open[1]->index == 2);
    // Fallbacks: text, caption, region name, then the location given.
    CHECK(objectiveLabel(s, s.quests[0], "x") == "Find Lady Myschala at the palace in Surdana.");
    CHECK(objectiveLabel(s, s.quests[2], "x") == "The Old Mill");
    CHECK(objectiveLabel(s, s.quests[3], "x") == "Surdana");
    QuestEntry orphan = s.quests[3];
    orphan.region = 9;
    CHECK(objectiveLabel(s, orphan, "Level 1A") == "Level 1A");

    // The native map section's region, when the inventory screen has its five sections.
    const uint32_t sections = w.g.alloc(0x20), map = w.g.alloc(0x100);
    w.g.w32(w.g.screen + addr::screen::kSections, sections);
    w.g.w32(w.g.screen + addr::screen::kSectionCount, 5);
    w.g.w32(sections + addr::section::kMapSectionIndex * 4, map);
    w.g.w32(map + addr::section::kMapRegion, 4);
    w.g.setRegion(4, "Wartok Canyon");
    s = w.read();
    CHECK(s.mapRegion == 4 && currentRegion(s) == 4 && locationName(s) == "Wartok Canyon");
    w.g.w32(map + addr::section::kMapRegion, 77); // out of range: ignored
    s = w.read();
    CHECK(s.mapRegion == -1);

    // On the dragon the quests are still read (the pause / map screens show them).
    const uint32_t dragon = w.g.alloc(0x100);
    w.g.w16(dragon + addr::actor::kResIndex, static_cast<uint16_t>(w.g.resource(addr::cls::kDragon, w.g.props())));
    w.g.w32(gameui::test::FakeGame::kCtx + addr::ctx::kPlayerActor, dragon);
    s = w.read();
    CHECK(s.native.onDragon && !s.native.valid && s.items.empty() && s.quests.size() == 7 && !s.regions.empty());
}

// ---- strip ----

void testStripLayout() {
    for (const Viewport& vp : kAspects) {
        const StripScreen::Layout l = StripScreen::layoutFor(vp);
        const Rect c0 = l.cell(0), c23 = l.cell(23);
        // Sized by the screen height: 0.05 .. 0.06 H per cell (4:3 and narrower shrink only slightly).
        CHECK(c0.h / vp.h >= 0.05f && c0.h / vp.h <= 0.06f);
        CHECK(near(c0.w, c0.h));
        // The plate (grid + card) centred horizontally, flush with the bottom (a small margin), entirely on screen,
        // card beside the bar.
        CHECK(near(l.plate.cx(), vp.x + vp.w * 0.5f));
        const float margin = vp.y + vp.h - l.bar.bottom();
        CHECK(margin > 0 && margin / vp.h < 0.02f);
        CHECK(inside(l.bounds(), vp));
        CHECK(l.card.x >= l.bar.right() && near(l.card.bottom(), l.bar.bottom()));
        CHECK(l.hints.bottom() <= l.bar.y && !overlaps(l.hints, l.card));
        CHECK(c0.x > l.bar.x && c23.right() < l.bar.right() && c23.bottom() < l.bar.bottom());
        // One plate holds it all; the native grid (icon crops) lies under it, well above the host cells' size.
        CHECK(inside(l.bar, l.plate) && inside(l.card, l.plate) && inside(l.hints, l.plate));
        CHECK(inside(l.nativeGrid, l.plate) && l.nativeGrid.w > (c23.right() - c0.x) * 1.4f);
        // Leaves the world above visible: the strip uses the bottom third at most.
        CHECK(l.bounds().y - vp.y > vp.h * 0.66f);
    }
    // Same proportions on 16:9 at two resolutions.
    const StripScreen::Layout a = StripScreen::layoutFor({0, 0, 1920, 1080}),
                              b = StripScreen::layoutFor({0, 0, 3840, 2160});
    CHECK(near(a.cell(5).x * 2, b.cell(5).x, 1.5f) && near(a.card.w * 2, b.card.w, 1.5f));
}

void testStripNavigationAndActions() {
    ScreenWorld w;
    ReaderState st;
    Snapshot s = w.read(st);
    StripScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    scr.open(s);
    FrameOutput o = scr.frame(s, {}, vp);
    CHECK(scr.isOpen() && scr.focus() == (TargetId{TargetKind::GridCell, 0}));
    // No full-screen backdrop: the world stays visible; a gradient sits behind the bar.
    bool backdrop = false;
    for (const DrawCmd& c : o.draw.commands())
        backdrop |= c.kind == DrawKind::Panel && c.panel == PanelStyle::Backdrop;
    CHECK(!backdrop && count(o.draw, DrawKind::Gradient) >= 1 && count(o.draw, DrawKind::WorldView) == 0);
    // Item icons through the same ImageRef crops as the Tab screen (by slot), with the category fallback.
    int icons = 0;
    for (const DrawCmd& c : o.draw.commands())
        if (c.kind == DrawKind::Image && c.image.kind == ImageKind::ItemIcon &&
            c.fallback.kind == ImageKind::CategoryIcon)
            ++icons;
    CHECK(icons >= static_cast<int>(s.items.size()));
    // Equipped mark and quick-slot numbers (elixir 1, arrows 2).
    CHECK(hasText(o.draw, "E") && hasText(o.draw, "1") && hasText(o.draw, "2"));
    // The dagger is equipped and cannot be dropped: Equip and Drop disabled, focus steers the native selection.
    CHECK(hint(scr.hints(), Prompt::Confirm) && !hint(scr.hints(), Prompt::Confirm)->enabled);
    CHECK(hint(scr.hints(), Prompt::Secondary) && !hint(scr.hints(), Prompt::Secondary)->enabled);
    CHECK(hint(scr.hints(), Prompt::Back) && hint(scr.hints(), Prompt::Back)->label == "Close");
    o = scr.frame(s, keys({UiKey::Confirm, UiKey::Secondary}), vp);
    CHECK(!findAction(o, ActionKind::UseOrEquip) && !findAction(o, ActionKind::Drop));

    // Right: the bow (1x2 in cell 1); Down from the bow skips its own footprint to row 2.
    o = scr.frame(s, keys({UiKey::Right}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 1}) && o.sound == UiSound::Move);
    // The bow's details are current: no SelectItem needed; its card shows its lines and the quantity bar.
    CHECK(!findAction(o, ActionKind::SelectItem));
    CHECK(hasText(o.draw, "Short Wood Bow") && hasText(o.draw, "Damage") && hasText(o.draw, "Arrows") &&
          hasText(o.draw, "181 / 200"));
    o = scr.frame(s, keys({UiKey::Down}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 17}));
    // One selection steer in flight at a time: the dagger's (sent on open) finishes first.
    scr.onResult({1, ActionKind::SelectItem, 0, Outcome::Confirmed});
    o = scr.frame(s, keys({UiKey::Up, UiKey::Right}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 2}));
    // The club has no details yet: focus steers the native selection to it.
    const UiAction* sel = findAction(o, ActionKind::SelectItem);
    CHECK(sel && sel->target == 2 && sel->handle == w.club);
    // Actions carry the item handle.
    o = scr.frame(s, keys({UiKey::Confirm}), vp);
    const UiAction* eq = findAction(o, ActionKind::UseOrEquip);
    CHECK(eq && eq->target == 2 && eq->handle == w.club);
    o = scr.frame(s, keys({UiKey::Secondary}), vp);
    CHECK(findAction(o, ActionKind::Drop) && findAction(o, ActionKind::Drop)->handle == w.club);
    o = scr.frame(s, keys({UiKey::Tertiary}), vp);
    CHECK(findAction(o, ActionKind::ToggleHotSlot) && findAction(o, ActionKind::ToggleHotSlot)->target == 2);
    // No pages or filters on the strip.
    o = scr.frame(s, keys({UiKey::PageNext, UiKey::FilterNext}), vp);
    CHECK(o.actions.empty() || (o.actions.size() == 1 && o.actions[0].kind == ActionKind::SelectItem));
    CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 2}));

    // A refusal shows a toast.
    scr.onResult({7, ActionKind::Drop, 2, Outcome::Refused});
    o = scr.frame(s, {}, vp);
    CHECK(!scr.toast().empty() && hasText(o.draw, scr.toast()));

    // Back closes; a refused Close reopens the strip where it was.
    o = scr.frame(s, keys({UiKey::Back}), vp);
    CHECK(o.wantsClose && findAction(o, ActionKind::Close) && !scr.isOpen() && scr.closePending());
    scr.onResult({8, ActionKind::Close, -1, Outcome::Refused});
    CHECK(scr.isOpen() && !scr.closePending());
    o = scr.frame(s, {}, vp);
    CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 2}));
    // Close (I / Tab / Select) closes immediately too.
    o = scr.frame(s, keys({UiKey::Close}), vp);
    CHECK(o.wantsClose && !scr.isOpen());
}

void testStripMouse() {
    ScreenWorld w;
    Snapshot s = w.read();
    for (const Viewport& vp : {Viewport{0, 0, 1920, 1080}, Viewport{0, 0, 2560, 1080}, Viewport{0, 0, 1440, 1080}}) {
        StripScreen scr;
        scr.open(s);
        scr.frame(s, {}, vp);
        const StripScreen::Layout l = StripScreen::layoutFor(vp);
        // Hover a continuation cell of the armour (2x2 at 5): focus goes to the item's origin.
        FrameOutput o = scr.frame(s, pointerAt(l.cell(14)), vp);
        CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 5}));
        CHECK(scr.hover() == (TargetId{TargetKind::GridCell, 5}));
        // A single click selects, a double click equips.
        o = scr.frame(s, pointerAt(l.cell(5), true), vp);
        CHECK(!findAction(o, ActionKind::UseOrEquip));
        o = scr.frame(s, pointerAt(l.cell(5), true, true), vp);
        CHECK(findAction(o, ActionKind::UseOrEquip) && findAction(o, ActionKind::UseOrEquip)->handle == w.armour);
        // Empty cells are targets too.
        o = scr.frame(s, pointerAt(l.cell(23)), vp);
        CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 23}));
        CHECK(hint(scr.hints(), Prompt::Confirm) && !hint(scr.hints(), Prompt::Confirm)->enabled);
        o = scr.frame(s, keys({UiKey::Confirm}), vp);
        CHECK(o.actions.empty());
        // The Close prompt is clickable; disabled prompts are not.
        const HitTarget* drop = target(scr.targets(), TargetKind::Hint, static_cast<int>(Prompt::Secondary));
        CHECK(drop && !drop->enabled);
        o = scr.frame(s, pointerAt(drop->rect, true), vp);
        CHECK(!findAction(o, ActionKind::Drop));
        const HitTarget* close = target(scr.targets(), TargetKind::Hint, static_cast<int>(Prompt::Back));
        CHECK(close && inside(close->rect, vp));
        o = scr.frame(s, pointerAt(close->rect, true), vp);
        CHECK(o.wantsClose && findAction(o, ActionKind::Close));
    }
    // Right click closes (native parity).
    StripScreen scr;
    scr.open(s);
    UiInput rc;
    rc.rightClick = true;
    FrameOutput o = scr.frame(s, rc, {0, 0, 1920, 1080});
    CHECK(o.wantsClose);
}

void testStripGuards() {
    ScreenWorld w;
    Snapshot s = w.read();
    StripScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    // In a shop the quick slot is unavailable.
    s.shop.active = true;
    scr.open(s);
    scr.frame(s, keys({UiKey::Right, UiKey::Right}), vp);
    CHECK(hint(scr.hints(), Prompt::Tertiary) && !hint(scr.hints(), Prompt::Tertiary)->enabled);
    FrameOutput o = scr.frame(s, keys({UiKey::Tertiary}), vp);
    CHECK(!findAction(o, ActionKind::ToggleHotSlot));
    // Not available (dragon): a message, no grid targets, Back still closes.
    Snapshot bad;
    bad.native.onDragon = true;
    scr.open(bad);
    o = scr.frame(bad, {}, vp);
    std::string msg;
    for (const DrawCmd& c : o.draw.commands())
        if (c.kind == DrawKind::Text)
            msg += c.text + " ";
    CHECK(msg.find("riding Arokh.") != std::string::npos && msg.find("...") == std::string::npos);
    CHECK(!target(scr.targets(), TargetKind::GridCell, 0) && o.actions.empty());
    o = scr.frame(bad, keys({UiKey::Back}), vp);
    CHECK(o.wantsClose);
    // Names not resident: the card falls back to "..." (the scroll in cell 16).
    scr.open(s);
    s.shop.active = false;
    scr.frame(s, {}, vp);
    o = scr.frame(s, pointerAt(StripScreen::layoutFor(vp).cell(16)), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::GridCell, 16}) && hasText(o.draw, "..."));
}

// ---- map ----

MapInput markersFor(const Snapshot& s, const Rect& image) {
    MapInput m;
    for (const QuestEntry& q : s.quests)
        if (q.marker() && q.enabled) {
            const Vec2 p = MapScreen::markerToScreen(q, image, 512, 512);
            m.markers.push_back({q.index, p.x, p.y});
        }
    return m;
}

void testMapRowsAndFallbacks() {
    ScreenWorld w;
    Snapshot s = w.read();
    MapScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    const Rect image{100, 40, 1000, 1000};
    scr.open(s);
    FrameOutput o = scr.frame(s, markersFor(s, image), {}, vp);
    // Location, the two sections, the struck-through completed objective, the fallbacks.
    CHECK(hasText(o.draw, "Surdana") && hasText(o.draw, "MAIN OBJECTIVES") && hasText(o.draw, "OPTIONAL OBJECTIVES"));
    CHECK(hasText(o.draw, "PLACES") && hasText(o.draw, "The Old Mill") && hasText(o.draw, "Surdana Gate"));
    CHECK(hasText(o.draw, "Location 2")); // caption not resident
    CHECK(hasText(o.draw, "Escape from the Wartok camp."));
    int strikes = 0;
    for (const DrawCmd& c : o.draw.commands())
        strikes += c.kind == DrawKind::Line && c.rect.h == 0 && c.rect.w > 0 && c.color != 0;
    CHECK(strikes >= 1);
    // First focus: the first open main objective; its marker is ringed.
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 0}) && scr.selectedQuest() == 0);
    // Disabled markers get no target; quests not in the snapshot are ignored.
    CHECK(!target(scr.targets(), TargetKind::Marker, 6) && target(scr.targets(), TargetKind::Marker, 4));
    MapInput extra = markersFor(s, image);
    extra.markers.push_back({42, 10, 10});
    o = scr.frame(s, extra, {}, vp);
    CHECK(!target(scr.targets(), TargetKind::Marker, 42));
    // Location override; nothing at all falls back to "Map".
    MapInput named = markersFor(s, image);
    named.location = "Level 1A - Surdana";
    o = scr.frame(s, named, {}, vp);
    CHECK(hasText(o.draw, "Level 1A - Surdana"));
    Snapshot empty;
    MapScreen e;
    e.open(empty);
    o = e.frame(empty, {}, {}, vp);
    CHECK(hasText(o.draw, "Map") && hasText(o.draw, "No objectives yet.") && e.selectedQuest() == -1);
    o = e.frame(empty, {}, keys({UiKey::Down, UiKey::Confirm}), vp);
    CHECK(!o.wantsClose && e.isOpen());
}

void testMapLinking() {
    ScreenWorld w;
    Snapshot s = w.read();
    MapScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    const Rect image{100, 40, 1000, 1000};
    const MapInput m = markersFor(s, image);
    scr.open(s);
    FrameOutput o = scr.frame(s, m, {}, vp);
    // The ring sits on quest 0's marker.
    const Vec2 p0 = MapScreen::markerToScreen(s.quests[0], image, 512, 512);
    bool ring = false;
    for (const DrawCmd& c : o.draw.commands())
        ring |= c.kind == DrawKind::Outline && near(c.rect.cx(), p0.x, 1) && near(c.rect.cy(), p0.y, 1) &&
                c.color == ScreenConfig{}.theme.select;
    CHECK(ring);
    // Confirm jumps to the marker and back.
    CHECK(hint(scr.hints(), Prompt::Confirm) && hint(scr.hints(), Prompt::Confirm)->enabled);
    o = scr.frame(s, m, keys({UiKey::Confirm}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Marker, 0}) && scr.selectedQuest() == 0);
    CHECK(hint(scr.hints(), Prompt::Confirm)->label == "Show in List");
    o = scr.frame(s, m, keys({UiKey::Confirm}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 0}));
    // Down walks the list (headings skipped); a row without a marker cannot jump.
    o = scr.frame(s, m, keys({UiKey::Down}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 1}));
    CHECK(!hint(scr.hints(), Prompt::Confirm)->enabled);
    o = scr.frame(s, m, keys({UiKey::Down}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 2}));
    // Left from the list reaches the map's markers.
    o = scr.frame(s, m, keys({UiKey::Left}), vp);
    CHECK(scr.focus().kind == TargetKind::Marker);
    // Pointer on a marker focuses it and highlights its row (selected quest shared).
    const Vec2 p4 = MapScreen::markerToScreen(s.quests[4], image, 512, 512);
    o = scr.frame(s, m, pointerAt({p4.x - 2, p4.y - 2, 4, 4}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Marker, 4}) && scr.selectedQuest() == 4);
    CHECK(hasText(o.draw, "Surdana Gate"));
    // Pointer on a row focuses it.
    const HitTarget* row2 = target(scr.targets(), TargetKind::Quest, 2);
    CHECK(row2 != nullptr);
    o = scr.frame(s, m, pointerAt(row2->rect), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 2}) && scr.selectedQuest() == 2);
    // Back / right click close.
    o = scr.frame(s, m, keys({UiKey::Back}), vp);
    CHECK(o.wantsClose && !scr.isOpen() && o.actions.empty());
    scr.open(s, 4);
    o = scr.frame(s, m, {}, vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 4}));
    UiInput rc;
    rc.rightClick = true;
    o = scr.frame(s, m, rc, vp);
    CHECK(o.wantsClose);
}

// The native map selection follows the focused record when it has a marker (rows and markers alike).
void testMapSteersNativeSelection() {
    ScreenWorld w;
    Snapshot s = w.read();
    s.native.section = 3;
    s.native.mapSelection = 4;
    MapScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    const Rect image{100, 40, 1000, 1000};
    const MapInput m = markersFor(s, image);
    const auto steered = [](const FrameOutput& o, int target) {
        return std::any_of(o.actions.begin(), o.actions.end(),
                           [&](const UiAction& a) { return a.kind == ActionKind::SelectMarker && a.target == target; });
    };
    scr.open(s);
    FrameOutput o = scr.frame(s, m, {}, vp); // quest 0's row: its marker is not the native selection
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 0}) && steered(o, 0));
    o = scr.frame(s, m, {}, vp);
    CHECK(o.actions.empty());                     // once per focus
    o = scr.frame(s, m, keys({UiKey::Down}), vp); // quest 1 has no marker
    CHECK(o.actions.empty());
    s.native.mapSelection = 0;
    const Vec2 p4 = MapScreen::markerToScreen(s.quests[4], image, 512, 512);
    o = scr.frame(s, m, pointerAt({p4.x - 2, p4.y - 2, 4, 4}), vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Marker, 4}) && steered(o, 4));
    // Not on the map section (or no selection known): nothing is sent.
    s.native.section = 0;
    scr.open(s);
    o = scr.frame(s, m, {}, vp);
    CHECK(o.actions.empty());
}

void testMapAspectAndScroll() {
    ScreenWorld w;
    Snapshot s = w.read();
    for (const Viewport& vp : kAspects) {
        const Rect p = MapScreen::panelRectFor(vp);
        CHECK(inside(p, vp));
        CHECK(near(p.right(), vp.x + vp.w - 40 * std::min(vp.h / 1080, vp.w / 1440), 1));
        CHECK(p.h / vp.h > 0.8f && p.w / vp.w < 0.4f);
        MapScreen scr;
        scr.open(s);
        scr.frame(s, {}, {}, vp);
        CHECK(scr.panelRect() == p);
        for (const HitTarget& t : scr.targets())
            if (t.id.kind == TargetKind::Quest || t.id.kind == TargetKind::Hint)
                CHECK(t.rect.x >= p.x && t.rect.right() <= p.right() && t.rect.bottom() <= p.bottom());
    }
    // Many objectives: the list scrolls to keep the focus visible.
    Snapshot big = s;
    for (int i = 0; i < 30; ++i) {
        QuestEntry q;
        q.index = 100 + i;
        q.status = 1;
        q.optional = i % 2;
        q.text = "Objective number " + std::to_string(i) + " with a long description that wraps onto two lines";
        big.quests.push_back(q);
    }
    MapScreen scr;
    const Viewport vp{0, 0, 1920, 1080};
    scr.open(big);
    scr.frame(big, {}, {}, vp);
    const Rect p = scr.panelRect();
    for (int i = 0; i < 40; ++i)
        scr.frame(big, {}, keys({UiKey::Down}), vp);
    const HitTarget* f = target(scr.targets(), scr.focus().kind, scr.focus().index);
    CHECK(f && scr.focus().index != 0 && f->rect.y >= p.y && f->rect.bottom() <= p.bottom());
    UiInput wheel;
    wheel.wheel = 3;
    for (int i = 0; i < 60; ++i)
        scr.frame(big, {}, wheel, vp);
    CHECK(scr.focus() == (TargetId{TargetKind::Quest, 0}));
}

// ---- pause ----

void testPauseRoot() {
    ScreenWorld w;
    Snapshot s = w.read();
    PauseScreen scr;
    PauseState st;
    const Viewport vp{0, 0, 1920, 1080};
    scr.open();
    PauseFrameOutput o = scr.frame(s, st, {}, vp);
    CHECK(scr.isOpen() && scr.page() == PausePage::Root);
    CHECK(scr.focus() == (TargetId{TargetKind::MenuRow, static_cast<int>(PauseRow::Resume)}));
    CHECK(hasText(o.draw, "Resume") && hasText(o.draw, "Save Game") && hasText(o.draw, "Load Game") &&
          hasText(o.draw, "Options") && hasText(o.draw, "Quit") && hasText(o.draw, "Paused"));
    // Status strip: location, up to three open objectives (the done one is not listed), stats.
    CHECK(hasText(o.draw, "Surdana") && hasText(o.draw, "The Old Mill") &&
          !hasText(o.draw, "Escape from the Wartok camp."));
    CHECK(hasText(o.draw, "Health") && hasText(o.draw, "84 / 100") && hasText(o.draw, "Level 3") &&
          hasText(o.draw, "646"));
    bool backdrop = false;
    for (const DrawCmd& c : o.draw.commands())
        backdrop |= c.kind == DrawKind::Panel && c.panel == PanelStyle::Backdrop;
    CHECK(backdrop);

    // Down: Save; Load disabled is skipped; Options; Quit; Up returns.
    st.canLoad = false;
    o = scr.frame(s, st, keys({UiKey::Down}), vp);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Save) && o.sound == UiSound::Move);
    o = scr.frame(s, st, keys({UiKey::Down}), vp);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Options));
    CHECK(hasText(o.draw, "Unavailable"));
    o = scr.frame(s, st, keys({UiKey::Down, UiKey::Down}), vp);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Quit));
    o = scr.frame(s, st, keys({UiKey::Up}), vp);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Options));

    // Options (phase 1): native, emits OpenOptions and suspends input until reactivate().
    o = scr.frame(s, st, keys({UiKey::Confirm}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::OpenOptions && scr.suspended());
    o = scr.frame(s, st, keys({UiKey::Confirm, UiKey::Back}), vp);
    CHECK(o.actions.empty() && scr.isOpen());
    scr.reactivate();
    CHECK(!scr.suspended());
    o = scr.frame(s, st, keys({UiKey::Up, UiKey::Confirm}), vp); // Up skips the disabled Load
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::OpenSave);
    scr.reactivate();
    // A row that becomes disabled under the focus: the focus moves on.
    st.canSave = false;
    o = scr.frame(s, st, {}, vp);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Options));
    o = scr.frame(s, st, keys({UiKey::Down, UiKey::Confirm}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::Quit);
    // The suspension times out (the native child never came).
    PauseConfig quick;
    quick.suspendFrames = 3;
    PauseScreen q(quick);
    q.open();
    q.frame(s, st, keys({UiKey::Down, UiKey::Confirm}), vp);
    CHECK(q.suspended());
    for (int i = 0; i < 3; ++i)
        q.frame(s, st, {}, vp);
    CHECK(!q.suspended());

    // Back resumes and closes; so do Close and right click.
    scr.reactivate();
    o = scr.frame(s, st, keys({UiKey::Back}), vp);
    CHECK(o.wantsResume && o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::Resume && !scr.isOpen());
    scr.open();
    o = scr.frame(s, st, keys({UiKey::Close}), vp);
    CHECK(o.wantsResume);
    scr.open();
    UiInput rc;
    rc.rightClick = true;
    o = scr.frame(s, st, rc, vp);
    CHECK(o.wantsResume);
    // Confirm on Resume.
    scr.open();
    o = scr.frame(s, st, keys({UiKey::Confirm}), vp);
    CHECK(o.wantsResume && o.actions[0].kind == PauseActionKind::Resume);
}

void testPauseMouseAndAspect() {
    ScreenWorld w;
    Snapshot s = w.read();
    PauseState st;
    st.canLoad = false;
    for (const Viewport& vp : kAspects) {
        PauseScreen scr;
        scr.open();
        scr.frame(s, st, {}, vp);
        const LayoutFrame lf = computeFrame(vp);
        for (const HitTarget& t : scr.targets())
            CHECK(inside(t.rect, vp) && t.rect.x >= lf.safe.x - 0.5f && t.rect.right() <= lf.safe.right() + 0.5f);
        const HitTarget* load = target(scr.targets(), TargetKind::MenuRow, static_cast<int>(PauseRow::Load));
        const HitTarget* quit = target(scr.targets(), TargetKind::MenuRow, static_cast<int>(PauseRow::Quit));
        CHECK(load && !load->enabled && !load->navigable && quit && quit->enabled);
        CHECK(near(quit->rect.h / vp.h, 88.0f / 1080 * lf.scale * 1080 / vp.h, 0.001f));
        // Hovering a disabled row does not focus it, clicking it does nothing.
        PauseFrameOutput o = scr.frame(s, st, pointerAt(load->rect, true), vp);
        CHECK(o.actions.empty() && scr.focus().index == static_cast<int>(PauseRow::Resume));
        // Hover focuses, a single click activates.
        o = scr.frame(s, st, pointerAt(quit->rect), vp);
        CHECK(scr.focus().index == static_cast<int>(PauseRow::Quit) && o.actions.empty());
        o = scr.frame(s, st, pointerAt(quit->rect, true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::Quit);
        scr.reactivate();
        // The footer "Resume" prompt is clickable.
        const HitTarget* back = target(scr.targets(), TargetKind::Hint, static_cast<int>(Prompt::Back));
        CHECK(back != nullptr);
        o = scr.frame(s, st, pointerAt(back->rect, true), vp);
        CHECK(o.wantsResume);
    }
}

void testPauseHostOptionsAndMissing() {
    ScreenWorld w;
    Snapshot s = w.read();
    PauseConfig cfg;
    cfg.hostOptions = true;
    PauseScreen scr(cfg);
    PauseState st;
    const Viewport vp{0, 0, 2560, 1080};
    scr.open();
    scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down}), vp);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Options));
    PauseFrameOutput o = scr.frame(s, st, keys({UiKey::Confirm}), vp);
    CHECK(scr.page() == PausePage::Options && o.actions.empty() && o.sound == UiSound::Confirm);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Controller) && hasText(o.draw, "PC Settings"));
    CHECK(hint(scr.hints(), Prompt::Back)->label == "Back");
    o = scr.frame(s, st, keys({UiKey::Down, UiKey::Confirm}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::OpenAudio);
    scr.reactivate();
    CHECK(scr.page() == PausePage::Options); // back from the native Audio screen: page kept
    o = scr.frame(s, st, keys({UiKey::Back}), vp);
    CHECK(scr.page() == PausePage::Root && scr.isOpen() && !o.wantsResume);
    CHECK(scr.focus().index == static_cast<int>(PauseRow::Options));
    o = scr.frame(s, st, keys({UiKey::Confirm, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}),
                  vp);
    CHECK(scr.page() == PausePage::Root); // Options -> Back row
    // Missing strings: no quest text, no region names -> the location override, then "Unknown".
    Snapshot bare = s;
    bare.regions.clear();
    for (QuestEntry& q : bare.quests) {
        q.text.clear();
        q.caption.clear();
    }
    PauseScreen p2;
    p2.open();
    st.location = "";
    o = p2.frame(bare, st, {}, vp);
    CHECK(hasText(o.draw, "Unknown"));
    st.location = "Level 1A - Surdana";
    o = p2.frame(bare, st, {}, vp);
    CHECK(hasText(o.draw, "Level 1A - Surdana"));
    // Nothing read at all (no context): the menu still works.
    Snapshot none;
    o = p2.frame(none, PauseState{}, keys({UiKey::Down, UiKey::Confirm}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::OpenSave &&
          hasText(o.draw, "No open objectives."));
}

void testPauseQueue() {
    PauseActionQueue q;
    const uint32_t a = q.push({0, PauseActionKind::OpenSave});
    const uint32_t b = q.push({0, PauseActionKind::Resume});
    CHECK(b == a + 1 && q.size() == 2);
    auto x = q.pop();
    CHECK(x && x->kind == PauseActionKind::OpenSave && x->id == a);
    q.clear();
    CHECK(!q.pop() && q.size() == 0);
    CHECK(std::string(pauseActionName(PauseActionKind::OpenPcSettings)) == "OpenPcSettings");
}

} // namespace

int main() {
    testReaderQuests();
    testStripLayout();
    testStripNavigationAndActions();
    testStripMouse();
    testStripGuards();
    testMapRowsAndFallbacks();
    testMapLinking();
    testMapSteersNativeSelection();
    testMapAspectAndScroll();
    testPauseRoot();
    testPauseMouseAndAspect();
    testPauseHostOptionsAndMissing();
    testPauseQueue();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
