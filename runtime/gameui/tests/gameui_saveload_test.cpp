// Unit tests for the modern Save / Load screen (SaveLoadScreen), the slot reader (readSaveSlots), the title menu
// (TitleScreen) and PauseScreen's embedded Options mode (PauseConfig::rootPage). Synthetic data only.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gameui/Addresses.h"
#include "gameui/SaveLoadScreen.h"
#include "gameui/SaveSlots.h"
#include "gameui/TitleScreen.h"

#include "SettingsFixture.h"

using namespace gameui;

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
const Viewport kVp{0, 0, 1920, 1080};

UiInput keys(std::initializer_list<UiKey> k) {
    UiInput in;
    in.keys = k;
    return in;
}

UiInput pointer(float x, float y, bool click = false) {
    UiInput in;
    in.pointerValid = in.pointerMoved = true;
    in.pointerX = x;
    in.pointerY = y;
    in.leftClick = in.leftDown = click;
    return in;
}

bool inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f && inner.right() <= outer.right() + 0.5f &&
           inner.bottom() <= outer.bottom() + 0.5f;
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

const DrawCmd* findImage(const DrawList& d, ImageKind k, uint32_t index) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Image && c.image.kind == k && c.image.index == index)
            return &c;
    return nullptr;
}

template <class O, class K>
int count(const O& o, K k) {
    int n = 0;
    for (const auto& a : o.actions)
        if (a.kind == k)
            ++n;
    return n;
}

template <class O, class K>
const auto* find(const O& o, K k) {
    for (const auto& a : o.actions)
        if (a.kind == k)
            return &a;
    return static_cast<decltype(&o.actions[0])>(nullptr);
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

SaveSlot used(int n, const char* where, int day = 25) {
    SaveSlot s;
    s.number = n;
    s.state = SlotState::Used;
    s.location = where;
    s.time = {2026, 10, day, 16, 23, 5};
    s.preview = {ImageKind::SavePreview, static_cast<uint32_t>(n)};
    return s;
}

SaveSlot empty(int n, SlotState st = SlotState::Empty) {
    SaveSlot s;
    s.number = n;
    s.state = st;
    return s;
}

// Page `page` of four: used, empty, used, empty (numbers page * 4 + 1 ..).
SaveSlotsState page(int page = 0, int pages = 1) {
    SaveSlotsState st;
    st.page = page;
    st.pageCount = pages;
    const int b = page * 4;
    st.slots = {used(b + 1, "Surdana"), empty(b + 2), used(b + 3, "Wartok Canyon", 24), empty(b + 4)};
    st.nativeRow = 0;
    return st;
}

// ---- reader ----

struct Ram {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x400000, 0);
    void put32(uint32_t a, uint32_t v) { std::memcpy(&b[a], &v, 4); }
    void str(uint32_t a, const char* s) { std::memcpy(&b[a], s, std::strlen(s) + 1); }
};

void testReader() {
    namespace su = addr::saveui;
    Ram r;
    const uint32_t scr = 0x200000, ents = 0x210000;
    r.put32(scr + addr::screen::kId, su::kScreenSave);
    r.put32(scr + su::kCount, 4);
    r.put32(scr + su::kEntries, ents);
    r.put32(scr + su::kSelection, 2);
    auto entry = [&](int i) { return ents + static_cast<uint32_t>(i) * su::kEntrySize; };
    // 0: a save
    r.str(entry(0) + su::kEntryPath, "BESCES-50006Slot5/Level 1A - Surdana.rsg");
    r.str(entry(0) + su::kEntryCaption, "Surdana");
    const int32_t t[] = {5, 23, 16, 25, 10, 2026};
    for (int k = 0; k < 6; ++k)
        r.put32(entry(0) + su::kEntryTime + 4u * static_cast<uint32_t>(k), static_cast<uint32_t>(t[k]));
    // 1: empty, 2: no space, 3: a save with a Latin-1 caption
    for (int i : {1, 2}) {
        r.put32(entry(i) + su::kEntrySortKey, 0xffffffffu);
        r.put32(entry(i) + su::kEntrySortKey + 4, 0x7fffffffu);
    }
    r.str(entry(1) + su::kEntryCaption, "Empty");
    r.str(entry(2) + su::kEntryCaption, "Insufficient space");
    r.str(entry(3) + su::kEntryPath, "BESCES-50006Slot8/x.rsg");
    r.str(entry(3) + su::kEntryCaption, "B\xe4"
                                        "ckerei");
    const FlatGuestMemory m(r.b.data(), r.b.size());
    SaveReadOptions o;
    o.page = 1;
    o.pageCount = 4;
    o.noSpaceCaption = "Insufficient space";
    SaveSlotsState st;
    CHECK(readSaveSlots(m, scr, o, st));
    CHECK(st.card == CardState::Ready && st.page == 1 && st.pageCount == 4 && st.slots.size() == 4);
    CHECK(st.slots[0].number == 5 && st.slots[3].number == 8);
    CHECK(st.slots[0].state == SlotState::Used && st.slots[0].location == "Surdana");
    CHECK(st.slots[0].file == "Level 1A - Surdana.rsg");
    CHECK((st.slots[0].time == SaveTimestamp{2026, 10, 25, 16, 23, 5}));
    CHECK(st.slots[0].preview == (ImageRef{ImageKind::SavePreview, 5}));
    CHECK(st.slots[1].state == SlotState::Empty && st.slots[1].location.empty());
    CHECK(st.slots[1].preview.kind == ImageKind::None);
    CHECK(st.slots[2].state == SlotState::NoSpace);
    CHECK(st.slots[3].state == SlotState::Used && st.slots[3].location == "B\xc3\xa4"
                                                                          "ckerei");
    CHECK(st.nativeRow == 2);
    // Without the no-space text the slot counts as empty.
    o.noSpaceCaption.clear();
    CHECK(readSaveSlots(m, scr, o, st) && st.slots[2].state == SlotState::Empty);
    // The list not built yet: Reading, four unknown slots of the page.
    r.put32(scr + su::kCount, 0);
    CHECK(readSaveSlots(m, scr, o, st) && st.card == CardState::Reading && st.slots.size() == 4);
    CHECK(st.slots[0].number == 5 && st.slots[0].state == SlotState::Unknown);
    // Not a slot screen.
    r.put32(scr + addr::screen::kId, 0x17);
    CHECK(!readSaveSlots(m, scr, o, st) && st.card == CardState::Error);
    CHECK(!readSaveSlots(m, 0, o, st));
}

void testFormatting() {
    CHECK(cardStateFromAnySave(0) == CardState::Ready && cardStateFromAnySave(1) == CardState::Ready);
    CHECK(cardStateFromAnySave(2) == CardState::NoCard && cardStateFromAnySave(3) == CardState::Unformatted);
    CHECK(cardStateFromAnySave(4) == CardState::Full && cardStateFromAnySave(9) == CardState::Error);
    const SaveTimestamp t{2026, 10, 25, 16, 3, 0};
    CHECK(formatSaveDate(t, DateOrder::DayMonthYear) == "25 Oct 2026");
    CHECK(formatSaveDate(t, DateOrder::MonthDayYear) == "Oct 25, 2026");
    CHECK(formatSaveDate(t, DateOrder::YearMonthDay) == "2026 Oct 25");
    CHECK(formatSaveTime(t, true) == "16:03" && formatSaveTime(t, false) == "4:03 PM");
    CHECK(formatSaveTime({2026, 1, 1, 0, 5, 0}, false) == "12:05 AM");
    CHECK(formatSaveDate(SaveTimestamp{}, DateOrder::DayMonthYear).empty());
    CHECK(formatPlayTime(-1).empty() && formatPlayTime(59) == "0 min" && formatPlayTime(3900) == "1 h 05 min");
    CHECK(newerThan({2026, 10, 25, 16, 3, 0}, {2026, 10, 25, 9, 59, 0}));
    CHECK(!newerThan({2025, 12, 31, 23, 0, 0}, {2026, 1, 1, 0, 0, 0}));
}

// ---- SaveLoadScreen ----

void testLoadFromTitle() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Load, SaveLoadContext::Title);
    const SaveSlotsState st = page();
    SaveLoadFrameOutput o = scr.frame(st, {}, kVp);
    CHECK(scr.focusedSlot() == 1);                        // the native selection
    CHECK(count(o, SaveLoadActionKind::SelectSlot) == 0); // already selected natively
    CHECK(hasText(o.draw, "Load Game") && hasText(o.draw, "Main Menu") && hasText(o.draw, "Surdana"));
    CHECK(findImage(o.draw, ImageKind::SavePreview, 1) != nullptr);
    CHECK(hint(scr.hints(), Prompt::Confirm)->enabled && hint(scr.hints(), Prompt::Confirm)->label == "Load");
    o = scr.frame(st, keys({UiKey::Down}), kVp);
    CHECK(scr.focusedSlot() == 2 && o.sound == UiSound::Move);
    CHECK(find(o, SaveLoadActionKind::SelectSlot) && find(o, SaveLoadActionKind::SelectSlot)->value == 2);
    CHECK(!hint(scr.hints(), Prompt::Confirm)->enabled); // an empty slot cannot be loaded
    o = scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(o.actions.empty() && scr.card() == SaveCard::None);
    o = scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(scr.focusedSlot() == 3);
    CHECK(find(o, SaveLoadActionKind::ConfirmLoad) && find(o, SaveLoadActionKind::ConfirmLoad)->value == 3);
    CHECK(count(o, SaveLoadActionKind::SelectSlot) == 1); // the native selection follows before the confirm
    CHECK(o.actions.back().kind == SaveLoadActionKind::ConfirmLoad);
    CHECK(scr.suspended());
    o = scr.frame(st, keys({UiKey::Up, UiKey::Confirm}), kVp); // ignored while the load runs
    CHECK(o.actions.empty() && scr.focusedSlot() == 3);
    scr.reactivate();
    CHECK(!scr.suspended());
    o = scr.frame(st, keys({UiKey::Up}), kVp);
    CHECK(scr.focusedSlot() == 2);
    // Down at the last slot stays.
    o = scr.frame(st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down}), kVp);
    CHECK(scr.focusedSlot() == 4);
}

void testLoadInGameAsks() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Load, SaveLoadContext::Pause);
    SaveSlotsState st = page();
    st.inGame = true;
    scr.frame(st, {}, kVp);
    SaveLoadFrameOutput o = scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(scr.card() == SaveCard::Load && o.actions.empty() && o.sound == UiSound::Confirm);
    CHECK(scr.focus() == (TargetId{TargetKind::CardButton, static_cast<int>(SaveCardButton::Cancel)}));
    CHECK(hasText(o.draw, "Load this game?") && hasText(o.draw, "Paused"));
    CHECK(hint(scr.hints(), Prompt::Back)->label == "Cancel");
    o = scr.frame(st, keys({UiKey::Confirm}), kVp); // Cancel
    CHECK(scr.card() == SaveCard::None && o.actions.empty() && scr.focusedSlot() == 1 && scr.isOpen());
    scr.frame(st, keys({UiKey::Confirm}), kVp);
    o = scr.frame(st, keys({UiKey::Up, UiKey::Confirm}), kVp); // Load
    CHECK(find(o, SaveLoadActionKind::ConfirmLoad) && find(o, SaveLoadActionKind::ConfirmLoad)->value == 1);
    CHECK(scr.card() == SaveCard::None && scr.suspended());
    // Back on a card closes the card only.
    scr.reactivate();
    scr.frame(st, keys({UiKey::Confirm}), kVp);
    o = scr.frame(st, keys({UiKey::Back}), kVp);
    CHECK(scr.card() == SaveCard::None && o.actions.empty() && !o.wantsBack && scr.isOpen());
    // The title asks too when configured.
    SaveLoadConfig cfg;
    cfg.confirmLoadOnTitle = true;
    SaveLoadScreen t(cfg);
    t.open(SaveLoadMode::Load, SaveLoadContext::Title);
    t.frame(page(), {}, kVp);
    t.frame(page(), keys({UiKey::Confirm}), kVp);
    CHECK(t.card() == SaveCard::Load);
}

void testSave() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Save, SaveLoadContext::Pause, 2);
    SaveSlotsState st = page();
    SaveLoadFrameOutput o = scr.frame(st, {}, kVp);
    CHECK(scr.focusedSlot() == 2); // focusSlot
    CHECK(find(o, SaveLoadActionKind::SelectSlot) && find(o, SaveLoadActionKind::SelectSlot)->value == 2);
    CHECK(hasText(o.draw, "Save Game") && hasText(o.draw, "Save here"));
    CHECK(hint(scr.hints(), Prompt::Confirm)->label == "Save");
    o = scr.frame(st, keys({UiKey::Confirm}), kVp); // empty: saves at once (as the native screen)
    CHECK(find(o, SaveLoadActionKind::ConfirmSave) && find(o, SaveLoadActionKind::ConfirmSave)->value == 2);
    CHECK(count(o, SaveLoadActionKind::SelectSlot) == 0 && scr.suspended());
    scr.reactivate();
    o = scr.frame(st, keys({UiKey::Up}), kVp);
    CHECK(hint(scr.hints(), Prompt::Confirm)->label == "Overwrite");
    o = scr.frame(st, keys({UiKey::Confirm}), kVp); // used: the overwrite card, Cancel focused
    CHECK(scr.card() == SaveCard::Overwrite && o.actions.empty());
    CHECK(scr.focus() == (TargetId{TargetKind::CardButton, static_cast<int>(SaveCardButton::Cancel)}));
    CHECK(hasText(o.draw, "Overwrite this save?") && hasText(o.draw, "Overwrite"));
    o = scr.frame(st, keys({UiKey::Up, UiKey::Confirm}), kVp);
    CHECK(find(o, SaveLoadActionKind::ConfirmOverwrite) && find(o, SaveLoadActionKind::ConfirmOverwrite)->value == 1);
    CHECK(count(o, SaveLoadActionKind::SelectSlot) == 0); // focusing slot 1 already selected it natively
    // No space and damaged slots.
    SaveLoadScreen s2;
    s2.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    SaveSlotsState full = page();
    full.slots[1].state = SlotState::NoSpace;
    full.slots[3].state = SlotState::NoSpace;
    full.slots[2].state = SlotState::Corrupt;
    s2.frame(full, {}, kVp);
    o = s2.frame(full, keys({UiKey::Down}), kVp);
    CHECK(s2.focusedSlot() == 2 && !hint(s2.hints(), Prompt::Confirm)->enabled);
    CHECK(hasText(o.draw, "Not enough space"));
    o = s2.frame(full, keys({UiKey::Confirm}), kVp);
    CHECK(count(o, SaveLoadActionKind::ConfirmSave) == 0 && s2.card() == SaveCard::None);
    o = s2.frame(full, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(s2.card() == SaveCard::Overwrite && hasText(o.draw, "Damaged save"));
    CHECK(!SaveLoadScreen::slotActionable(SaveLoadMode::Load, SlotState::Corrupt));
    CHECK(SaveLoadScreen::slotActionable(SaveLoadMode::Save, SlotState::Corrupt));
    // Asking before an empty-slot save.
    SaveLoadConfig cfg;
    cfg.confirmSave = true;
    SaveLoadScreen s3(cfg);
    s3.open(SaveLoadMode::Save, SaveLoadContext::Pause, 4);
    s3.frame(st, {}, kVp);
    o = s3.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(s3.card() == SaveCard::Save && o.actions.size() <= 1);
    CHECK(s3.focus() == (TargetId{TargetKind::CardButton, static_cast<int>(SaveCardButton::Primary)}));
    o = s3.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(find(o, SaveLoadActionKind::ConfirmSave) && find(o, SaveLoadActionKind::ConfirmSave)->value == 4);
}

void testPages() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Load, SaveLoadContext::Pause);
    SaveSlotsState p0 = page(0, 4);
    scr.frame(p0, {}, kVp);
    SaveLoadFrameOutput o = scr.frame(p0, keys({UiKey::Down, UiKey::Down}), kVp); // slot 3
    CHECK(scr.focusedSlot() == 3);
    CHECK(hasText(o.draw, "Page 1 of 4") && hasText(o.draw, "SLOTS 1 - 4"));
    o = scr.frame(p0, keys({UiKey::Right}), kVp);
    CHECK(find(o, SaveLoadActionKind::ChangePage) && find(o, SaveLoadActionKind::ChangePage)->value == 1);
    CHECK(scr.pendingPage() == 1 && scr.focusedSlot() == 0 && o.sound == UiSound::Page);
    CHECK(target(scr.targets(), TargetKind::SaveSlot, 1) == nullptr); // the list waits for the new page
    CHECK(hasText(o.draw, "Page 2 of 4"));
    o = scr.frame(p0, keys({UiKey::Right, UiKey::Confirm}), kVp); // still the old page: no input on the list
    CHECK(o.actions.empty() && scr.pendingPage() == 1);
    SaveSlotsState reading = page(1, 4);
    reading.card = CardState::Reading;
    o = scr.frame(reading, {}, kVp);
    CHECK(scr.pendingPage() == 1 && hasText(o.draw, "Reading the memory card..."));
    SaveSlotsState p1 = page(1, 4);
    p1.nativeRow = 0;
    o = scr.frame(p1, {}, kVp);
    CHECK(scr.pendingPage() < 0 && scr.focusedSlot() == 7); // the same row on the new page
    CHECK(find(o, SaveLoadActionKind::SelectSlot) && find(o, SaveLoadActionKind::SelectSlot)->value == 7);
    CHECK(hasText(o.draw, "SLOTS 5 - 8"));
    // Wrap backwards with PagePrev from page 1 -> 0, and from 0 -> 3.
    o = scr.frame(p1, keys({UiKey::PagePrev}), kVp);
    CHECK(find(o, SaveLoadActionKind::ChangePage)->value == 0);
    scr.frame(p0, {}, kVp);
    o = scr.frame(p0, keys({UiKey::Left}), kVp);
    CHECK(find(o, SaveLoadActionKind::ChangePage)->value == 3);
    // A page that never arrives is given up.
    SaveLoadConfig cfg;
    cfg.pageWaitFrames = 3;
    SaveLoadScreen t(cfg);
    t.open(SaveLoadMode::Load, SaveLoadContext::Pause);
    t.frame(p0, {}, kVp);
    t.frame(p0, keys({UiKey::PageNext}), kVp);
    for (int i = 0; i < 5; ++i)
        t.frame(p0, {}, kVp);
    CHECK(t.pendingPage() < 0 && t.focusedSlot() > 0);
    // One page: no page actions, no page hint.
    SaveLoadScreen one;
    one.open(SaveLoadMode::Load, SaveLoadContext::Pause);
    one.frame(page(), {}, kVp);
    o = one.frame(page(), keys({UiKey::Right, UiKey::PageNext}), kVp);
    CHECK(count(o, SaveLoadActionKind::ChangePage) == 0 && hint(one.hints(), Prompt::PageNext) == nullptr);
}

void testBackAndStates() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Load, SaveLoadContext::Pause);
    scr.frame(page(), {}, kVp);
    SaveLoadFrameOutput o = scr.frame(page(), keys({UiKey::Back}), kVp);
    CHECK(o.wantsBack && find(o, SaveLoadActionKind::Back) && !scr.isOpen());
    // Level change: Save only, Back reads "Skip".
    scr.open(SaveLoadMode::Load, SaveLoadContext::LevelChange);
    o = scr.frame(page(), {}, kVp);
    CHECK(scr.mode() == SaveLoadMode::Save && hint(scr.hints(), Prompt::Back)->label == "Skip");
    CHECK(hasText(o.draw, "Level complete"));
    // No memory card: a message, no slots, Back still works.
    SaveSlotsState none;
    none.card = CardState::NoCard;
    scr.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    o = scr.frame(none, keys({UiKey::Confirm, UiKey::Down}), kVp);
    CHECK(hasText(o.draw, "No memory card") && o.actions.empty() && scr.focusedSlot() == 0);
    CHECK(target(scr.targets(), TargetKind::SaveSlot, 1) == nullptr);
    o = scr.frame(none, keys({UiKey::Close}), kVp);
    CHECK(o.wantsBack);
    SaveSlotsState unf;
    unf.card = CardState::Unformatted;
    scr.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    CHECK(hasText(scr.frame(unf, {}, kVp).draw, "Memory card not formatted"));
    SaveSlotsState err;
    err.card = CardState::Error;
    err.message = "The save could not be written.";
    CHECK(hasText(scr.frame(err, {}, kVp).draw, "The save could not be written."));
    // Busy (a native box or the Saving screen): input waits, the picture stays.
    SaveSlotsState busy = page();
    busy.busy = true;
    scr.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    scr.frame(busy, {}, kVp);
    o = scr.frame(busy, keys({UiKey::Down, UiKey::Confirm, UiKey::Back}), kVp);
    CHECK(o.actions.empty() && scr.isOpen() && scr.focusedSlot() == 1 && hasText(o.draw, "Surdana"));
    // A message line from the glue.
    SaveSlotsState msg = page();
    msg.message = "Loading failed.";
    scr.open(SaveLoadMode::Load, SaveLoadContext::Pause);
    CHECK(hasText(scr.frame(msg, {}, kVp).draw, "Loading failed."));
    // Full card in Save mode: every empty slot has no space.
    SaveSlotsState full;
    full.card = CardState::Full;
    full.slots = {empty(1, SlotState::NoSpace), empty(2, SlotState::NoSpace), empty(3, SlotState::NoSpace),
                  empty(4, SlotState::NoSpace)};
    scr.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    scr.frame(full, {}, kVp);
    o = scr.frame(full, keys({UiKey::Confirm}), kVp);
    CHECK(o.actions.empty() && !hint(scr.hints(), Prompt::Confirm)->enabled);
}

void testMouse() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    const SaveSlotsState st = page(0, 2);
    scr.frame(st, {}, kVp);
    const HitTarget* t4 = target(scr.targets(), TargetKind::SaveSlot, 4);
    CHECK(t4 != nullptr);
    SaveLoadFrameOutput o = scr.frame(st, pointer(t4->rect.cx(), t4->rect.cy()), kVp);
    CHECK(scr.focusedSlot() == 4 && find(o, SaveLoadActionKind::SelectSlot)->value == 4);
    o = scr.frame(st, pointer(t4->rect.cx(), t4->rect.cy(), true), kVp);
    CHECK(find(o, SaveLoadActionKind::ConfirmSave) && find(o, SaveLoadActionKind::ConfirmSave)->value == 4);
    scr.reactivate();
    const HitTarget* next = target(scr.targets(), TargetKind::PageStep, 1);
    CHECK(next && !next->navigable);
    o = scr.frame(st, pointer(next->rect.cx(), next->rect.cy(), true), kVp);
    CHECK(find(o, SaveLoadActionKind::ChangePage) && find(o, SaveLoadActionKind::ChangePage)->value == 1);
    UiInput rc;
    rc.rightClick = true;
    scr.frame(page(1, 2), {}, kVp);
    o = scr.frame(page(1, 2), rc, kVp);
    CHECK(o.wantsBack);
    // A click on the card's primary button confirms.
    SaveLoadScreen c;
    c.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    c.frame(st, {}, kVp);
    c.frame(st, keys({UiKey::Confirm}), kVp); // slot 1 is used: the card
    const HitTarget* b = target(c.targets(), TargetKind::CardButton, 0);
    CHECK(b != nullptr);
    o = c.frame(st, pointer(b->rect.cx(), b->rect.cy(), true), kVp);
    CHECK(find(o, SaveLoadActionKind::ConfirmOverwrite) != nullptr);
}

void testSaveLoadAspects() {
    for (const Viewport& vp : kAspects) {
        SaveLoadScreen scr;
        scr.open(SaveLoadMode::Load, SaveLoadContext::Pause);
        const SaveLoadFrameOutput o = scr.frame(page(0, 4), {}, vp);
        const Rect view{vp.x, vp.y, vp.w, vp.h};
        const LayoutFrame lf = computeFrame(vp);
        const Rect list = lf.map(design::saveload::listPanel()), prev = lf.map(design::saveload::previewPanel());
        CHECK(inside(list, view) && inside(prev, view) && !overlaps(list, prev));
        for (const HitTarget& t : scr.targets()) {
            CHECK(inside(t.rect, view));
            if (t.id.kind == TargetKind::SaveSlot)
                CHECK(inside(t.rect, list));
        }
        for (int i = 0; i < 3; ++i)
            CHECK(!overlaps(lf.map(design::saveload::slotCard(i)), lf.map(design::saveload::slotCard(i + 1))));
        CHECK(inside(lf.map(design::saveload::previewImage()), prev));
        // The preview of the focused slot fills the preview image.
        const DrawCmd* img = nullptr;
        for (const DrawCmd& c : o.draw.commands())
            if (c.kind == DrawKind::Image && c.image.kind == ImageKind::SavePreview && c.image.index == 1 &&
                c.rect.w > lf.px(600))
                img = &c;
        CHECK(img && inside(img->rect, prev));
        CHECK(std::fabs(img->rect.w / img->rect.h - 16.0f / 9.0f) < 0.05f);
    }
}

void testQueue() {
    SaveLoadActionQueue q;
    const uint32_t a = q.push({0, SaveLoadActionKind::SelectSlot, 1});
    CHECK(q.push({0, SaveLoadActionKind::SelectSlot, 3}) == a && q.size() == 1);
    q.push({0, SaveLoadActionKind::ConfirmLoad, 3});
    q.push({0, SaveLoadActionKind::SelectSlot, 2});
    CHECK(q.size() == 3);
    CHECK(q.pop()->value == 3 && q.pop()->kind == SaveLoadActionKind::ConfirmLoad && q.pop()->value == 2);
    CHECK(!q.pop());
    TitleActionQueue tq;
    tq.push({0, TitleActionKind::NewGame});
    CHECK(tq.size() == 1 && tq.pop()->kind == TitleActionKind::NewGame && !tq.pop());
}

// ---- TitleScreen ----

TitleState titleState(bool withContinue = true) {
    TitleState st;
    if (withContinue) {
        st.continueSlot = 6;
        st.continueLocation = "Surdana";
        st.continueTime = {2026, 10, 25, 16, 23, 0};
    }
    st.options.valid = true;
    st.options.set(PauseOption::MasterVolume, 50);
    st.options.set(PauseOption::MusicVolume, 80);
    st.options.set(PauseOption::Subtitles, 1);
    st.options.set(PauseOption::Stereo, 1);
    st.options.set(PauseOption::Scheme, 0);
    st.options.set(PauseOption::Vibration, 1);
    st.options.set(PauseOption::ReversePitch, 0);
    return st;
}

bool titleFocused(const TitleScreen& s, TitleRow r) {
    return s.focus() == TargetId{TargetKind::TitleRow, static_cast<int>(r)};
}

void testTitleRows() {
    TitleScreen scr;
    scr.open();
    TitleState st = titleState();
    TitleFrameOutput o = scr.frame(st, {}, kVp);
    CHECK(scr.rows(st).size() == 7 && scr.rows(st).front() == TitleRow::Continue);
    CHECK(titleFocused(scr, TitleRow::Continue));
    CHECK(hasText(o.draw, "Continue") && hasText(o.draw, "Load Game") && hasText(o.draw, "PC Settings"));
    CHECK(hasText(o.draw, "Your newest save: Surdana, 25 Oct 2026 16:23."));
    // No save: no Continue, Load Game disabled, focus on New Game.
    TitleScreen t2;
    t2.open();
    TitleState none = titleState(false);
    none.canLoad = false;
    o = t2.frame(none, {}, kVp);
    CHECK(t2.rows(none).size() == 6 && titleFocused(t2, TitleRow::NewGame));
    CHECK(hasText(o.draw, "Unavailable") == false); // centred buttons carry no "Unavailable" tag
    o = t2.frame(none, keys({UiKey::Up}), kVp);
    CHECK(titleFocused(t2, TitleRow::NewGame)); // the disabled Load Game is skipped
    // Quit hidden without title_quit, PC Settings by config.
    TitleConfig cfg;
    cfg.showPcSettings = false;
    TitleScreen t3(cfg);
    t3.open();
    none.canQuit = false;
    t3.frame(none, {}, kVp);
    CHECK(t3.rows(none).size() == 4);
}

void testTitleActions() {
    TitleState st = titleState();
    auto first = [&](std::initializer_list<UiKey> k) {
        TitleScreen scr;
        scr.open();
        scr.frame(st, {}, kVp);
        TitleFrameOutput o = scr.frame(st, keys(k), kVp);
        CHECK(scr.suspended());
        return o.actions.empty() ? TitleAction{0, TitleActionKind::LoadGame, -1} : o.actions.front();
    };
    TitleAction a = first({UiKey::Confirm});
    CHECK(a.kind == TitleActionKind::Continue && a.value == 6);
    CHECK(first({UiKey::Down, UiKey::Confirm}).kind == TitleActionKind::LoadGame);
    CHECK(first({UiKey::Down, UiKey::Down, UiKey::Confirm}).kind == TitleActionKind::NewGame);
    CHECK(first({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}).kind == TitleActionKind::Credits);
    CHECK(first({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}).kind ==
          TitleActionKind::OpenPcSettings);
    // Suspended until reactivate().
    TitleScreen scr;
    scr.open();
    scr.frame(st, {}, kVp);
    scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    TitleFrameOutput o = scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(o.actions.empty() && titleFocused(scr, TitleRow::LoadGame));
    scr.reactivate();
    o = scr.frame(st, keys({UiKey::Down}), kVp);
    CHECK(titleFocused(scr, TitleRow::NewGame) && o.sound == UiSound::Move);
    // Quit card: Cancel focused, Back returns to Quit; Quit to Desktop.
    TitleScreen q;
    q.open();
    q.frame(st, {}, kVp);
    q.frame(st, keys({UiKey::Up}), kVp); // Continue is the first row: Up stays (no wrap)
    CHECK(titleFocused(q, TitleRow::Continue));
    for (int i = 0; i < 6; ++i)
        q.frame(st, keys({UiKey::Down}), kVp);
    CHECK(titleFocused(q, TitleRow::Quit));
    o = q.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(q.page() == TitlePage::QuitConfirm && o.actions.empty() && hasText(o.draw, "Quit the game?"));
    CHECK(q.focus() == (TargetId{TargetKind::CardButton, static_cast<int>(TitleRow::Cancel)}));
    o = q.frame(st, keys({UiKey::Back}), kVp);
    CHECK(q.page() == TitlePage::Main && titleFocused(q, TitleRow::Quit));
    q.frame(st, keys({UiKey::Confirm}), kVp);
    o = q.frame(st, keys({UiKey::Up, UiKey::Confirm}), kVp);
    CHECK(!o.actions.empty() && o.actions.front().kind == TitleActionKind::QuitToDesktop);
    // Mouse: hover focuses, click activates.
    TitleScreen m;
    m.open();
    m.frame(st, {}, kVp);
    const HitTarget* ng = target(m.targets(), TargetKind::TitleRow, static_cast<int>(TitleRow::NewGame));
    CHECK(ng != nullptr);
    m.frame(st, pointer(ng->rect.cx(), ng->rect.cy()), kVp);
    CHECK(titleFocused(m, TitleRow::NewGame));
    o = m.frame(st, pointer(ng->rect.cx(), ng->rect.cy(), true), kVp);
    CHECK(!o.actions.empty() && o.actions.front().kind == TitleActionKind::NewGame);
}

void testTitleOptions() {
    TitleState st = titleState();
    TitleScreen scr;
    scr.open();
    scr.frame(st, {}, kVp);
    TitleFrameOutput o = scr.frame(st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(scr.page() == TitlePage::Options && scr.options().page() == PausePage::Options);
    CHECK(o.optionActions.size() == 1 && o.optionActions[0].kind == PauseActionKind::EnterPage &&
          o.optionActions[0].value == static_cast<int32_t>(PausePage::Options));
    CHECK(o.actions.empty() && hasText(o.draw, "Options") && hasText(o.draw, "Main Menu"));
    CHECK(!hasText(o.draw, "LOCATION")); // no status strip
    // The menu column is centred.
    const HitTarget* ctl = nullptr;
    for (const HitTarget& t : scr.targets())
        if (t.id.kind == TargetKind::MenuRow && t.id.index == static_cast<int>(PauseRow::Controller))
            ctl = &t;
    CHECK(ctl && std::fabs(ctl->rect.cx() - kVp.w * 0.5f) < 2.0f);
    // Audio: Enter, a value, Leave.
    o = scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(scr.options().page() == PausePage::Audio && o.optionActions.size() == 1 &&
          o.optionActions[0].kind == PauseActionKind::EnterPage);
    CHECK(hasText(o.draw, "Main Menu  /  Options"));
    o = scr.frame(st, keys({UiKey::Left}), kVp);
    CHECK(o.optionActions.size() == 1 && o.optionActions[0].kind == PauseActionKind::SetMasterVolume &&
          o.optionActions[0].value == 45);
    o = scr.frame(st, keys({UiKey::Back}), kVp);
    CHECK(o.optionActions.size() == 1 && o.optionActions[0].kind == PauseActionKind::LeavePage &&
          o.optionActions[0].value == static_cast<int32_t>(PausePage::Audio));
    CHECK(scr.page() == TitlePage::Options);
    // Back on Options: Leave Options, the main menu again with Options focused, drawn the same frame.
    o = scr.frame(st, keys({UiKey::Back}), kVp);
    CHECK(scr.page() == TitlePage::Main && titleFocused(scr, TitleRow::Options));
    CHECK(o.optionActions.size() == 1 && o.optionActions[0].kind == PauseActionKind::LeavePage &&
          o.optionActions[0].value == static_cast<int32_t>(PausePage::Options));
    CHECK(hasText(o.draw, "New Game"));
    // PC Settings inside Options suspends until reactivate().
    scr.frame(st, keys({UiKey::Confirm}), kVp);
    o = scr.frame(st, keys({UiKey::Down, UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(o.optionActions.size() == 1 && o.optionActions[0].kind == PauseActionKind::OpenPcSettings);
    CHECK(scr.suspended());
    scr.reactivate();
    CHECK(!scr.suspended() && scr.page() == TitlePage::Options);
    // The Back row of the Options page also leaves.
    o = scr.frame(st, keys({UiKey::Down, UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(scr.page() == TitlePage::Main && o.optionActions.back().kind == PauseActionKind::LeavePage);
}

void testTitleLayout() {
    for (const Viewport& vp : kAspects) {
        TitleScreen scr;
        scr.open();
        TitleState st = titleState();
        st.backdrop = {ImageKind::TitleArt, 0};
        const TitleFrameOutput o = scr.frame(st, {}, vp);
        const Rect view{vp.x, vp.y, vp.w, vp.h};
        const float cx = vp.x + vp.w * 0.5f;
        for (const HitTarget& t : scr.targets()) {
            CHECK(inside(t.rect, view));
            if (t.id.kind == TargetKind::TitleRow)
                CHECK(std::fabs(t.rect.cx() - cx) < 2.0f);
        }
        // The art first, over the whole viewport.
        const DrawCmd& c0 = o.draw.commands().front();
        CHECK(c0.kind == DrawKind::Image && c0.image.kind == ImageKind::TitleArt && c0.rect == view);
    }
    // Without a backdrop the native art shows through: no full-screen backdrop panel on the main page.
    TitleScreen scr;
    scr.open();
    const TitleFrameOutput o = scr.frame(titleState(), {}, kVp);
    for (const DrawCmd& c : o.draw.commands())
        CHECK(!(c.kind == DrawKind::Panel && c.panel == PanelStyle::Backdrop));
}

// TitleConfig::pcSettingsPages: PC Settings (main row and Options > PC Settings / Key Bindings) open the modern pages.
void testTitlePcSettings() {
    test::SettingsWorld w;
    TitleConfig cfg;
    cfg.pcSettingsPages = true;
    TitleState st = titleState();
    // Without a schema the row still asks for the overlay.
    {
        TitleScreen scr(cfg);
        scr.open();
        scr.frame(st, {}, kVp);
        for (int i = 0; i < 5; ++i)
            scr.frame(st, keys({UiKey::Down}), kVp);
        TitleFrameOutput o = scr.frame(st, keys({UiKey::Confirm}), kVp);
        CHECK(!o.actions.empty() && o.actions.front().kind == TitleActionKind::OpenPcSettings);
    }
    st.pcSettings = &w.schema;
    TitleScreen scr(cfg);
    scr.open();
    scr.frame(st, {}, kVp);
    for (int i = 0; i < 5; ++i)
        scr.frame(st, keys({UiKey::Down}), kVp);
    CHECK(titleFocused(scr, TitleRow::PcSettings));
    TitleFrameOutput o = scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(o.actions.empty() && scr.page() == TitlePage::PcSettings && scr.pcSettingsOpen());
    CHECK(o.sound == UiSound::Confirm && hasText(o.draw, "Display") && !hasText(o.draw, "New Game"));
    CHECK(!scr.suspended() && scr.pcSettings().view() == SettingsView::Landing);
    // Into Display, change the first toggle-able row: the edit arrives as a SettingsAction.
    o = scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(scr.pcSettings().view() == SettingsView::Page);
    bool changed = false;
    for (int i = 0; i < 12 && !changed; ++i) {
        o = scr.frame(st, keys({UiKey::Right}), kVp);
        changed = !o.settings.empty();
        if (!changed)
            scr.frame(st, keys({UiKey::Down}), kVp);
    }
    CHECK(changed && o.settings.front().kind == SettingsActionKind::Change);
    // Back twice: the landing page, then the main menu with PC Settings focused, drawn the same frame.
    o = scr.frame(st, keys({UiKey::Back}), kVp);
    while (scr.pcSettings().card() != SettingsCard::None) // a keep card for a display change: revert
        o = scr.frame(st, keys({UiKey::Back}), kVp);
    if (scr.pcSettings().view() != SettingsView::Landing)
        o = scr.frame(st, keys({UiKey::Back}), kVp);
    CHECK(scr.pcSettings().view() == SettingsView::Landing);
    o = scr.frame(st, keys({UiKey::Back}), kVp);
    CHECK(scr.page() == TitlePage::Main && titleFocused(scr, TitleRow::PcSettings) && hasText(o.draw, "New Game"));
    CHECK(!scr.pcSettingsOpen());
    // The schema goes away while the pages are open: back to the menu.
    scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(scr.page() == TitlePage::PcSettings);
    TitleState noSchema = st;
    noSchema.pcSettings = nullptr;
    scr.frame(noSchema, {}, kVp);
    CHECK(scr.page() == TitlePage::Main);
    // Options > PC Settings: the pages inside the embedded Options page.
    TitleScreen opt(cfg);
    opt.open();
    opt.frame(st, {}, kVp);
    opt.frame(st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(opt.page() == TitlePage::Options);
    o = opt.frame(st, keys({UiKey::Down, UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(o.optionActions.empty() && opt.pcSettingsOpen() && opt.page() == TitlePage::Options);
    CHECK(&opt.pcSettings() == &const_cast<PauseScreen&>(opt.options()).pcSettings());
    o = opt.frame(st, keys({UiKey::Back}), kVp);
    CHECK(!opt.pcSettingsOpen() && opt.page() == TitlePage::Options);
}

// PauseScreen embedded with rootPage = Options (as the title uses it), without TitleScreen.
void testPauseRootPage() {
    PauseConfig cfg;
    cfg.hostPages = true;
    cfg.rootPage = PausePage::Options;
    cfg.statusPanel = false;
    PauseScreen scr(cfg);
    scr.open();
    const Snapshot s;
    PauseState ps;
    PauseFrameOutput o = scr.frame(s, ps, {}, kVp);
    CHECK(scr.page() == PausePage::Options && o.actions.empty());
    CHECK(scr.focus() == (TargetId{TargetKind::MenuRow, static_cast<int>(PauseRow::Controller)}));
    CHECK(hint(scr.hints(), Prompt::Back)->label == "Back");
    o = scr.frame(s, ps, keys({UiKey::Back}), kVp);
    CHECK(o.wantsBack && !o.wantsResume && o.actions.empty() && !scr.isOpen());
    // The normal pause still resumes from its root.
    PauseScreen normal;
    normal.open();
    normal.frame(s, ps, {}, kVp);
    o = normal.frame(s, ps, keys({UiKey::Back}), kVp);
    CHECK(o.wantsResume && !o.wantsBack);
}

} // namespace

// Loading / saving feedback: the confirm shows its progress card in the same frame; the glue's own load (Continue)
// shows it from the first frame; reactivate() (the load or save did not happen) brings the list back. Sounds: the
// confirm clicks, Back and a card's Cancel use the back sound.
void testProgressFeedback() {
    SaveLoadScreen scr;
    scr.open(SaveLoadMode::Load, SaveLoadContext::Title);
    const SaveSlotsState st = page();
    scr.frame(st, {}, kVp);
    SaveLoadFrameOutput o = scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(find(o, SaveLoadActionKind::ConfirmLoad) && o.sound == UiSound::Confirm);
    CHECK(scr.progress() == SaveProgress::Loading && hasText(o.draw, "Loading...") &&
          hasText(o.draw, "Slot 1: Surdana."));
    o = scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp); // no input while it runs
    CHECK(o.actions.empty() && hasText(o.draw, "Loading..."));
    scr.reactivate();
    o = scr.frame(st, {}, kVp);
    CHECK(scr.progress() == SaveProgress::None && !hasText(o.draw, "Loading..."));
    // Continue: the glue names the slot it loads.
    SaveLoadScreen c;
    c.open(SaveLoadMode::Load, SaveLoadContext::Title);
    SaveSlotsState reading;
    reading.card = CardState::Reading;
    reading.loadingSlot = 6;
    o = c.frame(reading, {}, kVp);
    CHECK(c.progress() == SaveProgress::Loading && hasText(o.draw, "Loading...") && hasText(o.draw, "Slot 6."));
    // Saving into an empty slot.
    SaveLoadScreen sv;
    sv.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    sv.frame(st, {}, kVp);
    o = sv.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(find(o, SaveLoadActionKind::ConfirmSave) && sv.progress() == SaveProgress::Saving &&
          hasText(o.draw, "Saving..."));
    // Back sounds.
    SaveLoadScreen b;
    b.open(SaveLoadMode::Save, SaveLoadContext::Pause);
    b.frame(st, {}, kVp);
    o = b.frame(st, keys({UiKey::Confirm}), kVp); // a used slot: the overwrite card
    CHECK(b.card() == SaveCard::Overwrite && o.sound == UiSound::Confirm);
    o = b.frame(st, keys({UiKey::Back}), kVp);
    CHECK(b.card() == SaveCard::None && o.sound == UiSound::Back);
    o = b.frame(st, keys({UiKey::Back}), kVp);
    CHECK(o.wantsBack && o.sound == UiSound::Back);
}

void testTitleContinueFeedback() {
    TitleState st = titleState();
    TitleScreen scr;
    scr.open();
    scr.frame(st, {}, kVp);
    TitleFrameOutput o = scr.frame(st, keys({UiKey::Confirm}), kVp);
    CHECK(!o.actions.empty() && o.actions.front().kind == TitleActionKind::Continue);
    CHECK(scr.loading() && hasText(o.draw, "Loading...") && o.sound == UiSound::Confirm);
    o = scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(o.actions.empty() && hasText(o.draw, "Loading...")); // no input while it runs
    scr.reactivate();
    o = scr.frame(st, {}, kVp);
    CHECK(!scr.loading() && !hasText(o.draw, "Loading..."));
    // Load Game: no loading card (the Load screen opens).
    o = scr.frame(st, keys({UiKey::Down, UiKey::Confirm}), kVp);
    CHECK(!scr.loading() && !hasText(o.draw, "Loading..."));
}

int main() {
    for (int operation : {1, 2}) {
        SaveLoadScreen scr;
        scr.open(operation == 1 ? SaveLoadMode::Save : SaveLoadMode::Load, SaveLoadContext::Pause);
        SaveSlotsState st;
        st.quickOperation = operation;
        st.busy = true;
        const auto out = scr.frame(st, keys({UiKey::Confirm}), kVp);
        CHECK(out.actions.empty());
        CHECK(hasText(out.draw, operation == 1 ? "Quick saving..." : "Quick loading..."));
        CHECK(!hasText(out.draw, "Save Game") && !hasText(out.draw, "Load Game"));
    }
    testReader();
    testFormatting();
    testLoadFromTitle();
    testLoadInGameAsks();
    testSave();
    testPages();
    testBackAndStates();
    testMouse();
    testSaveLoadAspects();
    testQueue();
    testTitleRows();
    testTitleActions();
    testTitleOptions();
    testTitleLayout();
    testTitlePcSettings();
    testPauseRootPage();
    testProgressFeedback();
    testTitleContinueFeedback();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
