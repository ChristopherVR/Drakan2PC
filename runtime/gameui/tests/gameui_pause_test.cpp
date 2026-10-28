// Unit tests for the modern pause menu's phase 2 / 3 pages (PauseScreen with hostPages / quitCard) and the options
// model (PauseOptionsState, readPauseOptions). Synthetic data only.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ScreenFixture.h"
#include "gameui/Addresses.h"
#include "gameui/PauseOptions.h"
#include "gameui/PauseScreen.h"

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

UiInput keys(std::initializer_list<UiKey> k, InputDevice dev = InputDevice::KeyboardMouse) {
    UiInput in;
    in.keys = k;
    in.device = dev;
    return in;
}

UiInput pointer(float x, float y, bool click = false, bool down = false) {
    UiInput in;
    in.pointerValid = true;
    in.pointerMoved = true;
    in.pointerX = x;
    in.pointerY = y;
    in.leftClick = click;
    in.leftDown = down || click;
    return in;
}

bool inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f && inner.right() <= outer.right() + 0.5f &&
           inner.bottom() <= outer.bottom() + 0.5f;
}

bool hasText(const DrawList& d, const std::string& s) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Text && c.text == s)
            return true;
    return false;
}

const HitTarget* rowTarget(const PauseScreen& scr, PauseRow r) {
    for (const HitTarget& t : scr.targets())
        if (t.id.kind == TargetKind::MenuRow && t.id.index == static_cast<int>(r))
            return &t;
    return nullptr;
}

const Hint* hint(const std::vector<Hint>& hs, Prompt p) {
    for (const Hint& h : hs)
        if (h.prompt == p)
            return &h;
    return nullptr;
}

const PauseAction* findAction(const PauseFrameOutput& o, PauseActionKind k) {
    for (const PauseAction& a : o.actions)
        if (a.kind == k)
            return &a;
    return nullptr;
}

bool focused(const PauseScreen& scr, PauseRow r) {
    return scr.focus() == TargetId{TargetKind::MenuRow, static_cast<int>(r)};
}

PauseState validState() {
    PauseState st;
    st.options.valid = true;
    st.options.set(PauseOption::MasterVolume, 50);
    st.options.set(PauseOption::MusicVolume, 80);
    st.options.set(PauseOption::Subtitles, 1);
    st.options.set(PauseOption::Stereo, 0);
    st.options.set(PauseOption::Scheme, 0);
    st.options.set(PauseOption::Vibration, 1);
    st.options.set(PauseOption::ReversePitch, 0);
    return st;
}

PauseConfig hostConfig() {
    PauseConfig cfg;
    cfg.hostPages = true;
    cfg.quitCard = true;
    return cfg;
}

// Root -> Options -> `page` (Audio or Controller) with the keyboard.
void openPage(PauseScreen& scr, const Snapshot& s, const PauseState& st, const Viewport& vp, PausePage page) {
    scr.open();
    scr.frame(s, st, {}, vp);
    scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp); // Options
    if (page == PausePage::Audio)
        scr.frame(s, st, keys({UiKey::Down, UiKey::Confirm}), vp);
    else
        scr.frame(s, st, keys({UiKey::Confirm}), vp);
}

// ---- model ----

void testReadOptions() {
    std::vector<uint8_t> ram(0x500000, 0);
    auto put = [&](uint32_t a, int32_t v) {
        for (int i = 0; i < 4; ++i)
            ram[a + i] = static_cast<uint8_t>(static_cast<uint32_t>(v) >> (8 * i));
    };
    put(addr::options::kMasterVolume, 70);
    put(addr::options::kMusicVolume, 35);
    put(addr::options::kSubtitles, 1);
    put(addr::options::kStereo, 1);
    put(addr::options::kControllerScheme, 2);
    put(addr::options::kVibration, 0);
    put(addr::options::kReversePitch, 1);
    const FlatGuestMemory m(ram.data(), ram.size());
    PauseOptionsState o;
    CHECK(readPauseOptions(m, o) && o.valid);
    CHECK(o.get(PauseOption::MasterVolume) == 70 && o.get(PauseOption::MusicVolume) == 35);
    CHECK(o.get(PauseOption::Subtitles) == 1 && o.get(PauseOption::Stereo) == 1);
    CHECK(o.get(PauseOption::Scheme) == 2 && o.get(PauseOption::Vibration) == 0 && o.get(PauseOption::ReversePitch));
    // Out of range: invalid, stored clamped.
    put(addr::options::kMasterVolume, 250);
    put(addr::options::kControllerScheme, -1);
    CHECK(!readPauseOptions(m, o) && !o.valid);
    CHECK(o.get(PauseOption::MasterVolume) == 100 && o.get(PauseOption::Scheme) == 0);
    // The address table matches the note.
    CHECK(pauseOptionAddress(PauseOption::MasterVolume) == 0x4fb75c);
    CHECK(pauseOptionAddress(PauseOption::MusicVolume) == 0x4fb760);
    CHECK(pauseOptionAddress(PauseOption::Subtitles) == 0x4fb740);
    CHECK(pauseOptionAddress(PauseOption::Stereo) == 0x4fb744);
    CHECK(pauseOptionAddress(PauseOption::Scheme) == 0x4fb9b8);
    CHECK(pauseOptionAddress(PauseOption::Vibration) == 0x4fb73c);
    CHECK(pauseOptionAddress(PauseOption::ReversePitch) == 0x4fb734);
    CHECK(clampPauseOption(PauseOption::MusicVolume, -5) == 0 && clampPauseOption(PauseOption::Stereo, 7) == 1);
    for (int i = 0; i < kPauseOptionCount; ++i) {
        const auto opt = static_cast<PauseOption>(i);
        CHECK(pauseActionOption(pauseSetAction(opt)) == opt);
    }
    CHECK(!pauseActionOption(PauseActionKind::Resume));
}

void testQueueCoalescing() {
    PauseActionQueue q;
    const uint32_t a = q.push({0, PauseActionKind::EnterPage, static_cast<int32_t>(PausePage::Audio)});
    const uint32_t b = q.push({0, PauseActionKind::SetMasterVolume, 55});
    const uint32_t c = q.push({0, PauseActionKind::SetMasterVolume, 60});
    CHECK(b == c && b == a + 1 && q.size() == 2);
    q.push({0, PauseActionKind::SetMusicVolume, 10});
    q.push({0, PauseActionKind::SetMasterVolume, 65}); // not adjacent: kept in order
    CHECK(q.size() == 4);
    auto x = q.pop();
    CHECK(x && x->kind == PauseActionKind::EnterPage && x->value == static_cast<int32_t>(PausePage::Audio));
    x = q.pop();
    CHECK(x && x->kind == PauseActionKind::SetMasterVolume && x->value == 60);
    x = q.pop();
    CHECK(x && x->kind == PauseActionKind::SetMusicVolume && x->value == 10);
    x = q.pop();
    CHECK(x && x->kind == PauseActionKind::SetMasterVolume && x->value == 65);
    // Non-Set actions never merge.
    q.push({0, PauseActionKind::LeavePage, 2});
    q.push({0, PauseActionKind::LeavePage, 1});
    CHECK(q.size() == 2);
}

// ---- navigation and the Back stack ----

void testNavigation() {
    ScreenWorld w;
    const Snapshot s = w.read();
    PauseScreen scr(hostConfig());
    const PauseState st = validState();
    const Viewport vp{0, 0, 1920, 1080};
    scr.open();
    scr.frame(s, st, {}, vp);
    PauseFrameOutput o = scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(scr.page() == PausePage::Options && o.sound == UiSound::Confirm && !scr.suspended());
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::EnterPage &&
          o.actions[0].value == static_cast<int32_t>(PausePage::Options));
    CHECK(scr.pageStack() == std::vector<PausePage>{PausePage::Root});
    CHECK(focused(scr, PauseRow::Controller) && hasText(o.draw, "Options") && hasText(o.draw, "Paused"));

    o = scr.frame(s, st, keys({UiKey::Down, UiKey::Confirm}), vp);
    CHECK(scr.page() == PausePage::Audio && focused(scr, PauseRow::MasterVolume));
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::EnterPage &&
          o.actions[0].value == static_cast<int32_t>(PausePage::Audio));
    CHECK((scr.pageStack() == std::vector<PausePage>{PausePage::Root, PausePage::Options}));
    CHECK(hasText(o.draw, "Master Volume") && hasText(o.draw, "50") && hasText(o.draw, "80") && hasText(o.draw, "On") &&
          hasText(o.draw, "Off"));
    CHECK(hint(scr.hints(), Prompt::Adjust) && !hint(scr.hints(), Prompt::Confirm)); // slider: Adjust only
    CHECK(hint(scr.hints(), Prompt::Back)->label == "Back");

    // Back: LeavePage(Audio), focus back on the Audio row.
    o = scr.frame(s, st, keys({UiKey::Back}), vp);
    CHECK(scr.page() == PausePage::Options && focused(scr, PauseRow::Audio) && !o.wantsResume);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::LeavePage &&
          o.actions[0].value == static_cast<int32_t>(PausePage::Audio));

    // Controller page by mouse, back with the Back row, then right click.
    const HitTarget* ctl = rowTarget(scr, PauseRow::Controller);
    CHECK(ctl != nullptr);
    o = scr.frame(s, st, pointer(ctl->rect.cx(), ctl->rect.cy(), true), vp);
    CHECK(scr.page() == PausePage::Controller && focused(scr, PauseRow::Scheme));
    CHECK(hasText(o.draw, "Control Scheme") && hasText(o.draw, "A") && hasText(o.draw, "Key Bindings"));
    CHECK(hint(scr.hints(), Prompt::Confirm)->label == "Change");
    o = scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(scr.page() == PausePage::Options && focused(scr, PauseRow::Controller));
    CHECK(findAction(o, PauseActionKind::LeavePage) &&
          findAction(o, PauseActionKind::LeavePage)->value == static_cast<int32_t>(PausePage::Controller));
    UiInput rc;
    rc.rightClick = true;
    o = scr.frame(s, st, rc, vp);
    CHECK(scr.page() == PausePage::Root && focused(scr, PauseRow::Options) && scr.pageStack().empty());
    CHECK(findAction(o, PauseActionKind::LeavePage) &&
          findAction(o, PauseActionKind::LeavePage)->value == static_cast<int32_t>(PausePage::Options));
    o = scr.frame(s, st, keys({UiKey::Back}), vp);
    CHECK(o.wantsResume && !scr.isOpen());

    // Key Bindings opens the F1 Controls page and suspends; reactivate keeps the page.
    PauseScreen k(hostConfig());
    openPage(k, s, st, vp, PausePage::Controller);
    o = k.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(findAction(o, PauseActionKind::OpenKeyBindings) && k.suspended());
    k.reactivate();
    o = k.frame(s, st, {}, vp);
    CHECK(k.page() == PausePage::Controller && focused(k, PauseRow::KeyBindings));
    // PC Settings from the host Options page.
    k.frame(s, st, keys({UiKey::Back}), vp);
    o = k.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(findAction(o, PauseActionKind::OpenPcSettings) && k.suspended());
}

// ---- sliders ----

void testSliderKeysAndPad() {
    ScreenWorld w;
    const Snapshot s = w.read();
    const Viewport vp{0, 0, 1920, 1080};
    for (InputDevice dev : {InputDevice::KeyboardMouse, InputDevice::Gamepad}) {
        PauseScreen scr(hostConfig());
        PauseState st = validState();
        openPage(scr, s, st, vp, PausePage::Audio);
        PauseFrameOutput o = scr.frame(s, st, keys({UiKey::Right}, dev), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::SetMasterVolume &&
              o.actions[0].value == 55 && o.sound == UiSound::Slider);
        CHECK(scr.optionValue(PauseOption::MasterVolume, st) == 55 && hasText(o.draw, "55"));
        // Two presses in one frame: one action with the last value.
        o = scr.frame(s, st, keys({UiKey::Right, UiKey::Right}, dev), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 65);
        o = scr.frame(s, st, keys({UiKey::Left}, dev), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 60);
        // Music: clamps at 100, no action once there.
        o = scr.frame(
            s, st, keys({UiKey::Down, UiKey::Right, UiKey::Right, UiKey::Right, UiKey::Right, UiKey::Right}, dev), vp);
        CHECK(focused(scr, PauseRow::MusicVolume) && o.actions.size() == 1 &&
              o.actions[0].kind == PauseActionKind::SetMusicVolume && o.actions[0].value == 100);
        o = scr.frame(s, st, keys({UiKey::Right}, dev), vp);
        CHECK(o.actions.empty() && scr.optionValue(PauseOption::MusicVolume, st) == 100);
        // Confirm on a slider does nothing.
        o = scr.frame(s, st, keys({UiKey::Confirm}, dev), vp);
        CHECK(o.actions.empty());
        // Clamp at 0.
        st.options.set(PauseOption::MasterVolume, 3);
        PauseScreen z(hostConfig());
        openPage(z, s, st, vp, PausePage::Audio);
        o = z.frame(s, st, keys({UiKey::Left}, dev), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 0);
        o = z.frame(s, st, keys({UiKey::Left}, dev), vp);
        CHECK(o.actions.empty());
    }
}

void testSliderMouse() {
    ScreenWorld w;
    const Snapshot s = w.read();
    for (const Viewport& vp : kAspects) {
        PauseScreen scr(hostConfig());
        const PauseState st = validState();
        openPage(scr, s, st, vp, PausePage::Audio);
        const Rect track = scr.sliderTrack(PauseRow::MasterVolume);
        const HitTarget* row = rowTarget(scr, PauseRow::MasterVolume);
        CHECK(!track.empty() && row && inside(track, row->rect));
        const float y = track.cy();
        // Click on the label: focus only.
        PauseFrameOutput o = scr.frame(s, st, pointer(row->rect.x + row->rect.w * 0.3f, row->rect.y + 4, true), vp);
        CHECK(o.actions.empty() && !scr.dragging());
        // Click on the track at 25 %: jumps there, starts a drag.
        o = scr.frame(s, st, pointer(track.x + track.w * 0.25f, y, true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::SetMasterVolume &&
              o.actions[0].value == 25 && scr.dragging());
        // Drag to 75 %, then past the end (and off the row): 100; the focus stays.
        o = scr.frame(s, st, pointer(track.x + track.w * 0.75f, y, false, true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 75 && hasText(o.draw, "75"));
        o = scr.frame(s, st, pointer(track.right() + 400, vp.y + vp.h * 0.9f, false, true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 100 && focused(scr, PauseRow::MasterVolume));
        o = scr.frame(s, st, pointer(track.x - 50, y, false, true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 0);
        // Not moving: no action.
        o = scr.frame(s, st, pointer(track.x - 50, y, false, true), vp);
        CHECK(o.actions.empty());
        // Release; moving afterwards changes nothing.
        o = scr.frame(s, st, pointer(track.x + track.w * 0.5f, y, false, false), vp);
        CHECK(!scr.dragging() && o.actions.empty());
        o = scr.frame(s, st, pointer(track.x + track.w * 0.6f, y), vp);
        CHECK(o.actions.empty() && scr.optionValue(PauseOption::MasterVolume, st) == 0);
        // Click-to-position is aspect independent: the middle is 50.
        o = scr.frame(s, st, pointer(track.cx(), y, true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].value == 50);
        // Music slider by click while Master is focused.
        const Rect mt = scr.sliderTrack(PauseRow::MusicVolume);
        o = scr.frame(s, st, pointer(mt.x + mt.w * 0.1f, mt.cy(), true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::SetMusicVolume &&
              o.actions[0].value == 10 && focused(scr, PauseRow::MusicVolume));
        // Every target inside the viewport and the 16:9 safe area.
        const LayoutFrame lf = computeFrame(vp);
        for (const HitTarget& t : scr.targets())
            CHECK(inside(t.rect, lf.safe));
    }
}

void testOverrideAndGuestValue() {
    ScreenWorld w;
    const Snapshot s = w.read();
    const Viewport vp{0, 0, 1920, 1080};
    PauseConfig cfg = hostConfig();
    cfg.optionHoldFrames = 5;
    PauseScreen scr(cfg);
    PauseState st = validState();
    openPage(scr, s, st, vp, PausePage::Audio);
    scr.frame(s, st, keys({UiKey::Right}), vp);
    CHECK(scr.optionValue(PauseOption::MasterVolume, st) == 55);
    // The guest applied it: the override ends, the guest's later value shows.
    st.options.set(PauseOption::MasterVolume, 55);
    scr.frame(s, st, {}, vp);
    st.options.set(PauseOption::MasterVolume, 40);
    CHECK(scr.optionValue(PauseOption::MasterVolume, st) == 40);
    // Never applied: after the hold time the guest's value is shown again.
    scr.frame(s, st, keys({UiKey::Right}), vp);
    CHECK(scr.optionValue(PauseOption::MasterVolume, st) == 45);
    for (int i = 0; i < 6; ++i)
        scr.frame(s, st, {}, vp);
    CHECK(scr.optionValue(PauseOption::MasterVolume, st) == 40);
}

// ---- toggles and the scheme selector ----

void testTogglesAndScheme() {
    ScreenWorld w;
    const Snapshot s = w.read();
    const Viewport vp{0, 0, 2560, 1080};
    PauseScreen scr(hostConfig());
    const PauseState st = validState();
    openPage(scr, s, st, vp, PausePage::Audio);
    PauseFrameOutput o = scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(focused(scr, PauseRow::Subtitles) && o.actions.size() == 1 &&
          o.actions[0].kind == PauseActionKind::SetSubtitles && o.actions[0].value == 0);
    CHECK(hint(scr.hints(), Prompt::Confirm)->label == "Toggle");
    o = scr.frame(s, st, keys({UiKey::Left}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].value == 1);
    const HitTarget* stereo = rowTarget(scr, PauseRow::Stereo);
    o = scr.frame(s, st, pointer(stereo->rect.cx(), stereo->rect.cy(), true), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::SetStereo && o.actions[0].value == 1);
    CHECK(hasText(o.draw, "On"));

    PauseScreen c(hostConfig());
    openPage(c, s, st, vp, PausePage::Controller);
    o = c.frame(s, st, keys({UiKey::Left}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::SetControllerScheme &&
          o.actions[0].value == 2 && hasText(o.draw, "C"));
    o = c.frame(s, st, keys({UiKey::Right}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].value == 0);
    o = c.frame(s, st, keys({UiKey::Confirm}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].value == 1 && hasText(o.draw, "B"));
    // Mouse: the "<" steps back, the rest of the row steps on.
    const Rect prev = computeFrame(vp).map(design::pause::choicePrev(0));
    o = c.frame(s, st, pointer(prev.cx(), prev.cy(), true), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].value == 0);
    const HitTarget* sch = rowTarget(c, PauseRow::Scheme);
    o = c.frame(s, st, pointer(sch->rect.x + 40, sch->rect.cy(), true), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].value == 1);
    o = c.frame(s, st, keys({UiKey::Down, UiKey::Confirm, UiKey::Down, UiKey::Right}), vp);
    CHECK(o.actions.size() == 2 && o.actions[0].kind == PauseActionKind::SetVibration && o.actions[0].value == 0 &&
          o.actions[1].kind == PauseActionKind::SetReversePitch && o.actions[1].value == 1);
}

// ---- disabled entries ----

void testDisabled() {
    ScreenWorld w;
    const Snapshot s = w.read();
    const Viewport vp{0, 0, 1920, 1080};
    PauseScreen scr(hostConfig());
    PauseState st; // options not read
    openPage(scr, s, st, vp, PausePage::Audio);
    CHECK(scr.page() == PausePage::Audio);
    // Every value row is disabled: the focus lands on Back.
    CHECK(focused(scr, PauseRow::Back));
    const HitTarget* m = rowTarget(scr, PauseRow::MasterVolume);
    CHECK(m && !m->enabled && !m->navigable);
    PauseFrameOutput o = scr.frame(s, st, keys({UiKey::Up, UiKey::Left, UiKey::Right}), vp);
    CHECK(o.actions.empty() && focused(scr, PauseRow::Back));
    const Rect track = scr.sliderTrack(PauseRow::MasterVolume);
    o = scr.frame(s, st, pointer(track.cx(), track.cy(), true), vp);
    CHECK(o.actions.empty() && !scr.dragging() && hasText(o.draw, "--"));
    // The options become readable: the rows enable, the focus stays on Back.
    st = validState();
    o = scr.frame(s, st, keys({UiKey::Up}), vp);
    CHECK(focused(scr, PauseRow::Stereo));
    // A drag ends when the row becomes disabled.
    o = scr.frame(s, st, pointer(track.cx(), track.cy(), true), vp);
    CHECK(scr.dragging());
    st.options.valid = false;
    o = scr.frame(s, st, pointer(track.x, track.cy(), false, true), vp);
    CHECK(!scr.dragging() && o.actions.empty());
    // Quit to title disabled on the card.
    PauseScreen q(hostConfig());
    PauseState qs = validState();
    qs.canQuitToTitle = false;
    q.open();
    q.frame(s, qs, {}, vp);
    o = q.frame(s, qs, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(q.page() == PausePage::QuitConfirm && focused(q, PauseRow::QuitToDesktop));
    const HitTarget* title = rowTarget(q, PauseRow::QuitToTitle);
    CHECK(title && !title->enabled);
    o = q.frame(s, qs, pointer(title->rect.cx(), title->rect.cy(), true), vp);
    CHECK(o.actions.empty());
}

// ---- confirm cards ----

void testCards() {
    ScreenWorld w;
    const Snapshot s = w.read();
    for (const Viewport& vp : kAspects) {
        const PauseState st = validState();
        // Default Settings: card with Cancel focused; Cancel returns, Restore emits RestoreDefaults.
        PauseScreen scr(hostConfig());
        scr.open();
        scr.frame(s, st, {}, vp);
        scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
        PauseFrameOutput o = scr.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
        CHECK(scr.page() == PausePage::DefaultsConfirm && focused(scr, PauseRow::Cancel) && o.actions.empty());
        CHECK(hasText(o.draw, "Restore default settings?") && hint(scr.hints(), Prompt::Back)->label == "Cancel");
        const Rect card = computeFrame(vp).map(design::pause::cardPanel(2));
        for (const HitTarget& t : scr.targets())
            if (t.id.kind == TargetKind::MenuRow)
                CHECK(inside(t.rect, card));
        o = scr.frame(s, st, keys({UiKey::Confirm}), vp);
        CHECK(scr.page() == PausePage::Options && focused(scr, PauseRow::Defaults) && o.actions.empty());
        scr.frame(s, st, keys({UiKey::Confirm}), vp);
        const HitTarget* restore = rowTarget(scr, PauseRow::RestoreDefaults);
        CHECK(restore != nullptr);
        o = scr.frame(s, st, pointer(restore->rect.cx(), restore->rect.cy(), true), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::RestoreDefaults);
        CHECK(scr.page() == PausePage::Options && !scr.suspended());

        // Quit card: Quit to title focused; Back returns to the root; Desktop emits and suspends.
        PauseScreen q(hostConfig());
        q.open();
        q.frame(s, st, {}, vp);
        o = q.frame(s, st, keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
        CHECK(q.page() == PausePage::QuitConfirm && focused(q, PauseRow::QuitToTitle) && o.actions.empty());
        CHECK(hasText(o.draw, "Quit the game?") && hasText(o.draw, "Quit to Desktop"));
        o = q.frame(s, st, keys({UiKey::Back}), vp);
        CHECK(q.page() == PausePage::Root && focused(q, PauseRow::Quit) && !o.wantsResume && o.actions.empty());
        q.frame(s, st, keys({UiKey::Confirm}), vp);
        o = q.frame(s, st, keys({UiKey::Down, UiKey::Confirm}), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::QuitToDesktop && q.suspended());
        q.reactivate();
        q.frame(s, st, keys({UiKey::Up}), vp);
        o = q.frame(s, st, keys({UiKey::Confirm}), vp);
        CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::QuitToTitle);
    }
    // Without quitCard Quit stays the native box (phase 1).
    PauseConfig plain;
    plain.hostPages = true;
    PauseScreen p(plain);
    const Viewport vp{0, 0, 1920, 1080};
    p.open();
    p.frame(s, validState(), {}, vp);
    PauseFrameOutput o =
        p.frame(s, validState(), keys({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}), vp);
    CHECK(o.actions.size() == 1 && o.actions[0].kind == PauseActionKind::Quit);
}

// ---- layout ----

void testAspect() {
    ScreenWorld w;
    const Snapshot s = w.read();
    for (const Viewport& vp : kAspects) {
        for (PausePage page : {PausePage::Audio, PausePage::Controller}) {
            PauseScreen scr(hostConfig());
            const PauseState st = validState();
            openPage(scr, s, st, vp, page);
            CHECK(scr.page() == page);
            const LayoutFrame lf = computeFrame(vp);
            const Rect panel = lf.map(design::pause::menuPanel());
            int rows = 0;
            for (const HitTarget& t : scr.targets()) {
                CHECK(inside(t.rect, lf.safe));
                if (t.id.kind == TargetKind::MenuRow) {
                    CHECK(inside(t.rect, panel));
                    ++rows;
                }
            }
            CHECK(rows == 5);
            // Row heights scale with the viewport height only (the 16:9 safe area).
            const HitTarget* back = rowTarget(scr, PauseRow::Back);
            CHECK(back && std::fabs(back->rect.h - 88 * lf.scale) < 0.5f);
        }
    }
}

} // namespace

int main() {
    testReadOptions();
    testQueueCoalescing();
    testNavigation();
    testSliderKeysAndPad();
    testSliderMouse();
    testOverrideAndGuestValue();
    testTogglesAndScheme();
    testDisabled();
    testCards();
    testAspect();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
