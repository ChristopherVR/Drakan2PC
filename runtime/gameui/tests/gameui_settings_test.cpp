// SettingsPages (the modern PC Settings pages) over the synthetic schema of SettingsFixture.h, and the PauseScreen
// hook (PauseConfig::pcSettingsPages).
#include <cstdio>
#include <string>
#include <utility>

#include "SettingsFixture.h"
#include "gameui/PauseScreen.h"
#include "gameui/SettingsPages.h"
#include "gameui/TitleScreen.h"

using namespace gameui;

static int checks = 0, failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(c)) {                                                                                                    \
            ++failures;                                                                                                \
            std::printf("FAIL %d: %s\n", __LINE__, #c);                                                                \
        }                                                                                                              \
    } while (0)

namespace {

// Drives the pages like the glue: every Change is applied to the schema before the next frame.
struct Driver {
    test::SettingsWorld w;
    SettingsPages pg;
    double t = 1.0;
    Viewport vp{0, 0, 1920, 1080};
    std::vector<SettingsAction> all;
    bool autoApply = true;

    explicit Driver(const std::string& page = {}) {
        pg.open(page);
        frame();
    }
    SettingsFrameOutput frame(SettingsInput in = {}) {
        t += 1.0 / 60.0;
        in.now = t;
        SettingsFrameOutput o = pg.frame(w.schema, in, vp);
        for (const SettingsAction& a : o.actions) {
            all.push_back(a);
            if (autoApply && a.kind == SettingsActionKind::Change)
                w.apply(a.key, a.value);
        }
        return o;
    }
    SettingsFrameOutput keys(std::initializer_list<UiKey> k) {
        SettingsInput in;
        in.ui.keys = k;
        return frame(in);
    }
    SettingsFrameOutput click(float x, float y, bool down = false) {
        SettingsInput in;
        in.ui.pointerValid = in.ui.pointerMoved = in.ui.leftClick = true;
        in.ui.leftDown = down;
        in.ui.pointerX = x;
        in.ui.pointerY = y;
        return frame(in);
    }
    const SettingRow* row(const std::string& k) const { return w.schema.find(k); }
};

bool hasText(const DrawList& d, const std::string& s) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Text && c.text.find(s) != std::string::npos)
            return true;
    return false;
}

void testLandingAndPages() {
    Driver d;
    CHECK(d.pg.view() == SettingsView::Landing);
    // Display, Audio, Key Bindings, Back.
    int rows = 0;
    for (const HitTarget& t : d.pg.targets())
        rows += t.id.kind == TargetKind::MenuRow;
    CHECK(rows == 4);
    SettingsFrameOutput o = d.keys({UiKey::Confirm});
    CHECK(d.pg.view() == SettingsView::Page && d.pg.pageIndex() == 0);
    CHECK(o.sound == UiSound::Confirm);
    CHECK(d.pg.focusedKey() == "video.window_mode");
    o = d.frame();
    CHECK(hasText(o.draw, "Window mode") && hasText(o.draw, "Windowed"));
    CHECK(!hasText(o.draw, "Custom window size")); // OverlayOnly rows are not shown
    CHECK(hasText(o.draw, "PC Settings"));         // breadcrumb
    // The help panel shows the focused row's help.
    CHECK(hasText(o.draw, "recommended"));
    // Next page with RB; Back returns to the landing with the focus on that category; Back again leaves.
    d.keys({UiKey::PageNext});
    CHECK(d.pg.pageIndex() == 1 && d.pg.focusedKey() == "audio.enabled");
    o = d.frame();
    CHECK(hasText(o.draw, "separate sliders")); // the page note
    o = d.keys({UiKey::Back});
    CHECK(d.pg.view() == SettingsView::Landing && o.sound == UiSound::Back);
    o = d.keys({UiKey::Confirm});
    CHECK(d.pg.pageIndex() == 1);
    d.keys({UiKey::Back});
    o = d.keys({UiKey::Back});
    CHECK(o.wantsClose && !d.pg.isOpen());
}

void testToggleChoiceSlider() {
    Driver d("display");
    CHECK(d.pg.view() == SettingsView::Page);
    // Down to VSync: window mode, size, fov, scale, then Presentation's VSync.
    for (int i = 0; i < 4; ++i)
        d.keys({UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.vsync");
    SettingsFrameOutput t = d.keys({UiKey::Confirm});
    CHECK(!d.all.empty() && d.all.back().kind == SettingsActionKind::Change && d.all.back().key == "video.vsync" &&
          d.all.back().value == "0");
    CHECK(t.sound == UiSound::Toggle);
    CHECK(!d.row("video.vsync")->on);
    // A disabled toggle: focusable, no change.
    d.keys({UiKey::Down, UiKey::Down});
    d.keys({UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.texture_dump");
    const size_t n = d.all.size();
    d.keys({UiKey::Confirm, UiKey::Right});
    CHECK(d.all.size() == n);
    SettingsFrameOutput o = d.frame();
    CHECK(hasText(o.draw, "Unavailable") || hasText(o.draw, "not available"));
    // Slider: Right steps by `step`.
    d.keys({UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.mip_bias");
    t = d.keys({UiKey::Right});
    CHECK(d.all.back().key == "video.mip_bias" && d.all.back().value == "0.05" && d.all.back().final);
    CHECK(t.sound == UiSound::Slider);
    // Info rows are not focusable: Down skips the status line.
    d.keys({UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.hd_textures");
}

void testChoicesAndKeepCard() {
    Driver d("display");
    // Right on window mode: borderless (exclusive is disabled and is skipped on the way).
    d.keys({UiKey::Right});
    CHECK(d.all.back().key == "video.window_mode" && d.all.back().value == "borderless");
    CHECK(d.pg.card() == SettingsCard::KeepDisplay);
    SettingsFrameOutput o = d.frame();
    CHECK(hasText(o.draw, "Keep these display settings?") && hasText(o.draw, "Reverting in 12"));
    // Time out: the previous value comes back as a revert.
    d.t += 13;
    d.frame();
    CHECK(d.pg.card() == SettingsCard::None);
    CHECK(d.all.back().key == "video.window_mode" && d.all.back().value == "windowed" && d.all.back().revert);
    CHECK(d.row("video.window_mode")->currentValue == "windowed");
    // Right past the end: exclusive is disabled, nothing after it.
    d.keys({UiKey::Right});
    d.keys({UiKey::Confirm}); // Keep
    CHECK(d.pg.card() == SettingsCard::None && d.row("video.window_mode")->currentValue == "borderless");
    const size_t n = d.all.size();
    d.keys({UiKey::Right});
    CHECK(d.all.size() == n);
    // The list card: Confirm opens it on the current value; Down twice and Confirm picks.
    d.keys({UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.window_size");
    d.keys({UiKey::Confirm});
    CHECK(d.pg.card() == SettingsCard::Choices);
    o = d.frame();
    CHECK(hasText(o.draw, "1280 x 960  (4:3)") && hasText(o.draw, "1600 x 900"));
    d.keys({UiKey::Down, UiKey::Down});
    d.keys({UiKey::Confirm});
    CHECK(d.all.back().key == "video.window_size" && d.all.back().value == "1280x720");
    CHECK(d.pg.card() == SettingsCard::KeepDisplay); // a risky row
    d.keys({UiKey::Back});                           // Back on the keep card reverts
    CHECK(d.all.back().value == "1280x960" && d.all.back().revert);
    // Back on a list card only closes it.
    d.keys({UiKey::Confirm});
    CHECK(d.pg.card() == SettingsCard::Choices);
    const size_t m = d.all.size();
    d.keys({UiKey::Back});
    CHECK(d.pg.card() == SettingsCard::None && d.all.size() == m);
}

void testRestartAndProblem() {
    Driver d("display");
    d.keys({UiKey::Down, UiKey::Down, UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.scale");
    SettingsFrameOutput o = d.frame();
    CHECK(hasText(o.draw, "Takes effect after a restart"));
    d.keys({UiKey::Right});
    CHECK(d.all.back().key == "video.scale" && d.all.back().value == "1");
    SettingsFeedback f;
    f.restartReasons = {"Internal render scale"};
    d.pg.onApplied(f);
    CHECK(d.pg.card() == SettingsCard::Restart);
    o = d.frame();
    CHECK(hasText(o.draw, "Restart required") && hasText(o.draw, "Internal render scale"));
    // Focus starts on Later; Up + Confirm = Restart Now.
    d.keys({UiKey::Up});
    d.keys({UiKey::Confirm});
    CHECK(d.all.back().kind == SettingsActionKind::Restart && d.all.back().value == "Internal render scale");
    CHECK(d.pg.card() == SettingsCard::None);
    CHECK(d.pg.restartNeeded().size() == 1);
    o = d.frame();
    CHECK(hasText(o.draw, "Restart to apply"));
    // A keep card first, the restart question after Keep.
    d.keys({UiKey::Up, UiKey::Up, UiKey::Up});
    d.keys({UiKey::Right});
    CHECK(d.pg.card() == SettingsCard::KeepDisplay);
    d.pg.onApplied(f);
    CHECK(d.pg.card() == SettingsCard::KeepDisplay);
    d.keys({UiKey::Confirm});
    CHECK(d.pg.card() == SettingsCard::Restart);
    d.keys({UiKey::Confirm}); // Later
    CHECK(d.pg.card() == SettingsCard::None);
    // A problem.
    SettingsFeedback bad;
    bad.ok = false;
    bad.error = "cannot write drakan.ini (access denied)";
    d.pg.onApplied(bad);
    CHECK(d.pg.card() == SettingsCard::Problem);
    o = d.frame();
    CHECK(hasText(o.draw, "access denied"));
    d.keys({UiKey::Confirm});
    CHECK(d.pg.card() == SettingsCard::None);
}

void testPointer() {
    Driver d("audio");
    // Click on the volume track at 25 %: a live (not final) change and a drag; release: the final value.
    const Rect t = d.pg.valueRect("audio.volume");
    CHECK(!t.empty());
    d.click(t.x + t.w * 0.25f, t.cy(), true);
    CHECK(d.pg.dragging());
    CHECK(d.all.back().key == "audio.volume" && d.all.back().value == "50" && !d.all.back().final);
    SettingsInput in;
    in.ui.pointerValid = in.ui.pointerMoved = in.ui.leftDown = true;
    in.ui.pointerX = t.x + t.w * 0.75f;
    in.ui.pointerY = t.y - 200; // anywhere on screen while held
    d.frame(in);
    CHECK(d.all.back().value == "150" && !d.all.back().final);
    in.ui.leftDown = false;
    d.frame(in);
    CHECK(!d.pg.dragging() && d.all.back().value == "150" && d.all.back().final);
    // Toggle by click.
    const Rect s = d.pg.valueRect("audio.enabled");
    d.click(s.cx(), s.cy());
    CHECK(d.all.back().key == "audio.enabled" && d.all.back().value == "0");
    // Choice arrows: ">" steps, the middle opens the list.
    const Rect c = d.pg.valueRect("audio.device");
    d.click(c.right() - 4, c.cy());
    CHECK(d.all.back().key == "audio.device" && d.all.back().value == "Speakers");
    d.click(c.cx(), c.cy());
    CHECK(d.pg.card() == SettingsCard::Choices);
    // Right click closes the card, then leaves the page.
    SettingsInput rc;
    rc.ui.rightClick = true;
    d.frame(rc);
    CHECK(d.pg.card() == SettingsCard::None);
    // A tab click switches pages and keeps the focus on the clicked tab (not on the new page's first row, which showed
    // as focused under the pointer until the mouse moved).
    const HitTarget tab = d.pg.targets()[0]; // tab 0 = Display
    d.click(tab.rect.cx(), tab.rect.cy());
    CHECK(d.pg.pageIndex() == 0);
    CHECK(d.pg.focusedKey().empty());
    CHECK(d.pg.focus() == d.pg.targets()[0].id && d.pg.focus() == tab.id);
}

void testScroll() {
    Driver d("display");
    CHECK(d.pg.scroll() == 0);
    for (int i = 0; i < 12; ++i)
        d.keys({UiKey::Down});
    CHECK(d.pg.focusedKey() == "video.smooth_menu_highlight");
    CHECK(d.pg.scroll() > 0);
    // The focused row is inside the list area.
    const Rect list = computeFrame(d.vp).map(SettingsPages::listArea());
    for (const HitTarget& t : d.pg.targets())
        if (t.id == d.pg.focus())
            CHECK(t.rect.y >= list.y - 1 && t.rect.bottom() <= list.bottom() + 1);
    // Up to the top scrolls back.
    for (int i = 0; i < 14; ++i)
        d.keys({UiKey::Up});
    CHECK(d.pg.scroll() == 0);
    // The wheel scrolls without moving the focus.
    SettingsInput in;
    in.ui.wheel = -2;
    const TargetId f = d.pg.focus();
    d.frame(in);
    CHECK(d.pg.scroll() > 0 && d.pg.focus() == f);
}

void testBindings() {
    Driver d("game");
    CHECK(d.pg.view() == SettingsView::Bindings);
    SettingsFrameOutput o = d.frame();
    CHECK(hasText(o.draw, "Forward") && hasText(o.draw, "Left Mouse") && hasText(o.draw, "MOVEMENT"));
    // Focus on Forward's primary chip; Right to the secondary.
    d.keys({UiKey::Right});
    d.keys({UiKey::Confirm});
    CHECK(d.pg.capturing());
    o = d.frame();
    CHECK(hasText(o.draw, "Waiting for input"));
    // A free key binds straight away.
    SettingsInput in;
    in.capturedKey = "Q";
    d.frame(in);
    CHECK(!d.pg.capturing());
    CHECK(d.all.back().kind == SettingsActionKind::Bind && d.all.back().layer == "game" &&
          d.all.back().action == "LSTICK_UP" && d.all.back().slot == 1 && d.all.back().source == "Q" &&
          !d.all.back().replace);
    // A key used by another action asks: Replace / Use for Both / Cancel.
    d.keys({UiKey::Confirm});
    in.capturedKey = "S";
    d.frame(in);
    CHECK(d.pg.card() == SettingsCard::Conflict);
    o = d.frame();
    CHECK(hasText(o.draw, "already used") && hasText(o.draw, "Back"));
    d.keys({UiKey::Confirm}); // Replace
    CHECK(d.all.back().kind == SettingsActionKind::Bind && d.all.back().source == "S" && d.all.back().replace);
    // Esc / B cancels a capture; it times out too.
    d.keys({UiKey::Confirm});
    d.keys({UiKey::Back});
    CHECK(d.pg.card() == SettingsCard::None);
    d.keys({UiKey::Confirm});
    d.t += 13;
    d.frame();
    CHECK(d.pg.card() == SettingsCard::None);
    // Clicking in the box binds the mouse button, but not the click that opened the capture.
    {
        Driver e("game");
        const HitTarget* chip = nullptr;
        for (const HitTarget& t : e.pg.targets())
            if (t.id == e.pg.focus())
                chip = &t;
        CHECK(chip != nullptr);
        if (chip) {
            const Rect r = chip->rect;
            e.click(r.cx(), r.cy(), true);
            CHECK(e.pg.capturing());
            const Rect box = e.pg.captureRect();
            e.click(box.cx(), box.cy(), true); // still the held button that opened it: ignored
            CHECK(e.pg.capturing());
            SettingsInput up; // released: armed
            e.frame(up);
            SettingsInput rc; // a right click in the box binds the right button (a free one)
            rc.ui.pointerValid = rc.ui.rightClick = true;
            rc.ui.pointerX = box.cx();
            rc.ui.pointerY = box.cy();
            e.frame(rc);
            CHECK(!e.pg.capturing() && !e.all.empty() && e.all.back().source == "MOUSE2");
            // The left button is Attack's: the conflict question.
            e.keys({UiKey::Confirm});
            e.frame(up);
            e.click(box.cx(), box.cy(), false);
            CHECK(e.pg.card() == SettingsCard::Conflict);
        }
    }
    // Secondary clears the focused slot.
    d.keys({UiKey::Secondary});
    CHECK(d.all.back().kind == SettingsActionKind::ClearBinding && d.all.back().action == "LSTICK_UP" &&
          d.all.back().slot == 1);
    // Tertiary: restore the layer's defaults (focus on Cancel first).
    d.keys({UiKey::Tertiary});
    CHECK(d.pg.card() == SettingsCard::Defaults);
    d.keys({UiKey::Confirm});
    CHECK(d.pg.card() == SettingsCard::None && d.all.back().kind != SettingsActionKind::ResetBindings);
    d.keys({UiKey::Tertiary});
    d.keys({UiKey::Up});
    d.keys({UiKey::Confirm});
    CHECK(d.all.back().kind == SettingsActionKind::ResetBindings && d.all.back().layer == "game");
    // Conflicts and reserved keys are listed in the help panel.
    d.keys({UiKey::Down, UiKey::Down, UiKey::Left}); // Jump's primary: Space, also Use's
    o = d.frame();
    CHECK(hasText(o.draw, "Also bound to: Use"));
    // Opened straight onto the layer: Back leaves.
    o = d.keys({UiKey::Back});
    CHECK(o.wantsClose);
}

void testPageDefaults() {
    Driver d("audio");
    d.keys({UiKey::Tertiary});
    CHECK(d.pg.card() == SettingsCard::Defaults);
    SettingsFrameOutput o = d.frame();
    CHECK(hasText(o.draw, "Audio page"));
    d.keys({UiKey::Up});
    d.keys({UiKey::Confirm});
    CHECK(d.all.back().kind == SettingsActionKind::ResetPage && d.all.back().page == "audio");
}

void testAspects() {
    for (const Viewport vp : {Viewport{0, 0, 2560, 1080}, Viewport{0, 0, 1440, 1080}, Viewport{0, 0, 1280, 720}}) {
        Driver d("display");
        d.vp = vp;
        const SettingsFrameOutput o = d.frame();
        bool inside = true;
        for (const DrawCmd& c : o.draw.commands())
            if (c.kind == DrawKind::Text)
                inside = inside && c.rect.x >= vp.x - 1 && c.rect.right() <= vp.x + vp.w + 1;
        CHECK(inside);
    }
}

void testPauseHook() {
    Snapshot s;
    PauseConfig cfg;
    cfg.hostPages = true;
    cfg.pcSettingsPages = true;
    PauseScreen scr(cfg);
    test::SettingsWorld w;
    PauseState st;
    st.options.valid = true;
    st.pcSettings = &w.schema;
    const Viewport vp{0, 0, 1920, 1080};
    auto run = [&](std::initializer_list<UiKey> k) {
        UiInput in;
        in.keys = k;
        st.now += 0.016;
        return scr.frame(s, st, in, vp);
    };
    scr.open();
    scr.frame(s, st, {}, vp);
    run({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}); // Options
    CHECK(scr.page() == PausePage::Options);
    PauseFrameOutput o = run({UiKey::Down, UiKey::Down, UiKey::Confirm}); // PC Settings
    CHECK(scr.pcSettingsOpen());
    bool emitted = false;
    for (const PauseAction& a : o.actions)
        emitted = emitted || a.kind == PauseActionKind::OpenPcSettings;
    CHECK(!emitted && !scr.suspended());
    CHECK(hasText(o.draw, "PC Settings") && hasText(o.draw, "Key Bindings"));
    o = run({UiKey::Confirm, UiKey::Right}); // Display, window mode -> borderless
    CHECK(o.settings.size() == 1 && o.settings[0].key == "video.window_mode");
    CHECK(scr.pcSettings().card() == SettingsCard::KeepDisplay);
    run({UiKey::Confirm});
    run({UiKey::Back});     // the landing
    o = run({UiKey::Back}); // the Options page again, focus on PC Settings
    CHECK(!scr.pcSettingsOpen() && scr.page() == PausePage::Options);
    CHECK(scr.focus() == (TargetId{TargetKind::MenuRow, static_cast<int>(PauseRow::PcSettings)}));
    CHECK(hasText(o.draw, "Default Settings"));
    // Key Bindings on the Controller page: straight onto the gameplay keys; Back returns to the Controller page.
    run({UiKey::Up, UiKey::Up, UiKey::Confirm});
    CHECK(scr.page() == PausePage::Controller);
    run({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm});
    CHECK(scr.pcSettingsOpen() && scr.pcSettings().view() == SettingsView::Bindings);
    run({UiKey::Back});
    CHECK(!scr.pcSettingsOpen() && scr.page() == PausePage::Controller);
    // Without a schema the rows emit the overlay actions as before.
    st.pcSettings = nullptr;
    o = run({UiKey::Confirm});
    emitted = false;
    for (const PauseAction& a : o.actions)
        emitted = emitted || a.kind == PauseActionKind::OpenKeyBindings;
    CHECK(emitted && !scr.pcSettingsOpen());
}

// PauseConfig::unifiedOptions: one Options screen, the game's options folded into Audio / Controls.
int count(const std::vector<PauseAction>& v, PauseActionKind k, int32_t value = -1000) {
    int n = 0;
    for (const PauseAction& a : v)
        n += a.kind == k && (value == -1000 || a.value == value) ? 1 : 0;
    return n;
}

void testUnifiedOptions() {
    Snapshot s;
    PauseConfig cfg;
    cfg.hostPages = true;
    cfg.quitCard = true;
    cfg.pcSettingsPages = true;
    cfg.unifiedOptions = true;
    PauseScreen scr(cfg);
    test::SettingsWorld w;
    {
        SettingPage c;
        c.id = "controls";
        c.title = "Controls";
        SettingSection m{"Mouse", {}};
        m.rows.push_back(test::toggleRow("input.mouse.invert_x", "Invert horizontal look", false, "Mirror."));
        c.sections.push_back(m);
        w.schema.pages.insert(w.schema.pages.begin() + 2, c); // display, audio, controls
    }
    PauseState st;
    st.options.valid = true;
    st.options.set(PauseOption::MasterVolume, 100);
    st.options.set(PauseOption::MusicVolume, 80);
    st.options.set(PauseOption::Scheme, 0);
    st.pcSettings = &w.schema;
    const Viewport vp{0, 0, 1920, 1080};
    auto run = [&](std::initializer_list<UiKey> k) {
        UiInput in;
        in.keys = k;
        st.now += 0.016;
        PauseFrameOutput o = scr.frame(s, st, in, vp);
        for (const SettingsAction& a : o.settings)
            if (a.kind == SettingsActionKind::Change)
                w.apply(a.key, a.value);
        return o;
    };
    scr.open();
    PauseFrameOutput o = scr.frame(s, st, {}, vp);
    CHECK(scr.rows().size() == 5); // Resume, Save, Load, Options, Quit: no PC Settings row
    o = run({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm});
    CHECK(scr.pcSettingsOpen() && count(o.actions, PauseActionKind::EnterPage, int32_t(PausePage::Options)) == 1);
    o = run({});
    CHECK(hasText(o.draw, "Options") && hasText(o.draw, "Audio") && !hasText(o.draw, "PC Settings"));
    // Audio: the native Audio screen is entered; the game's rows first, the PC rows after.
    o = run({UiKey::Down, UiKey::Confirm});
    CHECK(count(o.actions, PauseActionKind::EnterPage, int32_t(PausePage::Audio)) == 1);
    o = run({});
    CHECK(hasText(o.draw, "Game volume") && hasText(o.draw, "Music volume") && hasText(o.draw, "Output volume"));
    CHECK(scr.pcSettings().focusedKey() == "game.master_volume");
    o = run({UiKey::Left});
    CHECK(count(o.actions, PauseActionKind::SetMasterVolume, 95) == 1 && o.settings.empty());
    o = run({UiKey::Down, UiKey::Left});
    CHECK(scr.pcSettings().focusedKey() == "game.music_volume");
    CHECK(count(o.actions, PauseActionKind::SetMusicVolume, 75) == 1 && o.settings.empty());
    for (int i = 0; i < 6 && scr.pcSettings().focusedKey() != "audio.volume"; ++i)
        run({UiKey::Down});
    CHECK(scr.pcSettings().focusedKey() == "audio.volume");
    o = run({UiKey::Right});
    CHECK(o.settings.size() == 1 && o.settings[0].key == "audio.volume" && o.actions.empty());
    // Restore defaults on the page: the PC page defaults and the game's audio defaults.
    run({UiKey::Tertiary});
    CHECK(scr.pcSettings().card() == SettingsCard::Defaults);
    run({UiKey::Up});
    o = run({UiKey::Confirm});
    CHECK(o.settings.size() == 1 && o.settings[0].kind == SettingsActionKind::ResetPage);
    CHECK(count(o.actions, PauseActionKind::SetMasterVolume, 100) == 1 &&
          count(o.actions, PauseActionKind::SetMusicVolume, 80) == 1 &&
          count(o.actions, PauseActionKind::SetSubtitles, 1) == 1 &&
          count(o.actions, PauseActionKind::SetStereo, 1) == 1);
    CHECK(count(o.actions, PauseActionKind::SetControllerScheme) == 0);
    // Controls tab: leave Audio, enter Controller; the scheme is a choice there.
    o = run({UiKey::PageNext});
    CHECK(count(o.actions, PauseActionKind::LeavePage, int32_t(PausePage::Audio)) == 1 &&
          count(o.actions, PauseActionKind::EnterPage, int32_t(PausePage::Controller)) == 1);
    o = run({});
    CHECK(hasText(o.draw, "Control scheme") && hasText(o.draw, "Invert horizontal look"));
    o = run({UiKey::Right});
    CHECK(count(o.actions, PauseActionKind::SetControllerScheme, 1) == 1);
    // Back: the landing (Controller left), then the pause root with Options focused (Options left).
    o = run({UiKey::Back});
    CHECK(count(o.actions, PauseActionKind::LeavePage, int32_t(PausePage::Controller)) == 1);
    o = run({UiKey::Back});
    CHECK(!scr.pcSettingsOpen() && scr.page() == PausePage::Root && !o.wantsResume);
    CHECK(count(o.actions, PauseActionKind::LeavePage, int32_t(PausePage::Options)) == 1);
    CHECK(scr.focus() == (TargetId{TargetKind::MenuRow, static_cast<int>(PauseRow::Options)}));
    // The title: no PC Settings row, Options opens the same screen and Back leaves Options.
    TitleConfig tc;
    tc.pcSettingsPages = tc.unifiedOptions = true;
    TitleScreen t(tc);
    TitleState ts;
    ts.options = st.options;
    ts.pcSettings = &w.schema;
    t.open();
    t.frame(ts, {}, vp);
    CHECK(t.rows(ts).size() == 5); // Load, New, Options, Credits, Quit
    UiInput in;
    in.keys = {UiKey::Down, UiKey::Down, UiKey::Confirm};
    TitleFrameOutput to = t.frame(ts, in, vp);
    CHECK(t.page() == TitlePage::Options && t.pcSettingsOpen());
    CHECK(to.optionActions.size() == 1 && to.optionActions[0].kind == PauseActionKind::EnterPage);
    to = t.frame(ts, {}, vp);
    CHECK(hasText(to.draw, "Options") && hasText(to.draw, "Main Menu"));
    in.keys = {UiKey::Back};
    to = t.frame(ts, in, vp);
    CHECK(t.page() == TitlePage::Main && !t.pcSettingsOpen());
    CHECK(!to.optionActions.empty() && to.optionActions.back().kind == PauseActionKind::LeavePage);
}

} // namespace

int main() {
    using Test = void (*)();
    const std::pair<const char*, Test> tests[] = {
        {"landing", &testLandingAndPages},   {"toggle", &testToggleChoiceSlider}, {"choices", &testChoicesAndKeepCard},
        {"restart", &testRestartAndProblem}, {"pointer", &testPointer},           {"scroll", &testScroll},
        {"bindings", &testBindings},         {"defaults", &testPageDefaults},     {"aspects", &testAspects},
        {"pause", &testPauseHook},           {"unified", &testUnifiedOptions}};
    for (const auto& [name, fn] : tests) {
        std::printf("%s\n", name);
        std::fflush(stdout);
        fn();
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
