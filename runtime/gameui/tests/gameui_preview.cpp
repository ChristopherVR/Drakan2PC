// gameui_preview <outdir>
// Runs the strip, map, pause, save / load and title screens over the synthetic fixture (ScreenFixture.h) at several
// aspects and writes each frame's DrawList as JSON lines (<outdir>/<name>.jsonl, first line = the scene).
// render_drawlist.py turns them into PNGs for visual review. Synthetic data only; no game data, no window.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "DrawListJson.h"
#include "ScreenFixture.h"
#include "SettingsFixture.h"
#include "gameui/SettingsPages.h"
#include "gameui/Dialog.h"
#include "gameui/MapScreen.h"
#include "gameui/Multiplayer.h"
#include "gameui/PauseScreen.h"
#include "gameui/SaveLoadScreen.h"
#include "gameui/StripScreen.h"
#include "gameui/TitleScreen.h"

using namespace gameui;
using gameui::test::write;

namespace {

Rect fitMap(const Viewport& vp, float aspect) {
    const float h = vp.h * 0.98f, w = std::min(vp.w * 0.98f, h * aspect);
    return {vp.x + (vp.w - w) * 0.5f, vp.y + (vp.h - h) * 0.5f, w, w / aspect};
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: gameui_preview <outdir>\n");
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    test::ScreenWorld w;
    ReaderState st;
    const Snapshot s = w.read(st);

    struct Aspect {
        const char* name;
        Viewport vp;
    } aspects[] = {{"16x9", {0, 0, 1920, 1080}}, {"21x9", {0, 0, 2560, 1080}}, {"4x3", {0, 0, 1440, 1080}}};

    for (const Aspect& a : aspects) {
        // Native help and confirmation text in the modern menu style; native button rectangles are retained.
        {
            DialogContent c;
            c.valid = true;
            c.text = "Equipment: Compare the items you carry, check their requirements, and choose what to equip. "
                     "Use the tabs to browse the other sections of your inventory.";
            const Rect panel{a.vp.w * 0.5f - 580, 360, 1160, 360};
            c.buttons.push_back({"OK", {a.vp.w * 0.5f - 80, 636, 160, 60}, 0, true, true});
            write(dir, std::string("dialog_help_") + a.name, a.vp, "world", {}, drawDialog(c, panel, a.vp, {}));
            c.text = "Apply the skill points?";
            c.buttons = {{"Yes", {a.vp.w * 0.5f - 200, 636, 160, 60}, 0, false, true},
                         {"No", {a.vp.w * 0.5f + 40, 636, 160, 60}, 0, true, true}};
            write(dir, std::string("dialog_confirm_") + a.name, a.vp, "world", {}, drawDialog(c, panel, a.vp, {}));
        }
        // Strip: focus on the bow (details current).
        {
            StripScreen scr;
            scr.open(s);
            scr.frame(s, {}, a.vp);
            UiInput in;
            in.keys = {UiKey::Right};
            const FrameOutput o = scr.frame(s, in, a.vp);
            write(dir, std::string("strip_") + a.name, a.vp, "world", {}, o.draw);
        }
        // Map: the native image fitted, the panel at the right; focus on the first objective.
        {
            const float imgW = 640, imgH = 400;
            const Rect img = fitMap(a.vp, imgW / imgH);
            MapInput m;
            for (const QuestEntry& q : s.quests)
                if (q.marker() && q.enabled) {
                    const Vec2 p = MapScreen::markerToScreen(q, img, imgW, imgH);
                    m.markers.push_back({q.index, p.x, p.y});
                }
            MapScreen scr;
            scr.open(s);
            FrameOutput o = scr.frame(s, m, {}, a.vp);
            write(dir, std::string("map_") + a.name, a.vp, "map", img, o.draw);
            if (std::string(a.name) == "16x9") {
                UiInput in;
                in.keys = {UiKey::Down, UiKey::Down};
                scr.frame(s, m, in, a.vp);
                in.keys = {UiKey::Confirm};
                o = scr.frame(s, m, in, a.vp);
                write(dir, "map_16x9_marker", a.vp, "map", img, o.draw);
            }
        }
        // Pause: Load disabled, focus on Save.
        {
            PauseScreen scr;
            PauseState ps;
            ps.canLoad = false;
            scr.open();
            scr.frame(s, ps, {}, a.vp);
            UiInput in;
            in.keys = {UiKey::Down};
            const PauseFrameOutput o = scr.frame(s, ps, in, a.vp);
            write(dir, std::string("pause_") + a.name, a.vp, "world", {}, o.draw);
        }
    }
    {
        PauseConfig cfg;
        cfg.hostOptions = true;
        PauseScreen scr(cfg);
        scr.open();
        const Viewport vp{0, 0, 1920, 1080};
        UiInput in;
        in.keys = {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm};
        scr.frame(s, PauseState{}, {}, vp);
        const PauseFrameOutput o = scr.frame(s, PauseState{}, in, vp);
        write(dir, "pause_options_16x9", vp, "world", {}, o.draw);
    }
    // Phase 2 / 3 pages (hostPages, quitCard) over made-up option values.
    PauseState ps2;
    ps2.options.valid = true;
    ps2.options.set(PauseOption::MasterVolume, 80);
    ps2.options.set(PauseOption::MusicVolume, 45);
    ps2.options.set(PauseOption::Subtitles, 1);
    ps2.options.set(PauseOption::Stereo, 0);
    ps2.options.set(PauseOption::Scheme, 2);
    ps2.options.set(PauseOption::Vibration, 1);
    ps2.options.set(PauseOption::ReversePitch, 0);
    PauseConfig cfg2;
    cfg2.hostPages = true;
    cfg2.quitCard = true;
    const UiKey kOptions[] = {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm};
    auto run = [&](PauseScreen& scr, const PauseState& st, const Viewport& vp, std::initializer_list<UiKey> k) {
        UiInput in;
        in.keys = k;
        return scr.frame(s, st, in, vp);
    };
    for (const Aspect& a : aspects) {
        // Audio, focus on Music.
        PauseScreen scr(cfg2);
        scr.open();
        scr.frame(s, ps2, {}, a.vp);
        UiInput in;
        in.keys.assign(std::begin(kOptions), std::end(kOptions));
        scr.frame(s, ps2, in, a.vp);
        run(scr, ps2, a.vp, {UiKey::Down, UiKey::Confirm});
        const PauseFrameOutput o = run(scr, ps2, a.vp, {UiKey::Down});
        write(dir, std::string("pause_audio_") + a.name, a.vp, "world", {}, o.draw);
    }
    const Viewport vp{0, 0, 1920, 1080};
    auto atOptions = [&](PauseScreen& scr, const PauseState& st) {
        scr.open();
        scr.frame(s, st, {}, vp);
        UiInput in;
        in.keys.assign(std::begin(kOptions), std::end(kOptions));
        return scr.frame(s, st, in, vp);
    };
    {
        PauseScreen scr(cfg2);
        const PauseFrameOutput o = atOptions(scr, ps2);
        write(dir, "pause_options2_16x9", vp, "world", {}, o.draw);
    }
    {
        // Controller, focus on the scheme.
        PauseScreen scr(cfg2);
        atOptions(scr, ps2);
        const PauseFrameOutput o = run(scr, ps2, vp, {UiKey::Confirm});
        write(dir, "pause_controller_16x9", vp, "world", {}, o.draw);
    }
    {
        // Audio while dragging the master slider (mouse) to 35.
        PauseScreen scr(cfg2);
        atOptions(scr, ps2);
        run(scr, ps2, vp, {UiKey::Down, UiKey::Confirm});
        const Rect t = scr.sliderTrack(PauseRow::MasterVolume);
        UiInput in;
        in.pointerValid = in.pointerMoved = in.leftClick = in.leftDown = true;
        in.pointerX = t.x + t.w * 0.5f;
        in.pointerY = t.cy();
        scr.frame(s, ps2, in, vp);
        in.leftClick = false;
        in.pointerX = t.x + t.w * 0.35f;
        const PauseFrameOutput o = scr.frame(s, ps2, in, vp);
        write(dir, "pause_audio_drag_16x9", vp, "world", {}, o.draw);
    }
    {
        // Audio with the options unreadable: the value rows greyed, focus on Back.
        PauseScreen scr(cfg2);
        PauseState bad;
        atOptions(scr, bad);
        const PauseFrameOutput o = run(scr, bad, vp, {UiKey::Down, UiKey::Confirm});
        write(dir, "pause_audio_disabled_16x9", vp, "world", {}, o.draw);
    }
    {
        // Default Settings card.
        PauseScreen scr(cfg2);
        atOptions(scr, ps2);
        const PauseFrameOutput o = run(scr, ps2, vp, {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm});
        write(dir, "pause_defaults_16x9", vp, "world", {}, o.draw);
    }
    for (const Aspect& a : aspects) {
        // Quit card.
        PauseScreen scr(cfg2);
        scr.open();
        scr.frame(s, ps2, {}, a.vp);
        const PauseFrameOutput o =
            run(scr, ps2, a.vp, {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm});
        write(dir, std::string("pause_quit_") + a.name, a.vp, "world", {}, o.draw);
    }
    // PC Settings pages inside the pause menu (pcSettingsPages) over the synthetic schema (SettingsFixture.h; the real
    // schema: runtime/settings/tests/settings_pages_preview.cpp).
    {
        test::SettingsWorld sw;
        PauseConfig pc = cfg2;
        pc.pcSettingsPages = true;
        PauseState pst = ps2;
        pst.pcSettings = &sw.schema;
        for (const Aspect& a : aspects) {
            PauseScreen scr(pc);
            scr.open();
            scr.frame(s, pst, {}, a.vp);
            UiInput in;
            in.keys.assign(std::begin(kOptions), std::end(kOptions));
            scr.frame(s, pst, in, a.vp);
            run(scr, pst, a.vp, {UiKey::Down, UiKey::Down, UiKey::Confirm});
            PauseFrameOutput o = run(scr, pst, a.vp, {});
            write(dir, std::string("pcsettings_landing_") + a.name, a.vp, "world", {}, o.draw);
            run(scr, pst, a.vp, {UiKey::Confirm});
            o = run(scr, pst, a.vp, {UiKey::Down, UiKey::Down, UiKey::Down});
            write(dir, std::string("pcsettings_display_") + a.name, a.vp, "world", {}, o.draw);
        }
        PauseScreen scr(pc);
        scr.open();
        scr.frame(s, pst, {}, vp);
        UiInput in;
        in.keys.assign(std::begin(kOptions), std::end(kOptions));
        scr.frame(s, pst, in, vp);
        run(scr, pst, vp, {UiKey::Down, UiKey::Down, UiKey::Confirm});
        run(scr, pst, vp, {UiKey::Down, UiKey::Down, UiKey::Confirm}); // Key Bindings
        const PauseFrameOutput o = run(scr, pst, vp, {UiKey::Down, UiKey::Down, UiKey::Down});
        write(dir, "pcsettings_bindings_16x9", vp, "world", {}, o.draw);
    }

    // ---- Save / Load (made-up slots) ----
    auto slot = [](int n, SlotState st, const char* where, int day, int hour) {
        SaveSlot s;
        s.number = n;
        s.state = st;
        if (st == SlotState::Used || st == SlotState::Corrupt) {
            s.location = where;
            s.file = std::string("Level - ") + where + ".rsg";
            s.time = {2026, 10, day, hour, 23, 0};
            s.preview = {ImageKind::SavePreview, static_cast<uint32_t>(n)};
        }
        return s;
    };
    SaveSlotsState slots;
    slots.page = 1;
    slots.pageCount = 4;
    slots.slots = {slot(5, SlotState::Used, "Surdana", 25, 16), slot(6, SlotState::Empty, "", 0, 0),
                   slot(7, SlotState::Used, "Wartok Canyon", 24, 21), slot(8, SlotState::Used, "Dragon Keep", 22, 9)};
    slots.nativeRow = 0;
    slots.slots[0].playSeconds = 2 * 3600 + 14 * 60;
    for (const Aspect& a : aspects) {
        SaveLoadScreen scr;
        scr.open(SaveLoadMode::Load, SaveLoadContext::Title);
        scr.frame(slots, {}, a.vp);
        UiInput none;
        const SaveLoadFrameOutput o = scr.frame(slots, none, a.vp);
        write(dir, std::string("load_") + a.name, a.vp, "title", {}, o.draw);
    }
    {
        // Save from the pause, focus on the empty slot.
        SaveLoadScreen scr;
        scr.open(SaveLoadMode::Save, SaveLoadContext::Pause, 6);
        scr.frame(slots, {}, vp);
        SaveLoadFrameOutput o = scr.frame(slots, {}, vp);
        write(dir, "save_empty_16x9", vp, "world", {}, o.draw);
        // The overwrite card on slot 7.
        UiInput in;
        in.keys = {UiKey::Down, UiKey::Confirm};
        o = scr.frame(slots, in, vp);
        write(dir, "save_overwrite_16x9", vp, "world", {}, o.draw);
    }
    {
        // Load in game: the confirm card.
        SaveSlotsState g = slots;
        g.inGame = true;
        SaveLoadScreen scr;
        scr.open(SaveLoadMode::Load, SaveLoadContext::Pause, 7);
        scr.frame(g, {}, vp);
        UiInput in;
        in.keys = {UiKey::Confirm};
        const SaveLoadFrameOutput o = scr.frame(g, in, vp);
        write(dir, "load_ingame_card_16x9", vp, "world", {}, o.draw);
    }
    {
        // A page change rescanning, then the card states.
        SaveLoadScreen scr;
        scr.open(SaveLoadMode::Load, SaveLoadContext::Pause);
        scr.frame(slots, {}, vp);
        UiInput in;
        in.keys = {UiKey::PageNext};
        scr.frame(slots, in, vp);
        SaveSlotsState reading = slots;
        reading.card = CardState::Reading;
        write(dir, "load_reading_16x9", vp, "world", {}, scr.frame(reading, {}, vp).draw);
        SaveSlotsState none;
        none.card = CardState::NoCard;
        SaveLoadScreen s2;
        s2.open(SaveLoadMode::Save, SaveLoadContext::Pause);
        write(dir, "save_nocard_16x9", vp, "world", {}, s2.frame(none, {}, vp).draw);
        SaveSlotsState full = slots;
        full.slots[1].state = SlotState::NoSpace;
        full.slots[3].state = SlotState::Corrupt;
        SaveLoadScreen s3;
        s3.open(SaveLoadMode::Save, SaveLoadContext::LevelChange, 6);
        write(dir, "save_levelchange_nospace_16x9", vp, "world", {}, s3.frame(full, {}, vp).draw);
    }

    // ---- Title menu ----
    TitleState ts;
    ts.continueSlot = 5;
    ts.continueLocation = "Surdana";
    ts.continueTime = {2026, 10, 25, 16, 23, 0};
    ts.options = ps2.options;
    ts.footer = "PC port";
    for (const Aspect& a : aspects) {
        TitleScreen scr;
        scr.open();
        scr.frame(ts, {}, a.vp);
        UiInput in;
        in.keys = {UiKey::Down};
        const TitleFrameOutput o = scr.frame(ts, in, a.vp);
        write(dir, std::string("title_") + a.name, a.vp, "title", {}, o.draw);
    }
    {
        TitleScreen scr;
        scr.open();
        scr.frame(ts, {}, vp);
        UiInput in;
        in.keys = {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm};
        write(dir, "title_quit_16x9", vp, "title", {}, scr.frame(ts, in, vp).draw);
    }
    {
        TitleScreen scr;
        scr.open();
        scr.frame(ts, {}, vp);
        UiInput in;
        in.keys = {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm};
        scr.frame(ts, in, vp);
        write(dir, "title_options_16x9", vp, "title", {}, scr.frame(ts, {}, vp).draw);
        in.keys = {UiKey::Down, UiKey::Confirm};
        scr.frame(ts, in, vp);
        in.keys = {UiKey::Down};
        write(dir, "title_audio_16x9", vp, "title", {}, scr.frame(ts, in, vp).draw);
    }
    {
        // No save yet: no Continue, Load Game unavailable; the art drawn by the host (backdrop).
        TitleState fresh;
        fresh.canLoad = false;
        fresh.backdrop = {ImageKind::TitleArt, 0};
        TitleScreen scr;
        scr.open();
        write(dir, "title_nosave_16x9", vp, "world", {}, scr.frame(fresh, {}, vp).draw);
    }
    {
        // Multiplayer front end (synthetic arenas, servers and players; the maps' pictures are the generated sketches).
        using namespace gameui::mp;
        MpState mps;
        mps.defaultName = "Rynn";
        for (int i = 0; i < 6; ++i) {
            LevelEntry e;
            e.name = "Arena " + std::to_string(i);
            e.title = std::string("Arena ") + char('A' + i);
            e.arena = true;
            e.maxPlayers = 8 + i % 3;
            e.spawns = 8 + i;
            e.modes = i == 3 ? 1u : 0u;
            e.description = "A synthetic arena used for the offline layout check.";
            e.custom = i == 5;
            e.author = "demo pack";
            mps.arenas.push_back(e);
        }
        for (int i = 0; i < 5; ++i) {
            LanServer l;
            l.host = "192.168.1." + std::to_string(10 + i);
            l.port = 47800;
            l.name = "Server " + std::to_string(i + 1);
            l.level = mps.arenas[static_cast<size_t>(i)].name;
            l.mode = i % 2 ? GameMode::TeamDeathmatch : GameMode::Deathmatch;
            l.players = i + 1;
            l.maxPlayers = 8;
            l.passwordProtected = i == 2;
            l.pingMs = 12 + i * 9;
            l.mods = i == 4 ? "demo pack" : "";
            mps.lan.push_back(l);
        }
        const auto mpPage = [&](const char* name, MpPage pg, const MpState& state, std::vector<UiKey> keys = {},
                                Viewport v = {0, 0, 1920, 1080}) {
            MultiplayerScreen m;
            m.open(state);
            m.setPage(pg, state);
            UiInput none;
            m.frame(state, none, v);
            UiInput in;
            in.keys = keys;
            write(dir, name, v, "title", {}, m.frame(state, in, v).draw);
        };
        mpPage("mp_menu_16x9", MpPage::Menu, mps);
        mpPage("mp_host_16x9", MpPage::Host, mps);
        mpPage("mp_host_team_16x9", MpPage::Host, mps,
               {UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down,
                UiKey::Right, UiKey::Down, UiKey::Down, UiKey::Right});
        mpPage("mp_join_16x9", MpPage::Join, mps, {UiKey::Down, UiKey::Down});
        mpPage("mp_host_21x9", MpPage::Host, mps, {}, {0, 0, 2560, 1080});
        mpPage("mp_join_4k", MpPage::Join, mps, {UiKey::Down}, {0, 0, 3840, 2160});
        MpState lob = mps;
        lob.conn = Conn::Connected;
        lob.hosting = true;
        lob.serverName = "Rynn's server";
        lob.level = "Arena 1";
        lob.maxPlayers = 8;
        lob.fragLimit = 20;
        lob.timeLimitMin = 10;
        lob.roster = {{1, "Rynn", 0, true, true, true, 0, 0, 0, 12, 0},
                      {2, "Arokh", 1, false, false, false, 0, 0, 1, 31, 0},
                      {3, "Wartok", 0, true, false, false, 0, 0, 3, 48, 0}};
        lob.chat = {{"Arokh", "ready when you are", 0}, {"", "Wartok joined the game", 0}};
        mpPage("mp_lobby_16x9", MpPage::Lobby, lob);
        lob.mode = GameMode::TeamDeathmatch;
        lob.countdownSec = 3;
        mpPage("mp_lobby_team_16x9", MpPage::Lobby, lob);
        {
            MpState co = lob;
            co.mode = GameMode::Coop;
            co.hosting = false;
            co.countdownSec = -1;
            co.roster[0].characterName = "Rynn";
            co.roster[1].characterName = "Wartok";
            co.roster[2].characterName = "Sera";
            co.selfCharacter = 1;
            mpPage("mp_lobby_coop_16x9", MpPage::Lobby, co);
            HudState ch;
            ch.mode = GameMode::Coop;
            ch.now = 100;
            ch.serverName = "Co-op party";
            ch.level = "Level 1A - Surdana";
            for (int i = 0; i < 4; ++i) {
                HudPartner q;
                q.id = i + 2;
                q.name = i == 0 ? "Arokh" : "Partner " + std::to_string(i + 1);
                q.character = i % 2 ? "Sera" : "Wartok";
                q.hpFrac = 0.9f - 0.25f * static_cast<float>(i);
                q.down = i == 2;
                q.respawnInSec = 6;
                q.markerValid = true;
                q.onScreen = i < 2;
                q.markerX = 0.35f + 0.25f * static_cast<float>(i);
                q.markerY = 0.45f;
                q.dirDeg = i == 2 ? 200.0f : 20.0f;
                q.distance = 14.0f + 9.0f * static_cast<float>(i);
                ch.partners.push_back(q);
            }
            write(dir, "mp_coop_hud_16x9", vp, "world", {}, drawHud(ch, vp, ScreenConfig{}));
            for (int i = 0; i < 4; ++i) {
                RosterRow r;
                r.id = i + 1;
                r.name = i == 0 ? "Rynn" : "Partner " + std::to_string(i);
                r.self = i == 0;
                r.kills = 31 - i * 6;
                r.deaths = i;
                r.pingMs = 10 + i * 8;
                r.timeSec = 900 + i;
                r.characterName = i == 0 ? "Rynn" : "Wartok";
                ch.roster.push_back(r);
            }
            ch.scoreboard = true;
            write(dir, "mp_coop_scoreboard_16x9", vp, "world", {}, drawHud(ch, vp, ScreenConfig{}));
            ch.scoreboard = false;
            ch.results = true;
            ch.levelComplete = true;
            ch.timeLeftSec = 912;
            ch.resultsText = "Next level in 8 seconds";
            write(dir, "mp_coop_results_16x9", vp, "world", {}, drawHud(ch, vp, ScreenConfig{}));
        }
        // HUD
        HudState hs;
        hs.serverName = "Rynn's server";
        hs.level = "Arena B";
        hs.fragLimit = 20;
        hs.timeLeftSec = 222;
        hs.now = 100;
        for (int i = 0; i < 8; ++i) {
            RosterRow r;
            r.id = i + 1;
            r.name = i == 0 ? "Rynn" : "Player " + std::to_string(i + 1);
            r.self = i == 0;
            r.team = i % 2;
            r.kills = 14 - i * 2;
            r.deaths = i + 3;
            r.pingMs = 20 + i * 7;
            r.timeSec = 540 + i * 11;
            hs.roster.push_back(r);
        }
        hs.kills = {{"Rynn", "Player 2", "Sword", 98, true, false}, {"Player 3", "Rynn", "Bow", 99, false, true}};
        hs.chat = {{"Player 2", "nice one", 97}, {"Rynn", "thanks", 98.5}};
        hs.status = "3:42 left   First to 20";
        write(dir, "mp_hud_16x9", vp, "world", {}, drawHud(hs, vp, ScreenConfig{}));
        hs.scoreboard = true;
        write(dir, "mp_scoreboard_ffa_16x9", vp, "world", {}, drawHud(hs, vp, ScreenConfig{}));
        hs.mode = GameMode::TeamDeathmatch;
        hs.teamKills[0] = 38;
        hs.teamKills[1] = 31;
        write(dir, "mp_scoreboard_team_16x9", vp, "world", {}, drawHud(hs, vp, ScreenConfig{}));
        hs.scoreboard = false;
        hs.results = true;
        hs.resultsText = "Back to the lobby in 10 seconds";
        write(dir, "mp_results_16x9", vp, "world", {}, drawHud(hs, vp, ScreenConfig{}));
        hs.results = false;
        hs.alive = false;
        hs.respawnInSec = 2.2;
        hs.deathText = "Killed by Player 3 (Bow)";
        hs.chatOpen = true;
        hs.chatInput = "gg";
        write(dir, "mp_death_16x9", vp, "world", {}, drawHud(hs, vp, ScreenConfig{}));
    }
    return 0;
}
